# DEFERRED ISSUES (parked bugs, ideas, design explorations)

What is not being worked on now but must not be lost: bugs and puzzles
parked on purpose, half-explored designs, and ideas.  Moved out of
OPEN_ISSUES.md on 2026-11-28 so that file can stay short and current;
the text below is as it stood then (dates and run names inside each
entry).  To pick one up, move it back to OPEN_ISSUES.md (and when it is
done, to CLOSED_ISSUES.md), in the same commit as the work.

REMOVED FILES (2026-11-28 cleanup, phase 1): older numbered tools and
configs named below (ethfwd1-21, ethprobe/11, ethlinktest-13, e1fwd1/11/13-15
and their hosttests, e1order/13, t6arc1-3, t6arcrun1-3, expts10-15, seam5-20
and seamcycle1to3/23/ns .cmd, nu1010/nu1011/mpmd18) are gone from the tree;
read any with `git show pre-cleanup:PATH`.

## Parked bugs and puzzles

### AER 0.04-0.05 below the 11-19 soak (PARKED by Liv)

- AER 0.04-0.05 below the 11-19 soak (2026-11-25, PARKED by the user).  Honest
  FB1 AERre reads 0.94 flat now (dwell 4 8, no seeds; run 20261125-165051)
  against 0.987 at 180 s and 0.977 at 900 s in soak 124337 (same metric).
  Ruled out: BOGOMS, dwell (+0.03), seeding/fill, host cycle timing.  The hubs
  do ~5% fewer events per dwell second: something hub-side in the last week.
  Not bisected.  How, if it matters: CLAUDE_ARCHIVE.md "AER GAP, PARKED".

### EWP/hub car handshake lost at boot: fix proven, mechanism not established

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

### Video stream lockup at bring-up (hls.js after a NO DATA gap)

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

### HANG at cycle 250, run 125641 (seen once)

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

### mfmxctl segfault at exit after quit without close

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

### Live AER vs aer.py

