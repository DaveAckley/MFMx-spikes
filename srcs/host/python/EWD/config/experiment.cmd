# EXPERIMENT: BytePhys code cache in fast RAM: microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# Compiled types (7, 8) now run from a copy of their program in hart B's
# fast RAM (ByteCache.h, Physics::mCodeCache: 4 slots of 256 bytes, LRU,
# re-copied every 4096 uses), and the dispatch masks each opcode to a
# 32-entry table (corrupt opcodes fault BF_OPCODE; one AND per insn).
# Run 20261127-151122 (code in L1, table in fast RAM): FB1 bytecode 1815
# cycles/event (3.98x native), FB4 12199 (3.74x); microbenchmark
# cheapest ops 20-22 (L) / 13.5-15.6 (F).  Expect FB4 toward 9-10k and
# FB1 lower ('physics timing'); the bench L column rises ~1 cycle (the
# mask; bench programs run uncached).  New runsum section 'code cache
# and hart B stack': cache hit rate (expect ~100%) and the stack
# high-water (old figure 784 bytes; HB data now ends at 0x840).
# Bench phase: 20 Bench atoms (type 31), decay 31; they erase
# themselves after 104 events.  Then FB1 seeds, a whole-grid nuke, then
# FB4 seeds.  Expect no `.pf`.
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
wait 240
zhd
echo FB1: native type 4 vs bytecode type 7
seed 3 4
seed 3 7
wait 20
seed 3 4
seed 3 7
wait 400
zhd
echo FB4: clear the grid, then native type 5 vs bytecode type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 20
seed 3 5
seed 3 8
wait 420
zhd
status
quit close
