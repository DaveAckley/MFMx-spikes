# EXPERIMENT: DReg + Res (physics dreg), ONE DReg placed on chip 0 hub at023, policy ewsnap10, shortcut (AERse), ~31 min
# The deciding run for OPEN_ISSUES 'CHIP-0 EWP ACKS LOST'.  Run
# 20261128-111403 (ewsnap10, seed landed on chip 0) froze at scy 50:
# every chip-0 EWP logged its RUN_EVENTS(50) bolt return but the host
# saw none.  Run 20261128-120208 (fourcorners, seed on chip 1) was
# clean for 173 cycles.  This run is 111403 again with the seed PLACED
# with the new `seed hub at023 1 9`: one bytecode DReg at an uncached
# site of chip 0's hub at023.  A freeze like 111403's makes ewsnap10
# (with activity on chip 0) the cause; a clean run points at chance or
# at something else that differed.
# Expect: `EWP: physics .../dreg.bcp, 208 bytes, checksum 0xa52d19fe`;
# `Seed@x,y` with 28 <= x < 109 and 28 <= y < 80 (at023's uncached
# interior); `.bv:1:0:0:2:1:a52d19fe.`; no BCPBAD, no `.pf`.
# IF IT FREEZES: note which chip's tiles are lamers, type `cycle retry`
# once (do acks arrive?), let the slowscan sweep run a minute or two,
# then `quit close` and send the run directory.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy ewsnap10
events shortcut
elide on
grid off
autoseed off
decay 9
dwell 4 8
physics dreg
start
feed on
zoom auto
zoom shrink on
zhd
cycle repeat on
cycle start
wait 30
echo ONE DReg (bytecode type 9) on chip 0 hub at023
seed hub at023 1 9
echo block 1 of 6 (5 min each)
wait 300
status
zhd
echo block 2 of 6 (5 min each)
wait 300
status
zhd
echo block 3 of 6 (5 min each)
wait 300
status
zhd
echo block 4 of 6 (5 min each)
wait 300
status
zhd
echo block 5 of 6 (5 min each)
wait 300
status
zhd
echo block 6 of 6 (5 min each)
wait 300
zhd
status
quit close
