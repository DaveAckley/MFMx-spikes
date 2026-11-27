# EXPERIMENT: BytePhys v1 timing, FB1 (4 vs 7) 12 min then FB4 (5 vs 8) 12 min, honest, ~30 min
# BytePhys version 1 (8542b18): types 7 and 8 now read and write their
# counters through declared members (getm/setm: a layout table lookup
# plus bit extraction per access) instead of raw getf/setf, and
# arithmetic saturates.  This re-times them against native: version 0
# measured FB1 4.5x (~2260 vs ~500 cycles/event) and FB4 6.6x (21886 vs
# 3320).  Phase A runs FB1 only (FB4 would overrun it), then a nuke of
# the whole grid, a 60 s wait for the hubs to finish erasing, and phase
# B runs FB4 only.  Seeds go in two batches 20 s apart per type (whole
# batches were lost in run 183231, OPEN_ISSUES 'LOST SEED BATCHES').
# Read runsum.txt's 'physics timing': cycles/event, bytecode insns/event
# and the bytecode/native ratios; there should be no `.pf` faults.
# fourcorners, events honest, no autonuke.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
events honest
elide on
grid off
decay 4
dwell 4 8
start
feed on
zoom auto
zoom shrink on
zhd
cycle repeat on
cycle start
wait 30
echo PHASE A: FB1, native type 4 vs bytecode type 7
seed 3 4
seed 3 7
wait 20
seed 3 4
seed 3 7
wait 360
status
wait 360
zhd
echo PHASE B: clear the grid, then FB4, native type 5 vs bytecode type 8
nuke 959 540 1150
wait 60
seed 3 5
seed 3 8
wait 20
seed 3 5
seed 3 8
wait 360
status
wait 360
zhd
status
quit close
