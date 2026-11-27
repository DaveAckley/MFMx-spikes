# OPEN ISSUES (bugs, tuning, unproven things)

One short list; newest first within a section.  Rules (see CLAUDE.md sec 2):
fix something -> MOVE its entry to CLOSED_ISSUES.md (numbers kept, story cut)
in the SAME commit; find something and do not fix it -> add it here before
the turn ends.  Keep this file a concentrated dose of what needs work.  Design history stays in HANDOFF.md and
ERISC_NOTES.md.

## Bugs

- AER 0.04-0.05 below the 11-19 soak (2026-11-25, PARKED by the user).  Honest
  FB1 AERre reads 0.94 flat now (dwell 4 8, no seeds; run 20261125-165051)
  against 0.987 at 180 s and 0.977 at 900 s in soak 124337 (same metric).
  Ruled out: BOGOMS, dwell (+0.03), seeding/fill, host cycle timing.  The hubs
  do ~5% fewer events per dwell second: something hub-side in the last week.
  Not bisected.  How, if it matters: CLAUDE.md "AER GAP, PARKED".
- Single unexplained hang, fastframe10-12 (2026-11-25): at scy 3621, 3.1 h
  in, the leader (bh2) went mostly silent; not investigated.  The `.sc` mark
  (scy at NEW_LEADER entry) is now in every run to help next time.

- EWP/hub car handshake lost at boot (2026-11-21, seam21 at OW=20; fix
  PROVEN, MECHANISM NOT ESTABLISHED).  68 of 140 hubs ran ZERO events (AER
  0.50 vs 0.96) and one hub died at init (at14b, getAnkleOrDie).  Evidence from
  the dumps: idle hub at023 has its EwpBlock cars in their initial state (nonce
  2, state 4), its EWP neighbours' cars show nonce 4 INBOUND_DEPARTED (they
  shipped their first cars and then waited); the at14b core showed EWP-style
  returned cars sitting in a hub whose init had not finished.  Fix (5afdae3):
  ewp/EwpBlock.cpp no longer activates its endpoint in HTOC_INIT; it activates
  in LIVE at the first RUN_EVENTS.  PROVEN by seam21-12 (MFMX-20261121-161919,
  one hour): all 140 hubs ran events (min 51.27M, max 51.87M, spread 0.2%),
  AER 0.970, no gAOD lines, clean boot.
  Roles: the hub is the "in" end (isIn=true) and the EWP the "out" end, fixed
  by role, NOT by TLBI (unlike InterHub.cpp:271), so the fix does not depend on
  where the hub sits in the 2x2 cell.  InterHub endpoints are not exposed to
  this: HTOC_LIVE (and so updateOps) cannot run on a tile until it leaves the
  HOLD_AT_BIRTH block in stepNC, and NEW_LEADER is fired only after every tile
  acked that, i.e. after every tile's INIT and OPEN.
  OLD THEORY (not established, do not rely on it): the EWP shipped its cars
  into the hub's storage before the hub's `theEwpL1Data.reset()` (hub
  EwpBlock.cpp HTOC_INIT) and the reset wiped them.  This does not fit a boot
  where every tile acked HOLD_AT_BIRTH, since the hold keeps LIVE from running
  until all INITs are done; seam21-11 was such a boot and still had idle hubs.
  What is known: EWP activity before RUN_EVENTS loses the handshake (hub cars
  untouched, EWP cars shipped); activating at RUN_EVENTS does not.  Not yet
  traced: what happens to hub-side car storage during the NEW_LEADER phase
  (hub updateOps runs only in RUN_EVENTS).  To pin down: old activation plus a
  hub `dumpl1` right after the first NEW_LEADER.
  Note: dumps of a run need the matching hub.map; b1716a0 moved data by 0x340
  vs d3da41d (find theT6Grid by scanning for 100% atom parity and empty
  permanent rings).

- Duplicate HOLD_AT_BIRTH wedges every hub (2026-11-21, seam21 at OW=20): the
  host's start() waited only 5 s for all hubs to ack the pre-start
  HOLD_AT_BIRTH; one bh2 hub was late (139/140), so the cycle re-fired
  HOLD_AT_BIRTH and every hub that already held it hit
  `MFM_API_ASSERT(ecmd != nextCmd,ILLEGAL_STATE)` in
  `PhaseStepper::_switchToExiting` (fidl 88:99) and wedged (NC hart in BHANG,
  other harts in acquireLockASM).  Mitigation: start() now waits up to 60 s
  and logs `HOLD_AT_BIRTH ack wait`.  The 60 s wait did NOT cure it (seam21 retry, onPhases 0/0/0/0
  after the freeze), so start() no longer re-fires at all: a partial ack count
  now just skips HOLD_AT_BIRTH and the first NEW_LEADER names the laggards.
  STILL OPEN: why some tiles never ack at OW=20.  A hub should also tolerate
  (ack) a repeat of the command it already occupies.  Also the stuck report printed no fail file/line (no `STUCK?`
  lines) for this wedge.

- seamtest consensus stalls (2026-11-22): CAUSE FOUND AND CONFIRMED, a HOST-side measurement artifact, not a
  device or seam stall.  seamcycle13 (seamtest, cards 0+1, E/W) and seamcycle1to3-10 (seam13, cards 1+3, N/S, build
  abaf091): 5-8 of 22-26 bolts (NEW_LEADER and RUN_EVENTS alike, dwell unaffected) took 1.7-2.0 s instead of 0.1 s,
  never 0.5 or 1 s; seam counters clean (out = in = noc, txfail/nocfail 0), no TIMEOUT.  mfmxctl is ONE Python
  thread (a select() loop, mfmxctl.py) that runs the sequencer's bolt poll, the frame tick and the periodic
  slowscan (default every 2.0 s).  `Machine.slowscan()` sleeps `slowscan_gap` (0.2 s) per chip and then reads
  every hub's hostblock, all on that thread, so a bolt that the tiles finish in about 0.1 s is only SEEN as done
  after the scan returns.  seam21.cmd (and so the 4-card runs, which show no phase over 1 s) has
  `tick slowscan off`; seamcycle.cmd did not.  Old seamtest runs (2026-11-17) took 1.8-4.1 s for the same
  reason (4.0 = about two scans).  So the earlier "fewer than four cards" hypothesis is not needed.
  Fix applied to the four seamcycle*.cmd scripts: `tick slowscan off`.  CONFIRMED by seamcycle1to3-11
  (MFMX-20261122-014708, 228d129, same layout seam13, scan off): 28 bolts, 26 at 0.1 s, one 0.0, one 0.2, none
  over 0.2 s (before: 5 of 26 at 1.8-1.9 s); seam counters clean (5590 cars, out = in = noc, txfail/nocfail 0),
  seed type 4 (FB1) as intended.  Not rerun on seamtest/seamns/seam23.  Consequences: (1) the 0.1-0.2 s consensus times in all
  runs include the host poll granularity, so the device-side consensus may be well under 0.1 s (the user's
  "speed of light" numbers are an upper bound); (2) a bolt poll inside slowscan() between chips, or skipping the
  scan while a bolt is pending, would stop scans from delaying consensus when scans are on; (3) the video feed
  gap seen in seamcycle1to3-10 (FEEDSTAT maxgap 36 s, first real frames) is a separate thing, not looked into.
  CONSENSUS TIME RESOLUTION (2026-11-22, option A BUILT, UNPROVEN on hardware): the 0.1-0.2 s consensus times were
  only as fine as the host tick that noticed them: the sequencer polls from the `cycle` tick (0.1 s) and the
  `bolts` tick was 0.2 s, and Bolt.describe printed one decimal, so every fast bolt read 0.1 s.  The C++ transport
  thread refreshes the ack count every 256 loop passes (Blackhole.cpp), probably well under 1 ms (not measured).
  Now: both ticks 5 ms (mfmxctl.py), BOLT/CYCLE times print 3 decimals (machine.py, sequencer.py).  Expect about
  5 ms resolution; the number is still HOST-OBSERVED consensus (bolt write to every tile + tile loop + ack back),
  not a pure device time.  Tile-side timestamps in the PHASER return block (image change, per-chip clocks) and a
  spin poll after each fire (blocks the thread, can drop video frames) were not done.  To check: a seam21 run, then
  the consensus distribution in misc/mfmxctl-BOLT.txt (xchg.py and the analysis scripts accept 3 decimals).

- Video stream lockup at bring-up: REOPENED 2026-11-19.  Firefox (mediamtx HLS
  page on vidsrv:8888) froze again at "NO NEW DATA 46s" during bring-up of a
  seam6 run on c18fc98; no ffplay was connected that time.  Earlier: freeze at
  27 s, fixed-looking after 3516d21 (keepalive pacing, idle dup 133 -> 150 per
  30 s) and about three clean viewings (Firefox + ffplay) that then looked
  like a fix.  So the pacing was at best a contributing factor.  A page
  refresh recovers it, so frames are still being produced.  FEEDSTAT of that
  run (005507): the feed was HEALTHY through bring-up (idle dup 150-151 per
  30 s, maxgap 0.00 s, maxwrite 0.02-0.06 s), then real 153 per 30 s: the
  freeze is NOT the feed pacing; suspect the player (hls.js) or mediamtx.
  MORE DATA 2026-11-19 (width137 builds): three more freezes, all at the NO
  DATA banner with the seconds counter stuck at 13, 13 and 14 s, two of them
  during bring-up (runs 113217 pre-start, honest11, seam12) and one right after
  fullscreening the video (the fullscreen and the closed JS console were
  probably coincidence); Esc did nothing, a page refresh fixed each.  All
  five known freezes (13, 13, 14, 27, 46 s) are in the NO DATA gap at
  bring-up, so the player (hls.js) seems not to resume after a gap in frames.
  Not yet checked: whether a long gap is created host-side at bring-up.
  Next: the mediamtx log around
  the freeze (`journalctl -u mediamtx --since .. --until ..`, mask pass=),
  and run ffplay AND Firefox together from the start: if only Firefox stalls
  it is the player (hls.js) or its buffer, not the feed.
