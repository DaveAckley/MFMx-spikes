# EXPERIMENT: FB4 5 vs 8 rounds, 9 x (whole-grid nuke, seed 3 5 + 3 8, 15 min), honest, ~2.5 h
# Bytecode FB4 (type 8) out-held native FB4 (type 5) in 3 runs of 3
# (OPEN_ISSUES 'physics as bytecode'), though each 8 event costs 6.6x
# the cycles; a neutral voter model says coin flip.  This runs nine
# rounds in one run: a nuke of radius 1150 at the grid center (959,540)
# erases every site; a 60 s wait lets the hubs finish erasing (the
# nuke rides the next NEW_LEADER and takes a while), then 3 seeds of
# each type fight for ~15 min.
# Read runsum.txt's 'physics timing' window table: per 300 s window the
# events of each type and share = 8/(5+8); the last window of each round
# (about every 960 s) says who was winning.  Also a long soak of the
# log-car fix (a941d4c): live AER and tile logs should last to the end.
# fourcorners, events honest, no autonuke.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
events honest
elide on
grid off
decay 5
dwell 4 8
start
feed on
zoom auto
zoom shrink on
zhd
cycle repeat on
cycle start
wait 30
echo ROUND 1 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 2 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 3 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 4 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 5 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 6 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 7 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 8 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
echo ROUND 9 of 9: clear the grid, seed 3 x type 5 and 3 x type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 450
status
wait 450
zhd
status
quit close
