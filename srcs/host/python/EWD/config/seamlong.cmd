# Eth-seam long soak: the seamtest layout with SuperCycles running, so
# cars are exchanged across the seam through the E1s.
# Long enough (about 5 minutes of cycling) for the atoms seeded on chip 0 to
# diffuse across the grid to the seam and onto chip 1 (about 10 s per hub in
# run 093945: first atoms reached ct6 x=11 at ~90 s).  Then read the corner
# caches of the seam hubs:
#   scripts/cachecensus/cachecensus.py MFMX-<date>-<time>
# chip 0 x=13 hubs should show NE/SE caches (and chip 1 x=1 hubs NW/SW)
# nonzero once atoms are near; Dir4 columns ET/WT fill first.
#   python mfmxctl.py --layout seamtest --boot --rtmp -s config/seamlong.cmd --script-exit
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
