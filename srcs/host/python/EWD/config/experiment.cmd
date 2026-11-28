# EXPERIMENT: BytePhys BCP blobs: microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# Same script as runs 20261127-224448 and 20261128-003440.  The code is
# now stored as ONE pointer-free BCP blob per physics (BytePhys.h
# 'BCP'), still compiled in; the EWP opens and re-verifies its blobs in
# initB (a failure would log BCPBAD and run no bytecode).  Nothing about
# the run should change: expect bench call 43.3 F / 50.7 L, enter 49.5 /
# 59.3, FB4 ~9436 cycles/event, FB1 ~1700-1760, `.bc` 0 scans, no
# BCPBAD mark, no `.pf`.  HB fast-RAM data ends at 0x10c0 (+80 B).
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
