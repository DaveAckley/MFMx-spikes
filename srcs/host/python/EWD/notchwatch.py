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

Needs `grid off` (grid crosses count as occupied).  Pure numpy; the
caller passes the frame image (H x W x 3 BGR) and gets back a list of
Notch records.  Test: tools/notch_test.py.
"""
import collections

import numpy as np

import HubGeometry as HG
from zoomview import EMPTY_MAX, INACCESSIBLE_BGR   # the same empty test

HI = 0.60           #< fill a square had before it emptied
LO = 0.05           #< fill that counts as emptied
NEIGHBOR = 0.40     #< the other three squares at the corner
HISTORY = 15        #< frames back (~3 s at 5 Hz) to look for HI
STEP = 4            #< nested square sides: STEP, 2*STEP, ...

# quadrant name -> (dx, dy): direction away from the corner point
QUADS = (("NW", -1, -1), ("NE", 1, -1), ("SW", -1, 1), ("SE", 1, 1))


def occupied(px):
    """Occupied mask of a (..., 3) BGR array (zoomview.occupied_mask)."""
    b = px[..., 0]
    g = px[..., 1]
    r = px[..., 2]
    occ = (b > EMPTY_MAX) | (g > EMPTY_MAX) | (r > EMPTY_MAX)
    occ &= ~((b == INACCESSIBLE_BGR[0]) &
             (g == INACCESSIBLE_BGR[1]) &
             (r == INACCESSIBLE_BGR[2]))
    return occ


Notch = collections.namedtuple(
    "Notch", "x y quad side before fill outside px py")
# x, y: the corner point (picture pixels); quad: which square (NW NE SW
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
        pts = [(i * bw, j * bh)
               for j in range(1, height // bh + 1)
               for i in range(1, width // bw + 1)
               if i * bw + s <= width and j * bh + s <= height
               and i * bw - s >= 0 and j * bh - s >= 0]
        self.points = pts
        # gather indices: square q = (point, quadrant), k-th row/column
        # counted outward from the corner point
        k = np.arange(s)
        ys = []
        xs = []
        self.keys = []
        for (x, y) in pts:
            for (name, dx, dy) in QUADS:
                xs.append(x + (k if dx > 0 else -1 - k))
                ys.append(y + (k if dy > 0 else -1 - k))
                self.keys.append((x, y, name, dx, dy))
        self.ys = np.array(ys)[:, :, None]          # (Q, s, 1)
        self.xs = np.array(xs)[:, None, :]          # (Q, 1, s)
        self.hist = collections.deque(maxlen=HISTORY)
        self.armed = np.ones(len(self.keys), dtype=bool)
        self.frames = 0
        self.found = 0
        self.log = collections.deque(maxlen=50)

    def fills(self, img):
        """(Q, len(sides) + 1) occupied fraction of each nested square,
        the last column being the side + STEP square."""
        occ = occupied(img[self.ys, self.xs]).astype(np.int32)  # (Q,s,s)
        cs = occ.cumsum(axis=1).cumsum(axis=2)
        out = []
        for d in self.sides + [self.gside]:
            out.append(cs[:, d - 1, d - 1] / float(d * d))
        return np.stack(out, axis=1)

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
        if self.hist:
            before = np.max(np.stack(self.hist), axis=0)    # (Q, n+1)
            nq = len(QUADS)
            for q in range(len(self.keys)):
                if not self.armed[q]:
                    if f[q, -2] >= HI:
                        self.armed[q] = True
                    continue
                best = None
                for i, side in enumerate(self.sides):
                    if f[q, i] <= LO and before[q, i] >= HI:
                        best = i
                if best is None:
                    continue
                base = q - q % nq
                others = [f[base + j, best] for j in range(nq)
                          if base + j != q]
                if min(others) < NEIGHBOR:
                    continue
                side = self.sides[best]
                d = side + STEP
                outer = f[q, best + 1] if best + 1 < len(self.sides) \
                    else f[q, -1]
                outside = (outer * d * d - f[q, best] * side * side) / \
                    float(d * d - side * side)
                x, y, name, dx, dy = self.keys[q]
                px = x + (side // 2 if dx > 0 else -1 - side // 2)
                py = y + (side // 2 if dy > 0 else -1 - side // 2)
                n = Notch(x, y, name, side, float(before[q, best]),
                          float(f[q, best]), float(outside), px, py)
                found.append(n)
                self.armed[q] = False
        self.hist.append(f)
        self.found += len(found)
        self.log.extend(found)
        return found

    def reset(self):
        self.hist.clear()
        self.armed[:] = True

    def status(self):
        return (f"notch watch {'on' if self.on else 'off'}"
                f"{' (hold on first)' if self.hold else ''}: "
                f"{len(self.points)} corners x 4 squares up to "
                f"{self.side} sites; {self.frames} frames checked, "
                f"{self.found} notches"
                + (f", {self.skipped} frames of the wrong size skipped"
                   if self.skipped else ""))


def describe(n, scy=None, owner=None):
    """One line for the console and misc/...-NOTCH.txt."""
    w = f"scy {scy} " if scy is not None else ""
    o = f" {owner}" if owner else ""
    return (f"NOTCH {w}corner ({n.x},{n.y}) {n.quad} {n.side}x{n.side} "
            f"fill {n.before:.0%} -> {n.fill:.0%}, outside band "
            f"{n.outside:.0%}; who {n.px} {n.py}{o}")
