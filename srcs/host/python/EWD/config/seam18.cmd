# CACHE ELISION soak, ONE hour: seam16 (seed 25 4 stress, honest AER) with
# `elide on` and `cycle repeat on` (the corner is picked 0..3 uniformly, a repeat
# allowed).  Bands whose role is the same under the previous and the next corner
# are neither saved nor reloaded (hub .xk:MASK marks, one bit per Dir8 loader).
# Expected: about 42 percent less exchange traffic (xchg.py packets per phase),
# same AER or better, breaks none.  UNPROVEN: atom contents in the kept bands
# (the lender's lent sites stay stale until the next SAVE), see OPEN_ISSUES.
#   python mfmxctl.py --layout seam4 --boot -s config/seam18.cmd --script-exit --abort-on-error
# Afterwards (compare with a seam16/17 run of the same length)
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time>
#   grep -rh "\.xk:" MFMX-<date>-<time>/tiles | sort | uniq -c
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
