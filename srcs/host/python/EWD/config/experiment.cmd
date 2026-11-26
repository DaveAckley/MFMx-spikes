# EXPERIMENT: ewsnap first run (EW-lattice four corners, branch diagonal-tiles), FB1 honest, dwell 4 8, 15 min
# Step 4, run 1 of 2: `policy ewsnap` on the diagonal-tiles build.  Run 2
# (next experiment.cmd) is the SAME script with `policy fourcorners` on
# the SAME build (rings are 22 for both there), for the A/B.
# Settings are the last FB1 experiment's (honest events, elide on, decay
# 4, dwell 4 8, cycle repeat on, no seed, no nukes, 900 s).
# Look for: start completes (center tables built at init, `.ct` in the
# runsum "hub marks" section on all 140 hubs, no hang), bolts reach
# CONSENSUS with no TIMEOUT (a band mismatch would assert in a hub and
# time a bolt out), zhd clean, AERre (compare with run 2).  At the end a
# quiet pause and `zoom specks` (turd check; its RING/SELF and owner
# labels assume SELF rectangles, so read rows at sawtooth tips with care).
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
feed on
zoom auto
zoom shrink on
zoom status
zhd
cycle repeat on
cycle start
wait 900
zhd
cycle holdat RUN_EVENTS 0
cycle waitpause 120 quiet
zoom specks
zhd
quit close
