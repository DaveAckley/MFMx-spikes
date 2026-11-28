# EXPERIMENT: DReg + Res in ulam-lite (physics dreg), one DReg seeded, policy FOURCORNERS, shortcut (AERse), ~20 min
# Same as run 20261128-111403 except `policy fourcorners` (was
# ewsnap10).  That run froze at scy 50: every chip-0 EWP logged its
# RUN_EVENTS(50) bolt return (BOLTR_2ACK:102) but the host saw none of
# them, while chip 0's hubs and chips 1-3 acked (OPEN_ISSUES 'CHIP-0
# EWP ACKS LOST').  This run asks whether ewsnap10 matters.
# physics/dreg.bcp: bytecode DReg (type 9) and Res (10), drawn as darker
# native DReg / Res; autoseed off and ONE `seed 1 9`, so everything
# descends from one atom on one chip.
# Expect: `EWP: physics .../dreg.bcp, 208 bytes, checksum 0xa52d19fe`;
# `.bv:1:0:0:2:1:a52d19fe.`; DReg and Res spreading; no BCPBAD, no
# `.pf`, 0 timeouts, 0 desynced.  IF IT FREEZES: type `cycle retry`
# once (do acks arrive?), let the slowscan sweep run a minute or two,
# then `quit close` and send the run directory.
# Always run as:
#   python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
# The first comment line above is the experiment's name in runsum.txt.
tick slowscan off
policy fourcorners
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
echo ONE DReg (bytecode type 9)
seed 1 9
echo block 1 of 4 (~4.5 min each)
wait 270
status
zhd
echo block 2 of 4
wait 270
status
zhd
echo block 3 of 4
wait 270
status
zhd
echo block 4 of 4
wait 270
zhd
status
quit close
