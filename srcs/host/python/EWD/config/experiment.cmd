# EXPERIMENT: BytePhys optimized compiled code timing, FB1 (4 vs 7) 12 min then FB4 (5 vs 8) 12 min, shortcut (AERse), ~30 min
# Types 7 and 8 run ulam-lite code after the loop optimization (rotated
# loops, for counters on the operand stack): FB4 374 bytecode
# instructions per event (was 494 in run 20261127-015533, hand-written
# 369), FB1 ~31.  Also new since that run: EACH is a countdown and the
# per-event pew* modulo is gone (all images).  Run 015533 measured FB1
# 3167 cycles/event (6.60x native), FB4 33853 (10.22x).  Read runsum's
# 'physics timing': insns/event should drop to ~374 for type 8, and
# cycles/event with it if the cost per instruction holds.  Expect no
# `.pf`.  Phase A runs FB1 only, then a whole-grid nuke, a 60 s wait,
# and phase B runs FB4 only; seeds in two batches 20 s apart per type.
# fourcorners, events shortcut (AERse), no autonuke.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
events shortcut
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
