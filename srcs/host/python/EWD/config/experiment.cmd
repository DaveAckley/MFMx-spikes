# EXPERIMENT: BytePhys per-opcode microbenchmark, 20 Bench atoms (type 31) on an empty grid, shortcut (AERse), ~13 min
# Each event on a Bench atom (ewp Physics.cpp runBench) runs the next of
# 26 bench programs (scripts/bytephys/benchgen.py: 8 copies of one
# opcode pattern per pass, 50 passes) twice: code in L1 as real physics
# runs, then copied into fast RAM; the base program also times native
# byte loads from L1 and from fast RAM.  4 rounds through the list (104
# events), then the atom erases itself.  Read runsum's 'bytecode
# microbenchmark': cycles per instruction per opcode, L1 vs fast RAM.
# Run 20261127-094705 measured ~58 cycles per instruction (FB4).  Expect
# about 80 runs per program and variant, no `.pf`, and the grid empty
# again at the end.  Also new since that run: the logo stats red is a
# step redder (0xff,0x70,0x88).
# decay 31: a hub whose grid is still empty after its autoseed countdown
# drops a Bench atom (not FB1, as the first try of this run did), so the
# grid holds only Bench atoms and the autoseeds add bench data.
# fourcorners, events shortcut (AERse), no autonuke.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
events shortcut
elide on
grid off
decay 31
dwell 4 8
start
feed on
zoom auto
zoom shrink on
zhd
cycle repeat on
cycle start
wait 30
echo BENCH: 20 Bench atoms, type 31
seed 10 31
wait 20
seed 10 31
wait 360
status
wait 300
zhd
status
quit close
