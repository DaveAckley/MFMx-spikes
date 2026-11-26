#!/usr/bin/env python3
"""celllab.py -- what other CELL shapes would mean for hub size and L1.

A cell is a block of CW x CH T6 tiles: one hub plus CW*CH - 1 EWPs.
Today: 2 x 2 cells, hub at (1,1) (the SE corner), 7 x 5 hubs per
chip, 137 x 108 SELF per hub (GEOMETRY.md sec 2).  This lab tiles each
chip's 14 x 10 T6 array with other cells, derives the per-hub SELF
needed to cover the HD box (1920 x 1080 on 2 x 2 Blackhole cards), adds
the cache ring of a policy, and checks whether the hub's grid and side
tables fit in hub L1.  Host only, no hardware, no repo imports.

THE L1 MODEL (calibrated on the hub image of f291f11, ring 28):
  per stored site   12 B T6Grid (P4Atom) + 4 B DLGridList + 2 B
                    theSiteStamps + 2 bits center scratch (ewsnap*)
  fixed             everything else in the image: code, InterHub cars
                    (8 dirs), CornerHub, EWP cars (sized for 8 EWPs
                    already), HostBlock, log, center tables ... =
                    FIXED_BYTES below (measured: image end 0xfa474 with
                    193 x 164 stored sites)
  budget            the hub linker region, 0x120000 (3/4 of the 1.5 MB
                    L1, _BUD.ld.in); 'full' = all 0x180000, an upper
                    bound nobody has tried (firmware, mailboxes and the
                    other harts' needs are not modeled)
--atom-bytes, --fixed, --l1 and --no-side-tables change the model.

THE EWP SIDE (printed after the table): heavier physics loads the EWPs.
The EWP image is the same in every cell shape (one per T6 that is not a
hub), so its budget does not depend on the cell: the bytephys spike image
ends at 0x1f6b4 (125 KB: 89 KB code, of which physics + the bytecode
machine ~3 KB; 14 KB bss; 16 KB HostBlock), leaving about 1026 KB of the
same 0x120000 budget for physics code and data.  Physics runs on hart B,
whose 8 KB fast (local) RAM holds fAll (16 B), fB (720 B: Physics,
including the current EW mFastEW, the bytecode machine and the timing
counters) and the stack (the rest, ~7.45 KB).  The stack must stay in fast
RAM (moving it is a big performance loss, per TT), so it is the tight
limit for complex physics, not L1.

Hub position: NoC0 only runs east and south, NoC1 only west and north
(NRIUtils.cpp preferNoC).  An EWP at offset (dx, dy) from its hub is
DIRECT when dx and dy are not of opposite signs: then one NoC goes there
(and the other comes back) without wrapping around the torus.  EWPs may
use different NoCs, so any hub position in a 1xN or Nx1 cell is fine
(no turns at all); once both dimensions are > 1 only the NW (0,0) or SE
(CW-1,CH-1) corner keeps every EWP direct.  Any other position: flag
'noc'.
NW and SE are mirror images per cell (same hops), so they differ only
in where the idle T6s go at the chip edge and in seam wiring
(EthSeam.h), which this lab does not model.

Columns (one row per cell shape and policy):
  cell     CWxCH@HX,HY
  hubs     hubs per chip (CX x CY)          ewp   EWPs per chip
  e/h      EWPs per hub                      idle  unused T6s per chip
  SELF     per-hub owned sites BW x BH (floor of 960/CX, 540/CY)
  blank    image columns, rows left uncovered (of 1920 x 1080)
  grid     stored T6Grid TW x TH = SELF + 2 ring
  KB       hub L1 use (model), free  = 0x120000 minus that (- = over)
  xchg     sites borrowed per cycle / SELF (four corners: shift*(BW+BH)
           - shift^2, shift = ring for fourcorners, 18 for ewsnap*)
  hops     max NoC hops hub -> EWP in the cell (Manhattan)
  fits-box the biggest box (and % of the --box area) this cell could cover
           with SELF scaled, same aspect, until the hub just fits L1
  flags    u8    TW or TH > 255 (u8 grid coords, GEOMETRY.md sec 8)
           ewp8  more than 8 EWPs per cell (EwpBlock HBASSERT_LT)
           noc   some EWP is NE or SW of its hub: no NoC reaches it
                 without a torus wrap
           L1    over the 0x120000 budget;  L1!  over even 0x180000

Usage:
  celllab.py                     every cell with 2..9 tiles (+ policies)
  celllab.py 2x2@1,1 3x3@2,2 4x2@0,0 2x4@1,3
  celllab.py --policy ewsnap10 --atom-bytes 16 3x3@0,0
  celllab.py --box 1600x900 3x3@0,0   a smaller world
  celllab.py --check             self-test (reproduces today's numbers)
"""

