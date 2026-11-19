# SLEEP 0 experiment (PACKET_SLEEP_CYCLES = 0).  Same script as seam9.cmd.
# Eth-seam 2x2 array, ONE HOUR run (6 blocks of 10 minutes), seeded with
# PHY_FB1 (start symbol type 4): long enough for the grid to fill and the
# rates to settle (about 30 min; the 6 hour run 20261118-035205 plateaued at
# 73% fill).  Same layout, links and one-way centre channels as seam4.cmd;
# only chip 0 autoseeds.  Dwell is left at 4 8 so the result compares with
# that run.
#   python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam10.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --bolts seam10-NN.out
#   scripts/cachecensus/cachecensus.py MFMX-<date>-<time> \
#     --seam 0:E:1 --seam 2:E:3 --seam 0:S:2 --seam 1:S:3
# A failed boot or start ends the run cleanly (--abort-on-error).  A bolt
# TIMEOUT freezes the cycle: the script then idles to its end ('status' shows
# FROZEN), still shutting down in an orderly way.  Do not kill -9 it; to stop
# early type 'quit close' at the prompt (works during a wait) or pkill -TERM
# -f mfmxctl.py.
tick slowscan off
decay 4
dwell 4 8
start
seams
status
cycle start
wait 600
seams
status
wait 600
seams
status
wait 600
seams
status
wait 600
seams
status
wait 600
seams
status
wait 600
seams
status
cycle stop
wait 15
stop
seams
quit close