- HANG (seen once, run 125641 at PACKET_SLEEP_CYCLES=100,000; 1 hang in 4
  later and earlier long runs): cycle 250, bolt #500 RUN_EVENTS TIMEOUT.  10
  of 140 hubs on bh1 never acked: a cross of column x=12 and row y=9 around
  at1c9 (TLBI 23,51,79,99,101,103,105,107,109,135).  NEW_LEADER #499 had
  passed in 0.1 s; cars out==in==noc, no link trouble.  Lead (not proof, the
  mark streams are lossy): at1c9 has no `.xb:1` and its neighbours stop right
  at LOAD start, so a stall at at1c9 spreading outward.  Suspect a latent
  packet-exchange race that the old long busy-waits hid.  Not diagnosed.
  Clean since: 100k hour (165950) and sleep 0 hour (211051, now the default).
  In place for the next occurrence (all unproven on a real hang):
    * `.xw` heartbeat (LOGPTAG and HBPTAG, about 1 s while an exchange phase
      is not ready: need mask, leader mask, car ring lengths); xchg.py prints
      it as STALLS (reads tiles/*.mfmk only; HB marks land in all.mfmk).
    * a bolt TIMEOUT turns the slowscan tick on (sweep of every tile, about
      13 min per chip pass; output via Eprintf goes to all.mfmk, not .out).
    * HostBlock::packString drops the OLDEST words when the HB ring is full,
      so the latest marks survive (racy against slowscan's read, accepted).
  On a recurrence: find which hub waits on whom from the `.xw` marks, then
  read the slowscan sweep in all.mfmk.
- mfmxctl argument abbreviation: DONE (see git log).  Keyword arguments now resolve like
  commands (unique prefix, case blind, ambiguous/unknown refused): quit close, grid on|off,
  bolt diagnose on|off, cycle leader random / birth done|pending, zhd all|restart|break and
  restart on|off, tlbi lamers, seed [COUNT [TYPE]], nuke big, shoot BOLTNAME.  `step NAME` stays EXACT on
  purpose (setStep).  Tests: mock/selftest.py scenario abbrev_args.
- mfmxctl segfault at exit after `quit` WITHOUT close (2026-11-20, seam14-14 stopped
  at 3684 s by typing `q c`; before b71c52d `q c` did not mean `quit close`, so
  chips were left HAS_T6_CODE_RUNNING).  Shutdown printed "quietbox shutdown: ok"
  then "Segmentation fault (core dumped)".  No core file was found (apport
  pipes cores and usually skips unpackaged executables; ulimit -c may be 0).
  Unconfirmed guess: the no-close exit path, with code still deployed while the
  process tears down; not from the new hub instrumentation (host unchanged since
  5794297).  Not investigated: `quit close` exits clean.  To chase it: run
  mfmxctl with `python3 -X faulthandler` (or PYTHONFAULTHANDLER=1) and quit with
  plain `q`; compare with `q c`.  Low priority.

## Unproven on hardware

- Zoom slows host-observed consensus from about 0.06 s to 0.4-0.7 s (2026-11-23, built a mitigation, NOT yet run): proven by toggling `zoom off`/`zoom auto` mid-run.  Mitigation: poll the bolt between zoom stages, skip the box scan while a bolt is pending, per-stage timers in `zoom status` (CLAUDE.md).  To check: with zoom auto, consensus back near 0.1 s?  If not, the `cost:` line shows which stage (scan, step, render, whole frame) is long; render is one call and cannot be split without work (bands, or a worker thread).  The numpy fallback is used when cv2 is missing: the cost line says so.

- Turd ghost pixels after nukes, cause unproven (2026-11-23).  Clue from the
  user: no turds with FB4 (never empties an atom), only with FB1 (dies back).
  Suspected race: a borrower's late report of a cache-ring site beats the
  owner's newer empty report to the host.  Built REPORT EPOCHS (epoch marker
  records in the stream, per-pixel newest-epoch map on the host, stale reports
  dropped and counted in `zhd`; CLAUDE.md).  NOT yet run.  Check: `zhd` stale
  count > 0 and turds gone; if turds persist with stale ~ 0 the race is not
  the cause.  Idle refresh (20/frame) stays as the backstop.  Also unproven:
  `applyNuke` skips sites already empty in the hub grid (Grid.cpp), so a nuke
  cannot repaint a stale host pixel over one; a force re-report of the whole
  blast would (not built).
  UPDATE (user run of d74d44d, after nuke big): turds still exist; `zhd` showed 45
  stale reports ignored, so the race is real but rare and is not the whole story.
  Built `who X Y` (CLAUDE.md) to name the last painter of a turd pixel.
  FOUND (2026-11-23, user run of f66a298 / a1a5d89, refresh off): `zoom specks` now
  checks the hub's own grid: 8/8 specks were EMPTY in the hub, host last saw them
  non-empty 2-6 epochs earlier, no accepted report since.  SUSPECT (not proven):
  EWTs in flight at the RUN_EVENTS exit.  The EWP only works in RUN_EVENTS (ewp
  EwpBlock.cpp), so they return in a LATER epoch, and applyEWT applied any return
  whose old atoms still matched, in any phase, even on sites now lent to a leader;
  the hub reports the atom, then the leader's SAVE quietly overwrites the lent
  sites with its stale cache copy (lost update, host shows the atom).  workOnExitingRunEventsB
  has `if (true /* DONE DRAINING EWPS? */)`: nothing waits.  BUILT: EWTs carry
  their dispatch epoch (mHiddenYPos high 16 bits) and are dropped as STALE if it is
  not the current report epoch; books RETURNED = COMMITTED + OBSOLETED + STALE +
  UNAPPLIED, OUTSTANDING = OFFERED - RETURNED, marks .eo (exit) and .en (nuke).
  RESULT (run MFMX-20261123-164351, 29b8f89): stamp ran (all 140 hubs have .eo), STALE 516 in total (about 6 per hub per epoch, all dropped, UNAPPLIED 0, books balance), yet turds persisted: 5 specks, all on hub at0a9, which had just erased 6057 self + 2211 cache sites (.nk) in one nuke, 1-2 cycles before the pause; 2 definite (SELF, HUB: EMPTY, last report e19/e22), 3 RING (borrower's copy empty; ambiguous).  So stale EWTs are real but NOT the turd cause.  NEXT SUSPECT: report backlog after a big nuke (thousands of queued reports per hub not yet delivered); zoom specks now rescans and marks GONE/STILL to tell.
  TEST: turds gone and .eo stale > 0.  LATER: wait for OUTSTANDING == 0 (with EWs
  suspended, which now counts as returned-unapplied) before leaving RUN_EVENTS.

  UPDATE 2026-11-24: nuke not needed (control run, no nukes, still has HUB:
  EMPTY STILL specks); hub drains its DL fully after a nuke (.nl/.dl) and the
  host loses nothing (0 desync, 0 stale); every definite SELF turd lies in the
  20-site overlap band.  Suspect: quiet SAVE unload.  Built: unloadPacket
  pushes DL for changed SELF sites (CLAUDE.md SELF UNLOADS REPORT).  Next if
  turds persist: host-directed refresh of pixels not reported for N epochs.
  RESOLVED IN PRACTICE (2026-11-24; turdhunt10-10..13, quiet pauses with `zoom specks`):
  the turd rate went from about 3% of listed specks (8 of 249) to 0-1 per run
  (turdhunt10-11: 1 in 281, -12: 0 in 275, -13: 0 in 245, with a very large nuke in
  the middle of -13 that left a clean hole).  What changed, in order: SELF unloads
  report; a TURD verdict that knows cache elision (an owner's SELF is stale by design
  for a kept band, so "ring atom + owner empty" is not a turd); the EPOCH-OF-CHANGE
  STAMP (reports carry the epoch their site last CHANGED, so a borrower's late report
  loses to the owner's newer empty one); the idle close and idle pad (a partial car
  ships when the change list is dry; epoch-marker padding pushes the LZ encoder's held
  tail out); host-directed refresh (`zoom specks refresh`, hub queues the site).  We
  cannot say which of the last three did it, three runs is a small sample.
  STILL UNEXPLAINED: why a ring site sometimes went empty with no report at all (the
  lone `R4` rows of turdhunt10-10); not seen since.  Host refresh still answers a batch
  late inside a quiet pause (needs a periodic hub heartbeat car; not built).  The random
  idle refresh is retired.  See CLAUDE.md (EPOCH-OF-CHANGE STAMP, HOST-DIRECTED REFRESH,
  ACB IDLE CLOSE / PAD).
- Live AER vs aer.py: run 012507 (14f6dfd) live final 4.65, 33.7, 84.9 vs
  aer.py last 60 s 14.9, last 300 s 50.1, total 97.7 (104 hubs): the live
  figure was trimmed (read low while hubs differ); now a plain mean.  Compare
  again with misc/mfmxctl-LIVEAER.txt on the next run (aer.py's total starts
  at the autoseed; the live line's t counts from mfmxctl start).

- Atom CONTENTS match across seams (content-hash slice; user: pass on it
  indefinitely while things look good).
- pybind `portStatus`/`releaseIdlePorts`, `--abort-on-error` and typed `wait`
  beyond one clean run.

- T6 -> ETH write over NoC1: PROVEN 2026-11-21 (expts16, out16, ethfwd22).  expts15 had failed only because it
  addressed NoC0 (6,1), which on NoC1 is E13, not our E1 (E10) (it wrote one 64 B car into E13's L1; link
  counters unaffected).  Rows Y=0/1 are not translated (NIU cfg 0x6005 has translation ON for Tensix tiles);
  there X is the raw coordinate of the NoC in use and NoC1 mirrors it (x1 = 16 - x0, Y unchanged;
  tt-isa-documentation BlackholeA0/NoC/Coordinates.md).  With the node sent as (10,1) every step passed with
  0 BAD payloads, 0 noc-fail, 0 txfail: probe, sizes 64/576/4160 x 2 ch (about 1.80 / 1.16 / 0.30 M round
  trips/s, same as the NoC0 control), E1 replying over NoC0 and NoC1, tile rows y=2/6/10, and a 60 s soak
  (69.5 M round trips, 8.7 M payload checks).  So all four T6<->ETH legs now work on both NoCs.
  NRI3::initiateWriteToEthL1 now uses preferNoC with the mirrored node (40864a7): DEMONSTRATED (expts16) and
  OBSERVED in MFMx: seamcycle13 (seamtest layout, 2750 cars, txfail/nocfail 0) and seam21-13 (full QB, one hour:
  432,184 cars out = in = written to T6s on every print, txfail/nocfail 0, AER 0.968, all 140 hubs running, no
  timeouts; NEW_LEADER consensus mean 0.161 s vs 0.163 s before the change).  Not measured: xchg time per phase.

## NRI3::blockingL1ReadNoC0 return buffer alignment may be mod 64, not mod 16 (2026-11-22, RESOLVED: ARC-only; T6 L1 reads need only mod 16)

srcs/cross/src/NRIUtils.cpp matches the return buffer to the source address modulo 16 (the Wormhole rule, assumed).  On the card, NoC reads of the ARC tile
land at (return address rounded down to 64) + (source address mod 64) (ERISC_NOTES 9.19, t6arc3 sweep), so the buffer must match modulo 64.  Reads that
MFMx does today (image block headers and addresses of neighbouring tiles) work, probably because the sources are 64-aligned or the shift happens to be
tolerated; nothing shows it for T6 L1 sources.  RESOLVED on card 0 (t6l1rd, ERISC_NOTES 9.21): all 240 reads right, 192/192 with the NRI3 rule incl. the hub's exact case; T6 L1 reads land at the return address, so blockingL1ReadNoC0 needs no change.  The mod-64 rule is specific to the ARC tile (ArcTelemetry.cpp already uses a 64-aligned buffer).  Original to-do (not needed now):  Original to-do: a t6arc-style test reading a T6's L1 at sources 0..63 mod 64 from another tile, then fix
blockingL1ReadNoC0 (buffer = base + (src & 63), 64-aligned base, larger buffer) if it shows the same shift, and check the writers' rule (initiateWriteTo*
use src%16 == dst%16; they work).

## Design idea: diamond-shaped hub regions (2026-11-25, EXPLORING, host lab only)

liv: diamond windows bouncing against orthogonal hub lines is inelegant
where it is not necessary (only the HD edges need it); also EW close
packing.  Lab built: scripts/geomlab/geomlab.py, results and reading in
GEOMETRY.md sec 10 (diamonds cut the dead area 11.7% -> 9-10%, rings
0.69 -> 0.5-0.56; no diamond packs EWs exactly, only EW-lattice blocks;
brick rows / EW blocks do not fit the physical hub array, clumps of
small diamonds do).  CHOSEN (liv, 2026-11-25): rect-snap, random
centers, cache shift = two EW layers (SHIFT 18); work on branch
diagonal-tiles.  Step 1 DONE: srcs/shared/include/EWLattice.h + native
ewlattice_test (all checks, 3 mutations caught).  Step 2 DONE: geomlab
`ewlattice` layout + `--crosscheck` (header == lab model, 0 sites
differ; GEOMETRY.md sec 10).  Step 3 BUILT, NOT RUN: `policy ewsnap`
(CP_EWSNAP = 2): ring 22 for all policies on the branch, center tables
(.ct mark), CacheLoader band walks with ordinal check, ownership by EW
center, elision only on a repeated corner.  Step 4 RUN (2026-11-26,
GEOMETRY.md sec 10): ewsnap clean (0 timeouts, 0 turds), AERre 0.919 vs
fourcorners 0.922 on the same build; 0.936 on the older ring-20 build
(so the build, not ewsnap, costs most of that).  OPEN: ewsnap drops
~1.6x as many stale reports (1.70M vs 1.06M in 900 s), harmless so far
(3 h autonuke soak 005008: clean, 0 timeouts, 0 desynced, 0 turds, FB1
extinct at ~65 min; stale 1.65% of reports, even over hubs, sd 0.23%);
BIG TILES (2026-11-26, RUN 093329): `policy ewsnap10` = radius-10
tiles (EWLattice10), ring 28 for all policies on the build.  Run
093329 (1 h, shortcut, seed 25 4, later FB4 seeds, cycle repeat off,
6 nukes by hand): 0 timeouts, 0 desynced, 0 turds, `.cp:3` and `.ct`
on all 140 hubs, packed-grid AERse ~1.38 FB1, ~0.66 after FB4 took
over.  OPEN: live AER fell from [140] to [135] hubs right after the
30 min quiet pause (LIVEAER 1943 s -> 2004 s) and stayed there, while
bolts kept full consensus: 5 hubs stopped delivering `.ev` marks (hn
log path?).  Not seen in the ewsnap runs.  Needs the tiles: on the QB,
count `.ev:` per tiles/*.mfmk and look at the last lines of the low
ones;
why AER fell ~1.5% between builds 0d56df5 and 48f3a20 (ring 22?) is not
chased (cf. the parked AER gap).  (Old plan text:) step 3, hub code behind a compile
flag (CacheLoader walks, center tables, bigger T6Grid); step 4, QB.
Earlier plan: liv picks candidates; then a hub-side
sibling of T6Grid (packed rows, orthogonal coords) behind a compile flag.

## Ideas: EWP log delivery; host-side atom type census (2026-11-26, not started)

- EWP logging: works since the log-car fix (CLOSED_ISSUES 2026-11-26).
- Host census (Liv): the host counts atom types in what it displays
  (the 1920x1080 site image it already has) and logs the census
  somewhere, e.g. per minute in misc/ and in runsum.txt; would answer
  'did 5 eradicate 8' directly.  Maybe expand the 'logo block' (the
  stream overlay text) to show some of it.

## Design idea: physics as bytecode with explicit, bounded stacks (2026-11-26, SPIKE RUN and timed)

Reference manual: BYTEPHYS.md (instruction set, faults, assembler,
results, wish list).

SPIKE (2026-11-26, built, cross-built, host-tested; NOT run on the QB):
srcs/shared/include/BytePhys.h is a tiny switch-dispatch stack machine
(23 opcodes; u32 operand stack 16 deep and u16 call stack 8 deep in the
Machine, 84 bytes, living in fB beside mFastEW; every push, pop, call,
jump, site and field checked; per-event fuel 10,000; a fault FAILs into
the physics RecoverPoint, one FAIL line per kind so `.pw` names it).
scripts/bytephys/bpasm.py assembles fb1.bp / fb4.bp into the generated
BytePrograms.h (opcode numbers parsed from BytePhys.h; `--check` in
run_native_tests.sh).  Native FB1/FB4 moved unchanged into
srcs/shared/include/NativeFB.h so the host test runs the same code:
srcs/host/tools/bytephys_test.cpp shows bytecode == native, window and
random draws, on 18,000 windows under 3 random streams (two mutations
caught), and every machine fault is caught.  EWP: types 7 (FB1) and 8
(FB4) are the bytecode versions, colored like FB1/FB4 on the host; every
EWP times every event of types 4, 5, 7, 8 (tile wall clock, AI clock
cycles, around guardedPhysics) and logs `.pt:TYPE:EVENTS:TICKS:INSNS.`
(hex) every 4096 events of a type (65,536 in the first run; bytecode types are drawn as a
darker copy of the native color); runsum.txt's "physics timing"
section sums them over all tiles and prints cycles/event and the
bytecode/native ratio.  Sizes: FB1 129 bytes, FB4 29 bytes of bytecode;
instructions/event about 112 (FB1, mean over the test's mix) and 369
(FB4); interpreter 1.3 KB of code; fB 708 B, so hart B's stack shrinks
to ~7.45 KB.  First run: experiment.cmd (native and bytecode side by
side, FB1 then FB4 phase, 20 min).
FIRST RUN 20261126-122857 (a95e377, 21 min): ran clean (140 hubs, 0
bolt timeouts, consensus 0.12 s, 0 desynced streams); AERre 0.92-0.94 in
the FB1 phase (types 4+7), 0.654 once FB4 (5+8) filled the grid; NO
`.pt` marks at all (the log-car bug, see 'Log car dies after 65.5 s of
silence' under Bugs).
SECOND RUN 20261126-141643 (6058a42, `.pt` every 4096 events, `scan
140` sweeps after each phase; whole run dir in CLAUDES_PLACE): RESULT,
from the sweeps' HostBlock ring dumps in all.mfmk (all 105 EWP
positions per chip; the log path again carried only 3 EWPs), AI clock
cycles per event (800 MHz), mean over all marks:
  type 4 FB1 native      488
  type 7 FB1 bytecode   2163   28.6 insns/event   4.4x native
  type 5 FB4 native     3313
  type 8 FB4 bytecode  21879  369.0 insns/event   6.6x native
So the interpreter costs about 50-60 cycles per bytecode instruction
(FB4: (21879-3313)/369 = 50), i.e. it is slow per instruction; the
program and the switch's jump table sit in L1, not fast RAM, which
probably costs a load or two per instruction (unmeasured).  Native FB4
is also slow: ~83 cycles per 12-byte atom copy.  No `.pf` (no machine
faults) in any log or ring dump.  5 vs 8: in the FB4-phase ring dumps
type 8 had 151M events to type 5's 89M, so bytecode FB4 held more of
the grid at the end despite being 6.6x slower per event (consistent with
a neutral voter model plus chance).  The sweeps cost ~10 min each (PC
snapshots + 15 perl runs per tile) and blocked mfmxctl.
THIRD RUN 20261126-163422 (a941d4c, log-car fix, no sweeps): same
numbers through the normal log path, now from 560 tile files: FB1
native 501 / bytecode 2260 cycles/event (4.51x, 29.9 insns), FB4 native
3320 / bytecode 21886 (6.59x, 369 insns).  Events per type over the
run: type 8 505M on 375 tiles vs type 5 191M on 189 tiles, so again
bytecode FB4 ended up holding more of the grid (2 runs of 2).
FOURTH RUN 20261126-165929 (5b4e795, halo overlay): same timing
(4.51x, 6.59x); type 8 457M events on 357 tiles vs type 5 254M on 249:
8 ahead for the 3rd run of 3 (1 in 8 if it is a fair coin).  Worth a
look if it keeps up: something may favor the SLOWER type (e.g. a late
returning window winning an overwrite race), the opposite of the
obsolescence penalty expected.  A host type census would show it.
ROUNDS RUN 20261126-183231 (f85e9c5, 9 rounds of whole-grid nuke, 60 s,
seed 3 5 + 3 8, ~965 s each, 2.42 h): 6 rounds were real contests and
type 5 won 4 of them (final share 8/(5+8) about 0.46, 0.42, 0.38, 0.17),
type 8 won 1 (0.70) and 1 was even (0.51).  So the earlier 3 of 3 for
type 8 was chance plus seed placement; no sign that the slower type is
favored (if anything the reverse, as expected).  Total events stay
~424M per 300 s whether the grid is all 5 or mixed (the hubs, not the
EWPs, set the pace), so the events share is a fair proxy for the share
of the grid.  Timing unchanged (6.59x).  No type 4/7 (FB1) events ever,
no `.pf` in the 2 MB mark tail.  Log-car fix soak: tile logs and the
live AER line lasted the whole 2.4 h.
VERSION 1, DECLARED LAYOUTS (2026-11-27, host-tested, not yet on the QB):
ulam-style `quark`/`element` declarations in .bp, getm/setm through a
generated Layout checked against the site's type, saturating add/sub/
setm; FB1/FB4 rewritten on quark Aging.  BYTEPHYS.md sections 5, 12.
Next QB run should re-time 7/8 (getm costs a table scan + 64-bit
shifts per access) and show no `.pf`.
VERSION 1 TIMED (run 20261127-011502, 866a617, hand-written getm/setm
programs, events honest): FB1 bytecode 2955 cycles/event vs native 529
(5.59x; version 0 was 2260, 4.51x), FB4 23514 vs 3311 (7.10x; was
21886, 6.59x); instruction counts unchanged (29.9, 369).  No `.pf`, no
lost seed batches (8 of 8 grew).  FB4 5 vs 8 stayed even (share
0.46-0.52).  Cause of the slowdown not separated: per-access getm/setm
cost vs a slower interpreter loop; a single-word fast path in
getBits/setBits (no u64) is the cheap first fix.
COMPILED CODE TIMED (run 20261127-015533, 35f19e5, events shortcut):
FB1 bytecode 3167 cycles/event vs native 480 (6.60x, 30.7 insns), FB4
33853 vs 3314 (10.22x, 494 insns).  Interpretation cost per bytecode
instruction (bytecode - native, / insns), FB4: v0 50, v1 55, v2 62
cycles; FB1: 59, 83, 88.  The one-word fast path did not help: cost per
instruction grows with the interpreter, not with member access.  No
`.pf`; 8 of 8 seed batches grew; FB4 share 0.57-0.59 (bytecode ahead).
Compiler loop optimization DONE 2026-11-27 (FB4 494 -> 374 insns,
host-tested).  Still open: a per-opcode microbenchmark on the QB to
find where ~60 cycles per instruction go.
LOOP OPTIMIZATION TIMED (run 20261127-094705, 7e10820, events
shortcut): FB4 bytecode 24788 cycles/event vs native 3266 (7.59x, 374
insns; was 33853, 10.22x, so 27% fewer cycles for 24% fewer insns),
FB1 3013 vs 473 (6.37x, 30.0 insns; was 3167).  Per instruction, FB4
58 cycles (was 62), FB1 85 (was 88): stack ops (dup/over) are a bit
cheaper than loadl/storel.  Still ~13% slower than hand-written v0
(21886).  No `.pf`; 8 of 8 seed batches grew; FB4 share 0.32-0.36
(native ahead this time).
MICROBENCHMARK READY (2026-11-27, host-tested): Bench atoms, type 31,
time each opcode from L1 and from fast RAM (BYTEPHYS.md section 10);
experiment.cmd runs it.
VERSION 2 + ULAM-LITE (2026-11-27, host-tested): frames and calls in the
machine; FB1/FB4 now compiled from scripts/ulamlite/*.ulam (BYTEPHYS.md
section 13).  Compiled FB4 runs 494 instructions per event vs 369 hand
written.  Next: test-only .ulam programs that exercise the untested
compiler features in bytephys_test; then other atoms' members.
LOST SEED BATCHES (same run, NOT explained): in 3 rounds whole `seed 3 N`
batches never grew although the host said `seeded 3/3`: round 4 type 8,
round 5 both types (empty grid all round: no events 4200-4800 s, AER
0.939 at 4368 s), round 6 type 8.  12 of 54 seeds, always a whole
batch, so it looks like a timing race rather than a bad site per seed:
doSeed (EWControl.cpp) writes the atom straight into a T6 grid in L1
(QuietBox::storeP4Atom) at any moment of the cycle; something (a cache
SAVE/LOAD, an elided band kept by a borrower, ...) may overwrite it.
To test: read the seeded sites back a cycle or two after seeding, and
note the cycle phase at the write.  Liv's 'corner gaps' (square notches
at tile corners on the FB4 front, gaps.png 2026-11-26) may be the same
stale-copy business.


Liv's idea for complex, sparse, heavy physics on the EWPs.  The limit is
hart B's 8 KB fast RAM, not EWP L1: fAll 16 B + fB 576 B (Physics, with
the current EW mFastEW, which liv wants to stay there) + ~7.6 KB stack
(high-water seen 784 B).  The stack must stay in fast RAM (moving it out
is a big performance loss, per TT).  EWP L1 has ~1029 KB free (celllab
EWP section; GEOMETRY.md sec 9).

Idea: compile physics to bytecode run by an interpreter on hart B.
- Explicit VM operand/call stacks in fB beside mFastEW, sized up front.
  The interpreter NEVER recurses natively: a bytecode call pushes a frame
  on the VM call stack and the dispatch loop continues, so native stack
  use is constant (interpreter frame + deepest primitive, measured once
  with the stack canaries) and the 8 KB is an explicit split.
- Bytecode recursion is FINE (liv): overflowing a VM stack is just a
  failed bounds check, and the engine longjmps to the existing
  RecoverPoint (same policy as today: commit the pre-event window with
  the center erased, log it).  Same for a bad opcode or jump target and
  for running out of per-event fuel (an instruction budget; stack bounds
  stop runaway recursion, fuel stops infinite loops).  Cheap checks: the
  compiler records each function's max operand depth, so one check at
  call entry covers every push in that frame.
- A load-time verifier (declared depths, cf. JVM max_stack, WebAssembly
  validation) is then OPTIONAL: an early warning that reports problems
  before they happen, not a safety requirement.
- Caveat: a primitive must never fault while holding a lock
  (AtomicScopeLock does not recover): check arguments before locking.
- Bytecode lives in L1 as data: lots of element code, even different
  element sets on different EWPs.
- THE MAIN HOPE FOR A WIN (liv): rich native primitives, so most event
  time is spent in native code and interpretation is only glue: atom
  access and atom subfield access, and EW scans such as "find a closest
  atom of type X, breaking ties uniformly at random"; also symmetry
  mapping, random draws, type tests.
- Cost: plain switch dispatch on an in-order RV32 is often 5-20x slower
  per op than native; mitigations: computed-goto dispatch (GCC), a
  register VM, superinstructions, and above all the primitives.  Needs a
  compiler from the element language to the bytecode.
- First measurement (cheap): hand-write FB1 for a tiny switch-dispatch
  interpreter on the EWP and compare AER with native FB1 on the same
  build: the real overhead factor on this core, before designing a format.

## Design idea: raise the AI clock (2026-11-22, steps 1 and 2 DONE: arcpeek and arcmsg RUN)

The chip runs at 800 MHz AICLK (NoCs share it), honest AER 0.97 on FB1; if stable near 1.3 GHz the event path could scale toward AER 1.6 at best.
Step 1 DONE: `srcs/host/tools/arcpeek` (read-only) on all four cards: AICLK 800 (= sysfs tt_aiclk), AICLK_LIMIT_MAX 1350, ARC alive, no errors
(ERISC_NOTES 9.17).  Step 2 DONE (card 0): `srcs/host/tools/arcmsg` sends only TEST (0x90) and GET_AICLK (0x34) through the message queue (SCRATCH 11 info block,
doorbell ARC_MSI_FIFO), idle card only (ERISC_NOTES 9.18).  Result: TRANSPORT OK, GET_AICLK 800 MHz, mode 3 = PPM unforced (DVFS on, clock not forced).
Next: a separate, newly named tool for GO_BUSY / FORCE_AICLK, stepping 1000/1200/1300 MHz with honest-mode seam21.
Step 3 DONE on NIU0 and NIU1: t6arc4/t6arcrun4 read the ARC telemetry from a Tensix tile (AICLK 800, same as arcpeek); the first runs found the mod-64 return rule.  `t6pp/t6arc.c` + `t6arcrun` read the ARC telemetry from a Tensix tile over NIU0/NIU1 (ERISC_NOTES 9.19), the base for logging AICLK from inside
MFMx later (a hub `.ck:` mark built on NRI3::blockingL1ReadNoC0).
Step 4 DONE (run 121809: all 140 hubs `.ck:0:320:3c0:320:546.`): hub `.ck` mark (every hub logs the ARC clocks once at init; ERISC_NOTES 9.20, CLAUDE.md).
Open: does the chip idle below 800 until GO_BUSY (unverified); what the other 800-valued tags (17..20) are; reading the ARC from a T6.  Memory note
project-aiclk-ideas has the web findings.  Never write soft-reset on an ARC/DRAM/PCIe tile.

## Design idea: liv's sawtooth idea (2026-11-22, NOT started, analysed: does not shrink the dead zone)

The dead stripes in the video are the gap between two hubs' event bounds (CachingPolicy.h FourCornersPolicy::boundsImpl).
For a SE-corner supercycle: a hub's x range is [2*OW+R, TW-R) (R = WINDOW_RADIUS = 4); the west neighbour reaches
x < 2*OW-R in the hub's coordinates (neighbour offset = BASE).  Gap = [2*OW-R, 2*OW+R): 2R = 8 sites wide, centred
OW from the base-area origin, so its position scales with OW and its width does not.  It moves between the four
corner positions as the leader corner changes (seen in seam21 video).  Per hub the dead cross is about
8/137 + 8/108 = 13% of the area.  This is NOT an AER loss (events/s per hub is fixed by the hub's speed): it is
UNEVEN density, dead sites get nothing and live sites about 13% more.

The idea: ship OW+R wide strips so B's cache holds R extra sites technically still owned by A; give A and B
a deterministic split of the shared sites for one RUN_EVENTS, e.g. a zig-zag boundary whose segments have
slope +-1 (the Manhattan boundary of an event window), known to every tile; each side rejects any random EW
center whose window would cross the zig-zag.  Hoped result: events from the two sides can sit closer together
(one in a zig, one in a zag), a much softer dead zone.

Analysis (scripts/deadzone/deadzone_model.py, a lattice model with the real 41-site diamond):
- Dead centres per row: straight 8, 45 degree zig-zag (amplitude 2, 4, 8) 8, slope 1/2 zig-zag 8,
  alternating 16-row blocks 10.4.  It is 2R for every connected boundary, and it is a LOWER BOUND for any fixed
  partition: each row must cross from A's sites to B's sites somewhere, and the window row through the crossing
  alone makes 2R centres dead.  So a static zig-zag cannot reduce the dead area; it only reshapes the band
  (wavy instead of straight).  It does let the two sides' centers sit at Manhattan distance exactly 2R+1 (the
  tightest two disjoint diamonds can be) along the slopes, but the straight boundary has the same dead count.
- Costs: about +20% cache traffic at OW=20 (24/20 on the strip); the return of a borrowed strip must send only the
  sites the borrower OWNS, or it overwrites the other side's changes with stale values; a little arithmetic per
  center (reject if the window crosses the boundary).
- Possible visual value only: the stripe becomes wavy.  Not tried.

Variant ideas (all recorded, none started):
1. Moving boundary: each RUN_EVENTS both ends compute a boundary position b inside the shared band from a seeded
   hash of the supercycle number (no messages); the borrower owns band sites below b and returns only those; the
   lender owns the rest.  The dead stripe then wanders over the band (thicker OW = more room) instead of sitting
   at the fixed position.  Needs the owned-only return.  Value is LOW: the stripes already move as the corner
   changes, so the time-averaged density is already spread (user, 2026-11-22).
2. Dynamic claims: hubs tell each other which strips are in use during RUN_EVENTS (tokens or locks), so no strip is
   dead for a whole supercycle.  Can beat the 2R bound (which assumes a fixed agreed partition) but needs
   real-time protocol traffic and locking that does not exist.
3. Rotate partitions within a supercycle (split RUN_EVENTS into sub-phases with different boundaries): the same
   dead area at any moment, more even over time; costs extra barrier bolts.

## Design proposal: PERMANENT array edges (INACCESSIBLE) vs temporary dead bands

STATUS: IMPLEMENTED 2026-11-19 (HubEdges.h, GridManager mPermEdges,
withPermanentEdges, `.pe:MASK` mark per hub).  RUN on the 4-card QB, run
080358 (15 min, fourcorners): `.pe` correct on all 140 hubs (96 zero; 12 N,
12 S, 8 W, 8 E single-bit; 4 corners 3/6/9/c; 44 edge hubs as the host test
predicts), no errors in the log, live AER final 1.39 (1m).  The AER fell from
~150 as the grid filled and was flat at the end.  RING CHECK DONE, run 113217 (width 137, first dumpl1 + l1grid.py): hub at023
(NW corner, .pe:3) had its N and W rings 100% EMPTY (0 of 1570 and 0 of 1280
sites) while SELF was 97.8% occupied and the S/E cache strips were 60-90% full;
live AER final 1.50, no errors; `.pe` masks identical to run 080358.  The
cropped stream image was judged good by eye (crosses 137 apart, blank strip at
the right).  STILL UNCHECKED: edge AER/fill vs run 024650 (needs dumps of an
interior hub and an edge hub in the same run).  Original scope follows.  Four corners is
now the default policy (supercell is `policy supercell`).

The MFM rule (user): at a PERMANENT physical edge of the array, random event
centres go right up to the edge; window sites beyond it are INACCESSIBLE
(reads see EMPTY, writes are discarded, physics may ask whether a site is
inaccessible).  At a TEMPORARY dead band (a caching-policy artifact) centres
stay a full window radius (4) away so every EWP is full of persistent sites.

Today: the grid boundary is the T6Grid (158x128, the 10-wide cache ring
included); getAtomOrInaccessible() / setAtomOrDrop() treat only sites outside
the whole T6Grid as inaccessible.  An array-edge hub's unfilled ring is
ordinary storage (atoms written there just sit), and the policy bounds treat
every outer side as a temporary band.  Physics.cpp already refuses INACCESSIBLE (since 2026-11-22 an INACCESSIBLE CENTRE is a real FAIL(ILLEGAL_STATE))
(centre -> false, DREG avoids it, EventWindow::swap refuses).

Scope (no HostBlock change, no HostFlags bit):
 1. The hub already knows which neighbours exist: configureCacheLoaders'
    `if (!seamHere) continue; // off chip, no seam` from the live seam mask
    (HostBlock mEthSeamLo/Hi, already there).  Factor that into one
    per-Dir4 "neighbour exists" decision made once at hub init; a side with
    no on-chip hub and no seam is a PERMANENT edge.  Layouts then need
    nothing new: 0/1/2/3 alone = four edges; 01 (0 E 1), 23, 02 (0 S 2), 13 =
    three edges each; 0123 = outer sides only.  The four single-chip layouts
    cannot be told apart from the seams, and need not be: the hub only needs
    which of ITS sides have a seam.  (Seams distinguish 01, 23, 02, 13 and
    0123 by chip number + side: five classes, four hub-local shapes.)
 2. Keep the accessible rectangle in the grid (4 u16 in the hub's T6Grid or
    GridManager, NOT HostBlock): [0,TW) (158 at the old width, 157 now) minus the 10-wide ring on each
    permanent side.  getAtomOrInaccessible / setAtomOrDrop / matchesEW use it,
    so reads see INACCESSIBLE and writes drop, with no prefilled atoms and no
    new storage.  Cache exchange on such a side is already absent.
 3. Event bounds: per side, permanent edge -> the first real site (10 / TW-10
    / 10 / TH-10) whatever the policy or the Corner4; neighbour side -> the
    policy value (4 toward, 24 / 134 / 104 away).  A per-side override on the
    EventBounds that CachingPolicies::bounds() returns, so it applies to
    supercell too.  Add the permanent flags to cachepolicy_test.cpp.
 4. DECIDED (user, 2026-11-19): the MFM/T2 rule is that physics MAY ask
    isAccessible(sn), and an access that ignores it reads EMPTY / writes
    nothing.  MFMx cannot yet fail a physics run and exit non-locally
    without failing the engine, so for now keep the prior primitives:
    INACCESSIBLE_TYPE atoms stay VISIBLE in the window (no 41-bit mask),
    readEW already sends them, swap() with one returns false, and writeEW
    (via the accessible rectangle) discards writes to inaccessible grid
    sites.  isAccessible(sn) is just a type test.  FUTURE (needs physics
    failure + non-local exit): writing an INACCESSIBLE_TYPE atom into an
    accessible site becomes a physics failure; reads then really return
    EMPTY.  Wanted consequence: the caches along permanent edges are never
    used, so they stay EMPTY from initialisation and never change.
 5. Host/AER: the viewer already draws INACCESSIBLE (EWControl, ZHostDecompressor);
    AER sites stay 137x108 per hub.  Edge events now run over partly
    inaccessible windows (still count as events).  Expect a small AER/density
    change on single-chip runs (the ring is no longer a stash).
