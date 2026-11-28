#!/usr/bin/env python3
"""cornersim.py -- why four-corners corner squares go stale (OPEN_ISSUES
'CORNER NOTCHES', 2026-11-27).  Usage: cornersim.py [SEED] [on|off|fixed]
(on/off: the old full-width bands with elision on or off; fixed: the
trimmed bands and repeat-only elision of 2026-11-27).

Model of one corner square (D's NW square, the SE quadrant of point P)
under fourcorners + elision, from CachingPolicy.h / CacheLoader.cpp.
Holders: A (diagonal, SE corner cache) iff corner SE; B (north of D,
ST band) iff toSouth; C (west of D, ET band) iff toEast; D always has
its SELF copy.  The cycle's modifier (whose events reach the square):
SE->A, SW->B, NE->C, NW->D.  At a corner change each holder whose band
is not kept (keepAcross: same role under both corners) SAVEs its whole
copy into D (random order: last wins), and each new holder whose band
was not kept LOADs D's copy.  A copy's value = the cycle whose events
it last carries ('version'); the modifier's copy becomes version
'cycle' (it evolves whatever it holds, so we also track its BASE, the
version it started the cycle from)."""
import random, collections, sys
SE, SW, NW, NE = range(4)
NAME = "SE SW NW NE".split()
toEast = lambda c: c in (SE, NE)
toSouth = lambda c: c in (SE, SW)
MODE = sys.argv[2] if len(sys.argv) > 2 else "on"
def holds(h, c):
    if MODE == "fixed":         # trimmed bands: only the corner's modifier
        return {"A": c == SE, "B": c == SW, "C": c == NE, "D": True}[h]
    return {"A": c == SE, "B": toSouth(c), "C": toEast(c), "D": True}[h]
MOD = {SE: "A", SW: "B", NE: "C", NW: "D"}
random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
N = 200000
elide = MODE != "off"
copy = {"A": 0, "B": 0, "C": 0, "D": 0}     # version held
truth = 0                                   # newest legit version
c = random.randrange(4)
for h in "ABC":
    if holds(h, c): copy[h] = copy["D"]
lag_by_prev = collections.defaultdict(list)
shown = collections.Counter()
for cyc in range(1, N):
    # RUN_EVENTS of cycle cyc with corner c: the modifier evolves its copy
    m = MOD[c]
    base = copy[m]
    copy[m] = cyc
    truth = cyc
    # the picture: the modifier paints what it evolves; staleness of the
    # base it started from = how old the data it painted over was
    lag_by_prev[c].append(cyc - 1 - base)   # 0 = it had the latest
    nc = random.randrange(4)
    kept = {h: elide and holds(h, c) and holds(h, nc)
            and (MODE != "fixed" or c == nc) for h in "ABC"}
    writers = [h for h in "ABC" if holds(h, c) and not kept[h]]
    random.shuffle(writers)
    for h in writers:
        copy["D"] = copy[h]                 # last SAVE wins
    for h in "ABC":
        if holds(h, nc) and not kept[h]:
            copy[h] = copy["D"]             # LOAD
    c = nc
for k in range(4):
    v = lag_by_prev[k]
    print(f"modifier corner {NAME[k]}: base lag mean {sum(v)/len(v):.2f}, "
          f"P(lag>0) {sum(1 for x in v if x>0)/len(v):.2f}, max {max(v)}")
