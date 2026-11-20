# OPEN ISSUES (bugs, tuning, unproven things)

One short list; newest first within a section.  Rules (see CLAUDE.md sec 2):
fix something -> MOVE its entry to CLOSED_ISSUES.md (numbers kept, story cut)
in the SAME commit; find something and do not fix it -> add it here before
the turn ends.  Keep this file a concentrated dose of what needs work.  Design history stays in HANDOFF.md and
ERISC_NOTES.md.

## Bugs

- Display tiles freeze ("stuck" hubs): FOUND 2026-11-19 in the 3 h honest seam12
  (run 124337), diagnostics and a related fix added, cause of the stream break
  NOT yet known.  Four of 140 hubs stopped updating on screen: at1a3 (~1993 s run
  clock), at0e7 (~2575 s), at2a5 (~5698 s), at323 (~7783 s).  The hubs themselves
  kept running (mTotalChanges climbing in dumps 11-14, fill moving), AER stayed
  ~0.96.  Cause of the freeze: each hub's atom reports reach the host as an LZ
  stream (lzmfmx, 348 B window, no resync marker) decoded by ZHostDecompressor; the
  host counts reports and rejects any whose 16 bit spin differs from the count.
  After the break the tile logs show garbage records (HAR@(4048,20224), spin1 !=
  spin2) and the host report counter frozen (at0e7 #3270179, at2a5 #8387869, at323
  #11332202, at1a3 #804273); one lost/duplicated/corrupt compressed byte or packet
  does that and nothing recovers it.  Healthy hubs reached 13-20 M reports.  Rough
  rate: 4 breaks in 140 hubs in 2.2 h; the run ended at 10929 s (3 h 02 m) with the
  same four and no new one after 7783 s, so 4 in 3 h.  dumpl1 is NOT implicated (the four were
  never dumped before they froze; the first dump command was at023+at0a7).
  DONE (this commit): ZHostDecompressor detects it (spin check), logs FIRST-BAD with
  the last 48 decoded bytes and the last 16 packets (car, flags+len, start offset,
  head/tail bytes) in the tile log, declares DESYNCED after 20 in a row, and
  classifies it (kind 1: whole reports skipped, spins consistent; kind 2: garbage,
  byte misalignment); pybind `MFMx.zhdSnapshot()`; mfmxctl `zhd [all]`; a 10 s tick
  says and logs (misc/*-ZHD.txt) when the desynced set changes.  Also fixed: the
  host advanced its expected spin only when the atom's parity was good, so one
  report with a torn/INACCESSIBLE atom wedged every later report (kind 1 would show
  it); now the spin follows the transport and bad atoms are only counted
  (`bad atoms` in `zhd`).  RESTART BUILT (UNTESTED ON HARDWARE, hub image changed;
  test with config/seam15.cmd, which breaks hubs on purpose with `zhd break TAG`):
  the host asks in the empty cars it returns (flags RESTART_REQ + EPOCH, 6 bytes of
  feedback: its expected spin, packets, bad run); the hub's NC hart latches it
  before recvTC wipes the car, T0 finishes the record in flight and sets draining,
  T1's byte source ends the stream (EOF) once the ring is empty, the encoder
  flushes, the last car is EOF|CLOSABLE, the next SOF with the epoch echoed, T1 waits
  for it to ship, lzmfmx::reset(), T0 resumes; the host restarts its decoder thread
  on the matching SOF and adopts the hub's spin.  The tile freezes for about a second
  instead of for good.  `zhd restart off` stops the requests.  scripts/lzrestart/
  run.sh tests the compressor half on the host (restart works; without reset() the
  encoder HANGS).  Marks: hub `.zq:` asked, `.zd:` done.
  SEAM14 RESULT (2026-11-19, run 160322, 2 h, old detection build, no restart):
  3 of 140 hubs desynced: at029 (chip0 t99) packet 70464, report 5306382, kind 2;
  at2a3 (chip2 t21) packet 114229, report 8709980, kind 2; at225 (chip2 t43)
  packet 130967, report 9891230, kind 2.  All three broke inside the FIRST 4-6
  BYTES of a packet (compressed bytes in = packet start + 4, +4, +6), the decoded
  output was 20 B aligned, the atom of the bad record looked sane and the coord/
  spin that follow it were garbage.  Chance of that by luck ~ (6/990)^3, so the
  damage is at a packet JOIN: the head of a packet, or a byte lost/duplicated
  between the tail of one and the head of the next.  All packets were full (len
  990, flags 0).  at2a3 re-synced by luck twice (RESYNC after 1.7 M bad reports)
  and re-broke each time (same chip, same tile).  Rate: 3 in 2 h (4 in 3 h before).
  aer.py on the full 2 h run: wall 960.2, running 987.7 mAER, flat after 1 h.
  SEAM15 RESULT (run 202246, 12 min, restart + trailer build): RESTART WORKS on
  hardware.  at0a7 (injected twice, epochs 1 then 0) and at063 (chip0 t19, NATURAL
  break at packet 1136, report 87649, 5 bytes into the packet again) all desynced,
  were asked to restart, answered with SOF and resumed within ~10-30 s; zhd ended 0
  desynced, 3 restarts, hub marks .zq/.zd paired.  at323 and at2e9 were also named
  in `zhd break` but no TEST line appeared for them (unexplained: check the tag ->
  (chip,tlbi) mapping with `zhd break` interactively).  The natural break had
  0 bad checksums and 0 bad seq over 1136 packets: the packet arrived exactly as the
  hub sealed it, so the damage is NOT in transit.  Remaining suspects: hub before
  packetizing (T0->T1 ring, sink/car swap bookkeeping) or the decoder.  scripts/
  lzrestart/stress.sh (8 x 25 x 32 MB of report-like records with random source
  blocks and sink refusals): no mismatch in ~6 GB, so lzmfmx logic itself looks sound.
  SEAM14-11 RESULT (2 h, trailer build): 2 natural breaks (at2e3 chip2 t25 packet
  5981, at123 chip1 t15 packet 78496), both restarted OK; 0 bad checksum, 0 bad seq;
  first bad record 11 and 1 byte into the packet, so the damage is at the packet
  join, before the hub sealed it.  Host reader, sink retry and lzmfmx reviewed: no
  fault.  Trailer sum is now a write-time SHADOW sum (see CLAUDE.md) to catch memory
  damage between sink and seal; next break should say which side of that it is.
  SEAM14-12 RESULT (2 h, shadow-sum build, run 004004): ONE natural break (at0e7
  chip0 t81, packet 78458, 7 bytes into the packet, restarted OK).  The break
  packet had `ZHD PKTSUM BAD CHECKSUM` (seq 12921 = 78457 mod 65536, so the seq
  was right): the car's bytes differ from the hub's write-time shadow sum.  So
  the damage is a changed byte INSIDE the packet after the sink accepted it
  (hub memory/seal, NoC/transit, or the host copy), not a bad encoder input.  No
  `.SHADOWBAD` mark was in the dump (not proof the seal check passed: marks can
  wrap or not be exported).  The PKTSEQ at the next packet was a host artifact
  (mNextSeq was not advanced past a bad-sum packet): fixed.  Next run adds
  `ZHD PKTWHY` (trailer sum vs arrived sum, and for a single changed byte its
  offset: Fletcher da/db) + `ZHD PKTDUMP` of the whole first bad packet per hub,
  and a hub `.zs` mark (mShadowBad) next to `.zq`.  Compare the dump with the
  other bad-looking bytes; if the hub saw the car fine at seal, suspect transit
  or the host copy; if `.zs` > 0, hub memory/seal.
  SEAM14-13 RESULT (2 h, 5794297, run 030333): 4 natural breaks (chip2 t101,
  chip2 t109, chip3 t51, chip3 t129), all restarted OK.  EVERY break packet had
  PKTSUM BAD CHECKSUM, seq right, and each hub's `.zs` = 1: THE HUB ITSELF saw
  the car differ from the bytes the sink accepted, at seal time.  So the damage
  is on the hub, between sink accept (T1 write under mLock) and T0's seal;
  not transit, not the host, not the encoder.  The first bad record is 4, 4, 9,
  3 bytes into the packet.  The single-byte offsets that fit the Fletcher da/db
  are all >= 35 (not near the start), so it is NOT one changed byte: several
  bytes, probably near the packet start, changed.  Candidate mechanisms: a
  second writer into the car memory (a late/duplicate host return packet
  landing in a car the hub is already filling: the host stamps 6 feedback bytes
  at offset 0 and the flags word in every returned car, stampFeedback), or the
  NC hart's recvTC wipe (openTC) of a car index that is also the current car.
  The packet's first bytes in PKTDUMP do not equal the host's feedback fields at
  the time, but the feedback was stamped at an earlier return.  Next build
  keeps a byte copy of what the sink accepted (mShadowCar) and at seal, if the
  sum differs, logs hub marks `.zx` (seq:datalen:bytes differing:first offset)
  and up to five `.zy` (offset:meant:found).  Read those against the host's
  feedback struct (u16 spin, u16 seq, u16 consec at offset 0) and the flags word.
  NEXT RUN carries a per packet seq+checksum trailer (CLAUDE.md PACKET TRAILER):
  it separates damage in the packet from damage before packetizing.
  STILL OPEN: the cause (a) look at the car handoff at a packet join, hub T0 close
  (closeTC runs outside mLock) vs T1 writeByte, and the host copy of the payload
  head; (a0) NEW: the restart build should show whether the break repeats at a
  join (its FIRST-BAD block prints the same offsets); (a)
  read the FIRST-BAD lines of the next occurrence; (b) find the
  loss (hub byte/packet overwritten before the host reads it, or a host-side
  race).  Also compare the hub's mCompressedBytesPacked in a dump with the host's
  compressed bytes consumed.  Note for l1grid.py: theT6Grid is at 0x7274c in the
  honest build (0x7276c was the older build): pass --map from the run's own build.

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
- mfmxctl `status` line prints api=legacy under firePHASER (mislabel).
- `create(50)` seed switch in `GridManager::seekRandomNonEmptySite` is computed
  but unused (the autoseed type follows `decay N`, default 5).

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
    AER sites stay 138x108 per hub.  Edge events now run over partly
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
