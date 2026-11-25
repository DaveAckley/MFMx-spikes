# EXPERIMENT: FB1 honest-AER, dwell 4 8, NO 'seed 25 4' (autoseed only, like the soak)
# Same as the last run (AERre 0.940 at 110 s, 0.913/0.914/0.921 at 900 s) except no seed.
# Soak 124337 read 0.98 at 180-900 s with a single autoseed (grid nearly empty).
# If this reads 0.98, fill (25 seeds) explains it; else run the soak's commit.
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
quit close
