# EXPERIMENT: FB1 honest-AER at BOGOMS 50, dwell 4 8 (was 2 3), NO nukes
# Same as the previous run (AERre 0.885/0.886/0.894 at 900 s) except dwell.
# The 3 h soak (0.956-0.958, seam12.cmd) ran dwell 4 8; is dwell the lever?
# If it reads 0.95+, dwell is it; else bisect from the soak's commit.
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
seed 25 4
feed on
zoom auto
zoom shrink on
zoom status
zhd
cycle repeat on
cycle start
wait 900
zhd
quit close
