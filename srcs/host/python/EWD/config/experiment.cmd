# EXPERIMENT: BytePhys images + chunk cache: microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# The bytecode format changed: all the physics' .bp files are ONE image
# of functions cut into <= 256-byte chunks (jumps in a chunk take a u8
# offset, xjmp/xjz/xjnz cross chunks, call names a function), and the
# fast-RAM code cache holds chunks: 12 slots of 256 bytes (~3 KB, Liv's
# budget), asked only at entry, cross-chunk jumps, calls and returns.
# Runs 20261127-200923 / 204054 (whole-program cache): FB4 bytecode
# 9468 / 9454 cycles/event (2.89x native), FB1 1633 / 1635 (3.5x).
# Expect FB4 about the same or a little better (shorter jumps), FB1 a
# little worse or the same (its call into kabam is now a cache lookup
# each way).  The bench F column now runs through the cache: the call
# and enter patterns include a lookup per call and per return.  New
# runsum field: cache hits outside the MRU slot.  Stack: HB data now
# ends at 0x10b0 (link fails under 2 KB of stack).  Also check after the
# FB1-phase nuke: no turds at the outer edges.
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
