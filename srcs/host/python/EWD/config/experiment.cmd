# EXPERIMENT: fourcorners baseline for ewsnap (step 4, run 2 of 2), same build, FB1 honest, dwell 4 8, 15 min
# Step 4, run 2 of 2: IDENTICAL to run 1 (MFMX-20261125-234532, policy
# ewsnap) except `policy fourcorners`, on the same diagonal-tiles build
# (rings 22 for both).  Run 1: AERre 0.910/0.914/0.919, bolts 0.116 /
# 0.108 s, 0 timeouts, stale reports 1,704,826, 18 specks, 0 turds.
# The older ring-20 baseline 165051 read AERre 0.936 and 782,404 stale;
# this run says how much of the gap is ewsnap and how much the build.
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
