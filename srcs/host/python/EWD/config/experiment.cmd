# EXPERIMENT: DReg + Res in ulam-lite (physics dreg), one DReg seeded, policy ewsnap10, shortcut (AERse), ~20 min
# The first physics written for loading: scripts/ulamlite/dreg (DReg
# and Res, the same steps and random draws as native types 2 and 3;
# bytephys_test checks it) compiled to physics/dreg.bcp, bytecode types
# 9 (DReg) and 10 (Res), drawn as darker native DReg red / Res colors.
# DReg makes Res with the new OP_NEW (`ew[n] = Res;`).  The host loads
# dreg.bcp after the EWP image in place of the compiled-in BFB physics
# (so types 7 and 8 are unknown this run and would be erased).
# autoseed off and ONE `seed 1 9`: every DReg and Res in the run
# descends from that one atom.  Policy ewsnap10 (radius-10 tiles), so
# DRegs meet zig-zag hub boundaries.
# Expect: the host's `EWP: physics .../dreg.bcp, 208 bytes, checksum
# 0xa52d19fe, 2 types from 9` line; `.bv:1:0:0:2:1:a52d19fe.` on every
# EWP after its first bytecode event (runsum 'BCP opens'); a red speck
# spreading into a DReg-sprinkled sea of Res; no BCPBAD, no `.pf`, no
# BADC, 0 timeouts, 0 desynced streams.  (No `.pt` numbers: DReg and
# Res are not in the timing slots.)
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
