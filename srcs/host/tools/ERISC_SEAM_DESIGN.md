# T6 -> E1 seam design (rough draft, session 5, 2026-11-16)

STATUS: rough design only.  Nothing here is built on hardware.  Written from a read of
cross/include/T6EPs.{h,tcc}, cross/src/NRIUtils.cpp, cross/images/hub/src/{InterHub,
EP_InterHub}.cpp, shared/include/{UxC,MDist}.h, tools/e1fwd/e1fwd11.c and the layout
in python/EWD/config/nu1012.dtoml.  Items marked CHECK were not read or verified;
UNVERIFIED are hardware assumptions.

## 1. Goal and the big simplification

Complete a host-free car path across a card seam:

    T6(A) --NoC--> E1(A) OUT ring --TXQ2/link--> E1(B) IN ring --NoC--> T6(B)

and the mirror path for the same EPs (cars come back the same way).  Most of the
pieces exist: E1 FWD (OUT ring -> link) and E1 NOCFWD (IN ring -> NoC -> T6) both
pass on hardware.  What is missing is: (a) the T6 can NoC-write a car to an ETH tile,
(b) the T6 knows where, (c) one E1 image that runs both legs on many channels at
once (e1fwd13), (d) the host plumbing that wires addresses together.  Flow control
needs nothing new: the car exchange already is the flow control (ERISC_NOTES section
4), and the E1 stays a transparent wire.

## 2. Layout facts (from nu1012.dtoml, ewpandhub, cell C2X2_EWP_HUB)

- A card is 14 x 10 T6 (CT6).  Supercells are 2x2, HUB at cell (1,1): 7 x 5 = 35 hubs
  at CT6 x in {1,3,5,7,9,11,13}, y in {1,3,5,7,9}; NoC0 x in {2,4,6,10,12,14,16},
  y in {3,5,7,9,11}.  There are no hubs in CT6 column 0: the far-side neighbor hub
  across a seam is two CT6 columns away and the supercell tiling is continuous.
- bhchips selects the cards.  With bhchips = [0] no ERISC traffic is needed.  The
  x seam needs [0,1] (or [2,3]); the y seam needs [0,2] (or [1,3]).
- East/west edges have 5 hubs, north/south edges 7.  Each seam has 4 links.

## 3. How the T6 side works today (what we hook into)

- Each hub has 8 InterHub EPs (myInterHubEP[Dir8]).  Cardinal EPs ship to the
  neighbor hub's BC_INTERHUB storage at index oppositeDir8.  Diagonal (corner) EPs
  ship to the BC_CORNERHUB (CHUB) block of the orthogonal neighbor CCW of the
  corner (block index = that edge direction as Dir4).  Both the final hub and the
  first-hop hub must be "on board": InterHub.cpp tests onBoardCT6Coord, i.e. THIS
  CARD ONLY, so no cross-card EP can be configured today.
- CHUB forwarder (InterHub.cpp manageCornerHubNC, ~line 296): for each CHUB block i,
  every new complete car (nonce changed) is NoC-written (NRI3::initiateWriteToT6)
  to the neighbor hub in nextCWDir4(i), into its IHUB storage at index
  dir8ToCWDir8(dir4ToDir8(oppositeDir4(i))) (a CORNER index), same car slot.  It
  calls toU8COrDie() on the neighbor and opens via ngb.init/findIBAIfAny, so an
  off-card neighbor dies/fails there.  The forwarder is one-way: ownership comes
  back over the opposite diagonal EP and a different midpoint hub.
