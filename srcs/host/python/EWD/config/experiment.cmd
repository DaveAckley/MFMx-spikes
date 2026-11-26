# EXPERIMENT: ewsnap10 first run (radius-10 tiles snapped to hubs), FB1 shortcut (AERse), seed 25 4, no nukes, dwell 4 8, 1 h
# First run of `policy ewsnap10` (CachePolicyId 3, EWLattice10): every
# hub owns whole radius-10 diamonds (221 sites) of the close-packed
# lattice (10,11),(-11,10), so hub boundaries are zig-zags with
# 21-site straight 45-degree runs instead of ewsnap's 9.  Events still
# use radius-4 windows; cache shift still 18.  This build has the
# T6Grid ring 28 for EVERY policy (193 x 164), so ewsnap/fourcorners
# numbers from it are not directly comparable with the ring-22 runs.
# Shortcut mode (AERse) and `seed 25 4` so the grid fills fast; NO
# autonuke, so colonies can meet the boundaries (type
# `autonuke on 1 100 100 1000` live for fun, `grid edges on` to see the
# zig-zags).  6 blocks of 10 min with `status` and `zhd`; quiet pause
# and `zoom specks` at 30 and 60 min; `zhd all` at the end.
# Expect: `.cp:3` and `.ct` on all 140 hubs, no BADC, 0 timeouts,
# 0 desynced streams, few or no turds.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy ewsnap10
events shortcut
elide on
grid off
decay 4
dwell 4 8
start
feed on
zoom auto
zoom shrink on
zoom status
zhd
cycle repeat on
cycle start
wait 30
seed 25 4
echo block 1 of 6 (10 min each)
wait 600
status
zhd
echo block 2 of 6 (10 min each)
wait 600
status
zhd
echo block 3 of 6 (10 min each)
wait 600
status
zhd
echo 30 min: quiet pause and zoom specks
cycle holdat RUN_EVENTS 0
cycle waitpause 120 quiet
zoom specks
zhd
cycle start
echo block 4 of 6 (10 min each)
wait 600
status
zhd
echo block 5 of 6 (10 min each)
wait 600
status
zhd
echo block 6 of 6 (10 min each)
wait 600
status
zhd
echo 60 min: quiet pause and zoom specks
cycle holdat RUN_EVENTS 0
cycle waitpause 120 quiet
zoom specks
zhd
zhd all
quit close
