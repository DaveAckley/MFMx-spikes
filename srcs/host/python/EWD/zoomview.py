"""zoomview.py -- host-side zoom for the video feed.

The host keeps one 1920 x 1080 BGR image, a pixel per site.  ZoomView crops
and scales that image on its way to the feed; nothing on the hubs changes.

  mode "off"    frames go out untouched
  mode "auto"   fit the box of every non-empty site ever seen into the frame
                as large as possible, sites kept square.  The box only
                grows, so the zoom only moves outward; the shown scale and
                center glide toward the target, a fraction per frame.
  mode "fixed"  a given scale, centered on the box (or the image center)

Scaling is fractional.  Upscaling is nearest-neighbor to the next whole
factor (crisp sites) and then a bilinear step down to the exact scale; with
OpenCV that is cv2.warpAffine, without it a plain numpy nearest sample.
Cost: a few to a few tens of ms per frame; the feed has 200 ms at 5 Hz.

"Non-empty" means a pixel brighter than the empty-site color (5,10,15 BGR)
and not the inaccessible-site color; the hub grid crosses (`grid on`) are
drawn white into the same image and would count, so use zoom auto with the
grid off.
"""
import math

import numpy as np

try:
    import cv2
except ImportError:                       # tests, mock hosts
    cv2 = None

W, H = 1920, 1080
EMPTY_MAX = 24                  # brightest channel of an empty site is 15
INACCESSIBLE_BGR = (80, 64, 48)


