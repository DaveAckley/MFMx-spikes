"""sequencer.py -- the host-side SuperCycle, as an explicit state machine.

Protocol (per the design discussion):
  HOLD_AT_BIRTH           once: T6s hold until ALL have acked
  then, repeating:
    NEW_LEADER(leader, scy)   T6s do significant work before acking: slow
    RUN_EVENTS(scy)           T6s ack quickly
    DWELL                     host-side wait; amortizes NEW_LEADER's cost
  scy is the SuperCycle counter, bumped at each NEW_LEADER (as the old C++
  doASuperCycle did).  Leader choice: first_leader (2, the old debug default),
  then random in 0..3 != the previous leader.  'cycle leader N' overrides the
  next pick.

POLICY (current): a bolt that times out FREEZES the cycle.  Nothing further is
fired; the slowscan tick keeps running for diagnosis.  'cycle retry' re-fires
the failed step once (then holds); 'cycle stop' abandons.

CONTROL: start (run continuously) / hold (finish the current step, then wait)
/ step (run exactly one step, then wait) / stop / reset.  Everything advances
from tick(); there is no thread.

Don't use 'shoot' while a cycle step is active: it supersedes the sequencer's
bolt and the cycle freezes (loudly).
"""
import random
import time

from machine import fmt_ints

STEPS = ("NEW_LEADER", "RUN_EVENTS", "DWELL")
NLEADERS = 4

# NUKE: a blast is a disc in GLOBAL site coordinates (DemoGlobal.h: hubs are
# globally contiguous, 14 x 10 hubs of 137 x 108 sites).  It rides in words 2..4
# of the next NEW_LEADER bolt (x, y, radius^2; radius^2 0 = none) and each hub
# erases the disc from its whole grid, caches included, when it leaves
# NEW_LEADER: after SAVE, before LOAD (cross/images/hub/src/PhaseSteppers.cpp).
GLOBAL_SITES_X = 14 * 137
GLOBAL_SITES_Y = 10 * 108
NUKE_RADIUS_SMALL = (5, 54)         # as the old host-side nuke: Create(50) + 5
NUKE_RADIUS_BIG = (5, 504)          # Create(500) + 5


