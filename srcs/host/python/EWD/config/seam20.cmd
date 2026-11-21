# CACHE ELISION soak at the 15-wide cache ring (044edf1), ONE hour: seam18's
# setup (seam16 stress, honest AER, `elide on`, `cycle repeat on`) on the geometry
# OW = OH = 15 (hub grid 167 x 138) with the elision fix of 4dc688f (kept bands
# are skipped at BOTH the SAVE and the LOAD; seam19-11 showed 0 packets in kept
# directions).  First long run of elision applied correctly: watch the UNPROVEN
# parts (OPEN_ISSUES "Cache elision"): atom contents in kept bands, the lender's
# stale lent sites (display and census lag).
#   python mfmxctl.py --layout seam4 --boot -s config/seam20.cmd --script-exit --abort-on-error
# Afterwards (compare with seam17/18, same length)
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --dirs
#   grep -rh "\.xk:" MFMX-<date>-<time>/tiles | sort | uniq -c
#   grep -h "ZHD" MFMX-<date>-<time>/misc/*ZHD.txt   (no file = no stream breaks)
# Also: the picture still lines up (display crop uses OW), and hub.map's image end.
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
