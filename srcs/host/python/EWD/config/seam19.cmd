# Cache elision, per-direction packets, SHORT (20 min): same setup as seam18
# (seam16 stress, honest AER, `elide on`, `cycle repeat on`) but the hub .xe
# marks now carry the packets moved in each Dir8, so xchg.py --dirs can show
# which directions carry SAVE and LOAD traffic and whether a kept direction
# (mask in .xk) really moves nothing.  Question: seam18 showed SAVE packets
# falling with the keep mask (-47 percent overall) but LOAD staying flat
# (-11 percent) -- why?
#   python mfmxctl.py --layout seam4 --boot -s config/seam19.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --dirs
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   grep -h "ZHD" MFMX-<date>-<time>/misc/*ZHD.txt   (no file = no stream breaks)
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
zhd all
cycle stop
wait 15
stop
seams
quit close
