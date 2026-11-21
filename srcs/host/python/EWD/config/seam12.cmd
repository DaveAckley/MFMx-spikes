# FOUR CORNERS soak: THREE HOURS (18 blocks of 10 minutes), `policy fourcorners`,
# seeded with PHY_FB1 (start symbol type 4).  Same layout, links, dwell and
# telemetry as seam9/seam10 (the one-hour supercell runs), so the FIRST HOUR
# compares directly with them (aer.py --series 600: rows t=600..3600 against
# runs 165950 and 211051: steady wall AER about 1.55-1.58 at about 73% fill,
# events-per-hub spread, duty 97%); the other two hours are a soak for the
# intermittent hang and for slow drift (a falling car rate would mean the CA
# is dying out or a seam is stalling).  seam11.cmd (12 min) was the first
# clean four-corners run (022401).
#   python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam12.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --bolts seam12-NN.out
# Check df first: the run directory grows several MB a minute (more marks
# than leader/follower: three bands each way per hub).  A bolt TIMEOUT
# freezes the cycle (the hang diagnostics run: slowscan tick turns on, .xw
# heartbeat): the script then idles to its end ('status' shows FROZEN) and
# still shuts down in an orderly way.  Do not kill -9; to stop early type
# 'quit close' at the prompt (works during a wait) or pkill -TERM -f mfmxctl.py.
tick slowscan off
policy fourcorners
events honest
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
cycle stop
wait 15
stop
seams
quit close
