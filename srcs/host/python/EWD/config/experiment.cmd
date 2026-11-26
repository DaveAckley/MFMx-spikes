# EXPERIMENT: log-car fix check, bytephys FB1 then FB4 (types 4/7, then 5/8), no sweeps, 20 min
# First run of the bytecode physics spike (OPEN_ISSUES 'physics as
# bytecode'): types 7 and 8 are FB1 and FB4 run by the BytePhys machine
# (srcs/shared/include/BytePhys.h, scripts/bytephys/*.bp), proven
# window-identical to native types 4 and 5 by bytephys_test.  Native and
# bytecode run SIDE BY SIDE in one run: every EWP times every event
# (AI clock cycles in guardedPhysics) per type and logs `.pt` marks;
# runsum.txt's "physics timing" section gives cycles/event per type and
# the bytecode/native ratio.  AER is a mix of both types, so read the
# timing section, not AER.  FB1 phase: autoseed type 4 plus `seed 25 4`
# and `seed 25 7` (bytecode drawn darker).  After 10 min, FB4 phase:
# `seed 3 5` and `seed 3 8` (FB4s take over).  fourcorners, events
# honest, no nukes.  Expect: no `.pf` marks (no machine faults), ratios
# somewhere around 3-10x.
# THIRD RUN, LOG-CAR FIX: run 141643 (with `scan 140` sweeps) gave the
# timing (bytecode 4.4x FB1, 6.6x FB4) but showed every tile's log dying
# after 65.5 s with no mark (OPEN_ISSUES Bugs).  With the fix the `.pt`
# marks arrive through the normal log path: expect runsum.txt's
# "physics timing" over ~105 EWPs per chip (not 3), tiles/ holding hub
# AND EWP files (~560), the live AER line all the way, and timing close
# to 141643's.  No sweeps (they stalled mfmxctl ~10 min each).
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
seed 25 4
seed 25 7
echo FB1 phase, block 1 of 2 (5 min)
wait 300
status
zhd
echo FB1 phase, block 2 of 2 (5 min)
wait 300
status
zhd
echo FB4 phase: seed native (5) and bytecode (8) FB4
seed 3 5
seed 3 8
echo FB4 phase, block 1 of 2 (5 min)
wait 300
status
zhd
echo FB4 phase, block 2 of 2 (5 min)
wait 300
status
zhd
quit close
