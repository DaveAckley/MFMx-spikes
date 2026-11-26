#!/usr/bin/env python3
"""geomlab: compare ways of tiling the 1918 x 1080 MFMx grid onto hubs.

Host-only, no hardware.  Each LAYOUT gives every grid site a PART label
(the unit that events may not cross in one supercycle) and maps parts to
the 140 physical hubs (14 x 10 hub array, 7 x 5 per chip).  A SHIFT
POLICY moves the whole partition by a vector each supercycle, as four
corners does today (shift (+-OW, +-OH) = expand toward a corner).

THE MODEL (one supercycle with shift s):
  - part_s(p) = part(p - s): every part slides by s.
  - a site is a LIVE event center iff its radius-R diamond window lies
    in ONE part_s label (window sites off the grid are INACCESSIBLE and
    do not count, like a permanent edge); otherwise it is DEAD.
  - hub h BORROWS every site whose part_s belongs to h but whose owner
    is another hub: that is h's cache for this cycle.  The union over
    all shifts is the ring h must STORE; the per-cycle amount is the
    EXCHANGE.  For today's rectangles this reproduces the 177 x 148
    T6Grid and the 2R dead strips.

Metrics per layout (one row each):
  hubs      hubs owning any site, of 140
  sites/hub min / mean / max owned sites, and cv (spread)
  dead%     mean dead centers per cycle, percent of grid sites
  never%    sites that are a live center in NO shift of the cycle
  ring      mean stored cache per hub / mean owned (ring overhead)
  xchg      mean sites borrowed per hub per cycle
  ngb       mean / max neighbor hubs per hub (hubs it borrows from)
  hops      max physical hub distance (Chebyshev) to a neighbor, and
            how many neighbor pairs are farther than 1
  pack/hub  EW close packing: per hub, best EW tiling coset, sites in
            fully-inside EWs / owned (1.000 = every site covered)
  pack/all  same, one coset for the whole grid

Geometry inputs come from srcs/host/python/EWD/HubGeometry.py (never
write 137 / 108 here).  Pictures: OUT/NAME.png, hub colors, dead
centers of the first shift darkened, never-live sites red.

Usage: python3 geomlab.py [--out DIR] [--only NAME[,NAME..]] [--list]
       [--zoom X,Y] [--zsize SITES] [--zscale PX]  (close-ups: NAME-zoom.png,
       NAME-intensity.png)
       python3 geomlab.py --check      (self-test against HubGeometry)
       python3 geomlab.py --crosscheck (EWLattice.h == lab rect-snap)
       python3 geomlab.py --tileable   (exact EW covers of shapes)
       python3 geomlab.py --ewtiles    (picture of the EW tiling)
       python3 geomlab.py --ewblock    (one hub-sized block of EWs)
       python3 geomlab.py --ewrect     (today's hub on the EW tiling)
       python3 geomlab.py --ewsnap     (that hub rounded to whole EWs)
Needs numpy and PIL (no scipy).  About 20 s for all layouts.
"""

import argparse
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..",
                                "srcs", "host", "python", "EWD"))
import HubGeometry as HG                      # noqa: E402

R = 4                       # WINDOW_RADIUS (CachingPolicy.h)
W = HG.ACTIVE_HUB_WIDTH * HG.T6GRID_BASE_WIDTH * HG.CHIP_ARRAY_WIDTH
H = HG.ACTIVE_HUB_HEIGHT * HG.T6GRID_BASE_HEIGHT * HG.CHIP_ARRAY_HEIGHT
HUBS_X = HG.ACTIVE_HUB_WIDTH * HG.CHIP_ARRAY_WIDTH     # 14
HUBS_Y = HG.ACTIVE_HUB_HEIGHT * HG.CHIP_ARRAY_HEIGHT   # 10
NH = HUBS_X * HUBS_Y
OW = HG.T6GRID_OVERLAP_WIDTH
OH = HG.T6GRID_OVERLAP_HEIGHT

OFFSETS = [(dx, dy)
           for dx in range(-R, R + 1)
           for dy in range(-R, R + 1)
           if abs(dx) + abs(dy) <= R]

# The two perfect EW tilings of Z^2 (Lee spheres, R=4, 41 sites): a
# center p is on lattice k, coset c iff (KX*x + KY*y) mod 41 == c.
EW_SITES = 2 * R * R + 2 * R + 1
EW_LATTICES = [(R, R + 1),
               (R + 1, R)]

YY, XX = np.mgrid[0:H, 0:W]


