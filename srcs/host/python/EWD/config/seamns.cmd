# Eth-seam N/S check (slice 3a): cards 0 (above) and 2 on ETH x = 3,4,12,13,
# SuperCycles running for about 5 minutes so atoms (seeded at hub (5,5) of each
# chip) spread to the seam.
#   python mfmxctl.py --layout seamns --boot --rtmp -s config/seamns.cmd --script-exit
# Watch: all four links UP, retrain/FEC uncorrected 0, bolts CONSENSUS, and the
# 'seams' counters moving on 5 channels per link (19 in all).  Afterwards
#   scripts/cachecensus/cachecensus.py MFMX-<date>-<time> --seam 0:S:2
# every SEAM hub should be "ok" (NE/SE/ST etc. caches facing the seam nonzero).
dwell 4 8
start
seams
cycle start
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
