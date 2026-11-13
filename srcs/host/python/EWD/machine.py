"""machine.py -- the MFMx-facing side of mfmxctl.

Ports what nu1011/nu1012 did in EWD.configureImageManager/run/start/stop/
slowScan, minus Textual, and adds bolt tracking.

BOLTS.  Two QuietBox flavours exist (see mock/mfmx/MFMx.py):
  async API   shootPHASER() returns a nonce at once; phaserStatus() reports
              per-chip acks.  We poll it from a tick and own the timeouts.
  legacy API  shootPHASER() blocks (currently up to ~40s per chip, GIL held,
              no ticks, no Ctrl-C).  We still work, but loudly say so.
Per-command timeouts are DEFAULT GUESSES (HOLD/NEW_LEADER slow, RUN_EVENTS
fast); tune with the 'bolt timeout' command.
"""
import collections
import os
import subprocess
import time

DEFAULT_TIMEOUTS = {"HOLD_AT_BIRTH": 60.0, "NEW_LEADER": 60.0, "RUN_EVENTS": 10.0}
FALLBACK_TIMEOUT = 30.0


def fmt_ints(xs):
    """[1,2,3,7,9,10] -> '1-3,7,9-10'"""
    xs = sorted(xs)
    out, i = [], 0
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[j] + 1:
            j += 1
        out.append(str(xs[i]) if i == j else f"{xs[i]}-{xs[j]}")
        i = j + 1
    return ",".join(out)


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
        s += f" {self.elapsed(now):.1f}s"
        if self.state == "pending":
            s += f"/{self.timeout:g}s"
        for c, _n, o, l in self.rows:
            if l is None:
                s += f" bh{c}:{o} acked"
            elif l:
                s += f" bh{c}:{o} acked, lamers {fmt_ints(l)}"
        return s


class NullFeed:
    """Stands in for RTMPFeed when there is no video server (mock mode)."""

    def __init__(self):
        self.frames = 0
        self.shape = None

    def sendGraphicsFrame(self, frame, simnanos, info):
        self.frames += 1
        self.shape = getattr(frame, "shape", None)
        self.last_info = info

    def idle_image(self):
        class _Idle:
            shape = (1080, 1920, 3)
        return _Idle()


