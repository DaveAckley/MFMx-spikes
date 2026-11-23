#!/usr/bin/env python3
"""zoom_test.py -- native test of the video zoom (zoomview.py): the ever-seen box
only grows, the target scale fits it with square sites, the shown view glides
to the target, one card fills the screen at ~2x, and a frame renders well inside
the 200 ms budget.  Uses the numpy renderer (and OpenCV's if installed).
Run: python3 zoom_test.py
"""
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "python", "EWD"))
import zoomview  # noqa: E402
from zoomview import ZoomView  # noqa: E402

bad = 0


def check(ok, what):
    global bad
    if not ok:
        print("FAIL:", what)
        bad += 1


def blank():
    img = np.empty((1080, 1920, 3), np.uint8)
    img[:] = (5, 10, 15)                      # empty site, BGR
    return img


# --- observe: empty and inaccessible sites do not count -----------------------
z = ZoomView()
img = blank()
img[100:200, 300:310] = (80, 64, 48)          # inaccessible
z.observe(img)
check(z.box is None, "empty/inaccessible only: no box")
img[500, 700] = (250, 20, 30)
img[540, 960] = (50, 50, 50)
z.observe(img)
check(z.box == [700, 500, 961, 541], f"box of two atoms {z.box}")
img[10, 20] = (200, 200, 200)
z.observe(img)
check(z.box == [20, 10, 961, 541], f"box only grows {z.box}")
img[10, 20] = (5, 10, 15)                     # atom dies: box remembers
z.observe(img)
check(z.box == [20, 10, 961, 541], "box remembers dead atoms")

# --- target: square sites, margin, caps ---------------------------------------
z = ZoomView()
z.set_mode("auto")
check(z.target() is None, "no box, no target")
z.observe_box(960, 540, 961, 541)             # a lone start atom
cx, cy, s = z.target()
check(abs(s - min(1920 / (120 * 1.08), 1080 / (68 * 1.08))) < 1e-9 and (cx, cy) == (960.5, 540.5),
      f"lone atom: min box limits the zoom {s}")
z.reset_box()
z.observe_box(0, 0, 959, 540)                 # one card, 959 x 540
cx, cy, s = z.target()
check(1.8 < s < 2.0, f"one card ~2x (margin) {s}")
z.reset_box()
z.observe_box(0, 0, 1918, 1080)               # whole machine
check(z.target()[2] == 1.0, "four cards: 1x")
z.reset_box()
z.observe_box(100, 100, 400, 1000)            # tall thin: height limits
s = z.target()[2]
check(abs(s - 1080 / (900 * 1.08)) < 1e-9, f"tall box limited by height {s}")

# --- step: lazy goal, eased, only outward, centered --------------------------
z = ZoomView()
z.set_mode("auto")
z.observe_box(900, 500, 1000, 580)
z.step()
first = z.cur
check(z.anim is None and z.goal == first, "first frame snaps to the fit")
for _ in range(10):                           # box unchanged: the view must not move
    z.step()
check(z.cur == first, "no creep while the box fits")
z.observe_box(905, 505, 1005, 585)            # a little growth inside the margin... or not
fits = z.box_fits(z.goal)
moved = z.step() != first
check(moved == (not fits), f"view moves only when the box leaves it (fits={fits})")
z = ZoomView()
z.set_mode("auto")
z.observe_box(900, 500, 1000, 580)
z.step()
first = z.cur
z.observe_box(500, 200, 1500, 900)            # computation spread
seq = []
for _ in range(40):
    z.step()
    seq.append(z.cur)
sc = [first[2]] + [c[2] for c in seq]
check(all(b <= a + 1e-12 for a, b in zip(sc, sc[1:])), "zoom only moves outward")
check(z.anim is None and seq[-1] == z.goal, "ease finishes at the goal")
check(z.box_fits(z.goal), "goal fits the box")
check(z.goal[2] < z.target()[2], "goal has headroom beyond the tight fit")
check(sc[1] < sc[0] and sc[1] > z.goal[2], "first eased step is partial")
d = [sc[i] - sc[i + 1] for i in range(len(sc) - 1)]
check(max(d) < 0.35 * (sc[0] - sc[-1]), "ease is gradual (no single big jump)")
b0 = list(z.box)
z.observe_box(b0[0] + 5, b0[1] + 5, b0[2] - 5, b0[3] - 5)
check(z.box == b0, "inner growth does not shrink the box")
n = 0
for _ in range(100):                           # growth that stays inside the headroom
    z.observe_box(b0[0] - 3, b0[1] - 2, b0[2] + 3, b0[3] + 2)
    before = z.cur
    z.step()
    n += z.cur != before
