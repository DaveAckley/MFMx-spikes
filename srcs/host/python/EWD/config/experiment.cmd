# EXPERIMENT: ewsnap 3 h soak with autonuke (EW-lattice four corners), FB1 honest, seed 25 4, dwell 4 8, specks hourly
# Three hours of `policy ewsnap` on the diagonal-tiles build: how it holds
# up as the grid fills.  Like the step 4 runs (honest, elide on, decay 4,
# dwell 4 8, cycle repeat on) but with `autonuke on 1 100 100 1000` (as
# in liv's dave13.cmd: a blast every 1-100 s, radius 100-1000; AER is then
# NOT comparable with step 4)
# and `seed 25 4` once at the
# start (honest mode fills slowly from the lone autoseed; seeds cost
# ~0.015 AER in earlier runs).  18 blocks of 10 min, `status` and `zhd`
# after each; at each hour a quiet pause (holdat RUN_EVENTS, waitpause
# quiet) and `zoom specks` (turd check), then the cycle resumes.  `zhd
# all` at the end (stale reports per hub).  Step 4 for comparison:
# 234532 ewsnap AERre 0.919, stale 1.70M per 900 s, 0 turds.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy ewsnap
events honest
elide on
grid off
decay 4
dwell 4 8
start
seed 25 4
feed on
zoom auto
zoom shrink on
zoom status
zhd
cycle repeat on
cycle start
autonuke on 1 100 100 1000
echo block 1 of 18 (10 min each)
wait 600
status
zhd
echo block 2 of 18 (10 min each)
wait 600
status
zhd
echo block 3 of 18 (10 min each)
wait 600
status
zhd
echo block 4 of 18 (10 min each)
wait 600
status
zhd
echo block 5 of 18 (10 min each)
wait 600
status
zhd
echo block 6 of 18 (10 min each)
wait 600
status
zhd
echo hour 1: quiet pause and zoom specks
cycle holdat RUN_EVENTS 0
cycle waitpause 120 quiet
zoom specks
zhd
cycle start
echo block 7 of 18 (10 min each)
wait 600
status
zhd
echo block 8 of 18 (10 min each)
wait 600
status
zhd
echo block 9 of 18 (10 min each)
wait 600
status
zhd
echo block 10 of 18 (10 min each)
wait 600
status
zhd
echo block 11 of 18 (10 min each)
wait 600
status
zhd
echo block 12 of 18 (10 min each)
wait 600
status
zhd
echo hour 2: quiet pause and zoom specks
cycle holdat RUN_EVENTS 0
cycle waitpause 120 quiet
zoom specks
zhd
cycle start
echo block 13 of 18 (10 min each)
wait 600
status
zhd
echo block 14 of 18 (10 min each)
wait 600
status
zhd
echo block 15 of 18 (10 min each)
wait 600
status
zhd
echo block 16 of 18 (10 min each)
wait 600
status
zhd
echo block 17 of 18 (10 min each)
wait 600
status
zhd
echo block 18 of 18 (10 min each)
wait 600
status
zhd
echo hour 3: quiet pause and zoom specks
cycle holdat RUN_EVENTS 0
cycle waitpause 120 quiet
zoom specks
zhd
autonuke status
zhd all
quit close
