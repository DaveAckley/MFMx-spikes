# Eth-seam traffic check: the seamtest layout with SuperCycles running, so
# cars are exchanged across the seam through the E1s.
#   python mfmxctl.py --layout seamtest --boot --rtmp -s config/seamcycle.cmd --script-exit
# Watch: bolts reach CONSENSUS on both chips, and the 'seams' channel
# counters (out / in / noc) move while retrain and FEC uncorrected stay 0.
# A bolt TIMEOUT means a cache loader is waiting for a car that never came.
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
