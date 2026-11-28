# EXPERIMENT: corner notch fix check: FB1 fills the grid, notch detector with controls, trimmed four-corners bands, elide on, shortcut (AERse), ~25 min
# Same script as run 20261127-163420, on the hub build with the FIX:
# four-corners edge bands no longer carry the corner square at their far
# end, so no site has two borrowers, and elision keeps bands only on a
# repeated corner (CachingPolicy.h trimBand / bandRange / keepAcross).
# 163420 (before): notches per point corner 7.85, edge 3.16, center
# 1.63, and 50-55% of corner notches in the previous cycle's corner
# quadrant (chance 25%).  Expect corner close to edge/center and that
# correlation gone (runsum 'corner notches'; the correlation needs
# misc/mfmxctl-CYCLE.txt, Claude computes it).  Hub change: build_all.sh.
# Liv's CORNER NOTCHES (OPEN_ISSUES): square holes at hub corners, sharp
# to their borders, soon refilled, seen in FB1 tiles that had been full
# a long time.  `notch on` checks every frame for a hub-corner square
# (up to the 28-site cache ring) that empties while the other three
# squares at that corner stay full, and logs `[notch] NOTCH ...` with
# its size, scy, owner hub and a pixel for `who`
# (misc/mfmxctl-NOTCH.txt; runsum 'corner notches').  Read the per-point
# rates, the sides (28 = a whole corner cache) and the quadrants.  This is the
# `elide on` arm; an `elide off` twin will say whether the kept-band
# path is involved.  No nukes (they would make real holes), no hold
# (we want a count; `notch hold on` would freeze at the first one).
# fourcorners, events shortcut (AERse), no autonuke.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
events shortcut
elide on
grid off
decay 4
dwell 4 8
start
feed on
zoom auto
zoom shrink on
zhd
cycle repeat on
cycle start
wait 30
notch on
echo FB1: 20 seeds, then let it fill
seed 10 4
wait 20
seed 10 4
wait 300
zhd
notch
wait 300
zhd
notch
wait 300
zhd
notch
wait 300
zhd
notch
wait 240
zhd
notch
status
quit close
