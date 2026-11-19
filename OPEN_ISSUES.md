# OPEN ISSUES (bugs, tuning, unproven things)

One short list; newest first within a section.  Rules (see CLAUDE.md sec 2):
fix something -> delete or move its entry in the SAME commit; find something
and do not fix it -> add it here before the turn ends.  Fixed items are not
kept here (git log is the record).  Design history stays in HANDOFF.md and
ERISC_NOTES.md.

## Bugs

- Video stream lockup at bring-up: PROVISIONALLY SOLVED (user call, runs
  211051 and later).  Keepalive repeats were paced 12 percent slow (dup 133 per
  30 s vs 153); fixed in 3516d21 (repeatBase); idle dup now 150, and Firefox
  and ffplay stayed up through bring-up.  Reopen only if it recurs; then look
  at the player/mediamtx log.  ffplay's "duplicated MOOV atom" is harmless.
- HANG at PACKET_SLEEP_CYCLES=100,000 (run 125641, seam9-10.out): cycle 250,
  bolt #500 RUN_EVENTS TIMEOUT 10.2 s; 130/140 hubs on bh1 acked, 10 lamers
  (TLBI 23,51,79,99,101,103,105,107,109,135 = at1c3 at1c5 at1c7 at129 at149
  at169 at1a9 at1c9 at1e9 at1cb: a cross of column x=12 and row y=9 around
  at1c9).  NEW_LEADER #499 had passed in 0.1 s.  Clean up to then: 246 cycles,
  cars out==in==noc, txfail/nocfail 0, no retrain, FEC uncorrected 0, no BADC.
  First hang ever seen; 6 h run (10M) and 1M run (about 106 cycles) never
  hung.  Suspect a latent packet-exchange race that the 10M/1M busy-wait
  was hiding (hn needing time to drain), not a link fault.  Not diagnosed:
  need the hub-side state of the stuck cross (which loader, which Dir8, who is
  Dig results (tile marks of run 125641; marks are chronically lossy, see
  below, so this is a lead, not proof):
  * Eth transport lost nothing: cars out==in==noc (133,942) after the freeze.
  * Lamers show NO marks past about 20 ms into the cycle-250 LOAD (RUN_EVENTS):
    c5/c7/129/1a9/1cb have .xb:1 and no .xe:1.  at1c9 (the cross centre) has
    no .xb:1 at all; its marks end at the tail of the SAVE.  Its Dir4
    neighbours stop right at LOAD start, farther hubs a bit later: looks like
    a stall at at1c9 spreading outward along the packet dependencies.
    Candidate root: at1c9 (12,9), leader code 3 at scy 250.  Unconfirmed.
  * Mark streams are lossy on their own: at149, at1e9 and at1e5 stopped
    logging 5.8 s earlier (cycle 249 SAVE) yet kept acking bolts, so a mark
    log ending early does NOT mean the hub hung.
  * Mark volume does not single out the cross (lamers 21-22k hn marks, same as
    healthy hubs), so log back-pressure is not an obvious cause.
  Heartbeat added (.xw via LOGPTAG and HBPTAG, 1 s while an exchange phase is not ready: per-Dir8
  need mask, leader mask, car ring lengths); `xchg.py` prints it as STALLS.
  NEXT hang will name who waits on whom.  Unproven on hardware.  Slowscan output (Eprintf) goes to all.mfmk in the run dir, not the .out.  In
  run 125641 the after-timeout slowscan scanned only 4 tiles (the first EWP
  per chip), none a lamer, because `tick slowscan off`; fixed: a timeout now
  turns the tick on (sweep of every tile, about 13 min per chip pass).
  HB ring: it used to discard NEW marks when full (frozen on the earliest
  marks with the slowscan tick off).  Now HostBlock::packString drops the
  OLDEST words to keep the latest (torn head OK) (racy against slowscan's read/reset,
  accepted).  Unproven on hardware.  Open: xchg.py reads .xw from
  tiles/*.mfmk only; HB marks land in all.mfmk (slowscan dump).
  RERUN (run 165950, seam9-11.out, build f22f6a3, 100k): did NOT hang.
  Full hour, 565 cycles (bolt #1130), clean teardown, no bolt TIMEOUT,
  seams out=in=noc=304890, txfail 0, nocfail 0, no .xw stall (STALLS: none).
  So the hang is intermittent: 1 hang in 2 runs (246 and 565 cycles).  Still
  unexplained and still possible at 100k; the .xw heartbeat, sweep-on-timeout
  and HB-ring fixes remain unproven on a real hang.  PACKET_SLEEP_CYCLES is now 0 for all
  further runs (user decision after run 211051); the diagnostics (.xw, sweep on
  timeout, HB ring) stay in; revisit only if a hang recurs.

- mfmxctl `status` line prints api=legacy under firePHASER (mislabel).
- `create(50)` seed switch in `GridManager::seekRandomNonEmptySite` is computed
  but unused (the autoseed type follows `decay N`, default 5).

## Unproven on hardware

- Atom CONTENTS match across seams (content-hash slice; user: pass on it
  indefinitely while things look good).
- pybind `portStatus`/`releaseIdlePorts`, `--abort-on-error` and typed `wait`
  beyond one clean run.

## Design proposal: four-corners cache exchange (replaces leader/follower)

STATUS: discussed 2026-11-18, not started, no go-ahead yet.  Hub image only
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
  - runs events over its own 138x108 area shifted by (-10,-10), i.e. including
    the three borrowed caches and excluding the three lent bands.
Event areas tile the plane as same-size rectangles that slide by +-10 in x and
y each cycle.  The other three quadrants are mirrors.

Expected effects:
  1. Constant traffic: every interior hub moves exactly one long band
     (138x10), one short band (10x108) and one 10x10 corner, in and out, every
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
  1. MEASURE (DONE for the sleep, see CONFIRMED below; run 123006): hub marks .xb/.xe (PhaseSteppers.cpp
     xchgStep) give each hub's SAVE and LOAD time and packet count;
     scripts/xchg/xchg.py reports them and compares with the host bolt times.
     Run seam6.cmd on the new hub image (sleep 1M; compare with the 6 h run
     035205: NEW_LEADER 1.79 s, RUN_EVENTS 2.43 s, duty 74%), then
       python3 scripts/xchg/xchg.py MFMX-<date>-<time> --bolts seam4-NN.out
     See FINDING below.
  2. PROTOTYPE: quadrant cache config + event bounds in the hub image
     (configureCacheLoaders, seekRandomNonEmptySite bounds).  Same host.
  3. RUN: seam6.cmd style 12 minute run; compare duty, bolt times, AER and
     cars out == in == noc against the leader baseline (run 030230 etc.).
  4. Later: 8 codes (Dir4 mixed in) if the exchange is still the limit.

## Speed / measurement ideas (not started)

- CONFIRMED (run 20261118-123006): CacheLoader.cpp busy-waited 10,000,000
  cycles after EVERY packet loaded or unloaded (the "DEBUG give hn a chance"
  literal, serial on hart B).  At 1,000,000 (PACKET_SLEEP_CYCLES, now) a
  packet costs 6.4 ms (so about 64 ms before: 26 packets = the constant
  1.8 s), NEW_LEADER bolt 0.28 s (was 1.79), RUN_EVENTS 0.36 s (was 2.43),
  duty 94% (was 74%), nothing lost (cars out == in == noc, BADC 0, retrain
  0, FEC uncorrected 0, no TIMEOUT).  Remaining non-running time is the
  slowest hub's exchange, about 0.17 s save + 0.24 s load per cycle.  NOW
  DONE: 0 (seam10.cmd, run 211051, seam10-10.out): clean hour, 569 cycles,
  out=in=noc=305668, txfail/nocfail 0, retrain 0, FEC uncorrected 0, BADC 0,
  no TIMEOUT, STALLS none.  Per packet 0.2 ms (was 0.8), slowest hub 0.005 s
  SAVE / 0.011 s LOAD, but duty stayed 97% and steady wall AER ~1535-1580
  (same as 100k).  So sleep 0 buys nothing measurable: the remaining non-
  running time is the host bolt path (~0.13 s per bolt, 0.12 s of it not the
  exchange) and the dwell, not the exchange.  Next lever: bolt poll tick /
  shorter dwell.  One clean hour does not prove 0 is hang-free (100k hung 1
  in 2).  FEEDSTAT idle dup 150 per 30 s (was 133): keepalive fix works, no
  viewer lockup in Firefox or ffplay.
  DONE: 100,000 ran a clean hour (run 165950: wall AER 1561-1573 steady, duty 97%,
  slowest hub 0.013 s SAVE / 0.032 s LOAD) after one hang (125641); then 0: another 5 percent of duty, and it makes a
  much shorter `dwell` affordable.  Why the sleep existed is still unknown;
  re-check the same counters at each step.
- Bolt poll tick 0.05 s instead of 0.2 s (about 2 percent).
- Longer `dwell` (script only; physics trade-off).  `dwell` is the only knob
  for event time per cycle; duty is about 74 percent.
- `loadPacket` ships every site including EMPTY atoms (full cars regardless
  of occupancy): bandwidth/latency opportunity in the RUN_EVENTS cache load.
- AER phase 2 (live mAER on the video, host tap, no log files): BUILT, UNPROVEN
  on hardware (the host module could not be compiled here: LiveAER.cpp and the
  Python side are tested standalone, the OurTLBs/pybind hooks are not).  First
  real run: check the overlay shows `mAER <1m>/1m <5m>/5m <seed>/seed Nh`, that
  the figures sit near aer.py's wall AER (about 1570) after a few minutes, and
  that Nh is the hub count (about 560, fewer if marks are lost).  Known limit:
  .ev marks are LOGPTAG kind and lossy, so the figure is rough.
- FEEDSTAT per-tick timing.
- ACB ship rate (BOGOMS_PER_FRAME, EP_ACacheBlock.cpp) is a low-priority
  knob.  To check ACB is not crowding IH, add a dotted `.ab` mark at the
  ship point in `ACacheBlockPrivateControl::updateCars` (undotted tags such
  as SHIPFRM/WAITSENT/DGLLen are suppressed by HBF_NO_UTAG).  The DL backlog
  itself is by design (lossy DLGridList), not a bug.

## Cleanup / nice to have

- First-NEW_LEADER `once` hack in `workOnEnteringNewLeaderB` is probably
  removable (zero-initialized loaders make the first save a no-op).
- `.pw` waypoint marks on the T6 side are not emitted (analyzer parses them).
- Inter-T6 destinations are not in PKTS.txt (analyzer learns them from marks).
- Possible `sleep SECS` mfmxctl command (offered, not requested).
- FB4's renderer colours by hidden counter mStg[1]; a seed mix (DReg/FB1)
  would add variety.
