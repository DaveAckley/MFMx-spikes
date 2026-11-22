# Seam stall hunt (OPEN_ISSUES "seamtest consensus stalls"): seamcycle.cmd on a different layout with fewer
# than four cards: the right-hand pair, cards 1 above 3 (N/S seam).
# Compare the bolt consensus times (misc/mfmxctl-BOLT.txt) with seamcycle13 (seamtest, 2 cards E/W:
# about a third of the phases took 1.7-2.0 s instead of 0.1 s) and with seam21 (4 cards: no stalls).
#   python mfmxctl.py --layout seam13 --boot --rtmp -s config/seamcycle1to3.cmd --script-exit
# Watch: bolts reach CONSENSUS on every chip, 'seams' out/in/noc move, txfail/nocfail stay 0.
# The periodic slowscan runs on the SAME single thread that polls the bolts and blocks it for about 1.9 s
# on two chips, which showed up as consensus times of 1.7-2.0 s (OPEN_ISSUES "seamtest consensus stalls").
tick slowscan off
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
