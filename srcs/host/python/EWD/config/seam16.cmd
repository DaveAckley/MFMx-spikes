# seam14 as a STRESS soak: right after 'start' seed 25 FB1 atoms (type 4) at
# random sites, so many hubs are busy early instead of waiting for growth from
# the four autoseeding hubs per chip.  Busy hubs mean full ACBs sooner, which
# should surface stream breaks (and the PKTSUM / .zw / .zs diagnostics of the
# trailer investigation, OPEN_ISSUES.md) faster.  Atom tracking across the
# grid is meaningless here.  Normally run with MFMX_EVERY_EVENT 1 (Grid.h; the
# hubs log .ee:1) so empty centers cost a round trip like everything else.
# Everything else is seam14: two hours, 'zhd' every 10 minutes, no --rtmp:
#   python mfmxctl.py --layout seam4 --boot -s config/seam16.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   grep -h "ZHD" MFMX-<date>-<time>/misc/*ZHD.txt
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
zhd all
cycle stop
wait 15
stop
seams
quit close
