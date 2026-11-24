# HOST-DIRECTED REFRESH CHECK, short: the turdhunt setup, TWO quiet pauses
# (scy 150 and 250), `zoom specks refresh` at each: lists the specks as
# before (who, hub truth, OWNER, HIST, "=> TURD"), then asks the hubs behind
# every listed pixel to re-report it (in the cars the host returns), waits 3 s
# and looks again.  About 10 minutes.  Needs ./build_all.sh (hub + host).
#   python mfmxctl.py --layout seam4 --boot -s config/refreshcheck.cmd --script-exit --abort-on-error
# Afterwards:
#   grep -a 'refresh asked\|refresh totals' X.out
#   grep -a -c 'REPAIRED' X.out        (rows repainted empty by the refresh)
#   grep -a -c '=> TURD' X.out         (minus 1 for the build line)
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
cycle holdat RUN_EVENTS 150
cycle waitpause quiet 2
zoom specks refresh
cycle start
cycle holdat RUN_EVENTS 250
cycle waitpause quiet 2
zoom specks refresh
cycle start
wait 10
quit close
