"""Hub grid geometry for the Python side, derived the way DemoGlobal.h does.

The C++ source of truth is srcs/shared/include/DemoGlobal.h (namespace
DG).  Python cannot see those constants, so this module repeats the
FORMULAS (never the results): change the inputs in DemoGlobal.h and
change them here, and scripts/hubgeom/hubgeom_check.sh will fail until
the two agree.  Do not write 137, 108 and friends anywhere else.
"""

DEMO_GLOBAL_GRID_WIDTH = 1920       # DG::DEMO_GLOBAL_GRID_WIDTH
DEMO_GLOBAL_GRID_HEIGHT = 1080      # DG::DEMO_GLOBAL_GRID_HEIGHT
CHIP_ARRAY_WIDTH = 2                # DG::BLACKHOLE_CHIP_ARRAY_WIDTH
CHIP_ARRAY_HEIGHT = 2               # DG::BLACKHOLE_CHIP_ARRAY_HEIGHT
QUIETBOX_T6_WIDTH = 14              # DG::QUIETBOX_T6_WIDTH
QUIETBOX_T6_HEIGHT = 10             # DG::QUIETBOX_T6_HEIGHT
T6GRID_OVERLAP_WIDTH = 28           # DG::T6GRID_OVERLAP_WIDTH
T6GRID_OVERLAP_HEIGHT = 28          # DG::T6GRID_OVERLAP_HEIGHT

ACTIVE_HUB_WIDTH = QUIETBOX_T6_WIDTH // 2     # hubs per chip, x
ACTIVE_HUB_HEIGHT = QUIETBOX_T6_HEIGHT // 2   # hubs per chip, y

_naiveW = DEMO_GLOBAL_GRID_WIDTH // CHIP_ARRAY_WIDTH
_naiveH = DEMO_GLOBAL_GRID_HEIGHT // CHIP_ARRAY_HEIGHT

# Hub SELF region, excluding the cache rings (floor division, as in C++)
T6GRID_BASE_WIDTH = _naiveW // ACTIVE_HUB_WIDTH
T6GRID_BASE_HEIGHT = _naiveH // ACTIVE_HUB_HEIGHT

# Hub T6Grid including the cache rings
T6GRID_WIDTH = T6GRID_BASE_WIDTH + 2 * T6GRID_OVERLAP_WIDTH
T6GRID_HEIGHT = T6GRID_BASE_HEIGHT + 2 * T6GRID_OVERLAP_HEIGHT

SITES_PER_HUB = T6GRID_BASE_WIDTH * T6GRID_BASE_HEIGHT



def hub_self_rect(chip, tlbi, cell=(2, 2)):
    """The SELF sites (x0, y0, w, h) in global grid coordinates (the ones
    'Seed@x,y' reports) of the hub whose cell holds tile tlbi, as
    DG::mapCoordToAddress lays them out: chips in a CHIP_ARRAY_WIDTH-wide
    array, each ACTIVE_HUB_WIDTH x ACTIVE_HUB_HEIGHT hub rectangles of
    T6GRID_BASE_WIDTH x T6GRID_BASE_HEIGHT; cell = the layout cell size
    in tiles (one hub per cell)."""
    col, row = tlbi % QUIETBOX_T6_WIDTH, tlbi // QUIETBOX_T6_WIDTH
    cx, cy = col // cell[0], row // cell[1]
    ox = (chip % CHIP_ARRAY_WIDTH) * ACTIVE_HUB_WIDTH * T6GRID_BASE_WIDTH
    oy = (chip // CHIP_ARRAY_WIDTH) * ACTIVE_HUB_HEIGHT * T6GRID_BASE_HEIGHT
    return (ox + cx * T6GRID_BASE_WIDTH, oy + cy * T6GRID_BASE_HEIGHT,
            T6GRID_BASE_WIDTH, T6GRID_BASE_HEIGHT)


def hub_uncached_rect(chip, tlbi, cell=(2, 2)):
    """hub_self_rect shrunk by the T6Grid overlap on every side: sites no
    caching policy ever lends or borrows (no band is wider than the
    overlap ring), so an atom put there is touched only by its own
    hub's events until it moves."""
    x0, y0, w, h = hub_self_rect(chip, tlbi, cell)
    return (x0 + T6GRID_OVERLAP_WIDTH, y0 + T6GRID_OVERLAP_HEIGHT,
            w - 2 * T6GRID_OVERLAP_WIDTH, h - 2 * T6GRID_OVERLAP_HEIGHT)


if __name__ == "__main__":      # python3 HubGeometry.py: print NAME=value lines
    for k in ("T6GRID_BASE_WIDTH", "T6GRID_BASE_HEIGHT",
              "T6GRID_OVERLAP_WIDTH", "T6GRID_OVERLAP_HEIGHT",
              "T6GRID_WIDTH", "T6GRID_HEIGHT",
              "ACTIVE_HUB_WIDTH", "ACTIVE_HUB_HEIGHT"):
        print(f"{k}={globals()[k]}")
