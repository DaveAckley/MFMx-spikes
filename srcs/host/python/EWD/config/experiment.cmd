# EXPERIMENT: FB1 honest-AER at BOGOMS 50, NO nukes (build default 50u)
# Same script as the BOGOMS 25 run (AERre 0.917 falling to 0.885-0.894, 900 s).
# The 3 h soak at BOGOMS 50 read 0.956-0.986, so AER may rise with BOGOMS.
# If 50 reads 0.95+, BOGOMS is the lever (frame period vs events lost).
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
events honest
elide on
grid off
decay 4
dwell 2 3
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