- Live AER vs aer.py: run 012507 (14f6dfd) live final 4.65, 33.7, 84.9 vs
  aer.py last 60 s 14.9, last 300 s 50.1, total 97.7 (104 hubs): the live
  figure was trimmed (read low while hubs differ); now a plain mean.  Compare
  again with misc/mfmxctl-LIVEAER.txt on the next run (aer.py's total starts
  at the autoseed; the live line's t counts from mfmxctl start).

### Atom contents across seams (content-hash slice)

- Atom CONTENTS match across seams (content-hash slice; user: pass on it
  indefinitely while things look good).

## Design explorations and ideas

### Design idea: diamond-shaped hub regions (2026-11-25, EXPLORING, host lab only)

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

### Host-side atom type census (Liv, 2026-11-26, not started)

- Host census (Liv): the host counts atom types in what it displays
  (the 1920x1080 site image it already has) and logs the census
  somewhere, e.g. per minute in misc/ and in runsum.txt; would answer
  'did 5 eradicate 8' directly.  Maybe expand the 'logo block' (the
  stream overlay text) to show some of it.

### Design idea: raise the AI clock (2026-11-22, steps 1 and 2 DONE: arcpeek and arcmsg RUN)

The chip runs at 800 MHz AICLK (NoCs share it), honest AER 0.97 on FB1; if stable near 1.3 GHz the event path could scale toward AER 1.6 at best.
Step 1 DONE: `srcs/host/tools/arcpeek` (read-only) on all four cards: AICLK 800 (= sysfs tt_aiclk), AICLK_LIMIT_MAX 1350, ARC alive, no errors
(ERISC_NOTES 9.17).  Step 2 DONE (card 0): `srcs/host/tools/arcmsg` sends only TEST (0x90) and GET_AICLK (0x34) through the message queue (SCRATCH 11 info block,
doorbell ARC_MSI_FIFO), idle card only (ERISC_NOTES 9.18).  Result: TRANSPORT OK, GET_AICLK 800 MHz, mode 3 = PPM unforced (DVFS on, clock not forced).
Next: a separate, newly named tool for GO_BUSY / FORCE_AICLK, stepping 1000/1200/1300 MHz with honest-mode seam21.
Step 3 DONE on NIU0 and NIU1: t6arc4/t6arcrun4 read the ARC telemetry from a Tensix tile (AICLK 800, same as arcpeek); the first runs found the mod-64 return rule.  `t6pp/t6arc.c` + `t6arcrun` read the ARC telemetry from a Tensix tile over NIU0/NIU1 (ERISC_NOTES 9.19), the base for logging AICLK from inside
MFMx later (a hub `.ck:` mark built on NRI3::blockingL1ReadNoC0).
Step 4 DONE (run 121809: all 140 hubs `.ck:0:320:3c0:320:546.`): hub `.ck` mark (every hub logs the ARC clocks once at init; ERISC_NOTES 9.20, CLAUDE_ARCHIVE.md).
Open: does the chip idle below 800 until GO_BUSY (unverified); what the other 800-valued tags (17..20) are; reading the ARC from a T6.  Memory note
project-aiclk-ideas has the web findings.  Never write soft-reset on an ARC/DRAM/PCIe tile.

### Design idea: liv's sawtooth idea (2026-11-22, NOT started, analysed: does not shrink the dead zone)

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

### Xray (from the 2026-11-20 failure-recovery plan, not started)

Flip bits in the hub grid store with low probability, to exercise
physics-failure recovery (which is now built and proven, CLOSED_ISSUES).

### Design idea: use the Tensix SIMD / FPU / packers (2026-11-20, NOT started)

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

### Speed / measurement ideas (not started)

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

### Seam lines in the video (cosmetic, 2026-11-21, leave)
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

## Cleanup / nice to have

- Mock only (2026-11-25, seen on latest and diagonal-tiles): under
  `--mock`, `zoom auto` turns itself off at the first frame with
  `zoom disabled: IndexError: index 108 is out of bounds for axis 0 with
  size 108` (the mock frame is probably smaller than the 1920 x 1080 the
  zoom code assumes).  QB runs are not affected as far as known.

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

### REPO CLEANUP PLAN (2026-11-28, planned with Liv; PHASE 1 DONE)

Liv asked for the three ugliest things about the repo, then for a plan;
parked while chasing CHIP-0 EWP ACKS LOST.  Each phase is one commit;
OPEN_ISSUES / CLAUDE.md paths are fixed in the same commit as a move.
First: `git tag pre-cleanup` on HEAD (Liv pushes it), so docs can say
"removed, see `git show pre-cleanup:PATH`".  Checks per phase: native
tests, a cross build, a mock run of experiment.cmd here; on the QB,
build_all.sh after phases 2 and 3, and a short run after 3.

The three ugliest (as of 2026-11-28, 653 tracked files, 23 MB):
1. Versions kept as numbered copies (~80 files: ethfwd1..22,
   seam4..21.cmd, e1fwd1..16 + hosttests, t6arc*, expts10..16, ...).
2. srcs/host/tools/ is a junk drawer (97 files: the native test suite,
   one-off hardware probes, generated headers, design docs, committed
   .dis / _image.inc build products).
3. Generated files mixed in with hand-written ones (bpasm's headers in
   srcs/shared/include and host/tools, generated .bp beside hand .bp).
Honorable mentions: EWD/fonts is 13 MB for two used faces; README ends
in sample-code boilerplate; 8 top-level .md files; /data/ackley paths
in 18 tracked files.

Phase 1, delete superseded copies (no build path changes).  DONE
2026-11-28 (tag pre-cleanup before it): 180 files; also kept
seamtest/seamcycle/seamlong/seamns.cmd (HANDOFF procedures, selftest),
dropped every .dis / _image.inc (build outputs; ethlinktest14 embeds its
image), and the two DECIDEs went 'drop' (t6arcrun_mock now runs
t6arcrun4).  Keep the
newest of each series plus whatever CMake, run_native_tests.sh or
mfmxctl uses:
- ethfwd: keep ethfwd22 (ERISC_NOTES 9.16, PASSED); drop 1, 11-21.
- expts: keep expts16.sh; drop 10-15.
- ethprobe: keep ethprobe12; drop ethprobe, ethprobe11.
- ethlinktest / e1order: keep ethlinktest14, e1order14 (+ .ld,
  build.sh); drop the older tests and images with their .dis and
  _image.inc.
- e1fwd: keep e1fwd16 (the one e1fwd.cmake builds) and hosttest16;
  drop e1fwd1, 11, 13-15 and their hosttests (run_native_tests.sh runs
  hosttest13-16: drop 13-15 from its loop).  DECIDE: OK to drop those
  tests?
- t6arc: keep t6arc4, t6arcrun4, hosttest_arc4; drop the 1-3 chain
  (also in run_native_tests.sh).  DECIDE: same question.
- seam*.cmd: keep seam4 (docs, dtoml, xchg), seam9 (mock selftest),
  seam21 (latest); drop ~19 others and unreferenced seamcycle* etc.
- dtoml: keep nu1012; drop mpmd18, nu1010, nu1011.
- fonts: keep fonts/JetBrainsMono/ttf/JetBrainsMono-Light.ttf and
  fonts/NK57Monospace/NK57 Monospace Cd Rg.otf (RTMPFeed13 loads only
  those); add JetBrains Mono's OFL.txt (the OFL wants it shipped).
- README: drop the "RANDOM CRAP FROM SAMPLE CODE BELOW" tail.
- ERISC_NOTES / DEFERRED entries naming a dropped version: one line
  "removed, see pre-cleanup".

Phase 2, split srcs/host/tools/ (paths change):
- tools/tests/: *_test.*, bytephys_test, run_native_tests.sh.
- tools/probes/: eth*, arc*, t6*run, expts.
- Device C (e1fwd/, t6pp/, e1order/) under srcs/cross/ (RISC-V code
  built by the cross toolchain); update srcs/cross/codes/e1fwd.cmake.
- ERISC_NOTES.md, ERISC_SEAM_DESIGN.md to docs/ (phase 4).

Phase 3, a place for generated files (includes change):
- bpasm headers (BytePrograms.h, ByteBench.h, BytePhysTests.h,
  ByteDReg.h) to srcs/shared/include/gen/, included as
  "gen/BytePrograms.h".  NOT a separate include dir: the cross CMake
  passes -I${SHARED_INCLUDE_DIRS} as a single dir for the ld script.
- generated .bp to scripts/bytephys/gen/ (ulamlite OUTDIR, bpasm SETS).
- physics/*.bcp stay (runtime data mfmxctl loads).
- (Later, bigger: generate at build time instead of committing.)

Phase 4, docs and paths:
- docs/: BYTEPHYS, GEOMETRY, HANDOFF, ERISC_*; root keeps README,
  CLAUDE*, the three *_ISSUES; CLAUDE.md map updated.
- build_all.sh venv / wheels dirs overridable by environment (current
  values as defaults); FontCache.py demo path relative; AutoWash.sh and
  A1AAutoWash.sh (only removed configs) deleted.
- notes/: content untouched; README says what it is.
