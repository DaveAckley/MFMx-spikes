# CACHE RING 20 (seam20's setup), ONE hour: elision soak (honest AER, `elide on`,
# `cycle repeat on`) on OW = OH = 20 (hub grid 177 x 148, T6Grid 314356 bytes,
# ~337 KB of the hub's L1 left).  Compare with seam20 (OW = 15): AER, xchg.py
# packets and time per phase (bands are 33 percent deeper), consensus times
# (seen under 0.2 s at OW = 15), integrity lines, the display crop, the seam lines.
#   python mfmxctl.py --layout seam4 --boot -s config/seam21.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --dirs
#   grep -h "ZHD" MFMX-<date>-<time>/misc/*ZHD.txt   (no file = no stream breaks)
#   grep "end = " hub.map | grep hostblock_end  (image end)
tick slowscan off
policy fourcorners
events honest
elide on
decay 4
dwell 4 8
start
seed 25 4
feed on
seams
status
zhd
cycle repeat on
cycle start
wait 600
seams
status
zhd
wait 600
seams
status
zhd
wait 600
seams
status
zhd
wait 600
seams
status
zhd
wait 600
seams
status
zhd
wait 600
seams
status
zhd
zhd all
cycle stop
wait 15
stop
seams
quit close
