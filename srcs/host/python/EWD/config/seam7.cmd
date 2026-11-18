# Eth-seam 2x2 array, VERY long run (about 6 hours of cycling), seeded with
# PHY_FB1 (start symbol type 4), for when nobody is at the QuietBox.  Same
# layout, links and one-way centre channels as seam4.cmd; only chip 0
# autoseeds.  Telemetry is cheap: 'seams' (link health, every channel
# counter; a few seconds, blocking) and 'status' only every 10 minutes.
#   python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam7.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 1800
#   scripts/cachecensus/cachecensus.py MFMX-<date>-<time> \
#     --seam 0:E:1 --seam 2:E:3 --seam 0:S:2 --seam 1:S:3
# Before leaving: 'df -h' for the run directory and /data/ackley/AV (the run
# directory grows about 3.4 MB per minute, about 1.2 GB for 6 hours, and the
# stream also tees to a local file there).  A failed boot or start ends the
# run cleanly (--abort-on-error).  A bolt TIMEOUT freezes the cycle: the
# script then idles to its end ('status' shows FROZEN), still shutting down
# in an orderly way.  Do not kill -9 it; to stop early type 'quit close' at the
# prompt (works during a wait) or pkill -TERM -f mfmxctl.py.  To change the
# length, edit the number of blocks below (each block is 10 minutes).
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