- T6EP::configureDest has a PCIe-tile branch and a "dest is T6" branch (reads the
  dest tile's ImageBlockHeader over the NoC).  T6EP::shipTCTo branches on
  isNoC0CoordAT6 / PCIeTILE_NOC0 else "NO-DEST?".  An ETH tile is NoC0 y=1, so
  isNoC0CoordAT6 rejects it.  InterHubEP::shipTC computes
  destcaraddr = mDestBlockAddr + CAR_SIZE*carindex and logs .ps first.
- E1 ring geometry already matches: InterHubStorage = 4 cars of 4160 B (0x1040),
  and the E1 rings are 4 slots of 0x1040, slot index = car index.

## 4. Destinations and channel counts

Per hub and per cardinal neighbor there are THREE destination storages (not two):
IHUB at the even (cardinal) index, CHUB (diagonal first hop), and IHUB at the odd
(corner) index (the CHUB forwarder's second hop).  12 per hub.  A seam-crossing
diagonal crosses exactly once: on the first hop if the CHUB is across, else on the
forwarder's second hop (corner diagonals in the 2x2 ring also cross a second seam,
at a different hub).  One E1 "channel" = an OUT ring + an IN ring, and the two
directions of a seam pack into the same channels, so channels needed per seam =
directed storages in ONE direction:

| seam           | cardinal | diag, 1st hop | diag, 2nd hop | 2 cards | full ring (max) |
|----------------|----------|---------------|---------------|---------|-----------------|
| x (5 edge hubs)| 5        | 4             | 4             | 13      | 15              |
| y (7 edge hubs)| 7        | 6             | 6             | 19      | 21              |

Capacity: 4 links per seam.  e1fwd11 supports 4 channels per tile = 16 per seam: x
fits, y does NOT.  The 4 is an array size, not hardware.  By arithmetic (UNVERIFIED
until built): a ring is 16640 B, a channel (OUT+IN) ~33 KB; L1 from 0x15B40 to
0x70000 holds ~11 channels per tile.  Plan: 8 channels per tile (NCH_MAX) in e1fwd13;
the y seam then needs 3 tiles of 7 (21 <= 32).  Poll cost ~40 ns per ring head,
so 8 channels ~0.65 us per sweep, same order as the 0.56-0.68 us hop latency and
tiny against ms-scale car exchange.  Link bandwidth is not a constraint.

Build order: cardinal crossings first (5 on x, 7 on y); diagonals afterward.

## 5. T6-side changes

1. UxC.h: add isNoC0CoordAnETH(UxC) (y == 1, x in 1..7 or 10..16).
2. T6EP::shipTCTo: new branch for ETH destinations calling NRI3::initiateWriteToT6
   unchanged.  CHECK: isInL1() accepts an ETH L1 address; preferNoC() handles y=1.
   Keep the .ps mark.
3. T6EP::configureDest: ETH branch; skip findBlockCodeInNoC0 (no ImageBlockHeader on
   an ETH tile) and set mDestBlockAddr to the E1 OUT ring base of the channel.
4. InterHub.cpp EP setup: replace the two onBoardCT6Coord "no dest" tests with a test
   against the ASSEMBLED multi-card grid; when the first-hop hub is off-card use the
   seam table to pick (ETH tile, channel) and configureDest to that.
5. manageCornerHubNC (CHUB forwarder): seam variant of open() and of the shipping
   step.  When the destination hub is off-card, replace toU8COrDie/initiateWriteToT6
   with a write to the seam table's (ETH tile, channel) for (dest dir, kind IHUB-odd).
6. New shared header shared/include/EthSeam.h: ONE copy of the layout (CTL/channel
   block offsets, ring size, NCH_MAX, CAR size).  T6 images, E1 image and host tools
   all include it.  (Today the layout lives as #defines in e1fwd11.c.)
7. Seam table delivery to the T6s: HostBlock-style L1 write at deploy (precedent:
   host chunk address); keyed by (our hub, Dir4 neighbor, storage kind, idx) ->
   (ETH node, channel).  The table can be GENERATED by the Python config layer from
   bhchips and the cell layout (it already knows both).  CHECK HostBlock capacity.

## 6. The bidirectional ERISC router (e1fwd13)

Per channel c (config in the channel block, filled by the host):
- OUT leg: local T6 NoC-writes car i into OUT[c][i]; E1 sends it to the peer E1's
  IN[c][i] over TXQ2 (<= 1456 B chunks).
- IN leg: a new complete car in IN[c][i] is NoC-written to the local T6 storage slot
  i of the channel's (T6 node, T6 base).  A COUNT-only leg lets the host play the hubs.
- Per-channel (node, base) replace the single CTL node/base of e1fwd11 mode 7.
- Both legs run in one sweep over up to 8 channels, bounded to one ring's worth of
  cars per leg per visit (no channel can starve the others).
- Complete = header == ankle == footer; "new" = header != last forwarded for the slot.
  Per-slot last-header arrays live on the stack (images have no .bss); the host must
  ZERO the rings before release or a stale complete car is forwarded as new (L1 is not
  zeroed at boot).
- Diagnostics: per-channel counters incl. a "partial sightings" count (valid marker,
  incomplete) to help study the two-stream ordering question.
- Ring/CTL addresses are host-chosen (CTL base overridable at build time) so the image
  can move above tt-metal's reserved region later without a code change.
- Source reuse is safe by protocol: a ring slot is not rewritten until the far end
  has returned the car, long after the posted NoC write has been issued/read.
Versions are never reused: this is e1fwd13.c (its host tool will be ethfwd14: ethfwd13 became ethfwd12 + --image/--boot-only).

STATUS of e1fwd13 (written 2026-11-16, not yet run on hardware): e1fwd/e1fwd13.c and
the native harness e1fwd/hosttest13.c exist.  The harness (gcc -O2 -Wall -pthread
hosttest13.c, no riscv toolchain needed) runs two routers as threads with fake L1s,
link and per-channel fake T6s and passes for six car sizes (tcms 0,1,2,3,10,66): 6
full-duplex channels, a one-way channel, a count-only channel (T6 untouched),
duplicate suppression, and the once-per-occupancy partial counter.  Mutation checks
(duplicate suppression removed; NoC destination offset) make it fail as they should.
The RISC-V image builds cleanly with the sfpi gcc toolchain (`sh e1fwd/build.sh e1fwd13`,
2792 bytes, no .data/.bss) but has not been run.  NOT done: a host tool, ethfwd14,
that speaks the new channel-block layout: CHC+c*0x80 with config words 0..5 and stats
at words 8..23, CTL window 0x600 bytes); no hardware run; no fairness test (the
per-visit bound makes starvation impossible by construction, not by test).

## 7. Address plumbing (who knows what, when)

The sender cannot discover the remote T6's L1 layout.  So the HOST fills it in:
- After both cards' T6 images are deployed, per channel it computes the far T6 storage
  base (IBA table: iba.mBlockAddr + blockindex*storage size; BC_INTERHUB and
  BC_CORNERHUB both appear in the IBA table) and writes (node, base) into the LOCAL
  E1's channel block for the IN leg, and the peer's IN ring address (TXQ2 DEST) for
  the OUT leg.
- E1s launched and rings zeroed BEFORE T6s are released (HAS_T6_CODE_RUNNING).
  Proposal: a Blackhole::Step HAS_ETH_SEAM_UP, but only at stage 3c; 3a/3b use
  standalone tools.
- E1 lifetime: it dies when the owning fd closes (NOC_CLEANUP).  mfmxctl is one
  long-lived process, a natural owner for now (ERISC_NOTES open item 4).

## 8. Risks and things to CHECK

1. Header uniqueness: read in TCBase.h: setDepartingTC does `mTCMNonce++` on every
   departure of a car, so consecutive occupants of one ring slot carry different
   header words (an 8-bit nonce wraps only after 256 departures of that car, far more
   than one E1 sweep).  A resend after a failed shipTC repeats the same header, which
   the E1 correctly treats as one car.  spoilCompletePacket (nonce--) is disabled in
   the CHUB forwarder; keep it that way.  The CHUB forwarder uses the same
   nonce-changed test.  (Resolved by reading, not by test.)
2. Two-stream ordering (ERISC_NOTES section 3, 8.2): the stream split is by address
   (16 B granules alternating, 32 B period; assumed the same on BH T6 and ETH tiles per
   TT engineers via the Discord bot).  For a single-packet write, header == ankle ==
   footer proves both streams complete (derivation in ERISC_NOTES section 3), so the
   NoC leg into E1 L1 is covered by the same argument if the car goes as ONE NoC packet
   (4160 B is far under the 16 KiB single-packet NoC limit that MFMx embraces everywhere).  Still
   UNVERIFIED: cross-packet landing order on the ETH hop (a 4160 B car is 3 TT-link
   chunks).  Keep the sequence-stamped 4160 B stress
   test and try NOC_CMD_VC_STATIC on the NoC leg.
3. Posted writes: same as T6->T6 today; ack-counted writes would harden the hop.
4. Corner diagonals that cross two seams are two independent seam crossings at
   different hubs; nothing special for the E1.
5. tt-metal region overlap (ERISC_NOTES section 7) applies unchanged.

## 9. Staged plan

- Stage 3a: one-way T6 -> E1 -> link -> E1 -> T6 using the router image with legs
  split by card (A: OUT leg, B: IN leg).  New: a small sfpi T6 test image on card A
  using the new shipTCTo branch, and ethfwd14.
- Stage 3b: both legs both ways; two T6 test images ping-pong cars across the seam.
- Stage 3c: cardinal InterHub EPs across the x seam (bhchips [0,1]); seam table;
  HAS_ETH_SEAM_UP; ptrace learns the E1 hop.
- Stage 3d: y seam, diagonals (CHUB forwarder seam variant), more channels per tile,
  SuperCycle across cards, link retrain/FEC monitoring.

## 10. Questions for the user

1. Which edge hub rows/columns use which ETH tile and channel (policy; the table
   generator in Python can encode whatever you pick)?
2. Seam table delivery: HostBlock-style L1 write (recommended) or image block field?
3. HAS_ETH_SEAM_UP step: only at stage 3c (recommended)?
4. Channels per tile: 8 in e1fwd13 (this draft) with the first tests using 1-2.


## 11. Status update (2026-11-16, later)

Hardware (gcc images, 800 MHz): e1fwd13/e1fwd14 run on the QuietBox with ethfwd14/15: route (host as hubs) PASS on 2 and 8 channels
at 64/576/4160 B, reflect PASS with payload verification, fair across channels, ~4 M cars/s per E1 hop-limited (ERISC_NOTES 9.5, 9.6).
Prepared but NOT yet run: the NoC leg on the multi-channel image (ethfwd16 --noc, expts11.sh) and a VC_STATIC A/B (e1fwd15).
Still NOT done (needs decisions, section 10): T6-side ETH destination (shipTCTo currently dispatches only to a T6 coordinate or the
PCIe tile: see T6EPs.tcc, a third branch for an ETH tile NoC0 coordinate with the E1 OUT-ring address would do), seam table and
edge-hub policy, HAS_ETH_SEAM_UP.  The E1 is not the throughput bottleneck for plausible seam traffic (est.), so those come first.

## 12. The T6-side ETH-destination patch (2026-11-16): shipTCTo + NRI3::initiateWriteToEthL1

Change (commit "T6 side: ETH-tile destination ..."; MFMx source, deliberately its own commit so it can be dropped or reworked):
- shared/include/UxC.h: `UxC::isNoC0CoordAnETH(c)` (NoC0 row y==1, columns 1..7 and 10..16), sibling of isNoC0CoordAT6.
- cross/include/NRIUtils.h, cross/src/NRIUtils.cpp: `NRI3::initiateWriteToEthL1(sourcenoc0, src, wordCount, destnoc0, destaddr)`: the
  initiateWriteToT6 register sequence (NIU cmd buf 3, hart NC) with ETH-appropriate asserts (dest must be an ETH coord, destaddr < MEM_ETH_SIZE,
  16-byte alignment rule) and ALWAYS NoC0 instead of preferNoC().  Why NoC0 only: preferNoC picks the shorter of NoC0 (E,S) / NoC1 (W,N); an ETH
  tile is north of every T6, so NoC1 would be chosen most of the time, and NoC1 -> ETH tile has NOT been exercised on hardware (stage 3a used NoC0
  only).  MFMx passes NoC0 coordinates to NIU1 as well, which works for T6 destinations because firmware's coordinate translation makes both NoCs
  uniform (user notes, Dec 2025); whether that translation covers the ETH row identically is untested.  An opt-in NoC1 experiment (t6pp control
  word for the NIU base + ethfwd flag) would settle it; then the function could use preferNoC.
- cross/include/T6EPs.tcc: shipTCTo gets a branch for ETH destinations before the PCIe-tile branch (same wordCount/status handling);
  configureDest gets a branch that FAILs with INCOMPLETE_CODE for an ETH destination, because the T6 branch would read a remote T6's image block
  from an ETH tile (garbage).  The missing piece is exactly the seam table: mDestBlockAddr (E1 OUT ring address of the right channel and slot
  base) and mDestNoC0 (the ETH tile) must come from it (sections 5, 7, 10).  Nothing calls configureDest with an ETH coordinate today, so existing
  behaviour is unchanged.
Verification: the unmodified tree and the patched tree both build (hub, ewp) with the sfpi toolchain in a scratch copy (cmake -S . -B build_cross
-DPRIMARY_CMAKE_RUNNING=1 -DCMAKE_TOOLCHAIN_FILE=srcs/cross/toolchain-cross.cmake; the post-build `tee` into ../CROSS_BUILD_HISTORICAL needs that
directory to exist).  Code growth: hub.bin text +2032 B (146604 -> 148636), ewp +1600 B (83952 -> 85552); factoring the common NoC sequence of the two
write functions into one helper would roughly halve that if code size matters.  NOT run on hardware: the NoC sequence is the one t6pp2 proved
(ERISC_NOTES 9.11); the dispatch and the new function are unexercised until a hub image sends to an ETH tile.

## 13. Decisions of 2026-11-16 (user + session) and shared/include/EthSeam.h

Answers to the section 10 questions:
1. Which ETH tile / channel: BALANCED DEALING for both seams.  A literal "nearest ETH tile" works for the y seam but not the x seam (all ETH tiles are
   in NoC row y=1, so for the east/west edge hubs the distance is dominated by x and nearly equal; nearest would pile the 13 channels onto one tile),
   and tile position does not matter for latency anyway (ERISC_NOTES 9.11: every tile pair gave 2.59-2.61 us).  The rule (EthSeam.h): number the
   directed storages crossing one seam in one direction canonically (edge hub e = 0..E-1 from the north / west end; per hub CARDINAL, then the diagonal
   toward e-1, then toward e+1), and deal ordinal i to live link i mod L as channel i div L.  Link counts differ by at most one.
2. Seam table: there is no table to deliver.  The mapping (hub, direction, kind) -> (ETH tile, channel) is a pure function of the edge-hub count E and
   the live link list (<= 4 ETH x coordinates per seam, the only runtime data; it fits the HostBlock), computed identically by the host and the T6
   images from EthSeam.h.  Per-E1 data (which local T6 storage each channel's IN leg writes, peer ring addresses) differs per E1 and is written by the
   host into each E1's channel blocks at deploy, as the ethfwd tools do today; the E1 image stays one binary.  E1s are unicast anyway (ETH tiles cannot
   receive NoC multicast).  The T6 side's configureDest ETH branch (currently FAIL(INCOMPLETE_CODE), section 12) will compute
   mDestNoC0 = (ethX(link), 1) and mDestBlockAddr = outRingAddr(channel).
3. HAS_ETH_SEAM_UP: only at stage 3c, as proposed.
4. Channels per tile: a channel carries exactly one directed storage, so a seam needs 3E-2 channels (two cards; 3E for the full ring), E = edge hubs; the three
   kinds per hub and neighbor (ihub->ihub cardinal, ihub->chub first hop, chub->ihub second hop) are why it is not just E.  Dealt over L live links each E1
   serves ceil(N/L).  The image supports NCH_MAX = 8; the minimum needed is 4 (x) / 5-6 (y) per tile with four live links.  (The earlier "start with 2"
   applies only to a first stage with cardinal crossings only.)

Geometry (cell size) dependence.  The seam layer depends on the cell geometry ONLY through E: E/W seams have one edge hub per cell row (10 / cell height),
N/S seams one per cell column (14 / cell width), with one hub per cell.  EthSeam.h takes E as a parameter.  Table (ethseam_test.cpp, which also checks
bijection, assign/invert round trip and balance), channels per link with 4 live links, two cards (full ring in brackets):
  2x2: E/W E=5  4 (4);  N/S E=7  5 (6)       2x1: E/W E=10 7 (8);  N/S E=7 5 (6)
  1x2: E/W E=5  4 (4);  N/S E=14 10 (11)      1x1: E/W E=10 7 (8);  N/S E=14 10 (11)
All fit NCH_MAX = 8 except N/S for 1x2 and 1x1 (10-11 per tile), which need the image rebuilt with NCH_MAX 11 (EthSeam.h accepts ETHSEAM_NCH_MAX up to 11 and
grows the CTL window; 12 trips a static_assert because the rings would run into the syseng region).  With only 3 live links the y seam of 2x2 needs 7 (ring 7),
with 2 live links 10-11 > 8.
IMPORTANT, NOT part of the seam layer: the existing hub code is hard-wired to 2x2 regions.  cross/images/hub/src/InterHub.cpp:32-34 sets
uct6_per_ureg(2,2) / ureg_per_uscl(2,2) and does HBASSERT_EQ(ourstride, S8C(uct6_per_ureg)) ("confirm naming"), and cross/images/hub/src/CacheLoader.cpp:28-29 has the
same constants.  A 1x2 or 2x1 (or 1x1) cell layout would trip that assertion in the existing images regardless of the seam, so generalizing those constants (they are
derivable from CellBlock mCellStride / mCellSize, which the image already carries) is a prerequisite for any non-2x2 geometry.  Not audited: every other place (python
visualisation, host config) that may assume 2x2 or 7x5 hubs.
