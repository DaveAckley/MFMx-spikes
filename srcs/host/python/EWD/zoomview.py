"""zoomview.py -- host-side zoom for the video feed.

The host keeps one 1920 x 1080 BGR image, a pixel per site.  ZoomView crops
and scales that image on its way to the feed; nothing on the hubs changes.

  mode "off"    frames go out untouched
  mode "auto"   fit the box of every non-empty site ever seen into the frame
                as large as possible, sites kept square.  The box only
                grows, so the zoom only moves outward.  The goal is only
                recomputed when the box outgrows the shown view; it is then
                set with headroom, and the view eases to it over ~3 s.
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
EMPTY_BGR = (5, 10, 15)


class ZoomView:
    def __init__(self, width=W, height=H):
        self.w, self.h = width, height
        self.mode = "off"
        self.fixed = 1.0
        self.min_box = (120, 68)          # sites: caps the zoom on a lone start atom
        self.max_scale = 16.0
        self.margin = 0.04                # of the box, each side, when first fitted
        self.headroom = 0.14              # extra margin when zooming out, so growth is absorbed
        self.edge = 0.02                  # box nearer than this to the view edge: zoom out
        self.anim_frames = 15             # an ease takes this many frames (3 s at 5 Hz)
        self.goal = None                  # (cx, cy, scale) being eased toward
        self.anim = None                  # (from, i): ease progress
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
        self.goal = self.anim = None
        if mode == "off":
            self.cur = None
        return self.status()

    def reset_box(self):
        self.box, self.cur, self.goal, self.anim = None, None, None, None

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

    def target(self, margin=None):
        """(cx, cy, scale) that fits the ever-seen box with the margin, or None."""
        margin = self.margin if margin is None else margin
        if self.mode == "fixed":
            if self.box is None:
                return (self.w / 2.0, self.h / 2.0, self.fixed)
            b = self.box
            return ((b[0] + b[2]) / 2.0, (b[1] + b[3]) / 2.0, self.fixed)
        if self.box is None:
            return None
        x0, y0, x1, y1 = self.box
        bw = max(x1 - x0, self.min_box[0]) * (1.0 + 2.0 * margin)
        bh = max(y1 - y0, self.min_box[1]) * (1.0 + 2.0 * margin)
        s = min(self.w / bw, self.h / bh)
        s = max(1.0, min(self.max_scale, s))
        return ((x0 + x1) / 2.0, (y0 + y1) / 2.0, s)

    def box_fits(self, view):
        """Is the ever-seen box inside the view with the edge margin?"""
        if self.box is None:
            return True
        cx, cy, s = view
        hw, hh = self.w / s / 2.0, self.h / s / 2.0
        ex, ey = hw * self.edge, hh * self.edge
        b = self.box
        return (b[0] >= cx - hw + ex and b[2] <= cx + hw - ex and
                b[1] >= cy - hh + ey and b[3] <= cy + hh - ey)

    def step(self):
        """Advance the shown view one frame; return (cx, cy, scale) or None."""
        if self.goal is None:
            t = self.target()
            if t is None:
                return None
            self.goal = self.cur = t
            self.anim = None
            return self.cur
        if self.mode == "auto" and not self.box_fits(self.goal):
            self.goal = self.target(self.margin + self.headroom)
            self.anim = (self.cur, 0)
        if self.anim is not None:
            (fx, fy, fs), i = self.anim
            i += 1
            p = min(1.0, i / float(self.anim_frames))
            e = p * p * (3.0 - 2.0 * p)                  # smoothstep ease
            gx, gy, gs = self.goal
            self.cur = (fx + (gx - fx) * e, fy + (gy - fy) * e,
                        math.exp(math.log(fs) + (math.log(gs) - math.log(fs)) * e))
            self.anim = None if p >= 1.0 else ((fx, fy, fs), i)
        return self.cur

    def view_rect(self, cx, cy, s):
        """(x0, y0, vw, vh): the source rectangle shown, centered on (cx,cy).
        It may extend past the image; those parts are drawn empty-colored."""
        vw, vh = self.w / s, self.h / s
        return cx - vw / 2.0, cy - vh / 2.0, vw, vh

    # ---- pixels ----------------------------------------------------------------
    def render(self, img, cx, cy, s):
        """Crop and scale img to the full frame at scale s centered on (cx,cy);
        parts of the view outside the image are the empty-site color."""
        x0, y0, vw, vh = self.view_rect(cx, cy, s)
        if s <= 1.0001 and abs(x0) < 1e-6 and abs(y0) < 1e-6:
            return np.ascontiguousarray(img)
        bg = EMPTY_BGR
        if cv2 is None:
            xs = np.floor(x0 + (np.arange(self.w) + 0.5) / s).astype(np.int64)
            ys = np.floor(y0 + (np.arange(self.h) + 0.5) / s).astype(np.int64)
            xin = (xs >= 0) & (xs < self.w)
            yin = (ys >= 0) & (ys < self.h)
            out = img[np.clip(ys, 0, self.h - 1)][:, np.clip(xs, 0, self.w - 1)]
            if not yin.all():
                out[~yin] = bg
            if not xin.all():
                out[:, ~xin] = bg
            return out
        k = max(1, int(math.ceil(s - 1e-6)))
        xi0 = max(0, int(math.floor(x0)))
        yi0 = max(0, int(math.floor(y0)))
        xi1 = min(self.w, int(math.ceil(x0 + vw)) + 1)
        yi1 = min(self.h, int(math.ceil(y0 + vh)) + 1)
        if xi1 <= xi0 or yi1 <= yi0:
            out = np.empty((self.h, self.w, 3), np.uint8)
            out[:] = bg
            return out
        sub = img[yi0:yi1, xi0:xi1]
        up = cv2.resize(sub, None, fx=k, fy=k, interpolation=cv2.INTER_NEAREST)
        f = s / k                        # <= 1 when s > 1: the bilinear step down
        # `up` pixel (u,v) is source site (xi0+u/k, yi0+v/k); the output pixel is
        # the source site's offset from the view corner, times s
        m = np.float32([[f, 0, (xi0 - x0) * s], [0, f, (yi0 - y0) * s]])
        return cv2.warpAffine(up, m, (self.w, self.h), flags=cv2.INTER_LINEAR,
                              borderMode=cv2.BORDER_CONSTANT, borderValue=bg)

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
