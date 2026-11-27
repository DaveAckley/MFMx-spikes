"""notchwatch.py -- spot 'corner notches' in the picture as they happen.

Liv's corner notches (OPEN_ISSUES 'CORNER NOTCHES'): square holes at hub
corners, sharp to their borders, that neighboring physics soon fills.
Suspected: a whole corner cache (OW x OH sites) emptied and the
emptiness landing in live sites.  Too quick to catch by eye, so
mfmxctl looks at every frame (`notch on`, Machine.frame):

  * at every interior hub corner of the picture (hub origins sit at
    multiples of T6GRID_BASE_WIDTH x T6GRID_BASE_HEIGHT pixels,
    HubGeometry.py) take the four squares that meet there, one per
    hub, each up to T6GRID_OVERLAP_WIDTH x _HEIGHT (the cache ring)
  * per square, the occupied fraction of the nested squares of side
    4, 8, ... up to the ring size, measured outward from the corner
  * a NOTCH: a square that is nearly empty now (<= LO) but was mostly
    full (>= HI) within the last HISTORY frames, while the other three
    squares at that corner are still fairly full (>= NEIGHBOR); fronts
    and nuke holes do not empty one corner square alone
  * reported once, at its largest qualifying side, with the fill of
    the 4-site band just outside it (high = sharp border), then not
    again until the square refills

CONTROLS (2026-11-27).  FB1 makes holes of its own everywhere, so the
same test also runs at two kinds of control point, counted apart:
`edge` (the midpoint of every hub edge: two hubs, edge bands but no
corner cache) and `center` (every hub's center: one hub, no cache).
Only an excess of `corner` notches per point over the controls, mostly
in the large sides, is the artifact.  Run 154951 (corners only) found
~1000 per 25 min, mostly 4x4 and 8x8, which the controls should show
to be FB1 background.

Needs `grid off` (grid crosses count as occupied).  The empty test is
zoomview's brightest-channel threshold, without its inaccessible-gray
exception (no point is near a permanent edge).  Pure numpy; the caller
passes the frame image (H x W x 3 BGR) and gets back a list of Notch
records.  Test: tools/notch_test.py.
"""
import collections

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

import HubGeometry as HG
from zoomview import EMPTY_MAX   # the same empty threshold

HI = 0.60           #< fill a square had before it emptied
LO = 0.05           #< fill that counts as emptied
NEIGHBOR = 0.40     #< the other three squares at the corner
HISTORY = 15        #< frames back (~3 s at 5 Hz) to look for HI
SAY_SIDE = 16       #< corner notches this big go to the console too
STEP = 4            #< nested square sides: STEP, 2*STEP, ...

# quadrant name -> (dx, dy): direction away from the corner point
QUADS = (("NW", -1, -1), ("NE", 1, -1), ("SW", -1, 1), ("SE", 1, 1))

KINDS = ("corner", "edge", "center")    #< the test point kinds


Notch = collections.namedtuple(
    "Notch", "kind x y quad side before fill outside px py")
# kind: corner, edge or center (KINDS); x, y: the point (picture
# pixels); quad: which square (NW NE SW
# SE of the point); side: sites per side of the emptied square; before:
# its best fill in the history; fill: now; outside: fill of the band of
# width STEP just outside it; px, py: a pixel inside it (for `who`)