class SuperCycle:
    def __init__(self, machine, say, clock=time.monotonic, rng=None,
                 dwell=(3.0, 3.0), first_leader=2, hold_args=(0,)):
        self.m, self.say, self.clock = machine, say, clock
        self.rng = rng or random.Random()
        self.dwell = tuple(dwell)
        self.first_leader = first_leader
        self.repeat = False         # four corners: pick 0..3 uniformly, repeats allowed
        self.hold_args = tuple(hold_args)
        self.late_poll = 0.5
        self.reset()

    def reset(self):
        self.mode = "idle"          # idle | run | hold
        self.credits = 0            # steps allowed while mode == hold
        self.held = False           # HOLD_AT_BIRTH completed
        self.pointer = 0            # index into STEPS of the next step
        self.active = None          # dict(step, since, bolt, end) while a step runs
        self.frozen = None          # dict(step, reason, bolt, since)
        self.late = False
        self._late_at = 0.0
        self.retry_args = None      # (cmd, args) to re-fire verbatim on retry
        self.scy = 0
        self.leader = None
        self.next_leader = None
        self.pending_nuke = None    # (x, y, r2) to ride the next NEW_LEADER
        self.auto = None            # autonuke: dict(delay, radius, at) or None
        self.cycles = 0             # completed full cycles

    # ---- introspection -----------------------------------------------------------
    def next_step(self):
        return "HOLD_AT_BIRTH" if not self.held else STEPS[self.pointer]

    def state(self):
        if self.frozen:
            return "frozen"
        if self.mode == "run":
            return "running"
        if self.mode == "hold":
            return "finishing step, then holding" if self.active else "paused"
        return "idle"

    def status(self):
        now = self.clock()
        s = (f"cycle: {self.state()}  scy={self.scy} leader={self.leader} "
             f"cycles-done={self.cycles}  dwell={self.dwell[0]:g}"
             + (f"-{self.dwell[1]:g}" if self.dwell[1] != self.dwell[0] else "") + "s")
        if self.active:
            a = self.active
            s += f"\n  active: {a['step']} for {now - a['since']:.1f}s"
            if a["step"] == "DWELL":
                s += f" ({max(0.0, a['end'] - now):.1f}s left)"
            elif a["bolt"] is not None:
                s += f" -> bolt #{a['bolt'].seq}"
        else:
            s += f"\n  next: {self.next_step()}"
        if self.next_leader is not None:
            s += f"\n  next leader forced: {self.next_leader}"
        if self.pending_nuke is not None:
            x, y, r2 = self.pending_nuke
            s += f"\n  nuke queued for the next NEW_LEADER: ({x},{y}) radius {int(r2 ** 0.5)}"
        if self.auto is not None:
            a = self.auto
            s += (f"\n  autonuke on: every {a['delay'][0]:g}-{a['delay'][1]:g}s, radius "
                  f"{a['radius'][0]}-{a['radius'][1]}; next in "
                  f"{max(0.0, a['at'] - now):.0f}s")
        if self.frozen:
            f = self.frozen
            s += (f"\n  FROZEN at step {f['step']} ({now - f['since']:.0f}s ago): {f['reason']}"
                  + ("\n  (the frozen bolt has since reached consensus)" if self.late else "")
                  + "\n  'cycle retry' re-fires it once; 'cycle stop' abandons")
        return s

    # ---- commands (return a message) -------------------------------------------------
    def _check_ok(self):
        self.m.need_booted()
        if self.frozen:
            raise RuntimeError("cycle is FROZEN; 'cycle retry' or 'cycle stop' first")

    def start(self):
        self._check_ok()
        self.mode = "run"
        return "cycle running"

    def hold(self):
        if self.mode == "idle" and not self.active:
            return "cycle is idle"
        self.mode, self.credits = "hold", 0
        return "cycle will pause after the current step" if self.active else "cycle paused"

    def step(self):
        self._check_ok()
        if self.active or (self.mode == "hold" and self.credits > 0):
            raise RuntimeError(f"step {self.active['step'] if self.active else self.next_step()} in progress; wait for it")
        self.mode, self.credits = "hold", 1
        return f"running one step: {self.next_step()}"

    def stop(self):
        was = self.state()
        pend = self.active["bolt"] if self.active and self.active["bolt"] else None
        self.mode, self.credits, self.active, self.frozen = "idle", 0, None, None
        return (f"cycle stopped (was {was})"
                + (f"; bolt #{pend.seq} {pend.cmd} may still be pending" if pend and pend.state == "pending" else "")
                + f"; next step would be {self.next_step()}")

    def retry(self):
        if not self.frozen:
            raise RuntimeError("cycle is not frozen")
        f = self.frozen
        b = f["bolt"]
        self.retry_args = (b.cmd, list(b.args)) if b is not None else None
        self.frozen, self.late = None, False
        self.mode, self.credits = "hold", 1
        return (f"retrying {f['step']} once"
                + (f" with the same args {b.args}" if b is not None else "")
                + "; the cycle will then hold ('cycle start' to continue)")

    def set_leader(self, n):
        if n is None:
            self.next_leader = None
            return "next leader: random"
        n = int(n)
        if not 0 <= n < NLEADERS:
            raise ValueError(f"leader must be 0..{NLEADERS - 1}")
        self.next_leader = n
        return f"next NEW_LEADER will use leader {n}"

    def queue_nuke(self, big=False, x=None, y=None, r=None):
        """Queue one blast for the next NEW_LEADER (replacing any queued one).
        Unset x, y, r are chosen at random like the old host-side nuke."""
        lo, hi = NUKE_RADIUS_BIG if big else NUKE_RADIUS_SMALL
        x = self.rng.randrange(GLOBAL_SITES_X) if x is None else int(x)
        y = self.rng.randrange(GLOBAL_SITES_Y) if y is None else int(y)
        r = self.rng.randint(lo, hi) if r is None else int(r)
        if not (0 <= x < GLOBAL_SITES_X and 0 <= y < GLOBAL_SITES_Y):
            raise ValueError(f"nuke center must be inside 0..{GLOBAL_SITES_X - 1}, "
                             f"0..{GLOBAL_SITES_Y - 1}")
        if not 1 <= r <= 2000:
            raise ValueError("nuke radius must be 1..2000 sites")
        self.pending_nuke = (x, y, r * r)
        return f"nuke queued for scy {self.scy + 1}: ({x},{y}) radius {r}"

    def set_autonuke(self, dlo, dhi, rlo, rhi):
        """Every random delay of dlo..dhi seconds, queue a blast of random
        radius rlo..rhi at a random place.  The blast goes out with the next
        NEW_LEADER after its time comes, so it can lag by about one cycle."""
        if not 0 < dlo <= dhi:
            raise ValueError("autonuke delay must be 0 < LO <= HI seconds")
        if not 1 <= rlo <= rhi <= 2000:
            raise ValueError("autonuke radius must be 1 <= LO <= HI <= 2000 sites")
        self.auto = dict(delay=(dlo, dhi), radius=(rlo, rhi),
                         at=self.clock() + self.rng.uniform(dlo, dhi))
        return (f"autonuke on: a blast every {dlo:g}-{dhi:g}s, radius {rlo}-{rhi}, "
                f"random place (first in about {self.auto['at'] - self.clock():.0f}s; "
                "only while the cycle runs)")

    def clear_autonuke(self):
        was, self.auto = self.auto is not None, None
        return "autonuke off" if was else "autonuke was off"

    def _autonuke_due(self, now):
        """At a NEW_LEADER: queue the next autonuke blast if its time came."""
        a = self.auto
        if a is None or self.pending_nuke is not None or now < a["at"]:
            return
        self.queue_nuke(False, None, None, self.rng.randint(*a["radius"]))
        a["at"] = now + self.rng.uniform(*a["delay"])

    def set_dwell(self, lo, hi=None):
        lo = float(lo)
        hi = lo if hi is None else float(hi)
        if lo < 0 or hi < lo:
            raise ValueError("dwell: need 0 <= LO <= HI")
        self.dwell = (lo, hi)
        return f"dwell {lo:g}-{hi:g}s"

    # ---- the engine -----------------------------------------------------------------------
    def tick(self):
        if self.frozen:
            self._late_check()
            return
        if self.active:
            self._progress()
        if (not self.active and not self.frozen
                and (self.mode == "run" or (self.mode == "hold" and self.credits > 0))):
            self._begin()

    def _pick_leader(self):
        if self.next_leader is not None:
            n, self.next_leader = self.next_leader, None
            return n
        if self.leader is None:
            return self.first_leader
        if self.repeat and getattr(self.m, "policy", None) == "fourcorners":
            return self.rng.randrange(NLEADERS)
        return self.rng.choice([l for l in range(NLEADERS) if l != self.leader])

    def _begin(self):
        step, now, bolt, end = self.next_step(), self.clock(), None, None
        # If start() already fired and settled HOLD_AT_BIRTH via firePHASER,
        # skip it here: T6s are already blocked waiting for the first NEW_LEADER phase.
        if step == "HOLD_AT_BIRTH" and self.m.consume_hold_settled():
            self.m.logkt("CYCLE", "SKIP HOLD_AT_BIRTH (pre-settled by start())")
            self.say("[cycle] HOLD_AT_BIRTH phase pre-settled by start(); skipping to NEW_LEADER")
            self.held = True
            return   # next tick() will begin NEW_LEADER
        try:
            if self.retry_args is not None and step != "DWELL":
                cmd, args = self.retry_args
                self.retry_args = None
                self.say(f"[cycle] re-firing {cmd}({','.join(map(str, args))})")
                bolt = self.m.fire(cmd, args)
            elif step == "HOLD_AT_BIRTH":
                bolt = self.m.fire("HOLD_AT_BIRTH", self.hold_args)
            elif step == "NEW_LEADER":
                leader = self._pick_leader()
                self.scy += 1
                self.leader = leader
                self._autonuke_due(now)
                nx, ny, nr2 = self.pending_nuke or (0, 0, 0)
                if self.pending_nuke:
                    self.say(f"[cycle] scy {self.scy}: NUKE ({nx},{ny}) radius {int(nr2 ** 0.5)}")
                    self.m.logkt("NUKE", f"SHOOT scy={self.scy} x={nx} y={ny} r2={nr2}")
                    self.pending_nuke = None
                self.say(f"[cycle] scy {self.scy}: NEW_LEADER leader={leader}")
                # always 5 words: the unused ones must be written as 0, bolt
                # words persist from bolt to bolt
                bolt = self.m.fire("NEW_LEADER", [leader, self.scy, nx, ny, nr2])
            elif step == "RUN_EVENTS":
                bolt = self.m.fire("RUN_EVENTS", [self.scy])
            else:                                   # DWELL
                end = now + self.rng.uniform(*self.dwell)
        except Exception as e:
            self._freeze(step, f"could not begin: {type(e).__name__}: {e}", None)
            return
        if self.mode == "hold" and self.credits > 0:
            self.credits -= 1
        self.active = dict(step=step, since=now, bolt=bolt, end=end)
        self.m.logkt("CYCLE", f"BEGIN {step} scy={self.scy} leader={self.leader}")
        self._progress()            # e.g. legacy blocking bolts are already settled

    def _progress(self):
        a = self.active
        if a["step"] == "DWELL":
            if self.clock() >= a["end"]:
                self._complete()
            return
        self.m.poll_bolt()
        b = a["bolt"]
        if b.state == "consensus":
            self._complete()
        elif b.state != "pending":                  # timeout | superseded
            lam = b.lamers()
            self._freeze(a["step"],
                         f"bolt #{b.seq} {b.cmd} {b.state.upper()} after {b.elapsed(self.clock()):.1f}s"
                         + "".join(f"; bh{c} lamers {fmt_ints(l) if all(isinstance(x, int) for x in l) else l}"
                                   for c, l in lam.items()), b)

    def _complete(self):
        step = self.active["step"]
        self.m.logkt("CYCLE", f"DONE {step} scy={self.scy} in {self.clock() - self.active['since']:.3f}s")
        self.active = None
        if step == "HOLD_AT_BIRTH":
            self.held = True
        else:
            self.pointer = (self.pointer + 1) % len(STEPS)
            if self.pointer == 0:
                self.cycles += 1
        if self.mode == "hold" and self.credits == 0:
            self.say(f"[cycle] paused after {step} (scy {self.scy}); next: {self.next_step()}")

    def _freeze(self, step, reason, bolt):
        self.frozen = dict(step=step, reason=reason, bolt=bolt, since=self.clock())
        self.active, self.late, self._late_at = None, False, self.clock()
        self.m.logkt("CYCLE", f"FROZEN {step} scy={self.scy}: {reason}")
        self.say(f"[cycle] FROZEN at scy {self.scy}, step {step}: {reason}\n"
                 "        nothing further will be fired; slowscans keep running.\n"
                 "        'cycle status' for detail; 'cycle retry' to re-fire once; 'cycle stop' to abandon")

    def _late_check(self):
        """While frozen, notice if the frozen bolt finally reaches consensus
        (useful diagnostic: slow vs. dead tiles).  Does NOT unfreeze."""
        f, now = self.frozen, self.clock()
        b = f["bolt"]
        if (self.late or b is None or not self.m.async_api or b is not self.m.bolt
                or now - self._late_at < self.late_poll):
            return
        self._late_at = now
        if all(not r[3] for r in self.m.qb.phaserStatus()):
            self.late = True
            self.say(f"[cycle] frozen bolt #{b.seq} {b.cmd} reached consensus LATE "
                     f"({now - b.fired:.1f}s after firing; timeout was {b.timeout:g}s). "
                     "Still frozen: 'cycle retry' or 'cycle stop'.")
