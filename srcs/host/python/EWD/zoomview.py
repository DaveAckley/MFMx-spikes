"""zoomview.py -- host-side zoom for the video feed.

The host keeps one 1920 x 1080 BGR image, a pixel per site.  ZoomView crops
and scales that image on its way to the feed; nothing on the hubs changes.

  mode "off"    frames go out untouched
  mode "auto"   fit the box of every non-empty site ever seen into the frame
                as large as possible, sites kept square.  The box only
                grows, so the zoom only moves outward.  The goal is only
                recomputed when the box outgrows the shown view; it is then
                set with headroom, and the view eases to it over ~3 s.
                `shrink on` makes the box the union of the last few seconds'
                occupied boxes instead of everything ever seen, so the view
                also zooms back in (only after the tight fit stays well
                above the shown scale, to ignore flicker)
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
import collections
import time
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
OUTSIDE_BGR = (20, 4, 12)       # beyond the grid: very very dark purple (RGB 12,4,20)


class ZoomView:
    def __init__(self, width=W, height=H):
        self.w, self.h = width, height
        self.mode = "off"
        self.fixed = 1.0
        self.hub_base = (137, 108)        # DG T6GRID_BASE_WIDTH/HEIGHT (sites per hub)
        self.min_box = (120, 68)          # sites: caps the zoom on a lone start atom
        self.max_scale = 16.0
        self.margin = 0.04                # of the box, each side, when first fitted
        self.headroom = 0.14              # extra margin when zooming out, so growth is absorbed
        self.edge = 0.05                  # box nearer than this to the view edge: zoom out
        self.anim_frames = 15             # an ease takes this many frames (3 s at 5 Hz)
        self.shrink = False               # box = recent window, not ever-seen
        self.window = 25                  # frames the shrink box remembers (5 s at 5 Hz)
        self.zoom_in_ratio = 1.3          # zoom in only when the tight fit is this much tighter
        self.recent = collections.deque(maxlen=self.window)
        self.goal = None                  # (cx, cy, scale) being eased toward
        self.anim = None                  # (from, i): ease progress
        self.box = None                   # [x0, y0, x1, y1), ever-seen, in sites
        self.cur = None                   # (cx, cy, scale) being shown
        self.last_ms = 0.0
        # Cost of each stage, ms: [calls, total, max].  mfmxctl is one thread,
        # so a slow frame delays noticing a finished bolt (consensus times).
        self.cost = {"scan": [0, 0.0, 0.0], "step": [0, 0.0, 0.0],
                     "render": [0, 0.0, 0.0], "frame": [0, 0.0, 0.0]}
        self.scans_skipped = 0            # scans left out while a bolt was pending

    def note_cost(self, what, t0):
        """Record the time since t0 (perf_counter) under `what`."""
        ms = (time.perf_counter() - t0) * 1000.0
        c = self.cost[what]
        c[0] += 1
        c[1] += ms
        if ms > c[2]:
            c[2] = ms

    def cost_text(self):
        parts = []
        for k in ("scan", "step", "render", "frame"):
            n, tot, mx = self.cost[k]
            if n:
                parts.append(f"{k} {tot / n:.1f}/{mx:.0f}ms")
        return (("cv2" if cv2 is not None else "numpy (NO cv2: slow render)")
                + "; avg/max " + (", ".join(parts) if parts else "no frames yet")
                + (f"; {self.scans_skipped} scans skipped for a pending bolt"
                   if self.scans_skipped else ""))

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

    def set_shrink(self, on):
        self.shrink = bool(on)
        self.recent.clear()
        if not self.shrink:
            pass                          # ever-seen box resumes from the current one
        return self.status()

    def reset_box(self):
        self.box, self.cur, self.goal, self.anim = None, None, None, None
        self.recent.clear()

    def status(self):
        s = f"zoom: {self.mode}" + (" +shrink" if self.shrink else "")
        if self.mode == "fixed":
            s += f" {self.fixed:g}x"
        if self.box is not None:
            x0, y0, x1, y1 = self.box
            s += f"; seen box {x1 - x0}x{y1 - y0} at ({x0},{y0})"
        if self.cur is not None:
            s += f"; showing {self.cur[2]:.2f}x"
        return s

    def status_long(self):
        return self.status() + "\n  cost: " + self.cost_text()

    # ---- geometry (pure) -------------------------------------------------------
    def observe_box(self, x0, y0, x1, y1):
        """Union a box of occupied sites into the ever-seen box."""
        if self.box is None:
            self.box = [x0, y0, x1, y1]
        else:
            b = self.box
            self.box = [min(b[0], x0), min(b[1], y0), max(b[2], x1), max(b[3], y1)]

    def frame_box(self, img):
        """The box of occupied sites in this frame, or None if there are none."""
        occ = img.max(axis=2) > EMPTY_MAX
        occ &= ~((img[:, :, 0] == INACCESSIBLE_BGR[0]) &
                 (img[:, :, 1] == INACCESSIBLE_BGR[1]) &
                 (img[:, :, 2] == INACCESSIBLE_BGR[2]))
        rows = np.flatnonzero(occ.any(axis=1))
        if rows.size == 0:
            return None
        cols = np.flatnonzero(occ.any(axis=0))
        return (int(cols[0]), int(rows[0]), int(cols[-1]) + 1, int(rows[-1]) + 1)

    def occupancy_map(self, img, cols=48, rows=18):
        """Text map of occupied sites per cell (. none, 1-9 tenths of the cell
        full, # >= 90%), plus the box.  For finding what holds the box open."""
        occ = img.max(axis=2) > EMPTY_MAX
        occ &= ~((img[:, :, 0] == INACCESSIBLE_BGR[0]) &
                 (img[:, :, 1] == INACCESSIBLE_BGR[1]) &
                 (img[:, :, 2] == INACCESSIBLE_BGR[2]))
        ch, cw = self.h // rows, self.w // cols
        lines = []
        for r in range(rows):
            row = ""
            for c in range(cols):
                n = int(occ[r * ch:(r + 1) * ch, c * cw:(c + 1) * cw].sum())
                f = n / float(ch * cw)
                row += "." if n == 0 else ("#" if f >= 0.9 else str(max(1, int(f * 10))))
            lines.append(row)
        # hub-relative histograms: every hub also paints its 20-site south/east
        # cache ring onto its neighbor's pixels, so a stale copy would show up
        # as a spike in the first 20 columns/rows of a hub (x%137<20, y%108<20)
        bx, by = self.hub_base
        colcount = occ.sum(axis=0)
        rowcount = occ.sum(axis=1)
        hx = np.zeros(bx, np.int64)
        hy = np.zeros(by, np.int64)
        for i in range(bx):
            hx[i] = colcount[i::bx].sum()
        for i in range(by):
            hy[i] = rowcount[i::by].sum()
        def bucket(h):
            return " ".join(str(int(h[i:i + 20].sum())) for i in range(0, len(h), 20))
        return ("\n".join(lines) + f"\n(each cell {cw}x{ch} sites; {int(occ.sum())} occupied; "
                f"frame box {self.frame_box(img)})"
                f"\noccupied by x mod {bx}, columns 0-19 20-39 ...: {bucket(hx)}"
                f"\noccupied by y mod {by}, rows 0-19 20-39 ...: {bucket(hy)}")

    def occupied_mask(self, img):
        occ = img.max(axis=2) > EMPTY_MAX
        occ &= ~((img[:, :, 0] == INACCESSIBLE_BGR[0]) &
                 (img[:, :, 1] == INACCESSIBLE_BGR[1]) &
                 (img[:, :, 2] == INACCESSIBLE_BGR[2]))
        return occ

    def region_map(self, img, x0, y0, x1, y1, cw=3, ch=4):
        """Occupancy map of a pixel rectangle, one char per cw x ch sites
        (. none, 1-9 tenths, # >= 90%), laid out like scripts/l1dump/l1grid.py
        --profile so a hub's real grid and its picture compare cell by cell
        (a hub's whole grid is pixels (hx*137-20 .. hx*137+157) wide)."""
        occ = self.occupied_mask(img)
        x0, y0 = max(0, x0), max(0, y0)
        x1, y1 = min(self.w, x1), min(self.h, y1)
        out = [f"image region x {x0}..{x1 - 1}, y {y0}..{y1 - 1} (cells {cw}x{ch}):"]
        for y in range(y0, y1, ch):
            row = ""
            for x in range(x0, x1, cw):
                blk = occ[y:min(y + ch, y1), x:min(x + cw, x1)]
                n = int(blk.sum())
                f = n / float(blk.size)
                row += "." if n == 0 else ("#" if f >= 0.9 else str(max(1, int(f * 10))))
            out.append(f"{y - y0:3d} {row}")
        return "\n".join(out)

    def specks(self, img, radius=5):
        """Occupied sites with no other occupied site within `radius` (a box):
        lone pixels in an empty neighborhood, the signature of stale ghost
        pixels.  Returns (ys, xs) arrays."""
        occ = self.occupied_mask(img).astype(np.int32)
        h, w = occ.shape
        ii = np.zeros((h + 1, w + 1), np.int64)
        ii[1:, 1:] = occ.cumsum(axis=0).cumsum(axis=1)
        y0 = np.clip(np.arange(h) - radius, 0, h)
        y1 = np.clip(np.arange(h) + radius + 1, 0, h)
        x0 = np.clip(np.arange(w) - radius, 0, w)
        x1 = np.clip(np.arange(w) + radius + 1, 0, w)
        win = (ii[y1][:, x1] - ii[y0][:, x1] - ii[y1][:, x0] + ii[y0][:, x0])
        return np.nonzero((occ == 1) & (win == 1))

    def specks_report(self, img, radius=5, show=40):
        ys, xs = self.specks(img, radius)
        bx, by = self.hub_base
        out = [f"{len(ys)} lone occupied sites (no other within {radius} sites)"]
        if len(ys):
            hx = np.bincount(xs % bx, minlength=bx)
            hy = np.bincount(ys % by, minlength=by)
            out.append("by x mod %d, columns 0-19 20-39 ...: %s" % (
                bx, " ".join(str(int(hx[i:i + 20].sum())) for i in range(0, bx, 20))))
            out.append("by y mod %d, rows 0-19 20-39 ...: %s" % (
                by, " ".join(str(int(hy[i:i + 20].sum())) for i in range(0, by, 20))))
            order = np.lexsort((ys, xs))
            pts = [f"({int(xs[i])},{int(ys[i])})" for i in order[:show]]
            out.append("first %d (x,y): %s%s" % (len(pts), " ".join(pts),
                                                  " ..." if len(ys) > show else ""))
        return "\n".join(out)

    def note_frame_box(self, fb):
        """Fold one frame's box in: ever-seen union, or (shrink) the recent window."""
        if fb is None:
            return
        if not self.shrink:
            self.observe_box(*fb)
            return
        self.recent.append(fb)
        self.box = [min(b[0] for b in self.recent), min(b[1] for b in self.recent),
                    max(b[2] for b in self.recent), max(b[3] for b in self.recent)]

    def observe(self, img):
        self.note_frame_box(self.frame_box(img))

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
        if self.mode == "auto":
            if not self.box_fits(self.goal):
                self.goal = self.target(self.margin + self.headroom)
                self.anim = (self.cur, 0)
            elif self.shrink:
                t = self.target(self.margin + self.headroom)
                if t is not None and t[2] > self.goal[2] * self.zoom_in_ratio:
                    self.goal = t               # the computation shrank: zoom in
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
        It may extend past the image; those parts are drawn dark purple (OUTSIDE_BGR)."""
        vw, vh = self.w / s, self.h / s
        return cx - vw / 2.0, cy - vh / 2.0, vw, vh

    # ---- pixels ----------------------------------------------------------------
    def render(self, img, cx, cy, s):
        """Crop and scale img to the full frame at scale s centered on (cx,cy);
        parts of the view outside the image are OUTSIDE_BGR."""
        x0, y0, vw, vh = self.view_rect(cx, cy, s)
        if s <= 1.0001 and abs(x0) < 1e-6 and abs(y0) < 1e-6:
            return np.ascontiguousarray(img)
        bg = OUTSIDE_BGR
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

    def apply(self, img, between=None, scan=True):
        """img: (h,w,3) uint8.  Returns the frame to send (img itself when off).
        between: called after each stage (the one thread's chance to notice a
        finished bolt).  scan=False leaves out the box scan this frame (the box
        and view stay as they were)."""
        if self.mode == "off":
            return img
        if self.mode == "auto" or self.box is None:
            if scan:
                t0 = time.perf_counter()
                self.observe(img)
                self.note_cost("scan", t0)
                if between:
                    between()
            else:
                self.scans_skipped += 1
        t0 = time.perf_counter()
        v = self.step()
        self.note_cost("step", t0)
        if between:
            between()
        if v is None:
            return img
        t0 = time.perf_counter()
        out = self.render(img, *v)
        self.note_cost("render", t0)
        return out
