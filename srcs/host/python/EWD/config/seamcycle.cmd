# Eth-seam traffic check: the seamtest layout with SuperCycles running, so
# cars are exchanged across the seam through the E1s.
#   python mfmxctl.py --layout seamtest --boot --rtmp -s config/seamcycle.cmd --script-exit
# Watch: bolts reach CONSENSUS on both chips, and the 'seams' channel
# counters (out / in / noc) move while retrain and FEC uncorrected stay 0.
# A bolt TIMEOUT means a cache loader is waiting for a car that never came.
# The periodic slowscan runs on the SAME single thread that polls the bolts and blocks it for about 1.9 s
# on two chips, which showed up as consensus times of 1.7-2.0 s (OPEN_ISSUES "seamtest consensus stalls").
tick slowscan off
# Seed PHY_FB1 (type 4); without this the autoseed default is type 5 (FB4).
decay 4
dwell 4 8
start
seams
cycle start
wait 30
seams
wait 30
seams
wait 30
seams
cycle stop
wait 15
stop
seams
quit close
