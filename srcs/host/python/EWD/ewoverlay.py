"""Hub ownership edges as a display overlay (mfmxctl `grid edges on`).

A boolean mask, one entry per pixel of the 1920 x 1080 site image (pixel =
global site, DemoGlobal.h gridCoordToPixel), True where the hub that owns
the site differs from the owner of the site to its right or below: single
pixel lines along every hub boundary.

Ownership follows the cache policy:
  fourcorners, supercell  the SELF rectangles (straight lines)
  ewsnap                  every EW of the perfect lattice belongs to the
                          hub whose rectangle holds its center
                          (srcs/shared/include/EWLattice.h): zig-zag lines

The EW lattice is repeated here from EWLattice.h (radius 4, centers where
(4x + 5y) mod 41 == 0, C0 = 0); scripts/geomlab/geomlab.py --crosscheck
compares owner_map('ewsnap') with the header's own owner map site by site.
Hubs at the edge of the whole box keep EWs that hang off it (clamped), as
the header's permanent edges do on the full 2 x 2 array.
"""

import numpy as np

import HubGeometry as HG

EW_RADIUS = 4                   # EWLattice.h RADIUS
EW_KX = EW_RADIUS               # EWLattice.h KX
EW_KY = EW_RADIUS + 1           # EWLattice.h KY
EW_SITES = 2 * EW_RADIUS * EW_RADIUS + 2 * EW_RADIUS + 1   # 41
EW_C0 = 0                       # EWLattice.h C0

HUBS_X = HG.ACTIVE_HUB_WIDTH * HG.CHIP_ARRAY_WIDTH
HUBS_Y = HG.ACTIVE_HUB_HEIGHT * HG.CHIP_ARRAY_HEIGHT


def _offset_table():
    """dx[r], dy[r]: the EW offset whose residue (KX*dx + KY*dy) mod 41
    is r (EWLattice.h OffsetTable)."""
    dx = np.zeros(EW_SITES, dtype=np.int64)
    dy = np.zeros(EW_SITES, dtype=np.int64)
    for ox in range(-EW_RADIUS, EW_RADIUS + 1):
        for oy in range(-EW_RADIUS, EW_RADIUS + 1):
            if abs(ox) + abs(oy) <= EW_RADIUS:
                r = (EW_KX * ox + EW_KY * oy) % EW_SITES
                dx[r] = ox
                dy[r] = oy
    return dx, dy


def owner_map(policy, width=HG.DEMO_GLOBAL_GRID_WIDTH,
              height=HG.DEMO_GLOBAL_GRID_HEIGHT):
    """Owning hub index (hy * HUBS_X + hx) of every site of a height x
    width image, by the policy's rule."""
    yy, xx = np.mgrid[0:height, 0:width]
    if policy == "ewsnap":
        dx, dy = _offset_table()
        r = (EW_KX * xx + EW_KY * yy - EW_C0) % EW_SITES
        cx = xx - dx[r]
        cy = yy - dy[r]
    else:
        cx = xx
        cy = yy
    hx = np.clip(cx // HG.T6GRID_BASE_WIDTH, 0, HUBS_X - 1)
    hy = np.clip(cy // HG.T6GRID_BASE_HEIGHT, 0, HUBS_Y - 1)
    return hy * HUBS_X + hx


def edge_mask(policy, width=HG.DEMO_GLOBAL_GRID_WIDTH,
              height=HG.DEMO_GLOBAL_GRID_HEIGHT):
    """True on the last site of a hub before a boundary (to the right or
    below), inside the sites the chips cover."""
    own = owner_map(policy, width, height)
    m = np.zeros(own.shape, dtype=bool)
    m[:, :-1] |= own[:, :-1] != own[:, 1:]
    m[:-1, :] |= own[:-1, :] != own[1:, :]
    active_w = HUBS_X * HG.T6GRID_BASE_WIDTH
    active_h = HUBS_Y * HG.T6GRID_BASE_HEIGHT
    m[:, active_w - 1:] = False         # the blank columns past the chips
    m[active_h - 1:, :] = False
    return m


class EdgeOverlay:
    """Cached mask for the current policy; apply() returns a COPY of the
    frame with the edges painted (the site image itself is untouched)."""

    def __init__(self):
        self.on = False
        self.policy = None
        self.mask = None

    def apply(self, img, policy):
        if not self.on:
            return img
        if self.mask is None or self.policy != policy or \
                self.mask.shape != img.shape[:2]:
            self.mask = edge_mask(policy, img.shape[1], img.shape[0])
            self.policy = policy
        out = img.copy()
        out[self.mask] = 255
        return out
