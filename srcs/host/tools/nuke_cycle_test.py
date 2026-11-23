#!/usr/bin/env python3
"""nuke_cycle_test.py -- native test of the host side of `nuke`: a queued blast
rides exactly one NEW_LEADER bolt as words 2..4 [x, y, r2]; NEW_LEADER and only
NEW_LEADER always carries 5 words (unused ones 0: bolt words persist from bolt
to bolt on the device side); bad arguments are refused.  No hardware.
Run: python3 nuke_cycle_test.py
"""
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "python", "EWD"))
from sequencer import SuperCycle  # noqa: E402


class FakeBolt:
    def __init__(self, seq, cmd, args):
        self.seq, self.cmd, self.args, self.state = seq, cmd, list(args), "consensus"
        self.nonce = seq

    def lamers(self):
        return {}

    def elapsed(self, now):
        return 0.0


class FakeMachine:
    def __init__(self):
        self.fired = []
        self.bolt = None

    def need_booted(self):
        pass

    def consume_hold_settled(self):
        return False

    def logkt(self, *a):
        pass

    def poll_bolt(self):
        pass

    def fire(self, cmd, args=(), timeout=None):
        self.fired.append((cmd, list(args)))
        self.bolt = FakeBolt(len(self.fired), cmd, args)
        return self.bolt


bad = 0


def check(ok, what):
    global bad
    if not ok:
        print("FAIL:", what)
        bad += 1


m = FakeMachine()
cyc = SuperCycle(m, lambda *_: None, rng=random.Random(1), dwell=(0.0, 0.0))
cyc.mode = "run"


def run_cycles(n):
    for _ in range(40 * n):
        cyc.tick()


run_cycles(1)
nl = [a for c, a in m.fired if c == "NEW_LEADER"]
check(len(nl) >= 1 and all(len(a) == 5 for a in nl), "NEW_LEADER always 5 words")
check(all(a[2:] == [0, 0, 0] for a in nl), "no blast: words 2..4 are 0")
check(all(len(a) == 1 for c, a in m.fired if c == "RUN_EVENTS"), "RUN_EVENTS unchanged")

before = len(nl)
msg = cyc.queue_nuke(False, 100, 200, 30)
check("radius 30" in msg and cyc.pending_nuke == (100, 200, 900), "queued: " + msg)
check("nuke queued" in cyc.status(), "status shows the queued nuke")
run_cycles(1)
nl = [a for c, a in m.fired if c == "NEW_LEADER"]
check(nl[before][2:] == [100, 200, 900], "first NEW_LEADER after queue carries the blast")
check(all(a[2:] == [0, 0, 0] for a in nl[before + 1:]), "blast rides ONE bolt only")
check(cyc.pending_nuke is None, "queue cleared after shooting")

# random choices stay in bounds
for big in (False, True):
    for _ in range(500):
        cyc.queue_nuke(big)
        x, y, r2 = cyc.pending_nuke
        check(0 <= x < 1918 and 0 <= y < 1080 and 25 <= r2 <= (504 if big else 54) ** 2,
              f"random nuke in bounds {x},{y},{r2}")
for args in ((-1, 5, 10), (5, 5000, 10), (5, 5, 0), (5, 5, 5000)):
    try:
        cyc.queue_nuke(False, *args)
        check(False, f"bad nuke {args} refused")
    except ValueError:
        pass

print("nuke_cycle_test FAILED" if bad else "nuke_cycle_test ok")
sys.exit(1 if bad else 0)
