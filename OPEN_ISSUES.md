# OPEN ISSUES (what needs work now)

Short, punchy, current.  Rules (CLAUDE.md sec 2): fix something -> MOVE
its entry to CLOSED_ISSUES.md (numbers kept, story cut) in the SAME
commit; find something and do not fix it -> add it here before the turn
ends.  Anything parked on purpose, or an idea nobody is working on,
goes to DEFERRED_ISSUES.md instead (most of this file's old contents
moved there on 2026-11-28).  Design detail lives in BYTEPHYS.md,
GEOMETRY.md, HANDOFF.md and ERISC_NOTES.md, not here.  Older code
comments and .cmd headers cite sections of the old, long file ("OPEN_ISSUES
'physics as bytecode'", "'CORNER NOTCHES'", ...): find them in
DEFERRED_ISSUES.md, CLOSED_ISSUES.md (2026-11-28 block) or
`git show 954157f:OPEN_ISSUES.md`.

## Waiting on a QB run

- HOLD_AT_BIRTH laggard, init race fix (built 2026-11-27).  Cause: in
  HartTaskerPrivate::run a hart still walking an epoch's rows with its
  local copy hit the out-hart assert on a row naming a hart that had
  already gone out, before checking 'already done' (HartTasks.cpp:114,
  run 20261127-122216, hub at023, h0/h1 died, 139/140 acks, no events).
  Fix: test 'already done' first, assert only for an out hart that never
  did the task, EACH there on hb only.  Clean boots since: 13 (141156,
  151122, 154951, 163420, 182009, 200923, 204054, 224448,
  20261128-003440, -014339, -022755, -030540, -083650).  Move to
  CLOSED_ISSUES after a few dozen.  May also have been the 2026-11-21
  seam21 laggards.


## Bugs

- CHIP-0 EWP ACKS LOST (run 20261128-111403, ffa15bf: bytecode DReg +
  Res, `physics dreg`, policy ewsnap10, ONE seed, autoseed off).  DReg
  and Res ran and spread on chip 0 (the only chip with atoms).  At scy
  50 bolt RUN_EVENTS(50) timed out: chip 0's 35 HUBS acked, ALL 105 of
  its EWPs did not (the lamer list is exactly the EWP positions of the
  2x2 cells); chips 1-3 all acked.  The EWPs were not dead or stuck:
  each logged NEWACK:102 and BOLTR_2ACK:102.RUN_EVENTS 50 (so
  _returnBoltToHostNC ran), then kept running events (pewAt 700000,
  pewPF 0, `.bv:1:0:0:2:1:a52d19fe` on all 810 marks, no BCPBAD /
  `.pf`).  The host never saw those returns (onPhases 35/140/140/140
  for 400 s).  So the EWP -> host-RAM bolt write was lost, landed
  elsewhere, or was overwritten, for every chip-0 EWP at once.  Not
  known: whether bytecode DReg, ewsnap10, or chip 0 being the only busy
  chip matters.  Note initiateWriteToHost always returns 1 (no drop
  path in software), and the post-freeze EWP log lines came via
  slowscan (host reads), so they do not show the EWPs' writes to the
  host still worked.  Run 20261128-120208 (0a4d272, the same script
  under `policy fourcorners`): CLEAN, 173 cycles, 0 timeouts, `.bv` on
  all 5535 marks, no BCPBAD / `.pf`; the seed landed on chip 1
  (Seed@1424,84) and the DReg family reached 306 EWP tiles (so it
  crossed onto most chips).  So bytecode DReg alone does not do it,
  and ewsnap10 is the lead.  Next: ewsnap10 with the seed placed
  (`seed hub at023 1 9` puts it on chip 0; any other chip's hub tag
  for the other case), and on a freeze `cycle retry` before quitting.

- Single unexplained hang, fastframe10-12 (2026-11-25): at scy 3621,
  3.1 h in, the leader (bh2) went mostly silent; not investigated.  The
  `.sc` mark (scy at NEW_LEADER entry) is now in every run for next time.
- LOST SEED BATCHES (run 20261126-183231, not explained): in 3 rounds a
  whole `seed 3 N` batch never grew though the host said `seeded 3/3`
  (12 of 54 seeds, always a whole batch: a timing race, not a bad site).
  doSeed (EWControl.cpp) writes the atom straight into a hub's T6 grid
  in L1 at any moment of the cycle, so a SAVE/LOAD may overwrite it.
  The four-corners multi-borrower SAVE race (CLOSED 2026-11-27, corner
  notches) could have eaten seeds in corner squares; not rechecked since
  that fix.  To test: read seeded sites back a cycle or two later and
  note the cycle phase at the write.
- A FROZEN cycle is not an error to --abort-on-error: run 122216 kept
  waiting 30 min after the first NEW_LEADER timed out.  Ideas: end the
  run when the cycle freezes; a hub should ack a repeat of the command
  it already occupies; the stuck report printed no fail file/line for
  that wedge.

## Next work (needs Liv's go-ahead)

- Repo cleanup, four phases (planned 2026-11-28, not started, parked
  for CHIP-0 EWP ACKS LOST): the whole plan is DEFERRED_ISSUES 'REPO
  CLEANUP PLAN'.
- Loadable physics: choosing among several physics at start (Liv
  wants the option kept; one loads today, CLOSED_ISSUES 2026-11-28).
- Cross-element calls in ulam-lite (the whole-physics image is built,
  1b19246; the compiler side is not).
- getm/setm with member pos/len resolved at compile time (~120 / ~160
  cycles net today).
- Code cache: packed 64-byte units if small functions waste whole
  256-byte slots (allocator only).  If deep call chains ever show `.bc`
  scans, have CALL save the caller's slot for RET.
