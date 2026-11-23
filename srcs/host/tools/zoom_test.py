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

# --- step: monotone glide, converges, never jitters back -----------------------
z = ZoomView()
z.set_mode("auto")
z.observe_box(900, 500, 1000, 580)
z.step()
first = z.cur[2]
z.observe_box(500, 200, 1500, 900)            # computation spread
t = z.target()
prev, seq = first, []
for _ in range(60):
    z.step()
    seq.append(z.cur[2])
check(all(b <= a + 1e-12 for a, b in zip([first] + seq, seq)), "zoom only moves outward")
check(abs(seq[-1] - t[2]) < 1e-9 and abs(z.cur[0] - t[0]) < 1e-9, "glide converges")
check(seq[0] - first < 0 and seq[0] > t[2], "first step is partial")

# --- view rect stays inside the image -----------------------------------------
z = ZoomView()
for (cx, cy, s) in ((0, 0, 4), (1919, 1079, 4), (960, 540, 1.5), (5, 1075, 16)):
    x0, y0, vw, vh = z.view_rect(cx, cy, s)
    check(x0 >= -1e-9 and y0 >= -1e-9 and x0 + vw <= 1920 + 1e-9 and y0 + vh <= 1080 + 1e-9,
          f"view inside image {cx},{cy},{s}")
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
check(ms < 150, f"frame cost {ms:.0f} ms")
print(f"zoom_test: {ms:.0f} ms per frame ({'cv2' if zoomview.cv2 else 'numpy'})")

print("zoom_test FAILED" if bad else "zoom_test ok")
sys.exit(1 if bad else 0)
