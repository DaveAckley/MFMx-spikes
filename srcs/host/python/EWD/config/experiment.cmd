# EXPERIMENT: BytePhys BCP blobs, diagnosis: microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# Same script as runs 003440 and 014339.  Run 014339 (91907bb) ran NO
# bytecode: the EWPs' BCP views were empty at events (OPEN_ISSUES).  This
# build logs `.bv:OPEN:BENCH:LATE:RETRY:BRETRY:TYPES.` with every
# 100k-event mark (initB's openBCP answers, 0 = ok; LATE 0x10 = initB
# opened, +1 = a view was empty at an event and was reopened; the
# reopen answers; the physics view's type count) and reopens an empty
# view, so the bytecode may run anyway.  Wanted: the `.bv` values
# (grep -h '\.bv:' tiles/*.mfmk | sort | uniq -c, or the all.mfmk tail),
# then the usual bench / FB1 / FB4 numbers if the bytecode ran.
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
