#!/usr/bin/env python3
"""Decode the T6Grid out of a hub L1 dump (from mfmxctl 'dumpl1') and check
the permanent-edge cache rings.

  l1grid.py DUMP.l1 [--pe MASK] [--map hub.map | --addr 0xADDR] [--show N]

DUMP.l1     the whole 1.5MB L1 of one hub, e.g. <run>/dumps/10/at023.l1
--pe MASK   the hub's permanent-edge mask in hex (N=1 W=2 S=4 E=8).  Default:
            the '.pe:' mark in <run>/tiles/<tag>.mfmk, run found from the dump
            path (<run>/dumps/N/TAG.l1)
--map FILE  linker map of the hub image holding 'MFM::theT6Grid'.  Default:
            build_cross/srcs/cross/hub.map under the repo root
--addr A    L1 address of theT6Grid, instead of a map
--site X,Y  print the atom type at that grid site (repeatable)
--show N    list up to N offending sites per ring (default 8)

T6Grid layout (srcs/shared/include/T6Grid.h, P4Atom.h): P4Atom
mT6Grid[WIDTH][HEIGHT] (x major, 12 bytes each: u16 parity+type, u16 data0,
u32 stg[2]) then u32 mTotalChanges.  Geometry comes from HubGeometry.py.
Exit status 1 if a permanent ring holds anything but EMPTY, else 0.  The
dump is not an instant snapshot (the hub keeps running), but a ring that
must stay empty must be empty at every instant.
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'srcs', 'host', 'python', 'EWD'))
import HubGeometry as G

W, H = G.T6GRID_WIDTH, G.T6GRID_HEIGHT
OW, OH = G.T6GRID_OVERLAP_WIDTH, G.T6GRID_OVERLAP_HEIGHT
ATOM = 12
EMPTY, INACCESSIBLE = 0, (1 << 9) - 1
MASKS = (0x81c0, 0x4038, 0x2007, 0x1e00, 0x0249, 0x0492, 0x0924, 0xf000)
PE_N, PE_W, PE_S, PE_E = 1, 2, 4, 8
L1_SIZE = 1536 * 1024


def valid_parity(d):
    if (d & 0x1ff) == INACCESSIBLE:
        return False
    return all(bin(d & m).count("1") % 2 == 0 for m in MASKS)


def decode(l1, addr):
    """-> (grid[x][y] = (type, valid), totalChanges)"""
    need = addr + W * H * ATOM + 4
    if addr % 4 or need > len(l1):
        raise ValueError(f"theT6Grid at 0x{addr:x} does not fit the dump "
                         f"({len(l1)} bytes)")
    grid = []
    for x in range(W):
        col = []
        for y in range(H):
            w, = struct.unpack_from("<H", l1, addr + (x * H + y) * ATOM)
            col.append((w & 0x1ff, valid_parity(w)))
        grid.append(col)
    changes, = struct.unpack_from("<I", l1, addr + W * H * ATOM)
    return grid, changes


def ring_of(x, y):
    """Which cache ring strips (set of 'N','W','S','E') a site is in."""
    s = set()
    if y < OH: s.add("N")
    if x < OW: s.add("W")
    if y >= H - OH: s.add("S")
    if x >= W - OW: s.add("E")
    return s


def mask_from_marks(dump):
    tag = os.path.splitext(os.path.basename(dump))[0]
    run = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(dump))))
    path = os.path.join(run, "tiles", tag + ".mfmk")
    try:
        m = re.search(r"\.pe:([0-9a-f]+)", open(path, errors="replace").read())
    except OSError:
        return None
    return int(m.group(1), 16) if m else None


def addr_from_map(path):
    for line in open(path, errors="replace"):
        m = re.match(r"\s*0x([0-9a-f]+)\s+MFM::theT6Grid\s*$", line)
        if m:
            return int(m.group(1), 16)
    raise ValueError(f"no MFM::theT6Grid in {path}")


def analyse(grid, pe, show):
    sides = {"N": PE_N, "W": PE_W, "S": PE_S, "E": PE_E}
    perm = {k for k, b in sides.items() if pe & b}
    out, bad = [], 0
    total = {"self": 0, "selfFull": 0, "rings": 0, "ringsFull": 0}
    badpar = inacc = 0
    perm_sites = {k: [] for k in perm}
    ring_full = {k: [0, 0] for k in sides}   # [occupied, sites], per strip
    for x in range(W):
        for y in range(H):
            t, ok = grid[x][y]
            if not ok: badpar += 1
            if t == INACCESSIBLE: inacc += 1
            rs = ring_of(x, y)
            if not rs:
                total["self"] += 1
                total["selfFull"] += t != EMPTY
            else:
                total["rings"] += 1
                total["ringsFull"] += t != EMPTY
            for k in rs:
                ring_full[k][1] += 1
                ring_full[k][0] += t != EMPTY
                if k in perm and t != EMPTY:
                    perm_sites[k].append((x, y, t))
    out.append(f"grid {W}x{H}, rings {OW}x{OH}, permanent edges: "
               f"{''.join(sorted(perm)) or 'none'} (pe mask {pe:x})")
    out.append(f"SELF region: {total['selfFull']} of {total['self']} sites occupied "
               f"({100.0 * total['selfFull'] / total['self']:.1f}%)")
    for k in "NWSE":
        o, n = ring_full[k]
        tag = "PERMANENT" if k in perm else "cache"
        out.append(f"  {k} strip ({tag:9}): {o:5} of {n} occupied")
    if badpar:
        out.append(f"WARNING {badpar} sites fail the atom parity check "
                   f"(wrong address, or a torn read?)")
    if inacc:
        out.append(f"note: {inacc} sites hold INACCESSIBLE_TYPE")
    for k in sorted(perm):
        sites = perm_sites[k]
        if sites:
            bad += len(sites)
            out.append(f"FAIL {k} ring is permanent but {len(sites)} sites are "
                       f"not EMPTY, first: "
                       + ", ".join(f"({x},{y}) type {t}" for x, y, t in sites[:show]))
    if perm and not bad:
        out.append(f"OK all permanent rings ({''.join(sorted(perm))}) are EMPTY")
    return out, bad


def profile(grid):
    """Occupancy map: one char per 3x4 sites (. none, 1-9 tenths, # >= 90%);
    '|' and '-' mark where the SELF region starts/ends (ring boundaries)."""
    cw, ch = 3, 4
    cols = [x for x in range(0, W, cw)]
    out = ["map (rows = y, columns = x; local grid coordinates; | = self/ring boundary):"]
    hdr = "".join("|" if (x <= OW < x + cw or x <= W - OW < x + cw) else " " for x in cols)
    out.append("    " + hdr)
    for y in range(0, H, ch):
        row = ""
        for x in cols:
            n = tot = 0
            for xx in range(x, min(x + cw, W)):
                for yy in range(y, min(y + ch, H)):
                    tot += 1
                    n += grid[xx][yy][0] != EMPTY
            f = n / float(tot)
            c = "." if n == 0 else ("#" if f >= 0.9 else str(max(1, int(f * 10))))
            row += c
        mark = "-" if (y <= OH < y + ch or y <= H - OH < y + ch) else " "
        out.append(f"{y:3d}{mark}{row}")
    out.append("occupied sites per column, last 25 columns (x: count of %d rows; "
               "ring starts at x=%d):" % (H, W - OW))
    out.append(" ".join(f"{x}:{sum(grid[x][y][0] != EMPTY for y in range(H))}"
                        for x in range(W - 25, W)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dump")
    ap.add_argument("--pe")
    ap.add_argument("--map")
    ap.add_argument("--addr")
    ap.add_argument("--show", type=int, default=8)
    ap.add_argument("--site", action="append", default=[],
                    help="X,Y: print the atom type (0 = empty) at that grid site"
                         " (hub grid coords, as `who` prints them); repeatable")
    ap.add_argument("--profile", action="store_true",
                    help="also print an occupancy map of the whole grid (cells "
                         "3 wide x 4 high, ring boundaries marked) and the "
                         "occupied count of each of the last 25 columns")
    a = ap.parse_args()
    l1 = open(a.dump, "rb").read()
    if len(l1) != L1_SIZE:
        print(f"warning: dump is {len(l1)} bytes, expected {L1_SIZE}")
    try:
        if a.addr:
            addr = int(a.addr, 0)
        else:
            repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
            addr = addr_from_map(a.map or os.path.join(
                repo, "build_cross", "srcs", "cross", "hub.map"))
        if a.pe is not None:
            pe = int(a.pe, 16)
        else:
            pe = mask_from_marks(a.dump)
            if pe is None:
                print("no --pe given and no '.pe:' mark found for this tile")
                return 2
        grid, changes = decode(l1, addr)
    except (ValueError, OSError) as e:
        print(f"l1grid: {e}")
        return 2
    lines, bad = analyse(grid, pe, a.show)
    print(f"{a.dump}: theT6Grid at 0x{addr:x}, mTotalChanges {changes}")
    print("\n".join(lines))
    for t in a.site:
        sx, sy = (int(v) for v in t.split(","))
        ty, ok = grid[sx][sy]
        print(f"site ({sx},{sy}): type {ty}{' (EMPTY)' if ty == EMPTY else ''}"
              f"{'' if ok else ' PARITY BAD'}")
    if a.profile:
        print("\n".join(profile(grid)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
