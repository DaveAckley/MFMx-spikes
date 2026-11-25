"""machine_bolts.py -- the Bolt record and the Machine methods that fire a
PHASER bolt, poll its acks and own its timeouts (see machine.py for the
async/legacy API notes)."""
import time

from machine_util import fmt_ints

DEFAULT_TIMEOUTS = {"HOLD_AT_BIRTH": 60.0, "NEW_LEADER": 60.0, "RUN_EVENTS": 10.0}
FALLBACK_TIMEOUT = 30.0


class Bolt:
    def __init__(self, seq, cmd_name, args, timeout, now):
        self.seq, self.cmd, self.args = seq, cmd_name, list(args)
        self.timeout, self.fired = timeout, now
        self.done = None
        self.state = "pending"      # pending|consensus|timeout|superseded
        self.nonce = None
        self.rows = []              # [(chip, nonce, onPhase, lamers|None)]

    def elapsed(self, now):
        return (self.done if self.done is not None else now) - self.fired

    def lamers(self):
        return {c: l for (c, _n, _o, l) in self.rows if l}

    def describe(self, now):
        s = f"#{self.seq} {self.cmd}({','.join(map(str, self.args))}) {self.state.upper()}"
        s += f" {self.elapsed(now):.3f}s"
        if self.state == "pending":
            s += f"/{self.timeout:g}s"
        for c, _n, o, l in self.rows:
            if l is None:
                s += f" bh{c}:{o} acked"
            elif l:
                s += f" bh{c}:{o} acked, lamers {fmt_ints(l)}"
        return s


class BoltMixin:
    """Machine methods: fire and poll bolts."""

    # ---- bolts ----------------------------------------------------------------------
    def timeout_for(self, cmd_name):
        return self.timeouts.get(cmd_name, FALLBACK_TIMEOUT)

    def fire(self, cmd_name, args=(), timeout=None):
        self.need_booted()
        try:
            cmd = getattr(self.M.PhaserBolt.Cmd, cmd_name)
        except AttributeError:
            raise ValueError(f"unknown PHASER command {cmd_name!r}; known: "
                             f"{', '.join(self.cmd_names())}") from None
        now = time.monotonic()
        prev = self.bolt
        if prev is not None and prev.state == "pending":
            prev.state, prev.done = "superseded", now
            self.say(f"bolt #{prev.seq} {prev.cmd} superseded before consensus")
        self.bolt_seq += 1
        b = Bolt(self.bolt_seq, cmd_name, args,
                 timeout if timeout is not None else self.timeout_for(cmd_name), now)
        self.bolt = b
        self.bolt_history.append(b)
        self.logkt("BOLT", f"FIRE {b.seq} {cmd_name} {b.args} timeout={b.timeout}")
        if self.async_api:
            b.nonce = self.qb.shootPHASER(cmd, list(b.args))
            self.poll_bolt()                 # may already be done (e.g. no chips)
        elif self.has_fire_phaser:
            self.qb.firePHASER(cmd, list(b.args))  # non-blocking L1 write
            # bolt stays "pending"; tick_bolts() settles via poll_bolt() -> onPhases()
        else:
            self.qb.shootPHASER(cmd, list(b.args))    # BLOCKS
            self._settle_legacy(b)
        return b

    def cmd_names(self):
        return [n for n in self.M.PhaserBolt.Cmd.__members__]

    def _settle_legacy(self, b):
        counts = self._parse_on_phases()
        bh_by_chip = {bh.getChipNumber(): bh for bh in self.bhs}
        b.rows = [(c, None, n,
                   None if n >= self.t6s
                   else list(bh_by_chip[c].getNonPhaseTLBIs()) if c in bh_by_chip
                   else ["?"])
                  for c, n in counts.items()]
        b.done = time.monotonic()
        b.state = "consensus" if all(n >= self.t6s for n in counts.values()) else "timeout"
        self._report(b)
        self._diagnose_if_timeout(b)

    def _parse_on_phases(self):
        out = {}
        for i, tok in enumerate(self.qb.onPhases().split("/")):
            if tok != "x":
                out[i] = int(tok)
        return out

    def poll_bolt(self):
        """Advance the pending bolt's state.  Returns True when it just finished.
        Supports three bolt modes:
          async_api      -- uses phaserStatus() (async C++ API)
          has_fire_phaser -- uses onPhases() polled by the transport thread
          legacy          -- bolt is already settled synchronously; no-op here"""
        b = self.bolt
        if b is None or b.state != "pending":
            return False
        now = time.monotonic()
        if self.async_api:
            b.rows = [tuple(r) for r in self.qb.phaserStatus()]
            if all(not l for (_c, _n, _o, l) in b.rows):
                b.state, b.done = "consensus", now
            elif now - b.fired > b.timeout:
                b.state, b.done = "timeout", now
            else:
                return False
        elif self.has_fire_phaser:
            # Transport thread keeps mOnPhase current; read it here.
            counts = self._parse_on_phases()
            if counts and all(n >= self.t6s for n in counts.values()):
                b.rows = [(c, None, n, []) for c, n in counts.items()]
                b.state, b.done = "consensus", now
            elif now - b.fired > b.timeout:
                counts = self._parse_on_phases()
                bh_by_chip = {bh.getChipNumber(): bh for bh in self.bhs}
                b.rows = [(c, None, n,
                           None if n >= self.t6s
                           else list(bh_by_chip[c].getNonPhaseTLBIs()) if c in bh_by_chip
                           else ["?"])
                          for c, n in counts.items()]
                b.state, b.done = "timeout", now
            else:
                return False
        else:
            return False   # legacy: bolt was settled synchronously by fire()
        self._report(b)
        self._diagnose_if_timeout(b)
        return True

    def _diagnose_if_timeout(self, b):
        if b.state == "timeout" and self.diagnose:
            if not self.released():
                self.say("  (no diagnosis slowscan: T6s not released yet; 'scan' to force one)")
                return
            try:
                self.say(f"  diagnosing: slowscan -> {self.slowscan(1)}")
                # One slowscan reads one tile per chip, round robin, so a
                # single scan rarely reaches the lamers: keep scanning (the
                # hostblock path still works when an hn hart is hung).
                if self.start_scan_ticks is not None and self.start_scan_ticks():
                    self.say("  slowscan tick turned ON to sweep every tile "
                             "(one per chip per tick; see all.mfmk)")
            except Exception as e:
                self.say(f"  diagnosis failed: {type(e).__name__}: {e}")

    def _report(self, b):
        now = time.monotonic()
        self.logkt("BOLT", f"DONE {b.describe(now)}")
        self.say(f"bolt {b.describe(now)}")

    def tick_bolts(self):
        self.poll_bolt()