class ZoomView:
    def __init__(self, width=W, height=H):
        self.w, self.h = width, height
        self.mode = "off"
        self.fixed = 1.0
        self.min_box = (120, 68)          # sites: caps the zoom on a lone start atom
        self.max_scale = 16.0
        self.margin = 0.04                # of the box, each side
        self.alpha = 0.25                 # glide: fraction of the way per frame
        self.box = None                   # [x0, y0, x1, y1), ever-seen, in sites
        self.cur = None                   # (cx, cy, scale) being shown
        self.last_ms = 0.0

    # ---- commands -----------------------------------------------------------
    def set_mode(self, mode, fixed=None):
        if mode not in ("off", "auto", "fixed"):
            raise ValueError("zoom mode must be off, auto or a scale")
        self.mode = mode
        if fixed is not None:
            if not 1.0 <= fixed <= 64.0:
                raise ValueError("zoom scale must be 1..64")
            self.fixed = float(fixed)
        if mode == "off":
            self.cur = None
        return self.status()

    def reset_box(self):
        self.box, self.cur = None, None

    def status(self):
        s = f"zoom: {self.mode}"
        if self.mode == "fixed":
            s += f" {self.fixed:g}x"
        if self.box is not None:
            x0, y0, x1, y1 = self.box
            s += f"; seen box {x1 - x0}x{y1 - y0} at ({x0},{y0})"
        if self.cur is not None:
            s += f"; showing {self.cur[2]:.2f}x"
        return s

    # ---- geometry (pure) -------------------------------------------------------
    def observe_box(self, x0, y0, x1, y1):
        """Union a box of occupied sites into the ever-seen box."""
        if self.box is None:
            self.box = [x0, y0, x1, y1]
        else:
            b = self.box
            self.box = [min(b[0], x0), min(b[1], y0), max(b[2], x1), max(b[3], y1)]

    def observe(self, img):
        occ = img.max(axis=2) > EMPTY_MAX
        occ &= ~((img[:, :, 0] == INACCESSIBLE_BGR[0]) &
                 (img[:, :, 1] == INACCESSIBLE_BGR[1]) &
                 (img[:, :, 2] == INACCESSIBLE_BGR[2]))
        rows = np.flatnonzero(occ.any(axis=1))
        if rows.size == 0:
            return
        cols = np.flatnonzero(occ.any(axis=0))
        self.observe_box(int(cols[0]), int(rows[0]), int(cols[-1]) + 1, int(rows[-1]) + 1)

    def target(self):
        """(cx, cy, scale) that fits the ever-seen box, or None."""
        if self.mode == "fixed":
            if self.box is None:
                return (self.w / 2.0, self.h / 2.0, self.fixed)
            b = self.box
            return ((b[0] + b[2]) / 2.0, (b[1] + b[3]) / 2.0, self.fixed)
        if self.box is None:
            return None
        x0, y0, x1, y1 = self.box
        bw = max(x1 - x0, self.min_box[0]) * (1.0 + 2.0 * self.margin)
        bh = max(y1 - y0, self.min_box[1]) * (1.0 + 2.0 * self.margin)
        s = min(self.w / bw, self.h / bh)
        s = max(1.0, min(self.max_scale, s))
        return ((x0 + x1) / 2.0, (y0 + y1) / 2.0, s)

    def step(self):
        """Move the shown view a fraction toward the target; return it."""
        t = self.target()
        if t is None:
            return None
        if self.cur is None:
            self.cur = t
            return self.cur
        cx, cy, s = self.cur
        a = self.alpha
        ls = math.log(s) + (math.log(t[2]) - math.log(s)) * a   # glide in log scale
        ns = math.exp(ls)
        ncx, ncy = cx + (t[0] - cx) * a, cy + (t[1] - cy) * a
        if abs(ns / t[2] - 1.0) < 0.002:
            ns = t[2]
        if abs(ncx - t[0]) < 0.25 and abs(ncy - t[1]) < 0.25:
            ncx, ncy = t[0], t[1]
        self.cur = (ncx, ncy, ns)
        return self.cur

    def view_rect(self, cx, cy, s):
        """(x0, y0, vw, vh): the source rectangle shown, kept inside the image."""
        vw, vh = self.w / s, self.h / s
        x0 = min(max(cx - vw / 2.0, 0.0), self.w - vw)
        y0 = min(max(cy - vh / 2.0, 0.0), self.h - vh)
        return x0, y0, vw, vh

    # ---- pixels ----------------------------------------------------------------
    def render(self, img, cx, cy, s):
        """Crop and scale img to the full frame at scale s centered near (cx,cy)."""
        x0, y0, vw, vh = self.view_rect(cx, cy, s)
        if s <= 1.0001:
            return np.ascontiguousarray(img)
        if cv2 is None:
            xs = np.minimum((x0 + (np.arange(self.w) + 0.5) / s).astype(np.int64), self.w - 1)
            ys = np.minimum((y0 + (np.arange(self.h) + 0.5) / s).astype(np.int64), self.h - 1)
            return np.ascontiguousarray(img[ys][:, xs])
        k = int(math.ceil(s - 1e-6))
        xi0, yi0 = int(math.floor(x0)), int(math.floor(y0))
        xi1 = min(self.w, int(math.ceil(x0 + vw)) + 1)
        yi1 = min(self.h, int(math.ceil(y0 + vh)) + 1)
        sub = img[yi0:yi1, xi0:xi1]
        up = cv2.resize(sub, None, fx=k, fy=k, interpolation=cv2.INTER_NEAREST)
        f = s / k                        # <= 1: the bilinear step down
        # source pixel (u,v) of `up` maps to output (u - ox)*f, (v - oy)*f
        ox, oy = (x0 - xi0) * k, (y0 - yi0) * k
        m = np.float32([[f, 0, -ox * f], [0, f, -oy * f]])
        return cv2.warpAffine(up, m, (self.w, self.h), flags=cv2.INTER_LINEAR,
                              borderMode=cv2.BORDER_REPLICATE)

    def apply(self, img):
        """img: (h,w,3) uint8.  Returns the frame to send (img itself when off)."""
        if self.mode == "off":
            return img
        if self.mode == "auto":
            self.observe(img)
        elif self.box is None:
            self.observe(img)
        v = self.step()
        if v is None:
            return img
        return self.render(img, *v)