check(n == 0, f"headroom absorbs small growth ({n} frames moved)")

# --- shrink: the box is the recent window; the view zooms back in ------------------
z = ZoomView()
z.set_mode("auto")
z.set_shrink(True)
big, small = (200, 100, 1700, 1000), (800, 450, 1000, 580)
for _ in range(3):
    z.note_frame_box(big)
    z.step()
s_big = z.cur[2]
check(z.box == list(big), "shrink: box is the frame box")
for _ in range(z.window - 1):                 # big still inside the window: no zoom in
    z.note_frame_box(small)
    z.step()
check(z.box[0] == 200 and abs(z.cur[2] - s_big) < 1e-9, "shrink: big box remembered for the window")
for _ in range(z.window + z.anim_frames + 5):
    z.note_frame_box(small)
    z.step()
check(z.box == list(small), f"shrink: box shrank to {z.box}")
check(z.cur[2] > s_big * 1.3, f"shrink: zoomed back in {s_big:.2f} -> {z.cur[2]:.2f}")
check(z.box_fits(z.goal), "shrink: goal fits the small box")
# small wobble does not move the view (ratio hysteresis)
c0 = z.cur
for k in range(60):
    z.note_frame_box((800 + (k % 3), 450, 1000, 580 + (k % 2)))
    z.step()
check(z.cur == c0, "shrink: wobble inside the headroom does not move the view")
# ever-seen again after shrink off
z.set_shrink(False)
z.note_frame_box(big)
z.note_frame_box(small)
check(z.box[0] == 200 and "shrink" not in z.status(), "shrink off: ever-seen box again")
# frames with nothing occupied leave the box alone
z2 = ZoomView(); z2.set_mode("auto"); z2.set_shrink(True)
z2.note_frame_box(small); z2.note_frame_box(None)
check(z2.box == list(small), "empty frame leaves the shrink box alone")

# --- occupancy map ---------------------------------------------------------------
img = blank()
img[100:400, 50:700] = (200, 50, 50)
mp = ZoomView().occupancy_map(img).split("\n")
check(len(mp) == 21 and len(mp[0]) == 48 and "#" in mp[3] and set(mp[10]) == {"."},
      "occupancy map shape and content")
img2 = blank()
img2[:, 137 * 3:137 * 3 + 20] = (200, 50, 50)
xl = [l for l in ZoomView().occupancy_map(img2).split("\n") if l.startswith("occupied by x mod")][0]
check(xl.split(": ")[1].split()[1:] == ["0"] * 6 and int(xl.split(": ")[1].split()[0]) == 1080 * 20,
      "x mod 137 histogram puts a hub-edge strip in the first bucket")

# --- specks: lone occupied sites in an empty neighborhood ---------------------
img = blank()
img[100:400, 50:700] = (200, 50, 50)          # a dense block is not specks
img[500, 1000] = (0, 255, 0)
img[900, 1500] = (0, 255, 0)
img[600, 1100:1103] = (0, 255, 0)             # a 3-site streak is not lone
ys, xs = ZoomView().specks(img)
check(sorted(zip(xs.tolist(), ys.tolist())) == [(1000, 500), (1500, 900)],
      f"specks found exactly the two lone sites {list(zip(xs, ys))}")
check("2 lone occupied sites" in ZoomView().specks_report(img), "specks report")

# --- centered even near a corner: the view may extend past the image --------------
z = ZoomView()
z.set_mode("auto")
z.observe_box(0, 0, 959, 540)                 # one card in the top-left corner
cx, cy, s = z.target()
x0, y0, vw, vh = z.view_rect(cx, cy, s)
check(abs((x0 + vw / 2) - 479.5) < 1e-9 and abs((y0 + vh / 2) - 270) < 1e-9,
      "view centered on the box even at the corner")
