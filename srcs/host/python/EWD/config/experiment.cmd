# EXPERIMENT: BytePhys compiled code + fast path timing, FB1 (4 vs 7) 12 min then FB4 (5 vs 8) 12 min, shortcut (AERse), ~30 min
# Types 7 and 8 now run code COMPILED from scripts/ulamlite/*.ulam
# (ulam-lite, BYTEPHYS.md section 13; FB4 494 bytecode instructions per
# event vs 369 hand written, FB1 calls a kabam() method with a local),
# dispatched through the generated type -> program table, and members
# that sit inside one 32-bit word (all of FB1's and FB4's) skip the u64
# work in getBits/setBits.  Version 1 (hand code, no fast path, run
# 20261127-011502) measured FB1 2955 cycles/event (5.59x native), FB4
# 23514 (7.10x).  Read runsum.txt's 'physics timing': cycles/event and
# insns/event per type; cycles per instruction says what the fast path
# bought, insns per event what the compiler cost.  Expect no `.pf`.
# Phase A runs FB1 only (FB4 would overrun it), then a nuke of the whole
# grid, a 60 s wait for the hubs to finish erasing, and phase B runs FB4
# only.  Seeds go in two batches 20 s apart per type (whole batches were
# lost in run 183231, OPEN_ISSUES 'LOST SEED BATCHES').
# fourcorners, events shortcut (AERse: seeds grow faster than in honest
# AERre; use `events honest` only for AER studies), no autonuke.
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
