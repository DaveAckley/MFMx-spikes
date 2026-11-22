#!/usr/bin/env python3
"""deadzone_model.py -- dead event centres between two hubs for a fixed ownership boundary.

Model (see OPEN_ISSUES.md, "liv's sawtooth idea"): sites are owned by hub A or hub B for one RUN_EVENTS.
A centre is valid for a hub only if ALL 41 sites of its window (Manhattan radius 4, EventWindow.h) are owned
by that hub; a centre valid for neither is dead.  Prints the dead centres per row for several boundaries.
Result (2026-11-22): 8 = 2 * WINDOW_RADIUS for every connected boundary tried.  It is also a lower bound:
every row crosses from A's sites to B's sites somewhere, and the window row through the crossing alone
makes 2R centres dead.

Run:  python3 scripts/deadzone/deadzone_model.py
"""
R = 4                                   # CachingPolicy.h WINDOW_RADIUS
DIAMOND = [(dx, dy) for dx in range(-R, R + 1) for dy in range(-R, R + 1) if abs(dx) + abs(dy) <= R]
assert len(DIAMOND) == 41               # EventWindow::ATOM_COUNT
W, H = 80, 64


def dead_per_row(owner):
    """owner(x,y) -> 'A' or 'B'.  Returns dead centres per row (rows R..H-R-1)."""
    dead = 0
    for x in range(R, W - R):
        for y in range(R, H - R):
            if len({owner(x + dx, y + dy) for dx, dy in DIAMOND}) > 1:
                dead += 1
    return dead / (H - 2 * R)


def zigzag(amp):
    def f(y):
        p = y % (4 * amp)
        return p if p <= 2 * amp else 4 * amp - p   # 45 degree slopes
    return f


b = 40
print("straight vertical          ", dead_per_row(lambda x, y: 'A' if x < b else 'B'))
for amp in (2, 4, 8):
    f = zigzag(amp)
    print(f"45 degree zigzag, amp {amp}   ", dead_per_row(lambda x, y, f=f: 'A' if x < b + f(y) else 'B'))
print("slope 1/2 zigzag           ",
      dead_per_row(lambda x, y: 'A' if x < b + (((y % 32) if (y % 32) <= 16 else 32 - (y % 32)) // 2) else 'B'))
# interleaved vertical blocks of 16 rows: more boundary, so more dead centres
print("alternating 16-row blocks  ",
      dead_per_row(lambda x, y: 'A' if x < b + (8 if (y // 16) % 2 else 0) else 'B'))