Risks: a seam link down at deploy would look like a permanent edge (preflight
already refuses to run with a link down); an L-shaped 3-chip layout has an
inner corner notch a rectangle cannot express (not among the nine layouts);
all older single-chip runs change behaviour at their edges.
Test: a mock/host test of the edge classification from seam masks for the
nine layouts, the cachepolicy_test bounds, and one QB run of seam6 on a single
chip comparing edge-site occupancy.

## Design proposal: four-corners cache exchange (replaces leader/follower)

STATUS: FIRST RUN CLEAN 2026-11-19 (run 022401, seam11-10.out, 1159c97, 12
min, `policy fourcorners`): .cp:1 on all 140 hubs; cars out=in=noc=98824
(vs about 60k: three bands each way per hub), txfail/nocfail 0, no BADC, no
TIMEOUT, STALLS none, no retrain, FEC uncorrected 0; slowest hub 0.012 s save
/ 0.014 s load (same as supercell), duty 98%.  Fill came faster: 57% at 605 s
and 69% at 688 s vs 39-50% and 49-63% in four supercell runs (equal at 480 s).
Real (committed) events per hub per second: mean 9.0k vs 6.8-7.2k, spread
(cv over all 140 hubs) 37% vs 58-67%, at about the same average fill (28% vs
24-28%).  Not yet proven: an HOUR run (seam10 with `policy fourcorners`, wall
AER at steady state vs about 1.57), and whether the array edges lose atoms.
Visually (user): much less difference between visible and hidden sites.
THREE-HOUR SOAK CLEAN 2026-11-19 (run 024650, seam12-10.out, 1159c97 binaries,
10996 s): no TIMEOUT/FROZEN, no BADC, .cp:1 on all 140 hubs, control viewer
(ffmpeg on the HLS feed) ran 02:48-05:50 local with no stall or reconnect.
Live AER (1m, 5m) flat for the whole three hours at 1.50-1.61 and 1.52-1.55,
no drift (supercell hour runs: 1.55-1.58 and later drift); fill about 71% (vs
about 73% supercell).  So four corners fills the grid faster and is
steadier, but steady-state wall AER is the same as supercell (about 1.55);
duty 94% total / 97% last minute.  aer.py is useless on this run (only 1
hub kept: the cropped-mark hub drop); use the live line.  Not yet checked:
array-edge atom loss; cars out/in/noc counters.  Earlier status: PROTOTYPE BUILT
(step 2 below:
CachingPolicy.h, `policy fourcorners`, config/seam11.cmd; the NEW_LEADER
argument is a Corner4 under it).  UPDATE
2026-11-19: the speed case is mostly gone (duty is 97%, the exchange takes
about 0.01 s); what remains is physics quality: uniform per-site event rate
(events per hub spread 29-34% min to max today), more interhub events,
symmetry.  Read the numbers below as of the 74% duty days.  Hub image only
(T6 side); no HostBlock change; no host/sequencer change needed for step 2.

