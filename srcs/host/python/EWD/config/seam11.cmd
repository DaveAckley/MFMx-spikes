# FOUR CORNERS prototype run (12 minutes), the 'policy fourcorners' twin of
# seam6.cmd: same layout, links, seed (PHY_FB1), dwell and telemetry, so the
# numbers compare directly with seam6 runs 005507 / 012507 (leader/follower).
# Every hub now does the same thing each supercycle: the NEW_LEADER argument
# is a Corner4 (the corner the hubs expand toward), see CachingPolicy.h.
#   python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam11.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 120
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --bolts seam11-NN.out
# Watch: out == in == noc, no BADC marks (an exchange range mismatch), no
# bolt TIMEOUT, STALLS none, duty near 97%, and the events-per-hub spread
# that aer.py prints at the end (about 44% with leader/follower: the number
# four corners should shrink).  The hubs log '.cp:1' once to say which
# policy they really ran.  Policy is set before 'start' (applied at deploy).
tick slowscan off
policy fourcorners
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
