# Eth-seam 2x2 array (slice 3b): all four cards, four seams, 16 links; only
# chip 0 autoseeds (layout key seedchips), so atoms on chips 1, 2 and 3 got
# there through the cables (chip 3 only via 1 or 2).  The four centre
# diagonals are NOT wired yet (centre corner caches stay stale, expected).
#   python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam4.cmd --script-exit
# About 10 minutes of cycling.  Afterwards
#   scripts/cachecensus/cachecensus.py MFMX-<date>-<time> \
#     --seam 0:E:1 --seam 2:E:3 --seam 0:S:2 --seam 1:S:3
# Watch: 16 links UP, retrain/FEC uncorrected 0, bolts CONSENSUS (no TIMEOUT:
# a TIMEOUT would mean stale centre corners DO hang a phase), 'seams' counters
# moving on all four seams, atoms reaching chip 3 (rtmp view, cachecensus).
dwell 4 8
start
seams
cycle start
wait 60
wait 60
seams
wait 60
wait 60
seams
wait 60
wait 60
seams
wait 60
wait 60
seams
wait 60
wait 60
seams
cycle stop
wait 15
stop
seams
quit close
