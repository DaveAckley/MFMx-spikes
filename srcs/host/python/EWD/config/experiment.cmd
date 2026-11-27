# EXPERIMENT: corner notch hunt with controls: FB1 fills the grid, notch detector on, elide on, shortcut (AERse), ~25 min
# Rerun of 20261127-154951 (1004 corner 'notches', mostly 4x4 and 8x8,
# probably FB1's own holes) now that the detector also tests hub edge
# midpoints and centers: runsum 'corner notches' gives notches per
# point by side for each kind.  A corner excess, mostly at sides
# 20-28, is the artifact; equal rates mean FB1 background.
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
