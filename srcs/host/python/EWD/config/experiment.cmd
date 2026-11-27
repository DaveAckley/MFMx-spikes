# EXPERIMENT: BytePhys new dispatch: microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# The interpreter now runs only verified programs (checked at compile
# time), dispatches with computed goto built O2 inside the -Os image, and
# compares fuel only at backward jumps and calls.  Run 20261127-110936
# (old dispatch) measured: cheapest ops 42-48 cycles per instruction in
# L1, jumps/pushes ~55, getm ~175, setm ~215, copy ~150; and run
# 20261127-094705: FB1 bytecode 3013 cycles/event (6.37x native), FB4
# 24788 (7.59x).  Read runsum's 'bytecode microbenchmark' (expect the
# cheap ops well under 40) and 'physics timing' (types 7 and 8).
# Bench phase as in run 110936: 20 Bench atoms (type 31), decay 31 so
# the hub autoseeds drop Bench atoms too; they erase themselves after
# 104 events.  Then FB1 seeds, a whole-grid nuke, then FB4 seeds.
# Expect no `.pf`.
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
