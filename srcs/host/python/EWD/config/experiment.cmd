# EXPERIMENT: BytePhys cheaper cache hits: microbenchmark 11 min, then FB1 (4 vs 7) 7 min, then FB4 (5 vs 8) 8 min, shortcut (AERse), ~28 min
# Same script as run 20261127-224448; only the chunk cache changed
# (ByteCache.h): a hit is now the slot hint (chunk number's low 6 bits
# -> slot) plus the slot's tag compare, writing nothing; scans only when
# the hint is stale; misses fill slots in turn (FIFO); one slot is
# re-copied from L1 every 1024 events (not per hit); run() reads the
# image's tables through locals.  Run 224448: bench call (call + ret)
# 138.8 cycles per insn in fast RAM (whole-program cache before: 22.0),
# 52.3 from L1 (31.1); enter 114.3 F / 58.5 L.  Expect call and enter F
# near or under their L numbers, L a bit lower too; FB4 ~9423 and FB1
# ~1695 about the same.  `.bc` is now MISSES:SCANS:REFRESHES:BYTES (no
# hit count); runsum prints misses and scans per million events.
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
