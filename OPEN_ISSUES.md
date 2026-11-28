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
  did the task, EACH there on hb only.  Clean boots since: 9 (141156,
  151122, 154951, 163420, 182009, 200923, 204054, 224448,
  20261128-003440).  Move to
  CLOSED_ISSUES after a few dozen.  May also have been the 2026-11-21
  seam21 laggards.

## Bugs

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

- Loadable bytecode physics (Liv, 2026-11-28; name pending, "BCP" /
  `.bcp` proposed): pick a physics at start, like MFMs loading a .so.
  Decided: the host keeps loading a plain .bin (no ELF); no fixed-size
  section (find out at start whether it fits the L1 left); no HostBlock
  change; which images take a physics is a layout-config flag; the blob
  is appended AFTER the HostBlock (whose address is fixed at link time)
  and found at __hostblock_end rounded to 16.  The blob holds no
  pointers (offsets from its start), the host runs verify() before
  writing it to every EWP, the EWP checks magic / ISA version / size /
  checksum once at init, and the code cache binds to it.  Steps: rename
  Image and make it a pointer-free blob, still compiled in; then the
  start-time load.
- Cross-element calls in ulam-lite (the whole-physics image is built,
  1b19246; the compiler side is not).
- getm/setm with member pos/len resolved at compile time (~120 / ~160
  cycles net today).
- Code cache: packed 64-byte units if small functions waste whole
  256-byte slots (allocator only).  If deep call chains ever show `.bc`
  scans, have CALL save the caller's slot for RET.
