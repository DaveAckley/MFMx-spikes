# seam16 (seed 25 4 stress) as a THREE HOUR soak: 18 x 10 minutes, 'zhd' after
# each block, 'zhd all' at the end.  For counting stream breaks per hour on the
# fixed build (ordered host car return, 4573687; .zl/.zs/DUPRET marks in).  No
# --rtmp.  Normally run with MFMX_EVERY_EVENT 1 (Grid.h; hubs log .ee:1).
#   python mfmxctl.py --layout seam4 --boot -s config/seam17.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   grep -h "ZHD" MFMX-<date>-<time>/misc/*ZHD.txt   (no file = no breaks)
#   grep -rh "DUPRET" MFMX-<date>-<time>
tick slowscan off
policy fourcorners
decay 4
dwell 4 8
start
seed 25 4
feed on
seams
status
zhd
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