import argparse
import sys

BOX_W = 1920                    # HD image
BOX_H = 1080
CHIPS_X = 2                     # 2 x 2 Blackhole cards
CHIPS_Y = 2
T6_X = 14                       # T6 (Tensix) tiles per chip
T6_Y = 10

L1_HUB = 0x120000               # _BUD.ld.in hub region (3/4 of L1)
L1_FULL = 0x180000              # the whole 1.5 MB L1

# Calibration (hub image of f291f11, ring 28, 193 x 164 stored sites).
CAL_END = 0xfa474
CAL_SITES = 193 * 164
ATOM_BYTES = 12                 # P4Atom
DL_BYTES = 4                    # DLGridList per site
STAMP_BYTES = 2                 # theSiteStamps per site
CAL_T6GRID = ATOM_BYTES * CAL_SITES + 4
CAL_DL = DL_BYTES * CAL_SITES + 20
CAL_STAMPS = STAMP_BYTES * CAL_SITES
CAL_SCRATCH = 2 * ((CAL_SITES + 7) // 8)
FIXED_BYTES = CAL_END - CAL_T6GRID - CAL_DL - CAL_STAMPS - CAL_SCRATCH

# policy -> (ring, shift, center scratch?)
POLICIES = {
    "fourcorners": (20, 20, False),
    "ewsnap": (22, 18, True),
    "ewsnap10": (28, 18, True),
}

EWP_MAX = 8                     # EwpBlock HBASSERT_LT

# The EWP image (bytephys spike build, 2026-11-26; the same image in every
# cell shape, so its budget does not depend on the cell).  Same
# _BUD.ld.in: 0x120000 of L1.
EWP_BUILD = "bytephys spike"
EWP_END = 0x1b634 + 0x4080      # .hostblock end
EWP_TEXT = 0x1647c              # all code
EWP_PHYSICS_TEXT = 3200         # Physics::* + BytePhys::Machine::run
EWP_RODATA = 0x1468
EWP_BSS = 0x384c                # log cars 9840, EWP car 2096, fT2 2576 ...
EWP_HOSTBLOCK = 0x4080
EWP_FALL = 16                   # .fastram_all
EWP_FB = 0x2d0                  # .fastram_b: Physics (EW, VM, timing)
EWP_STACK = 8192 - EWP_FALL - EWP_FB   # hart B: the rest of 8 KB
EWP_STACK_HW = 784              # measured high-water (boom recovery run)


def parse_cell(text):
    """'3x3@2,2' -> (3, 3, 2, 2).  '3x3' alone puts the hub SE."""
    t = text.lower().strip()
    if "@" in t:
        dims, pos = t.split("@", 1)
        hx, hy = (int(v) for v in pos.split(","))
    else:
        dims, pos = t, None
    cw, ch = (int(v) for v in dims.split("x"))
    if pos is None:
        hx, hy = cw - 1, ch - 1
    if not (0 <= hx < cw and 0 <= hy < ch):
        raise ValueError(f"{text}: hub outside the cell")
    if cw * ch < 2:
        raise ValueError(f"{text}: a cell needs a hub and an EWP")
    return cw, ch, hx, hy


def default_cells():
    """Every cell shape with 2..9 tiles that fits a chip, hub SE."""
    out = []
    for ch in range(1, T6_Y + 1):
        for cw in range(1, T6_X + 1):
            if 2 <= cw * ch <= 9:
                out.append((cw, ch, cw - 1, ch - 1))
    return out


def evaluate(cell, policy, atom_bytes=ATOM_BYTES, fixed=FIXED_BYTES,
             side_tables=True, l1=L1_HUB, box=(BOX_W, BOX_H)):
    cw, ch, hx, hy = cell
    ring, shift, scratch = POLICIES[policy]
    cx = T6_X // cw                     # cells (hubs) per chip, across
    cy = T6_Y // ch
    hubs = cx * cy
    ewp = hubs * (cw * ch - 1)
    idle = T6_X * T6_Y - hubs * cw * ch
    bw = (box[0] // CHIPS_X) // cx      # floor, as DemoGlobal.h
    bh = (box[1] // CHIPS_Y) // cy
    blank_w = box[0] - bw * cx * CHIPS_X
    blank_h = box[1] - bh * cy * CHIPS_Y
    tw = bw + 2 * ring
    th = bh + 2 * ring
    sites = tw * th
    per_site = atom_bytes + ((DL_BYTES + STAMP_BYTES) if side_tables else 0)
    use = fixed + per_site * sites + 4 + (20 if side_tables else 0)
    if scratch:
        use += 2 * ((sites + 7) // 8)
    borrowed = shift * (bw + bh) - shift * shift
    hops = max(abs(x - hx) + abs(y - hy)
               for x in range(cw) for y in range(ch))
    flags = []
    if tw > 255 or th > 255:
        flags.append("u8")
    if cw * ch - 1 > EWP_MAX:
        flags.append("ewp8")
    if any((x - hx) * (y - hy) < 0
           for x in range(cw) for y in range(ch)):
        flags.append("noc")
    if use > L1_FULL:
        flags.append("L1!")
    elif use > l1:
        flags.append("L1")
    # the biggest box this cell could cover within the budget: scale
    # SELF (same aspect) down or up until the hub just fits
    room = l1 - fixed - 24
    lo, hi = 0.0, 4.0
    for _ in range(40):
        k = (lo + hi) / 2.0
        n = (int(bw * k) + 2 * ring) * (int(bh * k) + 2 * ring)
        cost = per_site * n + (2 * ((n + 7) // 8) if scratch else 0)
        if cost <= room:
            lo = k
        else:
            hi = k
    fit_w = int(bw * lo) * cx * CHIPS_X
    fit_h = int(bh * lo) * cy * CHIPS_Y
    return {
        "cell": f"{cw}x{ch}@{hx},{hy}",
        "policy": policy,
        "cx": cx, "cy": cy, "hubs": hubs, "ewp": ewp,
        "eph": cw * ch - 1, "idle": idle,
        "bw": bw, "bh": bh, "blank_w": blank_w, "blank_h": blank_h,
        "tw": tw, "th": th, "sites": sites,
        "use": use, "free": l1 - use, "room_sites": room // per_site,
        "xchg": borrowed / float(bw * bh),
        "hops": hops, "flags": flags,
        "fit_w": fit_w, "fit_h": fit_h,
        "fit_pct": 100.0 * fit_w * fit_h / (box[0] * box[1]),
    }


HEADER = ("cell       policy       hubs   ewp e/h idle   SELF     blank  "
          "grid       KB   free   xchg hops  fits-box (box%) flags")


def row(r):
    return (f"{r['cell']:<10} {r['policy']:<11} "
            f"{r['cx']:>2}x{r['cy']:<2} {r['ewp']:>4} {r['eph']:>3} "
            f"{r['idle']:>4} {r['bw']:>4}x{r['bh']:<4} "
            f"{r['blank_w']:>2},{r['blank_h']:<3} "
            f"{r['tw']:>3}x{r['th']:<3} {r['use'] // 1024:>6} "
            f"{r['free'] // 1024:>6} {r['xchg']:>6.3f} {r['hops']:>4} "
            f"{r['fit_w']:>5}x{r['fit_h']:<4} ({r['fit_pct']:>3.0f}) "
            f"{' '.join(r['flags'])}")


def ewp_report(l1, ewp_per_hub=None):
    free = l1 - EWP_END
    lines = [
        f"EWP L1 (one image for every cell shape; {EWP_BUILD}): image "
        f"{EWP_END // 1024} KB = code {EWP_TEXT // 1024} KB (physics "
        f"~{EWP_PHYSICS_TEXT / 1024:.1f} KB of it), rodata "
        f"{EWP_RODATA // 1024} KB, bss {EWP_BSS // 1024} KB (log cars, "
        f"one EWP car), hostblock {EWP_HOSTBLOCK // 1024} KB",
        f"  free for physics code + data: {free // 1024} KB of the "
        f"{l1 // 1024} KB budget ({(L1_FULL - EWP_END) // 1024} KB of the "
        f"full L1)",
        f"  hart B fast RAM 8192 B = fAll {EWP_FALL} + fB {EWP_FB} "
        f"(Physics: the current EW mFastEW, the bytecode machine, timing) "
        f"+ stack {EWP_STACK} B (high-water seen {EWP_STACK_HW} B); the "
        f"stack stays in fast RAM (performance), so bigger physics locals "
        f"or fast state come out of it",
    ]
    if ewp_per_hub:
        lines.append("  EWP L1 free per hub region: " + ", ".join(
            f"{c} {n} x {free // 1024} = {n * free // 1024} KB"
            for c, n in ewp_per_hub))
    return "\n".join(lines)


def check():
    bad = 0

    def ok(cond, what):
        nonlocal bad
        if not cond:
            print("FAIL:", what)
            bad += 1

    r = evaluate((2, 2, 1, 1), "ewsnap10")
    ok((r["cx"], r["cy"], r["hubs"], r["ewp"]) == (7, 5, 35, 105),
       "2x2: 7 x 5 hubs, 105 EWPs per chip")
    ok((r["bw"], r["bh"]) == (137, 108), "2x2: SELF 137 x 108")
    ok((r["blank_w"], r["blank_h"]) == (2, 0), "2x2: 2 blank columns")
    ok((r["tw"], r["th"]) == (193, 164), "2x2 ewsnap10: 193 x 164")
    ok(r["use"] == CAL_END, f"2x2 ewsnap10 reproduces the image end "
       f"(model {r['use']:#x}, image {CAL_END:#x})")
    r = evaluate((2, 2, 1, 1), "ewsnap")
    ok((r["tw"], r["th"]) == (181, 152), "2x2 ewsnap: 181 x 152")
    r = evaluate((3, 3, 2, 2), "fourcorners")
    ok((r["cx"], r["cy"], r["idle"]) == (4, 3, 32), "3x3: 4 x 3, 32 idle")
    ok(r["eph"] == 8 and "ewp8" not in r["flags"], "3x3: 8 EWPs ok")
    r = evaluate((3, 4, 2, 3), "fourcorners")
    ok("ewp8" in r["flags"], "3x4: 11 EWPs flagged")
    ok(parse_cell("3x3") == (3, 3, 2, 2), "default hub SE")
    ok(parse_cell("4x2@0,0") == (4, 2, 0, 0), "parse 4x2@0,0")
    ok("noc" not in evaluate((3, 3, 0, 0), "ewsnap")["flags"], "NW ok")
    ok("noc" in evaluate((3, 3, 2, 0), "ewsnap")["flags"], "NE flagged")
    ok("noc" in evaluate((3, 3, 1, 1), "ewsnap")["flags"], "center flagged")
    ok("noc" not in evaluate((1, 3, 0, 1), "ewsnap")["flags"],
       "1x3 middle hub is direct (no turns)")
    ok("noc" not in evaluate((4, 1, 2, 0), "ewsnap")["flags"],
       "4x1 inner hub is direct")
    ok("noc" not in evaluate((3, 3, 2, 2), "ewsnap")["flags"], "SE ok")
    ok(EWP_END == 0x1f6b4, "EWP image end = hostblock end")
    ok("1026 KB of the 1152" in ewp_report(L1_HUB),
       "EWP free at the 0x120000 budget")
    print("celllab --check:", "FAILED" if bad else "ok",
          f"(fixed {FIXED_BYTES} B)")
    return 1 if bad else 0


def main(argv):
    ap = argparse.ArgumentParser(
        description="cell shape lab: hub size and L1 per cell (columns "
        "and model: see the module docstring)")
    ap.add_argument("cells", nargs="*", help="CWxCH[@HX,HY] (default: "
                    "every shape with 2..9 tiles, hub SE)")
    ap.add_argument("--policy", action="append",
                    choices=sorted(POLICIES), help="repeatable; default "
                    "fourcorners and ewsnap10")
    ap.add_argument("--atom-bytes", type=int, default=ATOM_BYTES,
                    help="bytes per stored atom (P4Atom 12; P4 is locked "
                    "for this round, so a what-if only)")
    ap.add_argument("--fixed", type=int, default=FIXED_BYTES,
                    help="fixed hub L1 bytes (default measured)")
    ap.add_argument("--l1", type=lambda s: int(s, 0), default=L1_HUB,
                    help="hub L1 budget (default 0x120000)")
    ap.add_argument("--no-side-tables", action="store_true",
                    help="drop DLGridList and site stamps (6 B/site)")
    ap.add_argument("--box", default=f"{BOX_W}x{BOX_H}",
                    help="world to cover, WxH (default HD 1920x1080)")
    ap.add_argument("--fits", action="store_true",
                    help="list only rows with no flags")
    ap.add_argument("--check", action="store_true", help="self-test")
    a = ap.parse_args(argv)
    if a.check:
        return check()
    cells = [parse_cell(c) for c in a.cells] if a.cells else default_cells()
    box = tuple(int(v) for v in a.box.lower().split("x"))
    pols = a.policy or ["fourcorners", "ewsnap10"]
    print(f"box {box[0]}x{box[1]}; model: {a.atom_bytes} B/atom"
          f"{'' if a.no_side_tables else ' + 6 B side tables'} per stored "
          f"site, fixed {a.fixed // 1024} KB, budget {a.l1 // 1024} KB "
          f"(full L1 {L1_FULL // 1024} KB)")
    print(HEADER)
    for c in cells:
        for p in pols:
            r = evaluate(c, p, a.atom_bytes, a.fixed,
                         not a.no_side_tables, a.l1, box)
            if a.fits and r["flags"]:
                continue
            print(row(r))
    room = (a.l1 - a.fixed - 24) // (a.atom_bytes +
                                     (0 if a.no_side_tables else 6))
    print(f"room: about {room} stored sites fit the budget "
          f"(~{int(room ** 0.5)} square)")
    print(ewp_report(a.l1, [(f"{c[0]}x{c[1]}", c[0] * c[1] - 1)
                            for c in cells][:6]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