class NotchWatch:
    def __init__(self, width=HG.DEMO_GLOBAL_GRID_WIDTH,
                 height=HG.DEMO_GLOBAL_GRID_HEIGHT,
                 bw=HG.T6GRID_BASE_WIDTH, bh=HG.T6GRID_BASE_HEIGHT,
                 ring=min(HG.T6GRID_OVERLAP_WIDTH, HG.T6GRID_OVERLAP_HEIGHT)):
        self.on = False
        self.hold = False           #< pause the cycle at the first notch
        self.width = width
        self.height = height
        self.skipped = 0            #< frames of another size (not checked)
        self.side = ring - ring % STEP
        self.sides = list(range(STEP, self.side + 1, STEP))
        s = self.side + STEP        # gather one band more, for 'outside'
        self.gside = s
        self.dd = np.array(self.sides + [s])
        nx = width // bw + 1
        ny = height // bh + 1
        hx = bw // 2
        hy = bh // 2
        cand = []
        for j in range(ny):
            for i in range(nx):
                x = i * bw
                y = j * bh
                cand.append(("corner", x, y))
                cand.append(("edge", x + hx, y))    # top edge midpoint
                cand.append(("edge", x, y + hy))    # left edge midpoint
                cand.append(("center", x + hx, y + hy))
        pts = [(kd, x, y) for (kd, x, y) in cand
               if s <= x <= width - s and s <= y <= height - s]
        self.points = [(x, y) for (kd, x, y) in pts if kd == "corner"]
        self.npoints = {kd: sum(1 for p in pts if p[0] == kd)
                        for kd in KINDS}
        # square q = (point, quadrant); QUADS order within each point
        self.keys = []
        for (kd, x, y) in pts:
            for (name, dx, dy) in QUADS:
                self.keys.append((kd, x, y, name, dx, dy))
        # top-left corner of each square's window, and which windows
        # are mirrored (W / N of their point)
        self.x0 = np.array([x if dx > 0 else x - s
                            for (_k, x, _y, _n, dx, _dy) in self.keys])
        self.y0 = np.array([y if dy > 0 else y - s
                            for (_k, _x, y, _n, _dx, dy) in self.keys])
        self.flipx = np.array([dx < 0 for (*_r, dx, _dy) in self.keys])
        self.flipy = np.array([dy < 0 for (*_r, dy) in self.keys])
        self.hist = collections.deque(maxlen=HISTORY)
        self.armed = np.ones(len(self.keys), dtype=bool)
        self.frames = 0
        self.found = 0
        self.by_kind = {kd: 0 for kd in KINDS}
        self.log = collections.deque(maxlen=50)

    def fills(self, img):
        """(Q, len(sides) + 1) occupied fraction of each nested square,
        the last column being the side + STEP square."""
        # brightest channel once per frame (~3 ms on 1920 x 1080)
        m = np.maximum(np.maximum(img[:, :, 0], img[:, :, 1]), img[:, :, 2])
        if not m.flags.c_contiguous:
            m = np.ascontiguousarray(m)
        # every square's gside x gside window as one strided gather
        g = sliding_window_view(m, (self.gside, self.gside))[self.y0,
                                                                self.x0]
        occ = np.ascontiguousarray(g > EMPTY_MAX).view(np.uint8)
        # STEP x STEP block counts: 4 bytes of 0/1 in a u32, summed by
        # the multiply (the top byte collects all four)
        assert STEP == 4
        v = occ.view(np.uint32)                     # (Q, s, s/4)
        r = ((v * np.uint32(0x01010101)) >> np.uint32(24)).astype(np.int16)
        nb = self.gside // STEP
        r = r.reshape(-1, nb, STEP, nb)
        blk = r[:, :, 0] + r[:, :, 1] + r[:, :, 2] + r[:, :, 3]  # (Q,nb,nb)
        # orient so block (0, 0) touches the point
        blk[self.flipx] = blk[self.flipx][:, :, ::-1]
        blk[self.flipy] = blk[self.flipy][:, ::-1, :]
        cs = blk.cumsum(axis=1).cumsum(axis=2)
        k = self.dd // STEP                         # nested sides, blocks
        return cs[:, k - 1, k - 1] / (self.dd * self.dd).astype(np.float64)

    def check(self, img):
        """Look at one frame; return the new Notch records."""
        if not self.on:
            return []
        sh = getattr(img, "shape", (0, 0))
        if sh[0] < self.height or sh[1] < self.width:
            self.skipped += 1       # e.g. the mock's small frames
            return []
        f = self.fills(img)
        self.frames += 1
        found = []
        n = len(self.sides)
        if self.hist:
            before = np.max(np.stack(self.hist), axis=0)    # (Q, n+1)
            cond = (f[:, :n] <= LO) & (before[:, :n] >= HI)
            hit = cond.any(axis=1) & self.armed
            # largest qualifying side per square
            best = n - 1 - np.argmax(cond[:, ::-1], axis=1)
            # the smallest fill of the other three squares at the point
            f4 = f[:, :n].reshape(-1, len(QUADS), n)
            om = np.empty_like(f4)
            for j in range(len(QUADS)):
                rest = [k for k in range(len(QUADS)) if k != j]
                om[:, j, :] = f4[:, rest, :].min(axis=1)
            om = om.reshape(-1, n)
            qs = np.nonzero(hit)[0]
            for q in qs:
                i = int(best[q])
                if om[q, i] < NEIGHBOR:
                    continue
                side = self.sides[i]
                d = side + STEP
                outside = (f[q, i + 1] * d * d - f[q, i] * side * side) / \
                    float(d * d - side * side)
                kd, x, y, name, dx, dy = self.keys[q]
                px = x + (side // 2 if dx > 0 else -1 - side // 2)
                py = y + (side // 2 if dy > 0 else -1 - side // 2)
                nt = Notch(kd, x, y, name, side, float(before[q, i]),
                           float(f[q, i]), float(outside), px, py)
                found.append(nt)
                self.by_kind[kd] += 1
                self.armed[q] = False
            # a reported square re-arms once it has refilled (not in the
            # frame it is reported)
            refilled = ~self.armed & (f[:, n - 1] >= HI)
            refilled[qs] = False
            self.armed |= refilled
        self.hist.append(f)
        self.found += len(found)
        self.log.extend(nt for nt in found if nt.kind == "corner")
        return found

    def reset(self):
        self.hist.clear()
        self.armed[:] = True

    def status(self):
        return (f"notch watch {'on' if self.on else 'off'}"
                f"{' (hold on first)' if self.hold else ''}: "
                f"{sum(self.npoints.values())} points x 4 squares up to "
                f"{self.side} sites; {self.frames} frames checked, "
                f"{self.found} notches ("
                + ", ".join(f"{kd} {self.by_kind[kd]} at "
                            f"{self.npoints[kd]} points" for kd in KINDS)
                + ")"
                + (f", {self.skipped} frames of the wrong size skipped"
                   if self.skipped else ""))


def points_line(w):
    """The header line logged at `notch on`, for runsum's per-point
    rates."""
    return "NOTCHPOINTS " + " ".join(f"{kd} {w.npoints[kd]}" for kd in KINDS)


def describe(n, scy=None, owner=None):
    """One line for the console and misc/...-NOTCH.txt."""
    w = f"scy {scy} " if scy is not None else ""
    o = f" {owner}" if owner else ""
    return (f"NOTCH {w}{n.kind} ({n.x},{n.y}) {n.quad} {n.side}x{n.side} "
            f"fill {n.before:.0%} -> {n.fill:.0%}, outside band "
            f"{n.outside:.0%}; who {n.px} {n.py}{o}")