Today: per 2x2 supercell one hub LEADS (random code 0-3 per supercycle).  The
leader loads all 8 neighbour bands (4 edges + 4 corners) and runs events over
its whole 158x128 area; followers avoid their own 10-wide edge bands.  Costs:
the leader moves 8 bands and a follower 2-4, so the busiest hub sets the
NEW_LEADER / RUN_EVENTS (load) time; per-site event rate depends on role
(a leader spreads the same compute over a larger area); a band site gets
events only about half the cycles, hidden sites every cycle.

Proposal: reinterpret the NEW_LEADER argument (already 0-3, asserted < 4) as a
QUADRANT: NW, NE, SW or SE.  In a NW cycle EVERY hub
  - receives (loads) its W band, its N band and its NW corner cache;
  - sends (saves back) the matching E band, S band and SE corner to the hubs
    that borrow them;
  - runs events over its own 137x108 area shifted by (-10,-10), i.e. including
    the three borrowed caches and excluding the three lent bands.
Event areas tile the plane as same-size rectangles that slide by +-10 in x and
y each cycle.  The other three quadrants are mirrors.

Expected effects:
  1. Constant traffic: every interior hub moves exactly one long band
     (137x10), one short band (10x108) and one 10x10 corner, in and out, every
     cycle.  About a third of a leader's volume today, and no lopsidedness:
     less hub-to-hub variance, so shorter and tighter consensus times, and a
     higher duty (now about 74%).  loadPacket ships empty sites too, so the
     volume is occupancy independent (possibly why NEW_LEADER sits at 1.8 s).
     UNMEASURED: NoC or seam-link contention could dominate.
  2. Every hub runs events every cycle over the same size area: uniform
     per-site rate (no leader dilution), smaller visible-vs-hidden difference.
  3. Hub boundaries (CHUBs as usual) lie inside an event area in all 4 cycles,
     instead of about half of them: more interhub events per unit time.
  4. Total events per second does NOT rise by itself (hubs are already busy);
     any AER gain comes from shorter exchange (duty).
  5. No macroscopic flow or "sloshing": the physics is symmetric, so the
     lender/borrower choice does not bias atom motion.