check(x0 < 0 and y0 < 0, "margin shown beyond the image edge")
img = blank()
img[0:540, 0:959] = (60, 60, 60)
out = z.render(img, cx, cy, s)
check(tuple(out[0, 0]) == zoomview.OUTSIDE_BGR, "outside the image is the outside color")
check(zoomview.OUTSIDE_BGR != zoomview.EMPTY_BGR, "outside differs from empty")
lit = (out.max(axis=2) > 40)               # the 60-gray atoms, not the backgrounds
rows = np.flatnonzero(lit.any(axis=1))
cols = np.flatnonzero(lit.any(axis=0))
check(rows.size > 0 and abs((rows[0] + rows[-1]) / 2 - 540) < 4 and
      abs((cols[0] + cols[-1]) / 2 - 960) < 4,
      f"rendered box centered on the frame rows {rows[0]}..{rows[-1]} cols {cols[0]}..{cols[-1]}")

# --- view keeps 16:9 so sites stay square ----------------------------------------
z = ZoomView()
for (cx, cy, s) in ((0, 0, 4), (1919, 1079, 4), (960, 540, 1.5), (5, 1075, 16)):
    x0, y0, vw, vh = z.view_rect(cx, cy, s)
    check(abs(vw / vh - 1920 / 1080) < 1e-9, "view keeps 16:9, so sites stay square")

# --- render: integer scale is pixel exact; sites map to the right place ---------
z = ZoomView()
img = blank()
img[540, 960] = (255, 0, 0)                   # one blue atom at the center
out = z.render(img, 960, 540, 2.0)
check(out.shape == (1080, 1920, 3) and out.dtype == np.uint8, "output shape")
ys, xs = np.nonzero((out[:, :, 0] == 255))
check(len(ys) > 0 and ys.min() >= 535 and ys.max() <= 545 and xs.min() >= 955
      and xs.max() <= 965, f"atom stays near the center when zoomed {ys.min()}..{ys.max()}")
if zoomview.cv2 is None:
    check(ys.max() - ys.min() == 1 and xs.max() - xs.min() == 1, "numpy: 2x2 box")
out1 = z.render(img, 960, 540, 1.0)
check(np.array_equal(out1, img), "scale 1 is the image")
# off passes the very same array through
z.set_mode("off")
check(z.apply(img) is img, "off returns the input")

# --- with OpenCV: the warp path must equal the numpy sample at integer scale ----
if zoomview.cv2 is not None:
    img = blank()
    rng2 = np.random.default_rng(3)
    img[::7, ::5] = (250, 20, 30)
    img[rng2.integers(0, 1080, 500), rng2.integers(0, 1920, 500)] = (10, 250, 40)
    for s_, cx_, cy_ in ((2.0, 960, 540), (3.0, 700, 300), (4.0, 100, 100)):
        zz = ZoomView()
        x0_, y0_, vw_, vh_ = zz.view_rect(cx_, cy_, s_)
        cx2, cy2 = x0_ + vw_ / 2, y0_ + vh_ / 2        # whole-site origin
        got = zz.render(img, cx2, cy2, s_)
        saved, zoomview.cv2 = zoomview.cv2, None
        want = zz.render(img, cx2, cy2, s_)
        zoomview.cv2 = saved
        d = int(np.abs(got.astype(int) - want.astype(int)).max())
        check(d == 0 or float(np.mean(got != want)) < 0.001,
              f"cv2 warp matches numpy at {s_}x (max diff {d})")

# --- timing: a full fractional-zoom frame ---------------------------------------
z = ZoomView()
z.set_mode("auto")
img = blank()
rng = np.random.default_rng(1)
for _ in range(20000):
    img[rng.integers(300, 800), rng.integers(500, 1400)] = (250, 20, 30)
z.apply(img)
t0 = time.perf_counter()
n = 10
for _ in range(n):
    z.apply(img)
ms = (time.perf_counter() - t0) * 1000 / n
check(ms < 200, f"frame cost {ms:.0f} ms")
print(f"zoom_test: {ms:.0f} ms per frame ({'cv2' if zoomview.cv2 else 'numpy'})")

print("zoom_test FAILED" if bad else "zoom_test ok")
sys.exit(1 if bad else 0)
