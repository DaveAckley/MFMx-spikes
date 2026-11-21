# seam13 (feed started late) as a TWO HOUR soak that checks the report streams
# for desync by itself.  Purpose: catch hubs whose compressed atom-report stream
# breaks (their tile freezes on screen while the hub runs; OPEN_ISSUES.md,
# "Display tiles freeze").  Run 124337 lost 4 of 140 hubs in 2.2 h on the old
# code; this needs the build with ZHostDecompressor's desync detection.
#   - the 'zhd' tick (every 10 s) says and logs (misc/*-ZHD.txt) the moment the
#     set of desynced hubs changes, with where each one broke;
#   - 'zhd' is also run after every 10 minute block, and 'zhd all' once at the
#     end (every hub's good/bad report and bad-atom counts);
#   - the FIRST-BAD detail (last decoded bytes, last 16 packets) is in the tile
#     log of each broken hub: grep ZHD MFMX-*/tiles/*.mfmk
# Same policy, seed, dwell and telemetry as seam12.  Delete 'feed on' to run
# without video; add it back if you want to watch the tiles freeze.  Run WITHOUT
# --rtmp:
#   python mfmxctl.py --layout seam4 --boot -s config/seam14.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 600
#   grep -h "ZHD" MFMX-<date>-<time>/misc/*ZHD.txt
# After a break, `dumpl1 TAG` on the hub right away (with the hub.map of this
# build) shows whether the hub itself is fine (scripts/l1dump/l1grid.py).
tick slowscan off
policy fourcorners
events honest
decay 4
dwell 4 8
start
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
