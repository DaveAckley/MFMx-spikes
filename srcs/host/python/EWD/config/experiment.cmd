# EXPERIMENT: FB1 honest-AER control at BOGOMS 25, NO nukes (build 78a991c62e+)
# Is the flat AERre about 0.92 (BOGOMS 10, run 131756, which was really FB4 and
# had random nukes) down because of 10 ms frames?  Same script again at
# BOGOMS 10 (rebuild with -DACB_BOGOMS_PER_FRAME=10u) if this one reads 0.95+.
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
