# OPEN ISSUES (bugs, tuning, unproven things)

One short list; newest first within a section.  Rules (see CLAUDE.md sec 2):
fix something -> delete or move its entry in the SAME commit; find something
and do not fix it -> add it here before the turn ends.  Fixed items are not
kept here (git log is the record).  Design history stays in HANDOFF.md and
ERISC_NOTES.md.

## Bugs

- mfmxctl `status` line prints api=legacy under firePHASER (mislabel).
- `create(50)` seed switch in `GridManager::seekRandomNonEmptySite` is computed
  but unused (the autoseed type follows `decay N`, default 5).

## Unproven on hardware

- ZHD counter zero-init (61cb6de): `#N` in HAR lines should now be small.
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
  1. MEASURE (SET UP, awaiting a run): hub marks .xb/.xe (PhaseSteppers.cpp
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

- FINDING (read from the code, not yet measured): CacheLoader.cpp busy-waits
  PACKET_SLEEP_CYCLES (was 10,000,000, a "DEBUG give hn a chance" literal;
  now named and set to 1,000,000 as the first experiment, commit after 2108b5e) after EVERY packet loaded or unloaded, in
  loadPacketIfNeeded and unloadPacketIfNeeded, serially on hart B.  A packet
  carries up to 255 atoms; a leader moves about 26 packets per phase, so
  the busy-wait alone could be most of the constant 1.8 s NEW_LEADER and of
  the 1.7-3.4 s RUN_EVENTS load.  If xchg.py shows per-packet time near
  70 ms, shrinking PACKET_SLEEP_CYCLES (one constant in CacheLoader.h;
  rebuild per value; try 1M, 100k, 0) is the cheapest speed-up there is,
  before any four-corners work.  Why the sleep is there is unknown: watch
  for lost or out-of-order cars (cars out == in == noc, BADC marks, bolt
  TIMEOUTs) when shrinking it.
- Bolt poll tick 0.05 s instead of 0.2 s (about 2 percent).
- Longer `dwell` (script only; physics trade-off).  `dwell` is the only knob
  for event time per cycle; duty is about 74 percent.
- `loadPacket` ships every site including EMPTY atoms (full cars regardless
  of occupancy): bandwidth/latency opportunity in the RUN_EVENTS cache load.
- AER phase 2: live mAER in the lower-left text area (tail the marks once
  per cycle; verify log files are flushed in real time first).
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
