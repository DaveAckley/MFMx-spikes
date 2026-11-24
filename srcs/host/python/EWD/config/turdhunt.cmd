# TURD HUNT, many quiet pauses: the dave12 setup, then EIGHT scripted pauses
# at the next RUN_EVENTS (SAVE done, events stopped), each followed by
# `waitpause quiet` (DLs and ACBs drained) and `zoom specks` (hub truth, OWNER,
# HIST, "=> TURD" only when every copy that could hold the truth is empty).
# About 30 minutes (50 cycles between pauses, then about a minute of specks).
#   python mfmxctl.py --layout seam4 --boot -s config/turdhunt.cmd --script-exit --abort-on-error
# (or however you ran dave12.cmd; same options)
# Afterwards, in the output file:
#   grep -a -c '=> TURD' X.out               (definite turds; ignore the build line)
#   grep -a -c 'kept band' X.out             (ring atom + owner empty: fine under elision)
#   grep -a -c '^([0-9]*,[0-9]*) \(SELF\|RING\)' X.out   (rows)
#   grep -a 'reports quiet\|were gone' X.out (each pause must go quiet and stay: 0 gone)
tick slowscan off
policy fourcorners
# events honest
elide on
grid off
decay 4
dwell 2 3
start
feed on
zoom auto
zoom shrink on
zoom status
seams
status
zhd
cycle repeat on
cycle start
cycle holdat RUN_EVENTS 100
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 150
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 200
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 250
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 300
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 350
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 400
cycle waitpause quiet 2
zoom specks
cycle start
cycle holdat RUN_EVENTS 450
cycle waitpause quiet 2
zoom specks
cycle start
wait 10
quit close