class Machine:
    def __init__(self, mfmx, config, say, *, script_dir, logprefix="mfmxctl",
                 t6s_per_chip=140, verbose=False, decay=None, alt_decay=None,
                 tt_smi="/opt/tenstorrent/pipx/bin/tt-smi", reset_chips=True,
                 extra_bindirs=(), slowscan_gap=0.2):
        self.M = mfmx
        self.config = config
        self.say = say
        self.scriptDir = script_dir          # RTMPFeed reads ewd.scriptDir
        self.logprefix = logprefix
        self.t6s = t6s_per_chip
        self.verbose = verbose
        self.decay, self.alt_decay = decay, alt_decay
        self.tt_smi, self.do_reset = tt_smi, reset_chips
        self.extra_bindirs = list(extra_bindirs)
        self.slowscan_gap = slowscan_gap
        self.simDir = mfmx.getSimDir()
        self.mfmxVersion = mfmx.getVersion()
        self.bhs, self.qb, self.ewc, self.im = [], None, None, None
        self.configured = self.inited = False
        self.async_api = False
        self.has_fire_phaser = False   # True when qb.firePHASER() is available
        self.hold_settled = False      # True when start() pre-settled HOLD_AT_BIRTH
        self.feed = None
        self.frames_real = self.frames_idle = 0
        self.t0_ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        self.timeouts = dict(DEFAULT_TIMEOUTS)
        self.diagnose = True                 # slowscan right after a bolt timeout
        self.bolt = None
        self.bolt_history = collections.deque(maxlen=20)
        self.bolt_seq = 0
        self.scan_count = 0
        self.last_scan = None

    # ---- logging ---------------------------------------------------------------
    def logkt(self, key, text):              # RTMPFeed/FontCache call this
        path = f"{self.simDir}misc/{self.logprefix}-{key}.txt"
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "a") as f:
            f.write(f"{key}:{text}\n")

    def trace(self, *a):
        if self.verbose:
            print(*a)

    # ---- boot: configure -> chip reset -> init ----------------------------------
    # NB: the order mirrors nu1011 exactly: configure (which already opens the
    # devices and allocates TLBs via setStep) happens BEFORE 'tt-smi -r'.  That
    # looks odd -- see the notes in the hand-off message; kept as-is on purpose.
    def configure(self):
        """Port of EWD.configureImageManager()."""
        M = self.M
        cfg = self.config
        cfg.load()
        if self.extra_bindirs:
            cfg.hash["Config"]["bindirs"] = self.extra_bindirs + list(cfg.hash["Config"]["bindirs"])
        im = self.im = M.ImageManager()
        for k, v in cfg.hash.get("module", {}).items():
            hm = im.makeCommsModule(k)
            for b in v.get("blocks", []):
                hm.requireCommBlockNamed(b)
        keys = []
        for k, v in cfg.hash["image"].items():
            found = None
            for bindir in cfg.hash["Config"]["bindirs"]:
                bd = os.path.abspath(bindir.replace("$script_dir", self.scriptDir, 1))
                if not os.path.isdir(bd):
                    continue
                maybe = f"{bd}/{v['binfile']}"
                if os.path.isfile(maybe):
                    # as in nu1011: every match is (re)made, so the LAST match wins
                    img = im.makeT6Image(k, v["code"], maybe)
                    for i, tag in ((5, "BINPOS"), (6, "BINP2S"), (7, "BINP3S")):
                        self.logkt("KEYK", f"{tag} {k} {hex(img.getBinWord(i))}")
                    keys.append(k)
                    found = maybe
            if found:
                self.trace(f"image {k}: {found}")
            else:
                self.say(f"binfile {v['binfile']} for image {k} not found in any bindir")
        for k, c in cfg.hash["cell"].items():
            cell = im.makeCell(k, M.U8C(*c["size"]))
            if c.get("fill"):
                im.getT6Image(c["fill"])
                cell.addImage(M.U8C(255, 255), c["fill"])
            for x, v in (c.get("image") or {}).items():
                y = next(iter(v.keys()))
                val = v[y]
                x = 255 if x == "all" else int(x)
                y = 255 if y == "all" else int(y)
                im.getT6Image(val)
                cell.addImage(M.U8C(x, y), val)
        layouts = cfg.hash["layout"]
        for k, l in layouts.items():
            lay = im.makeLayout(k, l["defaultImage"])
            for bhc in l["bhchips"]:
                lay.addBlackholeChip(int(bhc))
            if l.get("fill"):
                im.getCell(l["fill"])
                lay.addCell(255, l["fill"])
            for c, cellname in (l.get("cell") or {}).items():
                lay.addCell(255 if c == "all" else int(c), cellname)
        act = cfg.hash["Config"]["activeLayout"]
        modules = layouts[act].get("hostModules", [])
        im.setActiveLayout(act)
        chips = im.getActiveLayout().getActiveBHChips()
        self.bhs = [M.Blackhole(i) for i in chips]
        for bh in self.bhs:                  # far enough to configure modules
            bh.setStep(M.Step.HAS_ALLOCATED_TLBS)
        for bh in self.bhs:
            for hmname in modules:
                bh.addCommsModule(im.getCommsModule(hmname))
        self.configured = True
        return f"layout '{act}', chips {list(chips)}, images {keys}, modules {modules}"

    def reset_chips(self):
        if not self.do_reset:
            return "chip reset skipped"
        r = subprocess.run([self.tt_smi, "-r"])
        return f"tt-smi -r exit {r.returncode}"

    def init_box(self):
        M = self.M
        self.im = M.ImageManager()
        self.ewc = M.EWControl.getEWControl()
        self.qb = M.QuietBox.get()
        self.logkt("QB", f"constants: {self.qb.getConstants()}")
        self.logkt("QB", f"ranges: {self.qb.getRanges()}")
        self.qb.init()
        self.qb.mDrawGrid = True             # nu1011's default checkbox value
        self.async_api = hasattr(self.qb, "phaserStatus")
        self.has_fire_phaser = hasattr(self.qb, "firePHASER")
        self.inited = True
        if self.async_api:
            return "async bolt API (phaserStatus available)"
        elif self.has_fire_phaser:
            return ("non-blocking firePHASER available: bolts are non-blocking; "
                    "start() will pre-settle HOLD_AT_BIRTH")
        else:
            return ("LEGACY blocking shootPHASER: bolts freeze this shell (and its ticks) "
                    "until consensus or the C++ timeout")

    def boot(self):
        out = [self.configure(), self.reset_chips(), self.init_box()]
        return "\n".join(out)

    def need_booted(self):
        if not (self.configured and self.inited):
            raise RuntimeError("not booted: run 'boot' first")

    # ---- lifecycle ----------------------------------------------------------------
    def steps(self):
        return [(bh.getChipNumber(), bh.getStep()) for bh in self.bhs]

    @staticmethod
    def pname(p):
        return getattr(p, "name", str(p))

    def start(self):
        """nu1011 start(): startEWProcessing on every chip (walks all BH init steps).
        When firePHASER is available, fires HOLD_AT_BIRTH to L1 first so T6s
        pick it up during their init (between CODE_RUNNING and EVENT_WINDOWS).
        On return, hold_settled=True means the sequencer can skip the cycle's
        HOLD_AT_BIRTH phase and go straight to NEW_LEADER."""
        self.need_booted()
        self.hold_settled = False
        out = []
        for bh in self.bhs:
            if self.decay is not None:
                bh.setStartDecayType(int(self.decay))
            t = time.monotonic()
            if self.has_fire_phaser:
                # Advance to HAS_T6_CODE_DEPLOYED: TLBs are now configured and
                # T6 code is in L1, but T6 cores are still in reset.
                # firePHASER needs the multicast TLB (AHAX_TLBI_L1_MULTI) set up;
                # calling it earlier (before HAS_CONFIGURED_TLBS) segfaults.
                bh.setStep(self.M.Step.HAS_T6_CODE_DEPLOYED)
            else:
                bh.startEWProcessing()
            out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())} "
                       f"({time.monotonic() - t:.1f}s)")
        if self.has_fire_phaser:
            # T6 cores are in reset: safe to write HOLD_AT_BIRTH to L1 now.
            # T6s will see it the moment they're released by startEWProcessing.
            self.say("start: firing HOLD_AT_BIRTH phase to L1 before T6 release")
            self.qb.firePHASER(self.M.PhaserBolt.Cmd.HOLD_AT_BIRTH, [0])
            for bh in self.bhs:
                t = time.monotonic()
                bh.startEWProcessing()   # starts transport thread, releases T6s
                out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())} "
                           f"({time.monotonic() - t:.1f}s)")
            # Poll for acks: T6s run birth code, see HOLD_AT_BIRTH, ack, and HOLD.
            deadline = time.monotonic() + 5.0
            counts = {}
            while time.monotonic() < deadline:
                counts = self._parse_on_phases()
                if counts and all(n >= self.t6s for n in counts.values()):
                    break
                time.sleep(0.1)
            all_acked = bool(counts) and all(n >= self.t6s for n in counts.values())
            if all_acked:
                self.hold_settled = True
                out.append(f"HOLD_AT_BIRTH phase pre-settled: "
                           f"{'/'.join(str(n) for n in counts.values())} acked; "
                           "cycle will begin at NEW_LEADER")
            else:
                out.append(f"HOLD_AT_BIRTH phase: only {counts} acked "
                           f"(expected {self.t6s}/chip); cycle will re-fire it")
        return "\n".join(out)

    def consume_hold_settled(self):
        """Return True (once) if start() pre-settled HOLD_AT_BIRTH; clear the flag."""
        r, self.hold_settled = self.hold_settled, False
        return r

    def stop(self):
        """stopEWProcessing only if already at EVENT_WINDOWS: the C++ call
        retreats-or-ADVANCES to CODE_RUNNING, so calling it too early would
        bring a chip UP."""
        self.need_booted()
        out = []
        for bh in self.bhs:
            if bh.getStep() >= self.M.Step.HAS_T6_EVENT_WINDOWS:
                bh.stopEWProcessing()
                out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())}")
            else:
                out.append(f"bh{bh.getChipNumber()} not at event windows "
                           f"({self.pname(bh.getStep())}); left alone")
        return "\n".join(out)

    def close(self):
        """Full retreat of every chip (resets fleet, joins transport thread, ...)."""
        self.need_booted()
        out = []
        for bh in self.bhs:
            bh.close()
            out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())}")
        return "\n".join(out)

    # ---- monitoring ---------------------------------------------------------------
    def slowscan(self, count=1):
        """nu1011 slowScan(): per chip, pace then runSlowScans(count).
        This is the hostblock-log path that still works when an 'hn' hart is hung."""
        self.need_booted()
        rets = []
        for bh in self.bhs:
            if self.slowscan_gap:
                time.sleep(self.slowscan_gap)
            rets.append(bh.runSlowScans(count))
        self.scan_count += 1
        self.last_scan = rets
        return rets

    def released(self):
        """True once every chip has been taken to CODE_RUNNING or beyond (the T6s
        have been released and have set up their hostblocks)."""
        return bool(self.bhs) and all(bh.getStep() >= self.M.Step.HAS_T6_CODE_RUNNING
                                      for bh in self.bhs)

    def tick_slowscan(self):
        # Before the T6s are released their hostblocks hold no valid magic, so
        # every scan would report BAD MAGIC and dump L1 to disk.  Manual 'scan'
        # still works at any phase.
        if self.configured and self.inited and self.released():
            self.slowscan(1)

    def frame(self):
        if self.feed is None:
            return "no feed"
        img = self.ewc.renderGraphicsGridWindowToImage()
        ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns
        self.feed.sendGraphicsFrame(img.asNP(), ns, f"mfmxctl/{self.mfmxVersion}")
        self.frames_real += 1

    def idle_frame(self):
        """Placeholder while there are no simulation frames (not booted yet).  The
        feed's own overlays (UTC clock, LIVE timer) tick on it, so a stream that
        shows this means ffmpeg + server are fine and only the sim is missing."""
        ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns
        self.feed.sendGraphicsFrame(self.feed.idle_image(), ns,
                                    f"mfmxctl/{self.mfmxVersion}  NO SIM FRAMES (not booted)")
        self.frames_idle += 1

    def tick_frame(self):
        if self.feed is None:
            return
        if self.inited:
            self.frame()
        elif hasattr(self.feed, "idle_image"):
            self.idle_frame()

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
        b.rows = [(c, None, n, None if n >= self.t6s else ["?"]) for c, n in counts.items()]
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
                b.rows = [(c, None, n, None if n >= self.t6s else ["?"])
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
            except Exception as e:
                self.say(f"  diagnosis failed: {type(e).__name__}: {e}")

    def _report(self, b):
        now = time.monotonic()
        self.logkt("BOLT", f"DONE {b.describe(now)}")
        self.say(f"bolt {b.describe(now)}")

    def tick_bolts(self):
        self.poll_bolt()

    # ---- status ---------------------------------------------------------------------
    def status(self):
        lines = []
        lines.append(f"booted: configured={self.configured} inited={self.inited} "
                     f"api={'async' if self.async_api else 'legacy' if self.inited else '-'}")
        if self.bhs:
            lines.append("steps: " + "  ".join(f"bh{c}={self.pname(p)}" for c, p in self.steps()))
        if self.inited:
            qb = self.qb
            lines.append(f"quietbox: onPhases {qb.onPhases()}  indices {qb.phaseIndices()}  "
                         f"log {qb.loggingSummary()}")
            lines.append(f"ewc: {self.ewc.statsLine()}")
        b = self.bolt
        lines.append("bolt: " + (b.describe(time.monotonic()) if b else "none yet"))
        lines.append("bolt timeouts: " + " ".join(f"{k}={v:g}s" for k, v in self.timeouts.items())
                     + f"  diagnose-on-timeout={self.diagnose}")
        lines.append(f"slowscans run: {self.scan_count}  last: {self.last_scan}")
        if self.feed is not None:
            lines.append(f"feed: {type(self.feed).__name__}  frames sent: "
                         f"{self.frames_real} sim, {self.frames_idle} placeholder")
        return "\n".join(lines)

    # ---- shutdown --------------------------------------------------------------------
    def shutdown(self, close=False):
        """Best-effort orderly teardown; every step is attempted."""
        out = []

        def step(name, fn):
            try:
                r = fn()
                out.append(f"{name}: {r if r else 'ok'}")
            except Exception as e:
                out.append(f"{name}: FAILED {type(e).__name__}: {e}")

        if self.configured and self.inited:
            if any(p >= self.M.Step.HAS_T6_EVENT_WINDOWS for _c, p in self.steps()):
                step("stop events", self.stop)
            if close:
                step("close chips", self.close)
        if self.qb is not None and hasattr(self.qb, "shutdown"):
            step("quietbox shutdown", self.qb.shutdown)
        return "\n".join(out)
