# OPEN ISSUES (bugs, tuning, unproven things)

One short list; newest first within a section.  Rules (see CLAUDE.md sec 2):
fix something -> MOVE its entry to CLOSED_ISSUES.md (numbers kept, story cut)
in the SAME commit; find something and do not fix it -> add it here before
the turn ends.  Keep this file a concentrated dose of what needs work.  Design history stays in HANDOFF.md and
ERISC_NOTES.md.

## Bugs

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

- Live AER vs aer.py: run 012507 (14f6dfd) live final 4.65, 33.7, 84.9 vs
  aer.py last 60 s 14.9, last 300 s 50.1, total 97.7 (104 hubs): the live
  figure was trimmed (read low while hubs differ); now a plain mean.  Compare
  again with misc/mfmxctl-LIVEAER.txt on the next run (aer.py's total starts
  at the autoseed; the live line's t counts from mfmxctl start).

- Atom CONTENTS match across seams (content-hash slice; user: pass on it
  indefinitely while things look good).
- pybind `portStatus`/`releaseIdlePorts`, `--abort-on-error` and typed `wait`
  beyond one clean run.

- T6 -> ETH write over NoC1 (and so preferNoC for ETH destinations): expts15 (2026-11-21) FAILED because it
  was addressed wrongly: it passed NoC0 (6,1), which on NoC1 is E13, not our E1 (E10).  Rows Y=0/1 are not
  translated (NIU cfg 0x6005 has translation ON for Tensix tiles); there X is the raw coordinate of the NoC in
  use and NoC1 mirrors it (x1 = 16 - x0, Y unchanged; tt-isa-documentation BlackholeA0/NoC/Coordinates.md).  The
  probe wrote one 64 B car into E13's L1 at 0x10000 (link counters unaffected).  expts16.sh / ethfwd22 send
  (16-6,1) = (10,1), refuse the unflipped node over NIU1, run the NoC0 control first and stop on a failed
  probe.  NOT RUN YET: `bash srcs/host/tools/expts16.sh` on the QB (needs ethfwd22, e1fwd16, t6pp3; output
  ./out16 in the directory you run it from).  If it passes, NRI3::initiateWriteToEthL1 should use preferNoC
  with the mirrored node for the ETH row.  E1 -> T6 over NoC1 is already proven (9.13).

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
every outer side as a temporary band.  Physics.cpp already refuses INACCESSIBLE
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

Then `nuke` and `xray` have something to test against.  Nuke idea: a bolt
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