Dead strips (not a new cost): where two event areas meet (the ownership
handoff, 10 sites inside the lending hub) no centre within 4 of the line can
run (8 wide, twice the window radius 4).  Today's scheme has the same strips
(follower starts 14 in, leader stops 6 in).  Here they sit at +-10 from each
hub boundary, alternate sides with the quadrant, and are dead about 2 of 4
cycles; the crossing of an x strip and a y strip is covered 1 of 4.

Dir4-only variant (one band per hub, W/E/N/S) is cheaper still but leaves a
dead zone at every hub corner (needs the diagonal hub's data in no cycle), so
it only works mixed with the diagonal quadrants (8 codes; host change, since
arg < 4 is asserted).  Not recommended first.

Risks / checks:
  - One-way centre diagonals across chips: all four quadrants must work in the
    directions that exist (today all leader codes exercise them; verify).
  - Seam hubs and array-edge hubs have fewer neighbours: less traffic, but the
    code must skip missing bands (configureCacheLoaders already does for chips).
  - Consistency: a lent band must not be touched by its owner during the cycle
    (owner avoids it: centres stop 4 before it).
  - Marks and tools that talk about "leader": .es/.ev unaffected; aer.py and
    docs need no change except wording; HubValue parity code becomes unused.

Plan, in order (each step needs a go-ahead):
  1. MEASURE: DONE (hub marks .xb/.xe, scripts/xchg/xchg.py; see
     CLOSED_ISSUES.md).
  2. PROTOTYPE: DONE (unproven).  Policy header srcs/shared/include/
     CachingPolicy.h (CRTP base, SuperCellPolicy, FourCornersPolicy, no
     virtuals), chosen per run by two HostFlags bits (mfmxctl `policy`),
     used by configureCacheLoaders and the event bounds in Grid.cpp.  Host
     test: scripts/cachepolicy/cachepolicy_test.cpp (all invariants hold for
     both policies).  Unproven: everything on hardware (the host files
     syntax-check clean here with pybind11 3.1.0).  Risks to watch on the run: array-edge hubs
     run events over a cache nobody fills (the old leader mode has the same
     exposure); one-way centre diagonals across chips in all four corners.
  3. RUN: config/seam11.cmd (the seam6 twin with `policy fourcorners`); compare duty, bolt times, AER and
     cars out == in == noc against the leader baseline (run 030230 etc.).
  4. Later: 8 codes (Dir4 mixed in) if the exchange is still the limit.

## Design idea: physics failure recovery, nuke, xray (2026-11-20, NOT started)

Parked on purpose: do not build ahead of the stream-break frontier.

Why it is cheap here: the EWP hart (HB) is the only one that runs physics, on
`Physics::mFastEW`, a COPY of `ewt.mOld` (Physics.cpp doTransition).  `mOld` is
never written, so the pre-event window always survives.  The grid exists only
on HUB images.

Today: `FAIL` -> `FATAL` -> `BHANG()` (FATAL.h) wedges the hart and the car is
never returned.  PHY_DREG has one such site (swap failure).  Also doTransition
copies `mFastEW` into `mNew` even when physics returns false ("succeed or
fail"), so a half-modified window would be committed.  Unknown center types are
already erased (the `default:` case); INACCESSIBLE center returns false
unchanged.

Plan (user decision: keep FAIL pristine):
1. A separate macro for physics, e.g. `PHYSICS_FAIL(code)` / `EWT_FAIL(code)`,
   that records the code and longjmps to a guard; FAIL stays the hart-wedging
   assert for real bugs.  Shared asserts (EventWindow::getAtom) stay FAIL.
2. `setjmp` once per event in doTransition (tens of cycles on RV32, 14 words
   saved; roll our own if newlib setjmp is not linked).  Locals read after the
   jump are `volatile`; no non-trivial destructors or held locks in the frames
   jumped over.
3. On a jump or a `false` return: mNew = mOld, center atom 0 = empty (the MFM
   response: erase the center, leave the EW otherwise unchanged), count it.
4. A `PHYSFAIL` mark (FAILCode, center type) in LOG and HB; counter in the AER /
   live output so hidden bugs stay visible.
Not covered: wild pointers / illegal instructions (needs a trap handler),
infinite loops (needs a step budget or the heartbeat watchdog).

STEPS 1-4 BUILT AND PROVEN 2026-11-22 (boom10 run 144848: 7 of 10 PHY_BOOM seeds logged `.pf:19974.` (decimal in the .mfmk, = 0x4e06), 140 hubs alive, final AER 0.999; the other 3 never fired, probably overwritten by FB1 neighbors before being an event center).  REDESIGNED 2026-11-22 AFTER that run (cross build OK for ewp and hub, NOT yet run): there is no PHYSICS_FAIL any more; plain FAIL is recoverable.  `srcs/cross/include/RecoverPoint.h`: a hart installs a RecoverPoint (14-word jmp buffer + code + file id + line = 64 bytes, owned by the installer) by pointing ITS OWN private `fAll.mRecover` at it (new field at fAll[12..15], inside the 16 bytes _BUD.ld.in already reserves, so no fastram cost on any hart).  `PrepareToDie` (FATAL.cpp, the first call on every FAIL path) calls `recoverNow(fAll.mRecover,...)` BEFORE touching the HostBlock fail record: if a recovery point is installed it records the failure, clears the slot and longjmps (per hart by construction; other harts' FAILs still hang); else it falls through to `PrepareToDieHard` as before.  `ENGINE_FAIL(code)` (Fail.h, FATAL_HARD) always hangs, for "the engine is broken" (the INACCESSIBLE-center check uses it).  `RecoverSuspend` clears the slot for a scope: engine services with shared state (marks, logging, ...) must wrap themselves in it so a FAIL inside still hangs.  WRAPPED (2026-11-22, built, not run): `AtomicScopeLock` itself holds a RecoverSuspend (declared first, so a FAIL while ANY lock is held, or inside lock()/unlock(), hangs instead of leaving the lock held forever; no raw lock()/unlock() callers exist outside AtomicLock.h), `debugPackToHost` and `markLogBlockDoIt` (the two chokepoints every HBPTAG/LOGPTAG/HBXTAG/SNAP/EACH mark ends in) are suspended, and the wrong-hart asserts (MFM_API_ASSERT_ON_HART/NOT_ON_HART, HBASSERT_ON_HART/NOT_ON_HART in FastLocal.h) are now ENGINE_FAIL.  Everything else reachable from doFastPhysics (EventWindow getAtom asserts, physics' own FAILs, random helpers) stays recoverable.  STACK CANARY FILL + OVERFLOW CHECK (2026-11-22, PROVEN on the QB: run 20261122-170519 boom10, 9 of 10 booms recovered, 140 hubs alive, AER ~1.0, every recovery logged `.sk:784` and `.sh:6816` (= 7600 - 784, the ewp HB stack span): HB's true high-water is 784 bytes): _BUD.ld.in now exports `__fastram_{b,0,1,2,n}_end` (end of each hart's fastram data, 16-aligned; stack grows down toward it) and _BUD.S (exit_setup) fills ALL of the hart's free stack with STACK_CANARY, from that symbol up to sp (was: only the top 1KB), choosing the symbol by hart number in s1.  `estimateStackUsage()` is now a true high-water mark (top - first non-canary word scanning up from the bottom) and the new `estimateStackHeadroom()` is the untouched canary above the data (0 = the stack hit the data); both share one STACKSCAN macro that picks bottom and top by hart.  `Physics::doTransition` ENGINE_FAILs (OUT_OF_ROOM) instead of recovering when the headroom is under 64 bytes, and a recovery logs `.sk:N.` (usage, bytes) and `.sh:N.` (headroom, bytes) in LOG and HB.  Verified in the link: ewp symbols fastram_b_end 0xffb00250, _0_ 0x...050, _1_ 0x...010 (empty), _2_ 0x...a20, _n_ 0x...0e0; stack tops 4K 0xffb01000 / 8K 0xffb02000; disassembly of the fill and scan read.  Stack space per hart (top - end): ewp B 7600, T0 4000, T1 4080, T2 1504, NC 7968 bytes.  Fill cost ~2K word stores once per boot.  Physics: `Physics::guardedPhysics` (the one setjmp per event, noinline, 32-byte frame) installs `Physics::mRecover` (lives in fB, i.e. HB fastram .fastram_b) around doFastPhysics; doTransition commits mOld with the center erased on a jump (always) and mOld unchanged on a false return; counts mEWsPhysFailed; marks `.pf:V.` (V = ((FAILCode+100)<<9)|center type, decimal in the .mfmk) and `.pw:W.` (W = (file id << 16)|line) in LOG and HB, first 8 then every 1024th, plus `pewPF` every 100k events.  PHY_BOOM (type 6, `config/boom10.cmd`) now calls plain FAIL.  Fastram budget (ewp map): per-hart stack = local RAM (8K on B and NC, 4K on T0..T2) minus 16 (fAll) minus that hart's .fastram_x: ewp B 0x240 -> ~7.6K stack; ewp T2 0xa10 -> only ~1.5K stack (tight); embedding the 64-byte RecoverPoint in fAll would cost 64 bytes on EVERY hart of every image, so it lives with its owner instead.  Not covered: wild pointers/illegal instructions (trap handler), infinite loops (separate), hub image (physics runs only in the ewp image).
Step 2 prerequisite DONE (run 2026-11-22, 7/7 ok; setjmp ~19 ticks, setjmp+longjmp ~50 ticks per round): t6sjlj/t6sjrun (ERISC_NOTES 9.22) tests a hand-written 14-word setjmp/longjmp on a T6 hart (regs, sp, deep unwind, nested guards, 20000 rounds, cost).
NUKE BUILT 2026-11-22 (cross build OK for ewp and hub, native tests pass, NOT yet run on the QB): `mfmxctl nuke [big] [X Y R]` (random center/radius like the old nuke when omitted; `nuke host [big]` keeps the old racy host-side one) queues ONE blast in GLOBAL site coordinates (0..1917, 0..1079); the sequencer puts it in words 2..4 [x, y, r^2] of the next NEW_LEADER bolt (NEW_LEADER now ALWAYS carries 5 words [leader, scy, x, y, r2], zeros when no blast, because bolt words persist from bolt to bolt; BoltArgs::MAX_ARGS 2 -> 5, PHASER.h argc 5).  Every hub applies it in PhaseSteppers workOnExitingNewLeaderB, after SAVE and before LOAD with events stopped, to its WHOLE T6Grid, caches included, deciding per site from its global coordinate (shared/include/NukeBlast.h; GridManager::applyNuke through setAtomOrDrop so the host display sees the erasures; a hub whose box misses the disc returns at once; dedupe by scy so a re-fired bolt never nukes twice).  Cache copies agree with their owners without hubs talking; kept caches (`elide on`) stay right for the same reason.  Mark `.nk:SCY:SELF:CACHE.` (hex, per touched hub, LOG and HB).  Tests: nukeblast_test (the 140 hubs' self regions tile 1918 x 1080 exactly once; blasts applied hub by hub erase exactly the disc and every cache copy agrees with its owner), nuke_cycle_test (a queued blast rides exactly one NEW_LEADER, always 5 words).  QB test: config/nuke10.cmd.  Open: the DL list that ships changes to the host display may saturate after a big blast (not checked).  Original nuke idea: a bolt
carrying the blast center in simulation coords (a hub's CT6 coord from mNoC0 in
the HostBlock, / cell size * tile size, 2x2 cells so far) and a nominal radius;
every hub decides from its own position, landing after saveCaches and before
the next loadCaches.  Hub site pitch is 137 x 108 (not 138).  Xray flips bits
in grid store with low probability and needs physics-fail recovery first.

## Design idea: use the Tensix SIMD / FPU / packers (2026-11-20, NOT started)

MFMx uses none of the per-tile custom h/w (32 lane SFPU, matrix unit,
unpackers/packers).  Parked: ideas only, no code.

Key point (user): real physics span 3 to 5+ orders of magnitude in instruction
count, so today's AER (DReg/FB1 are nearly free) mostly measures the FLOOR, as
if physics cost nothing.  Heavier EWTs often do "find the closest atom in the
EW matching this filter, ties broken uniformly at random".  That is the
workload to accelerate, and to benchmark against.

Per event, the general SFPU path is probably a loss: unpack -> queue
instructions through a TRISC -> pack has a fixed latency comparable to a cheap
scalar event, and 41 atoms do not fill 32 lanes.  It can win when the EW stays
resident across MANY queries, i.e. a query library, not accelerated user code.

Proposal: an EW query API in the physics code with a scalar reference and an
accelerated kernel behind the same signature, e.g. `ew.nearest(filter, tie)`,
`ew.count(filter)`, `ew.randomMatch(filter)`, `ew.fill(region, atom)`.
- nearest-match: lanes = EW sites in distance order (a static key per site);
  compare (atom & mask) == value gives a match mask; the min key over matches
  is the nearest distance class; a matmul of the mask with a site x distance
  class indicator matrix gives the tie count per class in one shot; then ONE
  RNG draw r < count and select the r-th set bit (scalar bit trick, or a
  prefix count in lanes).  Scalar reservoir sampling needs a draw per match.
- Cross-lane min/count may be awkward on the SFPU: lay the EW out transposed
  so the reduction runs across rows (elementwise min), or hand the bitmask to
  the scalar side after the compare.
- Test like the LZ stress harness: random EWs and filters, scalar vs
  accelerated, any mismatch fails.

Other fits (all unmeasured): bulk grid ops on hubs (nuke = masked clear, xray =
per lane RNG + masked XOR, strided cache edge copies by the packers); observables
(census / density maps, 5x5 neighborhood counts as banded matmul; fits observation,
not asynchronous events; also the spread metric for the dwell experiment); the
report stream (32 wide LZ match scan, or better, dirty bit filtering before the
compressor, which needs no special h/w).

Which hart drives it (user idea): the hub's compression hart (T1) is a hart the
image "owns"; the EWP image could likewise use a free T hart as a Tensix driver.
Harts per image (user): T2 is the PRNG server (Mersenne twister) on ALL images,
T0 does timing/timestamping (could be repurposed per image, not yet), T1 is the
hub's compressor and so FREE in the EWP image (ImageHartTasks.inc lists only
`b`).  So T1 is the Tensix driver candidate; the tie-break draw comes from the
T2 mailbox (one draw per query), while per-lane RNG for xray could use the SFPU's
own random instruction (weaker than MT; check quality before trusting it).  The TRISCs are also the harts wired to the Tensix front end
(thread 0 unpack, 1 math, 2 pack), so a T hart is the native driver.  Shape: B
runs the scalar physics and hands the EW (in L1, no copy) to T1 through a
mailbox (cf. MAILBOX_BASE_T2, preloadT2Mailbox); T1 runs the kernel and posts the
result.  The hand-off (write + poll, tens of cycles) argues for offloading only
heavy queries or whole batches, not cheap ones.  One hart driving all three
stages loses the unpack/math/pack overlap; acceptable at first.

Unknowns: how a single TRISC best sequences all three Tensix stages; no tt-metal LLK here, so kernels
would be hand written from the public ISA documentation; precision of the
matrix unit formats (counts are fine, full 32 bit atom bits go on the SFPU).

First steps: (1) cycle counters (rdcycle) around doTransition and the EWP wait,
and on the hub compress/ship path, to find the real bottleneck; (2) a synthetic
heavy element (the nearest-match query with a tunable filter and EW density) so
AER can see physics cost; (3) the scalar query library with a differential
test; only then (4) an SFPU kernel behind it, xray/nuke as the first bulk user.

- Shrink AtomReport from 20 to 16 bytes (2026-11-23, idea, NOT started, user:
  can wait, do not lose the thread).  Today: P4Atom 12 + U16C coord 4 + two
  u16 spins (mSpin1 == mSpin2 == expected, the record-level desync check) = 20,
  4-byte aligned, so dropping ONE spin saves nothing.  PREFERRED (user,
  2026-11-23): U8C coord (the 177x148 T6Grid including caches fits u8) + ONE
  u16 spin = 16.  RULE: the transport layer must not look at the atom layer
  (AtomReport::isValid already says so), so NO atom parity or coord range
  checks for desync.  The check is just spin == expected: after a lost byte a
  garbled record matches by chance 1 in 65536; the second spin only bought
  the skipped-vs-garbage diagnostic (give that up: one combined bad-record
  count).  Restart resync made deterministic: the hub resets its spin counter
  to 0 at each stream start and the host resets its expected value to 0 at
  the same point (restartDecoder), so mSpinSync (adopting the hub's spin
  from a record) goes away.  Epoch marker = coord (0xff,0xff) (no site has
  255) with the 16-bit epoch in the spin; the hub counts it in the sequence
  (next record = previous + 1), the host advances its expected value without
  checking the marker's own spin, so a lost marker reads as a skipped record.
  A garbled record passing as a marker (1 in 65536) would give a bogus epoch
  until the next real marker, about half a second.  The atom's own parity
  (atomvalid, mBadAtoms) stays where it is, after the transport accepts a
  record.  Other 16-byte layouts: U8C + two u8 spins (wraps every 256
  records); U16C and no spins.  FIRST measure what the 4 bytes cost: the
  spins compress poorly (incrementing counters), so compare compressed bytes
  per report (zhd counters) with and without.  Touches hub, host decoder,
  mock/selftest.

## Speed / measurement ideas (not started)

(The exchange itself is done, see CLOSED_ISSUES.md: duty 97%.  What is left
is the host bolt path, about 0.13 s per bolt, plus the dwell.)

- Bolt poll tick 0.05 s instead of 0.2 s (about 2 percent).
- Longer `dwell` (script only; physics trade-off).  `dwell` is the only knob
  for event time per cycle; duty is about 97 percent now.
- `loadPacket` ships every site including EMPTY atoms (full cars regardless
  of occupancy): bandwidth/latency opportunity in the RUN_EVENTS cache load.
- AER phase 2 (live AER on the video, host tap, no log files): BUILT and
  VISIBLE on hardware (first run 2026-11-19; layout and wording changed after
  that look: own line, AER units, `all`).  Still to check: the figures sit
  near aer.py's wall AER (about 1.57) after a few minutes, and the new
  overlay layout.  Known limit: .ev marks are LOGPTAG kind and lossy, so the
  figure is rough.
- FEEDSTAT per-tick timing.
- ACB ship rate (BOGOMS_PER_FRAME, EP_ACacheBlock.cpp) is a low-priority
  knob.  To check ACB is not crowding IH, add a dotted `.ab` mark at the
  ship point in `ACacheBlockPrivateControl::updateCars` (undotted tags such
  as SHIPFRM/WAITSENT/DGLLen are suppressed by HBF_NO_UTAG).  The DL backlog
  itself is by design (lossy DLGridList), not a bug.

## Cleanup / nice to have

- Mock only (2026-11-25, seen on latest and diagonal-tiles): under
  `--mock`, `zoom auto` turns itself off at the first frame with
  `zoom disabled: IndexError: index 108 is out of bounds for axis 0 with
  size 108` (the mock frame is probably smaller than the 1920 x 1080 the
  zoom code assumes).  QB runs are not affected as far as known.

- DONE 2026-11-24 (AERse / AERre, "est. " dropped; see CLAUDE.md): AER status line should show the event counting method (2026-11-23, user
  idea, not started).  The difference today is whether empty-site events are
  shortcut or actually run.  Avoid 'honest'/'cheating'.  Candidates: 'AERse'
  (Shortcut Empties) vs 'AERre' (Run Empties), or similar.  The user may drop
  the 'est. ' prefix on the line to pay for the space.  Needs the hubs'
  current mode visible to the host (check how the live AER line is built).

- Link preflight needed 3 extra chip resets (43.8 s) in run 012507:
  eth7 bh0/bh1, eth4 bh1/bh3, eth13 not Up after 12 s each time.  It
  recovered by itself; watch whether it is getting worse.

- First-NEW_LEADER `once` hack in `workOnEnteringNewLeaderB` is probably
  removable (zero-initialized loaders make the first save a no-op).
- `.pw` waypoint marks on the T6 side are not emitted (analyzer parses them).
- Inter-T6 destinations are not in PKTS.txt (analyzer learns them from marks).
- Possible `sleep SECS` mfmxctl command (offered, not requested).
- FB4's renderer colours by hidden counter mStg[1]; a seed mix (DReg/FB1)
  would add variety.

## Cache elision: BUILT 2026-11-21, UNPROVEN on hardware
- Four corners, host flag HBF_ELIDE_CACHES (mfmxctl `elide on`, `--elide`;
  default off) and `cycle repeat on` (corner picked 0..3 uniformly, repeats
  allowed, four corners only; default off = random different corner).  A
  direction whose role (send sites / receive caches) is the same under the
  previous and the next corner is neither SAVEd at that NEW_LEADER nor LOADed
  at the next RUN_EVENTS: CachingPolicies::keepAcross, CacheLoaders::
  setKeepMaskFor (decided once per SAVE, nothing kept before the first LOAD
  completed), hub mark `.xk:MASK` (decimal, one bit per Dir8 loader, only when
  nonzero).  Same corner keeps all 6 exchanged directions, adjacent corners 2,
  opposite 0 (scripts/cachepolicy/cachepolicy_test.cpp checks that and that
  both ends of a link agree).  Uniform choice: expected ~42% of the exchange
  traffic saved (1/4 same, 1/2 adjacent, 1/4 opposite).
- BUG FOUND 2026-11-21 (seam19-10 per-direction counts): in 7de4ea4..d4efa62
  configureCacheLoaders() (NEW_LEADER exit) called init(), which zeroed
  mKeepMask, so the kept directions were skipped at SAVE but LOADed in full:
  SAVE -47% packets, LOAD unchanged.  Worse, a kept band's SAVE was skipped
  and then its LOAD overwrote the borrower's cache from the lender's sites, so
  atom changes made in the cache during the events were dropped.  seam18 and
  seam19 therefore do NOT test elision (their stream/AER results stand).
  Fixed: mKeepMask and mHaveLoaded survive init().  Expect LOAD packets in
  kept directions to be 0 in the next `xchg.py --dirs` table (seam19.cmd).
- RISKS to check on the first runs (config/seam18.cmd, one hour):
  (1) the lender's lent sites stay STALE until the band is next saved, so the
  display and the `.an/.ae` census of a lent band lag by the number of phases
  it is kept; (2) atom CONTENTS in kept bands are not proven (the SITE_RANGES of
  adjacent bands overlap at the corner squares, two neighbors hold copies of
  those cells; this already holds without elision, see the content-hash slice
  parked above); (3) the first cycle and any run with `policy supercell` keep
  nothing.  Compare with a seam16/17 run of the same length: xchg.py packets
  per phase (expect ~42% fewer), AER, duty, no stream breaks, cars in == out ==
  noc, cachecensus on the seams.

## Seam lines in the video (cosmetic, 2026-11-21, leave)
- With four corners every hub boundary has a strip about 2R (8) sites wide, at
  the boundary +/- OW, where sites are reached by centers of one side only
  (lower event rate, so FB1 ages lag).  It moves between two positions per axis
  (2*OW apart) when the corner's component on that axis flips; `cycle repeat on`
  keeps it in place longer (same component 50% vs 33% for random-different).
  Seen as thin lines near a hub's S/E/W (N hidden by the text).  Not a bug.
- Considered and rejected: sawtooth lend/borrow edges.  A window must fit
  entirely on one side, so teeth under 2R+1 reach nothing, the longer seam
  excludes more centers, and every band range, bound and iterator assumes
  rectangles.  Possible blur: more than two seam positions (variable-depth
  lent bands), not done.
- Also considered, withdrawn: double painting of cache bands by two hubs.  The
  lender reports nothing for lent sites until SAVE, and SAVE returns the
  borrower's values, so there is no stale writer.

- PHASER arg-count table was misaligned (no NOT_A_CMD slot) so every command read its neighbor's count; NEW_LEADER got 1 word, never its scy/blast words (nuke10-10/11: .nw all zero, bogus blast from uninitialized args). Fixed with a leading 0 entry plus static_asserts in PHASER.h; .nw/.nb diagnostics stay.
