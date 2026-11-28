# EXPERIMENT: BytePhys physics loaded at start (physics bfb): microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# Same script as run 030540, plus `physics bfb` before `start`: the
# host verifies physics/bfb.bcp (bpasm.py's blob, the same bytes as the
# compiled-in BP_BCP) and writes it after the EWP image in L1 (0x23450),
# and the EWP opens THAT one.  Expect the host's `EWP: physics
# .../bfb.bcp, 280 bytes, checksum 0x7ed764ec` line, `.bv:1:0:0:2:1:
# 7ed764ec.` on every EWP after its first bytecode event (runsum 'BCP
# opens'; LOADED=1 is the new part), and everything else as 030540:
# bench call ~43 F / ~51 L, enter ~50 / ~59, FB4 ~9380 cycles/event,
# FB1 ~1650-1760, `.bc` 0 scans, no BCPBAD, no `.pf`.
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
physics bfb
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
