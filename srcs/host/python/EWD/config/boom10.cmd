# PHYSICS_FAIL SELF-TEST, about 4.5 minutes, no feed: seed 25 FB1 (type 4),
# start the cycle (events only run while the cycle runs), then seed 10 PHY_BOOM
# atoms (type 6) into the running machine.  Each PHY_BOOM center
# scribbles its four neighbors then calls PHYSICS_FAIL; recovery commits the
# OLD window with the center erased, so every BOOM dies on its first event,
# leaves no trace in its neighbors, and the run must keep going (before the
# guard that hart hung forever).  Needs ./build_all.sh at 1c17b62 or later.
#   python mfmxctl.py --layout seam4 --boot -s config/boom10.cmd --script-exit --abort-on-error
# Afterwards (expect 10 lines of `.pf:4e06.`, 4e06 hex = (39<<9)|6 where 39 is
# USER_REQUESTED_FAILURE and 6 the center type; the guard logs the first 8
# failures per hart and every 1024th, so 10 BOOMs on 10 different sites show up
# as up to 10 separate marks, never more than 8 on one hart):
#   grep -h '\.pf:' MFMX-<date>-<time>/tiles/*.mfmk | sed 's/.*\.pf://' | sort | uniq -c
#   grep -h 'PHYSFAIL' MFMX-<date>-<time>/tiles/*.mfmk | head
# and that nothing hung: python3 scripts/aer/aer.py MFMX-<date>-<time> --series 60
tick slowscan off
policy fourcorners
events honest
elide on
decay 4
dwell 4 8
start
seed 25 4
cycle repeat on
cycle start
wait 30
status
seed 10 6
wait 20
status
wait 60
status
wait 60
status
wait 60
status
cycle stop
wait 15
stop
status
quit close