def hub_xy(h):
    return (h % HUBS_X, h // HUBS_X)


def hub_chip(h):
    hx, hy = hub_xy(h)
    return (hx // HG.ACTIVE_HUB_WIDTH, hy // HG.ACTIVE_HUB_HEIGHT)


# ---------------------------------------------------------------- shifts

def corner_shifts(w, h):
    """Today's four corners: expand toward a corner by (w, h)."""
    return [(w, h), (-w, h), (-w, -h), (w, -h)]


def vertex_shifts(w):
    """Diamond analog: expand toward a vertex, moving two edges by w
    diagonal lines each."""
    return [(w, 0), (0, w), (-w, 0), (0, -w)]


# --------------------------------------------------------------- layouts

class Layout:
    def __init__(self, name, part, hub_of_part, shifts, note,
                 part_at=None):
        self.name = name
        self.part = part                    # H x W int64, -1 = none
        self.hub_of_part = hub_of_part      # int64 array, part -> hub
        self.shifts = shifts
        self.note = note
        # part labels for one shift; default: the partition slid by s
        self.part_at = part_at or (lambda s: shifted(self.part, s))

    def owner(self):
        own = np.full(self.part.shape, -1, dtype=np.int64)
        ok = self.part >= 0
        own[ok] = self.hub_of_part[self.part[ok]]
        return own


def layout_rect():
    """Today: hub pitch from HubGeometry, four corners shifts."""
    hx = XX // HG.T6GRID_BASE_WIDTH
    hy = YY // HG.T6GRID_BASE_HEIGHT
    part = (hy * HUBS_X + hx).astype(np.int64)
    return Layout("rect", part, np.arange(NH, dtype=np.int64),
                  corner_shifts(OW, OH),
                  "today: 137x108 hubs, four corners")


def diamond_cells(c, ox, oy):
    """Diamond cells: c x c squares in u = x+y, v = x-y about (ox, oy).
    A full cell holds c*c/2 sites and has c/2 sites per side."""
    u = (XX - ox) + (YY - oy)
    v = (XX - ox) - (YY - oy)
    return np.floor_divide(u, c), np.floor_divide(v, c)


def compact_ids(a, b):
    key = (a.astype(np.int64) << 32) + (b.astype(np.int64) & 0xffffffff)
    _, ids = np.unique(key, return_inverse=True)
    return ids.reshape(a.shape).astype(np.int64)


def balanced_assign(part, iters=600):
    """Map parts to hubs: each part goes to a nearby hub (hub 'home' =
    its physical position scaled onto the grid), with per-hub weights
    tuned so owned sites come out as even as the part sizes allow.
    Contiguity of a hub's parts is NOT enforced."""
    n = int(part.max()) + 1
    flat = part.ravel()
    cnt = np.bincount(flat, minlength=n).astype(np.float64)
    cx = np.bincount(flat, weights=XX.ravel(), minlength=n) / cnt
    cy = np.bincount(flat, weights=YY.ravel(), minlength=n) / cnt
    hubs = np.arange(NH)
    homex = (hubs % HUBS_X + 0.5) * W / HUBS_X
    homey = (hubs // HUBS_X + 0.5) * H / HUBS_Y
    d2 = ((cx[:, None] - homex[None, :]) ** 2 +
          (cy[:, None] - homey[None, :]) ** 2)
    scale = (W / HUBS_X) ** 2
    target = cnt.sum() / NH
    lam = np.zeros(NH)
    best = None
    best_score = None
    for it in range(iters):
        asg = np.argmin(d2 - lam[None, :], axis=1)
        load = np.bincount(asg, weights=cnt, minlength=NH)
        score = load.max() / target
        if best_score is None or score < best_score:
            best_score = score
            best = asg.copy()
        eta = 0.5 * (1.0 - it / iters) + 0.02
        lam += eta * scale * (target - load) / target
    return polish(best, cnt, d2 / scale, target)


def polish(asg, cnt, d2, target, near=8, gamma=0.2, passes=60):
    """Local search on the assignment: move one part to a nearby hub,
    or swap two parts between nearby hubs, whenever that lowers
    sum((load - target)^2) / target^2 + gamma * sum(cnt * d2) / target.
    Units are lumpy (a few parts per hub), so this matters."""
    asg = asg.copy()
    load = np.bincount(asg, weights=cnt, minlength=NH)
    cand = np.argsort(d2, axis=1)[:, :near]
    t2 = target * target

    def bal(la, lb, na, nb):
        return ((na - target) ** 2 + (nb - target) ** 2 -
                (la - target) ** 2 - (lb - target) ** 2) / t2

    for _ in range(passes):
        changed = 0
        for u in range(len(asg)):
            a = asg[u]
            c = cnt[u]
            for b in cand[u]:
                if b == a:
                    continue
                d = (bal(load[a], load[b], load[a] - c, load[b] + c) +
                     gamma * c * (d2[u, b] - d2[u, a]) / target)
                if d < -1e-12:
                    asg[u] = b
                    load[a] -= c
                    load[b] += c
                    changed += 1
                    break
                for v in np.nonzero(asg == b)[0]:
                    cv = cnt[v]
                    d = (bal(load[a], load[b],
                             load[a] - c + cv, load[b] + c - cv) +
                         gamma * (c * (d2[u, b] - d2[u, a]) +
                                  cv * (d2[v, a] - d2[v, b])) / target)
                    if d < -1e-12:
                        asg[u] = b
                        asg[v] = a
                        load[a] += cv - c
                        load[b] += c - cv
                        changed += 1
                        break
                if asg[u] != a:
                    break
        if changed == 0:
            break
    return asg.astype(np.int64)


def layout_units(name, part, shifts, note):
    return Layout(name, part, balanced_assign(part), shifts, note)


def layout_diamond_brick():
    """Big diamonds (about one hub of sites each) in brick rows,
    assigned to the nearest hubs; edge fragments join some hub."""
    c = int(round(math.sqrt(2 * HG.SITES_PER_HUB)))   # c*c/2 ~ a hub
    a, b = diamond_cells(c, W // 2, H // 2)
    part = compact_ids(a, b)
    return layout_units("dia-brick", part, vertex_shifts(OW),
                        f"one diamond per hub, c={c} ({c * c // 2} sites)")


def layout_diamond_rotated():
    """Hub array turned 45 degrees: diamond cell (a, b) IS hub
    (b, a), so edge neighbors are physical Dir4 neighbors.  The cell
    is as small as lets 10 cells span the grid diagonally."""
    span = W + H - 1
    c = -(-span // HUBS_Y)
    u = XX + YY
    v = XX - YY + (W - 1)
    a = u // c                              # 0..HUBS_Y-1
    b = v // c                              # 0..HUBS_Y-1
    part = (a * HUBS_X + b).astype(np.int64)
    return Layout("dia-rotated", part, np.arange(NH, dtype=np.int64),
                  vertex_shifts(OW),
                  f"hub array turned 45 deg, c={c} ({c * c // 2} sites)")


def layout_diamond_multi(side, fenced=False):
    """Several small diamonds per hub (side = sites per diamond side),
    clumped onto hubs by balanced_assign.  fenced: events may not
    cross ANY diamond edge, not just hub edges."""
    c = 2 * side
    a, b = diamond_cells(c, W // 2, H // 2)
    cells = compact_ids(a, b)
    hub_of_cell = balanced_assign(cells)
    tag = "fenced" if fenced else "multi"
    name = f"dia-{tag}{side}"
    note = f"diamonds of {side}/side ({c * c // 2} sites), clumped"
    if fenced:
        note += ", fenced per diamond"
        return Layout(name, cells, hub_of_cell, vertex_shifts(OW), note)
    # parts = hubs: events cross diamond edges inside a hub freely
    own = hub_of_cell[cells]
    return Layout(name, own, np.arange(NH, dtype=np.int64),
                  vertex_shifts(OW), note)


_EW_INDEX = None


def ew_tile_index():
    """(i, j) of the EW tile (lattice (4,5),(-5,4)) holding each site
    (computed once)."""
    global _EW_INDEX
    if _EW_INDEX is None:
        _EW_INDEX = _ew_tile_index()
    return _EW_INDEX


def _ew_tile_index():
    ax, ay = R, R + 1
    bx, by = -(R + 1), R
    det = ax * by - bx * ay                 # 41
    ti = (by * XX - bx * YY) / det
    tj = (-ay * XX + ax * YY) / det
    i0 = np.rint(ti).astype(np.int64)
    j0 = np.rint(tj).astype(np.int64)
    bi = np.zeros_like(i0)
    bj = np.zeros_like(j0)
    found = np.zeros(XX.shape, dtype=bool)
    for di in (-1, 0, 1):
        for dj in (-1, 0, 1):
            i = i0 + di
            j = j0 + dj
            cx = i * ax + j * bx
            cy = i * ay + j * by
            hit = (np.abs(XX - cx) + np.abs(YY - cy) <= R) & ~found
            bi[hit] = i[hit]
            bj[hit] = j[hit]
            found |= hit
    assert found.all(), "EW tile lookup missed a site"
    return bi, bj


def layout_ew_blocks():
    """Hub regions = k x k blocks of EW tiles on the perfect EW lattice
    (tilted about 51 degrees, stair-step edges that EWs pack flush)."""
    k = int(round(math.sqrt(HG.SITES_PER_HUB / EW_SITES)))
    i, j = ew_tile_index()
    part = compact_ids(np.floor_divide(i, k), np.floor_divide(j, k))
    return layout_units("ew-blocks", part, vertex_shifts(OW),
                        f"{k}x{k} EW tiles per hub "
                        f"({k * k * EW_SITES} sites)")


def layout_rect_snap(shift=OW, name="rect-snap"):
    """Today's hubs rounded to whole EWs: every EW (perfect lattice)
    belongs to the hub whose rectangle holds its center (clamped onto
    the grid at the edges).  Same hub array and neighbors as rect,
    sawtooth hub edges, every interior EW whole."""
    i, j = ew_tile_index()
    cx = i * R - j * (R + 1)
    cy = i * (R + 1) + j * R

    def part_at(s):
        # each cycle the hub's event area is every EW whose CENTER lies
        # in its rectangle shifted by s (edge hubs extend off the grid)
        hx = np.clip((cx - s[0]) // HG.T6GRID_BASE_WIDTH, 0, HUBS_X - 1)
        hy = np.clip((cy - s[1]) // HG.T6GRID_BASE_HEIGHT, 0, HUBS_Y - 1)
        return (hy * HUBS_X + hx).astype(np.int64)

    return Layout(name, part_at((0, 0)),
                  np.arange(NH, dtype=np.int64),
                  corner_shifts(shift, shift),
                  f"rect hubs rounded to whole EWs (EW center rule), "
                  f"shifted areas snapped too, shift {shift}", part_at)


# ------------------------------------------------- EWLattice.h (diag-tiles)

REPO = os.path.join(HERE, "..", "..")
EWL_HEADER = os.path.join(REPO, "srcs", "shared", "include", "EWLattice.h")
EWL_TEST = os.path.join(REPO, "srcs", "host", "tools", "ewlattice_test.cpp")
WORK_DIR = "geomlab-out"        # main() points this at --out


def ewl_shift():
    """SHIFT as EWLattice.h defines it (read from the header)."""
    import re
    with open(EWL_HEADER) as f:
        m = re.search(r"\bSHIFT = (\d+),", f.read())
    return int(m.group(1))


def ewl_dump():
    """Build ewlattice_test and load its --dump: the header's owner of
    every grid site and the hub whose event area holds it, per Corner4
    (SE, SW, NW, NE = the order of corner_shifts)."""
    import subprocess
    os.makedirs(WORK_DIR, exist_ok=True)
    exe = os.path.join(WORK_DIR, "ewlattice_test")
    out = os.path.join(WORK_DIR, "ewlattice.dump")
    inc = os.path.join(REPO, "srcs")
    subprocess.run(["g++", "-std=c++17", "-O2",
                    "-I" + os.path.join(inc, "host", "include"),
                    "-I" + os.path.join(inc, "shared", "include"),
                    EWL_TEST,
                    os.path.join(inc, "host", "src", "FATAL.cpp"),
                    "-o", exe], check=True)
    subprocess.run([exe, "--dump", out], check=True,
                   stdout=subprocess.DEVNULL)
    raw = np.fromfile(out, dtype=np.uint8)
    assert raw[:4].tobytes() == b"EWL1", "bad ewlattice dump"
    w, h = np.frombuffer(raw[4:12].tobytes(), dtype=np.uint32)
    assert (w, h) == (W, H), (w, h)
    maps = np.frombuffer(raw[12:].tobytes(), dtype=np.int16)
    return maps.reshape(5, H, W).astype(np.int64)


def layout_ewlattice():
    """rect-snap exactly as EWLattice.h computes it (via the dump)."""
    maps = ewl_dump()
    shift = ewl_shift()
    shifts = corner_shifts(shift, shift)
    by_shift = {s: maps[1 + k] for k, s in enumerate(shifts)}
    return Layout("ewlattice", maps[0], np.arange(NH, dtype=np.int64),
                  shifts, f"EWLattice.h rect-snap, SHIFT {shift}",
                  lambda s: by_shift[s])


def crosscheck():
    """EWLattice.h vs the lab's own rect-snap at the same shift: every
    site's owner and per-condition area hub must agree."""
    hdr = layout_ewlattice()
    lab = layout_rect_snap(ewl_shift(), "rect-snap-lab")
    bad = int((hdr.part != lab.part).sum())
    print(f"owner: {bad} sites differ")
    for s in hdr.shifts:
        d = int((hdr.part_at(s) != lab.part_at(s)).sum())
        print(f"area for shift {s}: {d} sites differ")
        bad += d
    # mfmxctl's display overlay (EWD/ewoverlay.py) must agree too
    import ewoverlay
    ov = ewoverlay.owner_map("ewsnap", W, H)
    d = int((ov != hdr.part).sum())
    print(f"ewoverlay owner map: {d} sites differ")
    bad += d
    print("crosscheck " + ("ok" if bad == 0 else f"FAILED ({bad})"))
    return 0 if bad == 0 else 1


def tile_centers(t):
    """Center (x, y) of the radius-t close-packed diamond holding each
    site: the Lee-sphere tiling with lattice (t, t+1), (-(t+1), t); a
    site is a center iff (t*x + (t+1)*y) mod (2t^2+2t+1) == 0, and the
    residue names the site's offset in its diamond.  t = R is the EW
    lattice itself."""
    n = 2 * t * t + 2 * t + 1
    dx = np.zeros(n, dtype=np.int64)
    dy = np.zeros(n, dtype=np.int64)
    for ox in range(-t, t + 1):
        for oy in range(-t, t + 1):
            if abs(ox) + abs(oy) <= t:
                r = (t * ox + (t + 1) * oy) % n
                dx[r] = ox
                dy[r] = oy
    r = (t * XX + (t + 1) * YY) % n
    return XX - dx[r], YY - dy[r]


def layout_snap_t(t, shift=None):
    """rect-snap with BIG tiles: close-packed radius-t diamonds (t >= R),
    each belonging to the hub whose rectangle holds its center; each
    cycle's area is the big tiles whose centers lie in the shifted
    rectangle.  Events stay radius R.  t = R is rect-snap."""
    if shift is None:
        shift = ewl_shift()
    cx, cy = tile_centers(t)

    def part_at(s):
        hx = np.clip((cx - s[0]) // HG.T6GRID_BASE_WIDTH, 0, HUBS_X - 1)
        hy = np.clip((cy - s[1]) // HG.T6GRID_BASE_HEIGHT, 0, HUBS_Y - 1)
        return (hy * HUBS_X + hx).astype(np.int64)

    n = 2 * t * t + 2 * t + 1
    return Layout(f"snap{t}", part_at((0, 0)),
                  np.arange(NH, dtype=np.int64),
                  corner_shifts(shift, shift),
                  f"rect hubs snapped to radius-{t} close-packed diamonds "
                  f"({n} sites), shift {shift}", part_at)


LAYOUTS = {
    "rect": layout_rect,
    "rect-snap": layout_rect_snap,
    "ewlattice": layout_ewlattice,
    "dia-brick": layout_diamond_brick,
    "dia-rotated": layout_diamond_rotated,
    "dia-multi48": lambda: layout_diamond_multi(48),
    "dia-multi50": lambda: layout_diamond_multi(50),
    "dia-fenced48": lambda: layout_diamond_multi(48, fenced=True),
    "ew-blocks": layout_ew_blocks,
    "snap4": lambda: layout_snap_t(4),
    "snap6": lambda: layout_snap_t(6),
    "snap10": lambda: layout_snap_t(10),
    "snap15": lambda: layout_snap_t(15),
}


# --------------------------------------------------------------- measure

def shifted(lab, s):
    """lab moved by s; off-grid parts extend by edge replication."""
    sx, sy = s
    p = max(abs(sx), abs(sy))
    ext = np.pad(lab, p, mode="edge")
    return ext[p - sy:p - sy + H, p - sx:p - sx + W]


def live_mask(lab, strict=False):
    """Centers whose window stays in one label.  Off-grid window sites
    are fine unless strict (then the window must be all on-grid)."""
    off = -2
    pad = np.pad(lab, R, mode="constant", constant_values=off)
    ok = lab >= 0
    for dx, dy in OFFSETS:
        nb = pad[R + dy:R + dy + H, R + dx:R + dx + W]
        if strict:
            ok &= nb == lab
        else:
            ok &= (nb == lab) | (nb == off)
    return ok


def intensity(live):
    """SITE INTENSITY: per site, how many live centers' windows contain
    it this cycle (41 deep inside an area; 1 on a straight orthogonal
    edge; 5 on a straight 45-degree edge; 0 = no window can write it)."""
    pad = np.pad(live.astype(np.int16), R)
    out = np.zeros(live.shape, dtype=np.int16)
    for dx, dy in OFFSETS:
        out += pad[R + dy:R + dy + H, R + dx:R + dx + W]
    return out


def measure(lay):
    own = lay.owner()
    hop = lay.hub_of_part
    owned = np.bincount(own[own >= 0], minlength=NH)
    m = {}
    m["hubs"] = int((owned > 0).sum())
    used = owned[owned > 0]
    m["min"] = int(used.min())
    m["mean"] = float(used.mean())
    m["max"] = int(used.max())
    m["cv"] = float(used.std() / used.mean())
    m["unowned"] = int((own < 0).sum())

    live_count = np.zeros((H, W), dtype=np.int16)
    dead = []
    xchg = []
    cache_keys = []
    pair_keys = []
    bor_keys = []
    runs_max = 0
    runs_n = []
    live_n = []
    rowsY = np.broadcast_to(YY, (H, W))
    site = (YY * W + XX).astype(np.int64)
    lives = []
    ihist = np.zeros(EW_SITES + 1, dtype=np.int64)
    isum = np.zeros((H, W), dtype=np.int32)
    for s in lay.shifts:
        lab = lay.part_at(s)
        live = live_mask(lab)
        lives.append(live)
        it = intensity(live)
        ihist += np.bincount(it.ravel(), minlength=EW_SITES + 1)
        isum += it
        live_count += live
        dead.append(1.0 - live.mean())
        hub_s = np.where(lab >= 0, hop[np.maximum(lab, 0)], -1)
        bor = (hub_s >= 0) & (own >= 0) & (hub_s != own)
        xchg.append(np.bincount(hub_s[bor], minlength=NH))
        cache_keys.append(site[bor] * NH + hub_s[bor])
        bor_keys.append(site[bor] * NH + hub_s[bor])
        # CENTER TABLE: this cycle's live centers of each hub, as runs
        # of consecutive x in each row (what a hub would store to pick
        # a center uniformly: per row, runs + a running count).
        lh = np.where(live, hub_s, -1)
        prev = np.full_like(lh, -1)
        prev[:, 1:] = lh[:, :-1]
        st = (lh >= 0) & (lh != prev)
        per_row = np.bincount(lh[st] * H + rowsY[st],
                              minlength=NH * H)
        runs_max = max(runs_max, int(per_row.max()))
        runs_n.append(np.bincount(lh[st], minlength=NH))
        live_n.append(np.bincount(lh[lh >= 0], minlength=NH))
        pair_keys.append(hub_s[bor] * NH + own[bor])
    m["dead"] = 100.0 * float(np.mean(dead))
    pi = ihist / ihist.sum()
    m["int0"] = 100.0 * float(pi[0])
    m["int1"] = 100.0 * float(pi[1])
    m["int4"] = 100.0 * float(pi[:5].sum())
    m["int_sum"] = (int(isum.min()), float(isum.mean()), int(isum.max()))
    ei, ej = ew_tile_index()
    ecx = ei * R - ej * (R + 1)
    ecy = ei * (R + 1) + ej * R
    latc = (ecx == XX) & (ecy == YY)
    m["deadL"] = 100.0 * float(np.mean(
        [1.0 - lv[latc].mean() for lv in lives]))
    # Are the borrowed bands unions of whole EWs?  Count, per (hub,
    # EW) pair, borrowed sites; whole iff 41 (EWs cut by the grid edge
    # are skipped).
    ekey = compact_ids(ei, ej)
    inside = np.bincount(ekey.ravel(), minlength=int(ekey.max()) + 1)
    whole_ew = inside[ekey] == EW_SITES
    parts_ok = True
    ews = []
    for bk in bor_keys:
        site_ok = whole_ew.ravel()[bk // NH]
        pk = ekey.ravel()[bk // NH][site_ok] * NH + bk[site_ok] % NH
        _, cnt = np.unique(pk, return_counts=True)
        parts_ok &= bool((cnt == EW_SITES).all())
        per_hub = np.bincount(np.unique(pk) % NH, minlength=NH)
        ews.append(per_hub)
    m["bands_whole"] = parts_ok
    rn = np.array(runs_n)[:, owned > 0]
    ln = np.array(live_n)[:, owned > 0]
    m["runs_max"] = runs_max
    m["runs_mean"] = float(rn.mean())
    m["runs_top"] = int(rn.max())
    m["live_rng"] = (int(ln.min()), int(ln.max()))
    ews = np.array(ews)
    m["band_ews"] = (int(ews[:, owned > 0].min()),
                     int(ews[:, owned > 0].max()))
    # Dead area scales like boundary / area, i.e. 1/sqrt(hub area), so
    # also quote it as if hubs had today's SITES_PER_HUB (fair to
    # layouts that leave hubs idle and so have bigger regions).
    area = owned.sum() / m["hubs"]
    m["dead_n"] = m["dead"] * math.sqrt(area / HG.SITES_PER_HUB)
    never = live_count == 0
    m["never"] = 100.0 * float(never.mean())
    cache = np.unique(np.concatenate(cache_keys)) % NH
    ring = np.bincount(cache, minlength=NH)
    m["ring"] = float(ring[owned > 0].mean() / owned[owned > 0].mean())
    m["ring_max"] = int(ring.max())
    m["store_max"] = int((ring + owned).max())
    m["xchg"] = float(np.mean([x[owned > 0].mean() for x in xchg]))
    pairs = np.unique(np.concatenate(pair_keys))
    pa = pairs // NH
    pb = pairs % NH
    ngb = np.bincount(pa, minlength=NH)
    m["ngb_mean"] = float(ngb[owned > 0].mean())
    m["ngb_max"] = int(ngb.max())
    dist = []
    seam = 0
    for a, b in zip(pa, pb):
        ax_, ay_ = hub_xy(a)
        bx_, by_ = hub_xy(b)
        dist.append(max(abs(ax_ - bx_), abs(ay_ - by_)))
        seam += hub_chip(a) != hub_chip(b)
    dist = np.array(dist)
    m["hop_max"] = int(dist.max())
    m["hop_far"] = int((dist > 1).sum())
    m["pairs"] = len(pairs)
    m["seam_pairs"] = int(seam)
    m["pair_list"] = list(zip(pa.tolist(), pb.tolist()))

    strict = live_mask(lay.part, strict=True)
    best_hub = np.zeros(NH)
    best_all = 0
    for kx, ky in EW_LATTICES:
        coset = (kx * XX + ky * YY) % EW_SITES
        hs = own[strict]
        cs = coset[strict]
        cnt = np.bincount(hs * EW_SITES + cs,
                          minlength=NH * EW_SITES).reshape(NH, EW_SITES)
        best_hub = np.maximum(best_hub, cnt.max(axis=1))
        best_all = max(best_all, int(cnt.sum(axis=0).max()))
    ok = owned > 0
    m["pack_hub"] = float(EW_SITES * best_hub[ok].sum() / owned[ok].sum())
    m["pack_all"] = float(EW_SITES * best_all / owned.sum())
    return m, own, lives, never


# --------------------------------------------------------------- picture

PALETTE = np.array([
    (0.90, 0.30, 0.25),
    (0.25, 0.60, 0.90),
    (0.45, 0.80, 0.30),
    (0.95, 0.75, 0.20),
    (0.65, 0.35, 0.85),
    (0.20, 0.80, 0.75),
    (0.95, 0.50, 0.75),
    (0.55, 0.45, 0.25),
    (0.75, 0.75, 0.75),
    (0.30, 0.35, 0.65),
    (0.85, 0.55, 0.30),
    (0.40, 0.55, 0.40),
])


def hub_rgb(own, extra=()):
    """One color per hub; hubs whose sites touch (Moore neighbors), or
    that are listed in extra (e.g. hubs that exchange caches), always
    get different palette colors (greedy graph coloring)."""
    pairs = set()
    for x, y in extra:
        pairs.add((int(x), int(y)))
        pairs.add((int(y), int(x)))
    for a, b in ((own[:, :-1], own[:, 1:]),
                 (own[:-1, :], own[1:, :]),
                 (own[:-1, :-1], own[1:, 1:]),
                 (own[:-1, 1:], own[1:, :-1])):
        d = (a != b) & (a >= 0) & (b >= 0)
        for x, y in np.unique(np.stack([a[d], b[d]], axis=1), axis=0):
            pairs.add((int(x), int(y)))
            pairs.add((int(y), int(x)))
    ngb = [[] for _ in range(NH)]
    for x, y in pairs:
        ngb[x].append(y)
    color = [-1] * NH
    for h in sorted(range(NH), key=lambda h: -len(ngb[h])):
        used = {color[g] for g in ngb[h]}
        c = 0
        while c in used:
            c += 1
        color[h] = c % len(PALETTE)
    return PALETTE[color]


def picture(path, own, live, never, pairs=()):
    from PIL import Image
    rgb = hub_rgb(own, pairs)
    img = np.zeros((H, W, 3))
    ok = own >= 0
    img[ok] = rgb[own[ok]]
    img[ok & ~live] *= 0.35
    img[never] = (0.9, 0.1, 0.1)
    Image.fromarray((img * 255).astype(np.uint8)).save(path)


def zoom_picture(path, own, lives, shifts, never, center, size, scale,
                 pairs=()):
    """Close-up, one square of scale x scale pixels per site, a size x
    size site window around center, one panel per shift (2 across).
    Colors are the owning hub, dark = dead center in that shift, red =
    never live; thin lines separate sites."""
    from PIL import Image, ImageDraw
    rgb = hub_rgb(own, pairs)
    cx, cy = center
    x0 = min(max(cx - size // 2, 0), W - size)
    y0 = min(max(cy - size // 2, 0), H - size)
    crop = own[y0:y0 + size, x0:x0 + size]
    base = np.zeros((size, size, 3))
    ok = crop >= 0
    base[ok] = rgb[crop[ok]]
    gap = 4
    bar = 18
    side = size * scale
    cols = 2
    rows = (len(shifts) + cols - 1) // cols
    sheet = Image.new("RGB",
                      (cols * side + (cols + 1) * gap,
                       rows * (side + bar) + (rows + 1) * gap),
                      (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    for k, (live, s) in enumerate(zip(lives, shifts)):
        img = base.copy()
        lv = live[y0:y0 + size, x0:x0 + size]
        img[ok & ~lv] *= 0.35
        img[never[y0:y0 + size, x0:x0 + size]] = (0.9, 0.1, 0.1)
        big = np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)
        if scale >= 4:
            big[scale - 1::scale, :, :] *= 0.75
            big[:, scale - 1::scale, :] *= 0.75
        px = gap + (k % cols) * (side + gap)
        py = gap + (k // cols) * (side + bar + gap)
        draw.text((px + 2, py + 3),
                  f"shift {s}   sites x {x0}..{x0 + size - 1}, "
                  f"y {y0}..{y0 + size - 1}",
                  fill=(230, 230, 230))
        tile = Image.fromarray((big * 255).astype(np.uint8))
        sheet.paste(tile, (px, py + bar))
    sheet.save(path)


def ew_picture(path, size, scale, block=False):
    """Close-up of the perfect EW tiling: every 41-site window its own
    color, center site darker.  Color (i + 2j) mod 5 of the tile index
    differs across every edge and corner contact (+-a, +-b, a+-b), so
    five colors are enough.  block: show one hub-sized k x k block of
    EWs (as ew-blocks uses) at full color, everything else dimmed, and
    frame the picture around the block (size is then ignored)."""
    from PIL import Image
    i, j = ew_tile_index()
    k = int(round(math.sqrt(HG.SITES_PER_HUB / EW_SITES)))
    inblk = None
    if block:
        bi = np.floor_divide(i, k)
        bj = np.floor_divide(j, k)
        mi = bi[H // 2, W // 2]
        mj = bj[H // 2, W // 2]
        inblk = (bi == mi) & (bj == mj)
        ys, xs = np.nonzero(inblk)
        pad = 6
        x0 = max(int(xs.min()) - pad, 0)
        y0 = max(int(ys.min()) - pad, 0)
        sw = min(int(xs.max()) + pad + 1, W) - x0
        sh = min(int(ys.max()) + pad + 1, H) - y0
    else:
        x0 = W // 2 - size // 2
        y0 = H // 2 - size // 2
        sw = size
        sh = size
    win = (slice(y0, y0 + sh), slice(x0, x0 + sw))
    ti = i[win]
    tj = j[win]
    img = PALETTE[(ti + 2 * tj) % 5].copy()
    cx = ti * R - tj * (R + 1)
    cy = ti * (R + 1) + tj * R
    center = (cx == XX[win]) & (cy == YY[win])
    img[center] *= 0.45
    if block:
        img[~inblk[win]] = 0.25 * img[~inblk[win]] + 0.1
    big = np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)
    if scale >= 4:
        big[scale - 1::scale, :, :] *= 0.75
        big[:, scale - 1::scale, :] *= 0.75
    if block:
        # Today's hub rectangle (SELF, BW x BH), same centroid, white
        # outline on the site borders.
        bw = HG.T6GRID_BASE_WIDTH
        bh = HG.T6GRID_BASE_HEIGHT
        rx0 = int(round(xs.mean())) - bw // 2
        ry0 = int(round(ys.mean())) - bh // 2
        rect = np.zeros(inblk.shape, dtype=bool)
        rect[ry0:ry0 + bh, rx0:rx0 + bw] = True
        both = int((rect & inblk).sum())
        print(f"hub rectangle {bw} x {bh} = {bw * bh} sites on the same "
              f"center: {both} shared, {int(inblk.sum()) - both} only "
              f"in the block, {bw * bh - both} only in the rectangle")
        px0 = (rx0 - x0) * scale
        py0 = (ry0 - y0) * scale
        px1 = px0 + bw * scale
        py1 = py0 + bh * scale
        t = max(scale // 2, 2)
        white = (1.0, 1.0, 1.0)
        big[py0:py0 + t, px0:px1] = white
        big[py1 - t:py1, px0:px1] = white
        big[py0:py1, px0:px0 + t] = white
        big[py0:py1, px1 - t:px1] = white
    Image.fromarray((big * 255).astype(np.uint8)).save(path)
    if block:
        print(f"block of {k}x{k} EWs = {int(inblk.sum())} sites, "
              f"bounding box {sw - 12} x {sh - 12} sites")


def ew_rect_picture(path, scale, hub=(2, 2), snap=False):
    """One of today's hubs (SELF, BW x BH) on the perfect EW tiling:
    EWs wholly inside it bright, EWs cut by its edge half bright,
    all others dark, hub outlined in white.  The hub is placed where
    it really is (hub coords, default (2,2) of chip 0).  snap: light
    every EW with a MAJORITY (21 of 41) of its sites inside, darken the
    rest: the hub rounded to whole EWs."""
    from PIL import Image
    i, j = ew_tile_index()
    bw = HG.T6GRID_BASE_WIDTH
    bh = HG.T6GRID_BASE_HEIGHT
    rx0 = hub[0] * bw
    ry0 = hub[1] * bh
    pad = 2 * R + 2
    x0 = rx0 - pad
    y0 = ry0 - pad
    sw = bw + 2 * pad
    sh = bh + 2 * pad
    win = (slice(y0, y0 + sh), slice(x0, x0 + sw))
    ti = i[win]
    tj = j[win]
    key = compact_ids(ti, tj)
    inrect = np.zeros(key.shape, dtype=bool)
    inrect[pad:pad + bh, pad:pad + bw] = True
    nkeys = int(key.max()) + 1
    inside = np.bincount(key[inrect], minlength=nkeys)
    full = inside == EW_SITES
    cut = (inside > 0) & ~full
    img = PALETTE[(ti + 2 * tj) % 5].copy()
    cx = ti * R - tj * (R + 1)
    cy = ti * (R + 1) + tj * R
    img[(cx == XX[win]) & (cy == YY[win])] *= 0.45
    if snap:
        major = inside * 2 > EW_SITES
        lvl = np.where(major[key], 1.0, 0.18)
    else:
        lvl = np.where(full[key], 1.0, np.where(cut[key], 0.55, 0.18))
    img *= lvl[:, :, None]
    big = np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)
    if scale >= 4:
        big[scale - 1::scale, :, :] *= 0.75
        big[:, scale - 1::scale, :] *= 0.75
    px0 = pad * scale
    py0 = pad * scale
    px1 = px0 + bw * scale
    py1 = py0 + bh * scale
    t = max(scale // 2, 2)
    big[py0:py0 + t, px0:px1] = 1.0
    big[py1 - t:py1, px0:px1] = 1.0
    big[py0:py1, px0:px0 + t] = 1.0
    big[py0:py1, px1 - t:px1] = 1.0
    Image.fromarray((big * 255).astype(np.uint8)).save(path)
    nf = int(full.sum())
    nc = int(cut.sum())
    cutin = int(inside[cut].sum())
    print(f"hub {hub} {bw} x {bh} = {bw * bh} sites: {nf} whole EWs "
          f"({nf * EW_SITES} sites, {nf * EW_SITES / (bw * bh):.3f}), "
          f"{nc} EWs cut by the edge ({cutin} of their sites inside)")
    if snap:
        lit = major[key]
        cin = np.zeros(nkeys, dtype=bool)
        center = (cx == XX[win]) & (cy == YY[win])
        cin[key[center & inrect]] = True
        print(f"snapped: {int(major.sum())} EWs = {int(lit.sum())} "
              f"sites; {int((lit & ~inrect).sum())} gained outside, "
              f"{int((~lit & inrect).sum())} lost inside; majority rule "
              f"and center-inside rule differ on "
              f"{int((major != cin).sum())} EWs")


def intensity_picture(path, lives, shifts, centre, size, scale):
    """Close-up of site intensity, one panel per shift: gray level =
    intensity / 41 (white = 41, deep inside an area), intensity 1 orange,
    0 red."""
    from PIL import Image, ImageDraw
    cx, cy = centre
    x0 = min(max(cx - size // 2, 0), W - size)
    y0 = min(max(cy - size // 2, 0), H - size)
    gap = 4
    bar = 18
    side = size * scale
    cols = 2
    rows = (len(shifts) + cols - 1) // cols
    sheet = Image.new("RGB",
                      (cols * side + (cols + 1) * gap,
                       rows * (side + bar) + (rows + 1) * gap),
                      (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    for k, (live, s) in enumerate(zip(lives, shifts)):
        it = intensity(live)[y0:y0 + size, x0:x0 + size]
        g = (it.astype(np.float64) / EW_SITES)[:, :, None]
        img = np.repeat(g, 3, axis=2) * 0.9 + 0.05
        img[it == 1] = (1.0, 0.55, 0.0)
        img[it == 0] = (0.9, 0.1, 0.1)
        big = np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)
        if scale >= 4:
            big[scale - 1::scale, :, :] *= 0.85
            big[:, scale - 1::scale, :] *= 0.85
        px = gap + (k % cols) * (side + gap)
        py = gap + (k // cols) * (side + bar + gap)
        draw.text((px + 2, py + 3),
                  f"intensity, shift {s}  (white 41, orange 1, red 0)",
                  fill=(230, 230, 230))
        tile = Image.fromarray((big * 255).astype(np.uint8))
        sheet.paste(tile, (px, py + bar))
    sheet.save(path)


# ---------------------------------------------------------------- checks

def tileable(region):
    """Can a set of (x, y) sites be covered EXACTLY by EWs?  The tiling
    is forced: the first uncovered site in row-major order must be the
    top vertex of its EW.  So one greedy pass decides it, for any
    tiling, not just lattice ones."""
    left = set(region)
    for (x, y) in sorted(region, key=lambda p: (p[1], p[0])):
        if (x, y) not in left:
            continue
        cells = [(x + dx, y + R + dy) for dx, dy in OFFSETS]
        if not all(p in left for p in cells):
            return False
        left.difference_update(cells)
    return True


def report_tileable(limit=120):
    hits = []
    for d in range(2 * R, 2 * limit):           # doubled radius
        for hx in (0, 1):
            for hy in (0, 1):
                reg = {(x, y)
                       for x in range(-limit - 1, limit + 2)
                       for y in range(-limit - 1, limit + 2)
                       if abs(2 * x + hx) + abs(2 * y + hy) <= d}
                if reg and tileable(reg):
                    hits.append((d / 2, hx / 2, hy / 2))
    print(f"diamonds (radius, center offset) EW-tileable up to "
          f"{limit}: {hits}")
    sq = [s for s in range(1, limit)
          if tileable({(x, y) for x in range(s) for y in range(s)})]
    print(f"squares s x s EW-tileable up to {limit}: {sq}")


def check_rect():
    """Today's layout must reproduce the hub's real geometry."""
    m = measure(layout_rect())[0]
    assert m["store_max"] == HG.T6GRID_WIDTH * HG.T6GRID_HEIGHT, m
    ring = HG.T6GRID_WIDTH * HG.T6GRID_HEIGHT - HG.SITES_PER_HUB
    assert m["ring_max"] == ring, m
    assert m["hubs"] == NH and m["cv"] == 0.0, m
    assert m["ngb_max"] == 8 and m["hop_max"] == 1, m
    assert m["never"] == 0.0, m
    # interior dead cross: 2R columns + 2R rows per hub
    bw = HG.T6GRID_BASE_WIDTH
    bh = HG.T6GRID_BASE_HEIGHT
    interior = 1.0 - (bw - 2 * R) * (bh - 2 * R) / (bw * bh)
    assert m["dead"] < 100.0 * interior, m
    print(f"check ok: store {m['store_max']}, ring {m['ring_max']}, "
          f"dead {m['dead']:.1f}% (interior {100.0 * interior:.1f}%)")


# ------------------------------------------------------------------ main

HEAD = ("layout         hubs  sites/hub min/mean/max   cv    dead% "
        "dead@hub never%  ring  xchg   ngb      hops     "
        "pack/hub pack/all")


def row(name, m):
    return (f"{name:14s} {m['hubs']:4d}  "
            f"{m['min']:5d}/{m['mean']:7.0f}/{m['max']:6d}  "
            f"{m['cv']:5.3f}  {m['dead']:5.1f}  {m['dead_n']:6.1f}  "
            f"{m['never']:5.2f}  "
            f"{m['ring']:4.2f}  {m['xchg']:5.0f}  "
            f"{m['ngb_mean']:4.1f}/{m['ngb_max']:2d}  "
            f"{m['hop_max']:2d}({m['hop_far']:3d})  "
            f"{m['pack_hub']:7.3f}  {m['pack_all']:7.3f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="geomlab-out")
    ap.add_argument("--only", default="")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--zoom", default="",
                    help="X,Y center of the close-ups (default: a hub "
                         "corner inside chip 0)")
    ap.add_argument("--zsize", type=int, default=100,
                    help="close-up width in sites")
    ap.add_argument("--zscale", type=int, default=6,
                    help="pixels per site in the close-ups")
    ap.add_argument("--crosscheck", action="store_true",
                    help="EWLattice.h vs the lab's rect-snap, site by site")
    ap.add_argument("--check", action="store_true",
                    help="self-test: rect reproduces the real T6Grid")
    ap.add_argument("--ewtiles", action="store_true",
                    help="close-up of the perfect EW tiling "
                         "(OUT/ew-tiling.png; --zsize, --zscale)")
    ap.add_argument("--ewblock", action="store_true",
                    help="one hub-sized block of EWs with its edges and "
                         "today's hub rectangle outlined "
                         "(OUT/ew-block.png; --zscale)")
    ap.add_argument("--ewrect", action="store_true",
                    help="today's hub (2,2) on the EW tiling: whole EWs "
                         "bright, cut EWs half (OUT/ew-rect.png)")
    ap.add_argument("--ewsnap", action="store_true",
                    help="as --ewrect, but light every EW with a "
                         "majority inside (OUT/ew-snap.png)")
    ap.add_argument("--tileable", action="store_true",
                    help="which diamonds / squares EWs cover exactly")
    args = ap.parse_args()
    if args.list:
        for k in LAYOUTS:
            print(k)
        return 0
    global WORK_DIR
    WORK_DIR = args.out
    if args.check:
        check_rect()
        return 0
    if args.crosscheck:
        return crosscheck()
    if args.tileable:
        report_tileable()
        return 0
    if args.ewsnap:
        os.makedirs(args.out, exist_ok=True)
        ew_rect_picture(os.path.join(args.out, "ew-snap.png"),
                        args.zscale, snap=True)
        return 0
    if args.ewrect:
        os.makedirs(args.out, exist_ok=True)
        ew_rect_picture(os.path.join(args.out, "ew-rect.png"),
                        args.zscale)
        return 0
    if args.ewblock:
        os.makedirs(args.out, exist_ok=True)
        ew_picture(os.path.join(args.out, "ew-block.png"),
                   0, args.zscale, block=True)
        return 0
    if args.ewtiles:
        os.makedirs(args.out, exist_ok=True)
        ew_picture(os.path.join(args.out, "ew-tiling.png"),
                   args.zsize, args.zscale)
        return 0
    names = [n for n in args.only.split(",") if n] or list(LAYOUTS)
    if args.zoom:
        zc = tuple(int(v) for v in args.zoom.split(","))
    else:
        zc = (2 * HG.T6GRID_BASE_WIDTH, 2 * HG.T6GRID_BASE_HEIGHT)
    os.makedirs(args.out, exist_ok=True)
    lines = [f"grid {W} x {H}, {NH} hubs, R {R}, ring/shift {OW}",
             HEAD]
    print(lines[0])
    print(lines[1])
    notes = []
    for n in names:
        lay = LAYOUTS[n]()
        m, own, lives, never = measure(lay)
        line = row(n, m)
        print(line, flush=True)
        lines.append(line)
        notes.append(f"  {n}: lattice-center dead {m['deadL']:.1f}%; "
                     f"borrowed bands whole EWs: {m['bands_whole']}, "
                     f"EWs borrowed per hub per cycle {m['band_ews'][0]}"
                     f"..{m['band_ews'][1]}")
        notes.append(f"  {n}: site intensity per cycle P(0) "
                     f"{m['int0']:.2f}% P(1) {m['int1']:.2f}% P(<=4) "
                     f"{m['int4']:.2f}%; summed over the cycle min/mean/max "
                     f"{m['int_sum'][0]}/{m['int_sum'][1]:.1f}/"
                     f"{m['int_sum'][2]}")
        notes.append(f"  {n}: center table: <= {m['runs_max']} runs per "
                     f"row, {m['runs_mean']:.0f} runs per hub-cycle "
                     f"(max {m['runs_top']}), live centers per "
                     f"hub-cycle {m['live_rng'][0]}..{m['live_rng'][1]}")
        notes.append(f"  {n}: {lay.note}; unowned {m['unowned']}, "
                     f"store max {m['store_max']}, ring max "
                     f"{m['ring_max']}, neighbor pairs {m['pairs']} "
                     f"({m['seam_pairs']} cross a chip seam)")
        picture(os.path.join(args.out, n + ".png"), own, lives[0], never,
                m["pair_list"])
        zoom_picture(os.path.join(args.out, n + "-zoom.png"), own, lives,
                     lay.shifts, never, zc, args.zsize, args.zscale,
                     m["pair_list"])
        intensity_picture(os.path.join(args.out, n + "-intensity.png"),
                          lives, lay.shifts, zc, args.zsize, args.zscale)
    lines.append("")
    lines.extend(notes)
    print("")
    print("\n".join(notes))
    with open(os.path.join(args.out, "geomlab.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
