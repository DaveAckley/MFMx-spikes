# NUKE TEST, about 7 minutes, no feed: let 25 FB1 (type 4) spread for two
# minutes under four corners with `elide on` (kept caches), then three blasts in
# global site coordinates (1918 x 1080): (959,540) radius 300 sits exactly on the
# chip-seam corner of all four chips, (400,800) radius 250 is mostly inside one chip,
# (1500,250) radius 250 reaches the top edge.  Each blast rides the next NEW_LEADER bolt
# (words 2..4) and every hub erases the disc from its WHOLE grid, caches
# included, after SAVE and before LOAD.  Needs ./build_all.sh at this commit.
#   python mfmxctl.py --layout seam4 --boot -s config/nuke10.cmd --script-exit --abort-on-error
# Afterwards:
#   grep -h '\.nk:' MFMX-<date>-<time>/tiles/*.mfmk | sed 's/.*\.nk://; s/}//' | sort | uniq -c
#       lines SCY:SELF:CACHE (hex) from every hub the disc touched; per blast the
#       sum of SELF over hubs is the number of atoms erased
#   grep -h '\.an:' MFMX-<date>-<time>/tiles/*.mfmk | head     (census after each NEW_LEADER: the
#       first number is the hub's atom total, it should drop in the touched hubs)
#   the video / screen: a disc-shaped hole appears at the next cycle, no hub hangs
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
wait 120
status
nuke 959 540 300
wait 60
status
nuke 400 800 250
wait 60
status
nuke 1500 250 250
wait 60
status
cycle stop
wait 15
stop
status
quit close
