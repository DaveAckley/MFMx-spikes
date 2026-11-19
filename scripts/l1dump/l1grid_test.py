#!/usr/bin/env python3
"""Tests for l1grid.py on synthetic dumps.  Run: python3 scripts/l1dump/l1grid_test.py"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import l1grid as L

fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else "   " + str(detail)))
    fails += not ok


PARITY = (0x8000, 0x4000, 0x2000, 0x1000, 0x0200, 0x0400, 0x0800, 0x1000)


def add_parity(d):
    for m, p in zip(L.MASKS, PARITY):
        if bin(d & m).count("1") & 1:
            d ^= p
    return d


ADDR = 0x7276c


def make(atoms, bad_parity=()):
    """atoms: {(x, y): type}.  Returns a dump image."""
    l1 = bytearray(L.L1_SIZE)
    for (x, y), t in atoms.items():
        w = add_parity(t)
        struct.pack_into("<H", l1, ADDR + (x * L.H + y) * L.ATOM, w)
    for (x, y), w in dict(bad_parity).items():
        struct.pack_into("<H", l1, ADDR + (x * L.H + y) * L.ATOM, w)
    struct.pack_into("<I", l1, ADDR + L.W * L.H * L.ATOM, 4242)
    return bytes(l1)


def run(l1, *args):
    d = tempfile.mkdtemp()
    p = os.path.join(d, "at023.l1")
    open(p, "wb").write(l1)
    r = subprocess.run([sys.executable, os.path.join(HERE, "l1grid.py"), p,
                        "--addr", hex(ADDR), *args], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


selfatoms = {(x, y): 5 for x in range(L.OW, L.W - L.OW, 3) for y in range(L.OH, L.H - L.OH, 3)}

# valid parity helper agrees with the C++ rules
check("parity: empty valid", L.valid_parity(add_parity(0)))
check("parity: type 5 valid", L.valid_parity(add_parity(5)))
check("parity: inaccessible type never valid", not L.valid_parity(add_parity(L.INACCESSIBLE)))

rc, out = run(make({**selfatoms, (L.W - 3, 50): 7, (60, L.H - 4): 7}), "--pe", "3")
check("clean: N,W permanent and empty; S,E rings may hold atoms", rc == 0 and "OK all permanent rings (NW) are EMPTY" in out, out)
check("clean: reports changes and addr", "mTotalChanges 4242" in out and "0x7276c" in out, out)

rc, out = run(make({**selfatoms, (3, 50): 9}), "--pe", "3")
check("atom in permanent W ring fails", rc == 1 and "FAIL W ring" in out and "(3,50) type 9" in out, out)

rc, out = run(make({(5, 5): 9}), "--pe", "1")
check("corner site counts for the N strip", rc == 1 and "FAIL N ring" in out, out)

rc, out = run(make({(5, 5): 9}), "--pe", "4")
check("corner (NW) is not the S strip: pe=S is clean", rc == 0, out)

rc, out = run(make({}), "--pe", "f")
check("empty grid, all four permanent: OK", rc == 0 and "(ENSW)" in out, out)

rc, out = run(make({(L.W - 1, L.H - 1): 3}), "--pe", "c")
check("SE corner fails S and E", rc == 1 and "FAIL S ring" in out and "FAIL E ring" in out, out)

rc, out = run(make({}, bad_parity={(70, 70): 0x0001}), "--pe", "0")
check("bad parity warned", "WARNING 1 sites fail the atom parity check" in out, out)

rc, out = run(make({}), "--pe", "0")
check("no permanent edges: nothing to check, rc 0", rc == 0 and "permanent edges: none" in out, out)

p = subprocess.run([sys.executable, os.path.join(HERE, "l1grid.py"), "/nonexistent.l1"],
                   capture_output=True, text=True)
check("missing file is an error, not a crash trace", p.returncode != 0, p.stderr[-200:])

# auto .pe from the run's tiles/ mark
d = tempfile.mkdtemp()
os.makedirs(os.path.join(d, "dumps", "10"))
os.makedirs(os.path.join(d, "tiles"))
open(os.path.join(d, "tiles", "at023.mfmk"), "w").write("{23:45 21177 0 2,3 hb .pe:3}\n")
dp = os.path.join(d, "dumps", "10", "at023.l1")
open(dp, "wb").write(make({(3, 50): 9}))
r = subprocess.run([sys.executable, os.path.join(HERE, "l1grid.py"), dp, "--addr", hex(ADDR)],
                   capture_output=True, text=True)
check("pe read from tiles/TAG.mfmk, W ring fails", r.returncode == 1 and "pe mask 3" in r.stdout, r.stdout + r.stderr)

print("ALL PASS" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
