#!/usr/bin/env python3
"""notch_test.py -- native test of the corner notch detector
(srcs/host/python/EWD/notchwatch.py): a cache-sized square emptied at a
hub corner is reported once, with its size and a sharp border; the
same at the control points (hub centers, edge midpoints) is counted by
kind; a hole
over all four squares (a nuke, a front) is not; the
square re-arms after it refills; a frame costs well under 50 ms.
Run: python3 notch_test.py
"""
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "python", "EWD"))
import HubGeometry as HG  # noqa: E402
import notchwatch  # noqa: E402

bad = 0


def check(ok, what):
    global bad
    if not ok:
        print("FAIL:", what)
        bad += 1


EMPTY = (5, 10, 15)
ATOM = (200, 120, 40)
BW = HG.T6GRID_BASE_WIDTH
BH = HG.T6GRID_BASE_HEIGHT


def full():
    img = np.empty((HG.DEMO_GLOBAL_GRID_HEIGHT, HG.DEMO_GLOBAL_GRID_WIDTH, 3),
                   np.uint8)
    img[:, :] = ATOM
    return img


def watch():
    w = notchwatch.NotchWatch()
    w.on = True
    return w


# 1. one 20 x 20 square SE of hub corner (3*BW, 2*BH) empties
w = watch()
for _ in range(3):
    check(w.check(full()) == [], "full frames: no notch")
x, y = 3 * BW, 2 * BH
img = full()
img[y:y + 20, x:x + 20] = EMPTY
got = w.check(img)
check(len(got) == 1, f"one notch, got {got}")
if got:
    n = got[0]
    check((n.x, n.y, n.quad, n.side) == (x, y, "SE", 20),
          f"notch at the corner, SE, side 20: {n}")
    check(n.before == 1.0 and n.fill == 0.0, f"fill 100% -> 0%: {n}")
    check(n.outside == 1.0, f"sharp border (outside band full): {n}")
    check(x <= n.px < x + 20 and y <= n.py < y + 20, f"who pixel inside: {n}")
    print(notchwatch.describe(n, scy=42, owner="at023"))
check(w.check(img) == [], "reported once while it stays empty")
check(w.check(full()) == [], "refilled: nothing")
img2 = full()
img2[y - 12:y, x - 12:x] = EMPTY      # a 12 x 12 NW notch at the same corner
got = w.check(img2)
check(len(got) == 1 and got[0].quad == "NW" and got[0].side == 12,
      f"NW 12 x 12 notch: {got}")
img3 = full()
img3[y:y + 20, x:x + 20] = EMPTY
got = w.check(img3)
check(len(got) == 1 and got[0].quad == "SE", f"SE re-armed after refill: {got}")

# 1b. the controls: the same test at a hub center and an edge midpoint
w = watch()
w.check(full())
img = full()
cx, cy = 4 * BW + BW // 2, 3 * BH + BH // 2       # a hub center
img[cy:cy + 12, cx - 12:cx] = EMPTY               # SW square, 12 x 12
ex, ey = 6 * BW + BW // 2, 5 * BH                 # a top edge midpoint
img[ey - 16:ey, ex:ex + 16] = EMPTY               # NE square, 16 x 16
got = sorted(w.check(img))
check([(n.kind, n.x, n.y, n.quad, n.side) for n in got] ==
      sorted([("center", cx, cy, "SW", 12), ("edge", ex, ey, "NE", 16)]),
      f"center and edge controls: {got}")
check(w.by_kind == {"corner": 0, "edge": 1, "center": 1},
      f"counted by kind: {w.by_kind}")
check(len(w.log) == 0, "the corner log keeps corners only")
check(w.npoints["corner"] == len(w.points) and w.npoints["center"] > 0
      and w.npoints["edge"] > 0, f"points: {w.npoints}")
print(notchwatch.points_line(w))

# 2. a hole over all four squares at a corner is not a notch
w = watch()
w.check(full())
img = full()
x, y = 5 * BW, 3 * BH
img[y - 30:y + 30, x - 30:x + 30] = EMPTY
check(w.check(img) == [], "nuke-like hole: no notch")

# 3. corners too close to the picture's edge are skipped
w = watch()
check(len(w.points) > 0 and
      all(x >= w.gside and y >= w.gside for (x, y) in w.points),
      f"edge corners skipped: {len(w.points)}")

# 4. off: nothing is looked at
w = notchwatch.NotchWatch()
img = full()
img[2 * BH:2 * BH + 20, 3 * BW:3 * BW + 20] = EMPTY
check(w.check(full()) == [] and w.check(img) == [] and w.frames == 0,
      "off: no checks")

# 5. cost
w = watch()
img = full()
t = time.perf_counter()
for _ in range(20):
    w.check(img)
ms = (time.perf_counter() - t) / 20 * 1000
print(f"notch check: {ms:.1f} ms per frame, "
      f"{sum(w.npoints.values())} points")
check(ms < 50, f"frame cost {ms:.1f} ms")

if bad:
    print(f"notch_test: {bad} FAILED")
    sys.exit(1)
print("notch_test: all checks passed")
