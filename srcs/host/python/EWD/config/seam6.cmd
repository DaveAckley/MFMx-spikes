# Eth-seam 2x2 array, medium run (about 12 minutes of cycling), seeded with
# PHY_FB1 (start symbol type 4: full and empty patches at steady state, a more
# plausible CA load than type 5 = PHY_FB4).  Same layout, links and one-way
# centre channels as seam4.cmd; only chip 0 autoseeds.
#   python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam6.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 120
#   scripts/cachecensus/cachecensus.py MFMX-<date>-<time> \
#     --seam 0:E:1 --seam 2:E:3 --seam 0:S:2 --seam 1:S:3
# Telemetry is deliberately cheap: slowscan is OFF (it starved the frame tick
# and doubled bolt times), 'seams' (link health + every channel counter, a
# few seconds, blocking) and 'status' run only every 4 minutes.  Watch: retrain
# and FEC uncorrected 0, txfail 0, no bolt TIMEOUT, the 8 centre channels
# moving cars one way each, out == in == noc, steady bolt times (about 2 s),
# and no change in the numbers of cars per 4 minutes (a falling rate would
# mean the CA is dying out or a seam is stalling).
tick slowscan off
policy supercell
decay 4
dwell 4 8
start
seams
status
cycle start
wait 240
seams
status
wait 240
seams
status
wait 240
seams
status
cycle stop
wait 15
stop
seams
quit close
