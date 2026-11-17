# T6 -> E1 seam design (rough draft, session 5, 2026-11-16)

See also: /CLAUDE.md (start here, formatting rules), ERISC_NOTES.md (hardware record), ../python/EWD/HANDOFF.md (mfmxctl), ../../../scripts/ptrace/README.txt, ../../../scripts/cachecensus/cachecensus.py.

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

- Each hub has 8 InterHub EPs (myInterHubEP[Dir8]).  Dir4 EPs ship to the
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

Per hub and per Dir4 neighbor there are THREE destination storages (not two):
IHUB at the even (Dir4) index, CHUB (diagonal first hop), and IHUB at the odd
(corner) index (the CHUB forwarder's second hop).  12 per hub.  A seam-crossing
diagonal crosses exactly once: on the first hop if the CHUB is across, else on the
forwarder's second hop (corner diagonals in the 2x2 ring also cross a second seam,
at a different hub).  One E1 "channel" = an OUT ring + an IN ring, and the two
directions of a seam pack into the same channels, so channels needed per seam =
directed storages in ONE direction:

| seam           | Dir4 | diag, 1st hop | diag, 2nd hop | 2 cards | full ring (max) |
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

Build order: Dir4 crossings first (5 on x, 7 on y); diagonals afterward.

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
- Stage 3c: Dir4 InterHub EPs across the x seam (bhchips [0,1]); seam table;
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
   directed storages crossing one seam in one direction canonically (edge hub e = 0..E-1 from the north / west end; per hub DIR4, then the diagonal
   toward e-1, then toward e+1), and deal ordinal i to live link i mod L as channel i div L.  Link counts differ by at most one.
2. Seam table: there is no table to deliver.  The mapping (hub, direction, kind) -> (ETH tile, channel) is a pure function of the edge-hub count E and
   the live link list (<= 4 ETH x coordinates per seam, the only runtime data; it fits the HostBlock), computed identically by the host and the T6
   images from EthSeam.h.  Per-E1 data (which local T6 storage each channel's IN leg writes, peer ring addresses) differs per E1 and is written by the
   host into each E1's channel blocks at deploy, as the ethfwd tools do today; the E1 image stays one binary.  E1s are unicast anyway (ETH tiles cannot
   receive NoC multicast).  The T6 side's configureDest ETH branch (currently FAIL(INCOMPLETE_CODE), section 12) will compute
   mDestNoC0 = (ethX(link), 1) and mDestBlockAddr = outRingAddr(channel).
3. HAS_ETH_SEAM_UP: only at stage 3c, as proposed.
4. Channels per tile: a channel carries exactly one directed storage, so a seam needs 3E-2 channels (two cards; 3E for the full ring), E = edge hubs; the three
   kinds per hub and neighbor (ihub->ihub Dir4, ihub->chub first hop, chub->ihub second hop) are why it is not just E.  Dealt over L live links each E1
   serves ceil(N/L).  The image supports NCH_MAX = 8; the minimum needed is 4 (x) / 5-6 (y) per tile with four live links.  (The earlier "start with 2"
   applies only to a first stage with Dir4 crossings only.)

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

## 14. E1 management in the MFMx host (2026-11-16)

What exists (MFMx source; NOT run on hardware; native-tested with a mock pair of cards, ASan/UBSan clean; the python glue was NOT compiled where it was written):
- shared/include/EthSeam.h PART 3: CTL word offsets, channel-block word indices, E1 modes/flags/legs, image id range (e1fwd13..16).
- host/include/EthLinks.h, host/src/EthLinks.cpp: the ethfwd flows as a library with no pybind and no MFMx runtime dependency.  EthPort (L1/register access of
  an ETH tile; KmdEthPort = tt-kmd windows over a borrowed fd), EthLinkEnd (validate, queue 2, smoke test, deploy E1 with NOC_CLEANUP, channel setup, start/stop,
  reset, restore, health), EthLink (both ends: bring-up with rollback on any failure, teardown).  ONE write allowlist; a write outside it aborts.  Refusals before
  any write: not an ETH tile, harvested, link down, chip-info invalid, peers not each other's, queue 2 in use, E1 not in reset (override with force).  Channel setup is
  checked (channel < NCH_MAX, no duplicates, ring depth 1..4, and for IN_NOC legs the T6 must be a Tensix tile with a 16-byte-aligned base whose ring fits in L1)
  because the E1 will NoC-write 4 cars there.
- host/include/EthSeamHost.h, host/src/EthSeamHost.cpp: the per-process owner/registry (bringUpLink / tearDownLink / tearDownAll / status / health / channelStat),
  chip ports created on demand through an injected factory.
- host/include/EthSeamHostPy.h: pybind glue (module MFMx, class EthSeamHost); installs the factory (a KmdEthPort over Blackhole::getChipFD()) and a logger (HTprintf).
- Edits to existing files: Blackhole.h getChipFD(); Blackhole::closeChip() calls EthSeamHost::get().chipClosing(chip) BEFORE close(fd) (links torn down and TLB windows
  freed while the fd is still ours: an ioctl on a recycled fd number could hit another device); host/CMakeLists.txt gets the two new .cpp files; mfmx_wrapper.cpp registers
  the class.  Nothing in MFMx calls bringUpLink yet.
- Test: host/tools/ethlinks_test.cpp (see its header for the build line): happy path with exact CTL/channel-block contents and a full restore (soft-reset, PC/END, queue
  2, rings), seven refusal cases that leave the hardware untouched, force, eight bad channel setups, the image loader, and the EthSeamHost registry including chipClosing.

Deliberately NOT done: a Blackhole::Step HAS_ETH_SEAM_UP (a link is inherently two-chip, the per-chip step machine is the wrong place; the user agreed it is a stage-3c item,
and an explicit python call between "images deployed" and "startMachine" does the same job today), and deriving the per-channel T6 storage addresses (they come from
the image-block table once the T6-side wiring exists; until then the caller passes (channel, legs, t6x, t6y, t6base) explicitly).
Known limits: queue 2's RX buffer (L1 0x7E000) lies in the base-firmware reserved region (empirically free, unnamed in the map); KmdEthPort allocates up to 8 2-MiB TLB windows
per chip (two per ETH tile: L1 and registers), out of the budget left after OurTLBs' 144; the E1 dies with the fd, so the process owning the chip must outlive the links.

## 15. T6-side wiring for DIR4 crossings (2026-11-16)

What exists now (builds with the sfpi toolchain; NOT run on hardware):

1. Seam mask.  The host declares seams BEFORE deploying T6 images:
   `EthSeamHost.get().declareSeam(chipA, dir4, chipB, [ethx...])`
   (dir4: N=0 W=1 S=2 E=3, chipB lies that way from chipA; both
   directions are recorded).  Each x must be in the cabling list for the
   seam's axis (`EthSeam::SEAM_LINKS_EW` = 6,7,10,11 and
   `SEAM_LINKS_NS` = 3,4,12,13); link order is always the cabling order.
   `CodeManager::deployRISCVCodeFromImage` stores the result in the
   HostBlock's two former reserved bytes, now `mEthSeamLo/mEthSeamHi`
   (accessors `getEthSeamMask` / `setEthSeamMask`): 4 bits per Dir4,
   bit i = cabling-list entry i is live.  HostBlock layout and size are
   unchanged (offsets 30, 31; static_asserted in InterHub.cpp).  No
   declaration = zero = no seam.  The T6 decodes the mask with
   `EthSeam::decodeSeamMask`.
   CABLING IS COMPILED IN: the lists live in EthSeam.h and are in every
   T6 image and in the host.  A cable change (or another machine) means
   editing them and a full MFMx rebuild; the HostBlock carries no x
   values.
2. Hub (InterHub.cpp).  When a Dir4 neighbor hub is off this card and
   the table has links that way, `configureSeamInterHubEP` calls
   `EthSeam::destDir4` (balanced dealing: link and channel from the hub's
   edge index) and then `configureDest(ournoc0, ethNoC0, {BC_INTERHUB,
   channel})`.  `T6EP::configureDest`'s ETH branch now sets
   `mDestBlockAddr = EthSeam::outRingAddr(channel)`; the channel rides in
   `mBlockCodeIndex`.
3. In/out.  `weAreInDir4(d4)`: the end whose neighbor is E or S starts
   'in', the W/N end 'out', so the two ends always disagree.
4. Planner (host).  `EthSeam::recvDir4(senderTable, d4, link, channel,
   cell, out)` gives, for one channel of one sending E1, the CT6 of the
   receiving hub on the facing edge and the Dir8 index of its InterHub
   storage (2 * opposite Dir4).  `EthSeamHost::planLink` calls it once per
   end (each end describes where ITS incoming cars land), asks the hub
   lookup for each receiving hub's NoC0 and deployed BC_INTERHUB base,
   and fills `EthChannel` t6x/t6y/t6Base = hub NoC0, IBA base + recvDir8 *
   RING_BYTES, legs = OUT | IN_NOC.  `bringUpSeams(image)` does that for
   every live link of every declared seam and then `bringUpLink`; any
   failure tears down the links that call brought up.  The Python glue
   installs the lookup (`lookupHub` in EthSeamHostPy.h: the image deployed
   at the hub's TLBI, its BC_INTERHUB iba).  With a single live link the
   Dir4 channels run past NCH_MAX (channel numbers up to 11), so
   bring-up fails with a clear error; 2 or more links are needed with
   NCH_MAX 8.
5. Tests: `ethseam_test.cpp` (sender dest, host plan and far hub's own dest
   agree for every edge hub, every link count 1..4), `ethlinks_test.cpp`
   (declareSeam; planner and bringUpSeams on a mock E/W seam, checking each
   channel's T6 node and base against the hubs' own destDir4).

NOT done, on purpose:
- Diagonals (CHUB first hop, CHUB forwarder second hop) are still skipped
  at a card edge.
- The hub code still assumes 2x2 cells; CELL_C2X2 is passed explicitly.
- Cache loaders, leader/follower choice and supercell parity are still
  per-card (HubValue::init in CodeManager.cpp computes the supercell
  position from the chip-local hub coordinate; the 7-hub row width is odd,
  so a second card needs a global offset).  Cars will cross, but the
  grid logic above the InterHub EPs does not yet know about the other card.
- HAS_ETH_SEAM_UP: the order is declareSeam, deploy T6 images, bringUpLink
  for each live link, then release T6s.

## 16. Slice 1 of cross-card traffic (2026-11-17): Dir4 cache loaders, global parity

Hardware status before this slice (c942e6f): wiring proven, T6s idle, no
traffic, because every card was an island: a hub's cache loaders skipped any
neighbor off the card.  Slice 1 lets the Dir4 loaders (NT/WT/ST/ET) work
across a live seam.  NOT yet run on hardware.

1. Supercell parity is now GLOBAL.  Hubs are contiguous across chips (a chip is
   7 x 5 hubs, chip origins are multiples of that), the 2x2 supercell grid runs
   over all of them, and 7 and 5 are odd.  Counting per chip would put the
   first hub column of chip 1 in the same parity as the last of chip 0, so two
   touching hubs could both lead.  `DG::getChipHubOrigin(chip)` and
   `DG::getHubSuperCellCoord(chip, hubx, huby)` (DemoGlobal.h) count globally;
   hub coordinates may be negative or past the chip to name a neighbor on
   another chip.  Used by `GridManager::init` and `configureCacheLoaders` (T6)
   and `CodeManager::HubValue::init` (host; only the #if 0 host path reads it).
   Chip 0 is unchanged; chips 1-3 relabel which member of a supercell is
   which, which is harmless on their own.
   Test: `host/tools/hubparity_test.cpp` (no two Moore-adjacent hubs are both
   leader on 1, 2 or 4 chips for every leader code; the old per-chip rule fails
   it on 2 and 4 chips; exactly one leader per 2x2 hub block).
2. `CacheLoaders::configureCacheLoaders`: a neighbor off the chip exists iff
   the direction is Dir4 and `EthSeam::destDir4` says this hub has live seam
   links that way (the same test that creates the InterHub endpoint).
   Diagonal loaders across a seam stay UNUSED (they would need the CHUB
   forwarder seam variant; an unwired loader would never get its cars and
   stall the phase).  Consequence, deliberate and temporary: corner caches
   across the seam stay stale, so the physics is wrong in the 10 x 10 corner
   regions next to the seam, while Dir4 caches are exchanged.
3. `seams` prints the E1 channel counters of every live link end
   (`EthSeamHost.channelsOf`, `channelStat`): out = cars sent over the link,
   in = new cars seen from the link, noc = cars written into the local T6,
   plus txfail / nocfail / partial.
4. `config/seamcycle.cmd` runs the seamtest layout with SuperCycles.  Success:
   HOLD_AT_BIRTH / NEW_LEADER / RUN_EVENTS reach CONSENSUS on both chips, the
   out / in / noc counters move, and retrain and FEC uncorrected stay 0.  A
   bolt TIMEOUT means a loader is waiting for a car that did not come.

Next: slice 2, the diagonal seam crossings (corner-hub first hop and CHUB
forwarder second hop, SLOT_DIAG channels, planner support), then slice 3, the
N/S seam and the 2x2 chip array.

## 17. Slice 2 plan: the diagonal crossings (2026-11-17)

Status: the ROUTING is done and tested (EthSeam.h PART 5: diagRoute,
diagForwarderSlot, alongEdge, cornerOf; checked in ethseam_test.cpp against an
oracle that runs InterHub.cpp's own two-hop arithmetic over a global hub grid
and records every hop that crosses the chip boundary, for all four seam
directions).  WIRED (steps 1-4, untested on hardware): planner (recvDiag,
HubLookup.chubBase), T6 lookups diagSend / diagForwardDest in EthSeam.h
(checked natively against the planner's dealing), InterHub.cpp
configureSeamCornerEP + the forwarder's seam variant (CornerHubStateNC
mSeam), CacheLoader corner loaders across the seam.  In/out for a diagonal
pair is by corner (cornerIn: NW, SW in), since tlbis on two cards are not
comparable.  Step 5, the hardware run, is next.

What the oracle established.  A corner car makes two hops (hop 1: the source hub
H's endpoint ships to the CHUB block `b` of hub P, the hub CCW of the corner; hop
2: P's forwarder ships to the final hub F).  At a seam exactly one hop crosses:
- DIAG_FIRST_HOP (P is across): H's own endpoint writes to the E1; it lands in
  P's BC_CORNERHUB block, index = the seam direction as a Dir4.  East seam: the
  SE corner (slot HI).  West seam: NW (LO).  South seam: SW (LO).  North seam:
  NE (HI).
- DIAG_SECOND_HOP (P is the next hub along the edge): P's forwarder writes to the
  E1; it lands in F's InterHub storage at the corner index
  dir8ToCWDir8(dir4ToDir8(opposite(b))).  East seam: NE (LO), West: SW (HI),
  South: SE (HI), North: NW (LO).
- 2(E-1) diagonals per direction: 8 on the 5-hub E/W seam, 12 on the 7-hub N/S
  seam, as the section 4 table says.  Each is named by the SOURCE hub's edge
  index and slot, so PART 2's dealing applies unchanged; the forwarder finds its
  channel through diagForwarderSlot (exactly one of a hub's four CHUB blocks ships
  across a given seam).

To wire it (in this order; each step checked natively before the next):
1. Planner (host): HubLookup also returns the hub's BC_CORNERHUB base; planLink
   adds a channel per diagonal slot: first-hop channels land in a CHUB block
   (base + recvIndex * storage size), second-hop channels in an IHUB corner
   storage.  Extend ethlinks_test.cpp's mock fabric check the same way as for
   Dir4.  Channel counts with Dir4 + diagonals, balanced over L links: E/W (13
   storages) L=4: 4 channels per link, L=2: 7, L=1: 13 (> NCH_MAX 8, refused);
   N/S (19 storages) L=4: 5, L=3: 7, L=2: 10 (> 8, refused unless the E1 image is
   rebuilt with ETHSEAM_NCH_MAX >= 10).  So the N/S seam needs three or more live
   links with the current image.
2. InterHub.cpp, endpoint setup: a corner whose FINAL hub is off the card is
   skipped today by the "final dest exists" test.  Make that test seam-aware (the
   same diagRoute): first-hop crossings configure the endpoint toward the E1
   (configureDest ETH branch, dest EPA {BC_CORNERHUB, recvIndex}; the storage
   sizes are equal, so its RING_BYTES assert holds); second-hop crossings keep
   the normal on-card first hop to P's CHUB.
3. InterHub.cpp, manageCornerHubNC: the forwarder's open() fails off the card.
   Add the seam variant: when diagForwarderSlot says this block ships across,
   write to outCarAddr(channel, carindex) with NRI3::initiateWriteToEthL1 instead
   of the on-card IHUB address (CornerHubStateNC gets the ETH tile and channel).
4. configureCacheLoaders: enable the corner loaders across the seam where
   diagRoute exists (today only Dir4; diagonals `continue`).
5. Hardware: seamcycle.cmd again; corner caches next to the seam should now fill
   (atom census .an/.ae), counters on the diagonal channels move.

Open question to settle first: both hops crossing at a card corner shared by two
seams (the 2x2 array, slice 3); diagRoute returns false there by design.

## 18. Slice 3 scope: the N/S seam and the 2x2 array (2026-11-17)

State at the start: E/W seam (chips 0-1) works on hardware, Dir4 and
diagonals, caches filled across the seam (runs seamcycle11/12, seamlong10).
The planner, EthSeam.h helpers, InterHub.cpp and CacheLoader.cpp were written
for all four seam directions and the oracle test covers N and S, but only the
E/W case has run on hardware.

Cabling (section 4): N/S seams are cards 0-2 and 1-3 on ETH x = 3,4,12,13;
E/W are 0-1 and 2-3 on x = 6,7,10,11.  Only x=3 (N/S) has ever been exercised.
The two axes use disjoint tiles, so a chip with both seams runs up to eight E1s.
Chip n sits at array position (n%2, n/2): 0 top-left, 1 top-right, 2
bottom-left, 3 bottom-right.

### 18a. N/S seam alone (chips 0 and 2)   -- config + hardware only
No new design: the seam is the E/W code with D4_S/D4_N (weAreIn for S, 7 edge
hubs, 3E-2 = 19 channels, see section 4).  Work:
- layout `seamns` in nu1012.dtoml: bhchips [0,2], ethseams
  "0:S:2:3,4,12,13"; a command file `seamcyclens.cmd` (the seamcycle.cmd with
  the layout changed) and the usual counters.
- Channels per link with 4 live links: ceil(19/4) = 5; with 3: 7; 2 would need
  10 > NCH_MAX 8 and is refused with the existing NCH_MAX message.
- Unknowns that only hardware answers: do links 4, 12, 13 train and pass
  smoke; the parity of the vertical hub grid (5 hubs per chip, odd) -- the
  global supercell counting (DG::getHubSuperCellCoord) is tested for chips
  0..3 in hubparity_test, so a bolt TIMEOUT here would be a surprise.
- Verify with cachecensus.py: chip 0 bottom-row hubs (ct6 y=9) and chip 2
  top-row hubs (y=1) need ST/SW/SE and NT/NW/NE nonzero (the script's SEAM tag
  assumes E/W; extend it with a --seam option first).

STATUS 18a (2026-11-17): layout `seamns`, `config/seamns.cmd` (5-minute soak, run
with --rtmp), cachecensus.py `--seam CHIPA:DIR:CHIPB` verdicts, native planner
test for the S seam (Dir4 + 12 diagonals, 2-link refusal), mock selftest
scenario ethseams_ns.  Hardware run pending.

STATUS 18a: RAN ON HARDWARE (run 20261117-110545): 4 links up, 53 bolts, cars in==out
(4096), retrain/FEC uncorrected 0, all 14 seam hubs `ok` in cachecensus.

### 18b. 2x2 array, centre diagonals still off   -- layout + host validation
Chips 0-3, four seams.  Every chip has one E/W and one N/S seam, so its
HostBlock mask has two nonzero nibbles; each seam is independent, the T6 code
already reads one nibble per direction.  What is new:
- layout `seam4` (bhchips [0,1,2,3], ethseams "0:E:1:6,7,10,11",
  "2:E:3:6,7,10,11", "0:S:2:3,4,12,13", "1:S:3:3,4,12,13").  The planner
  already loops over declarations; check bringUpSeams with 16 links, the mock
  fabric test with four chips, and mfmxctl's `seams` report layout.
- At a card corner shared by two seams, diagSend / diagForwardDest return
  false (onA == onB, or the virtual source slot does not exist), so the four
  centre diagonals and their caches are simply unused: stale caches at the four
  centre corner hubs (the same as the E/W seam before slice 2), no hang.
  Need to CONFIRM that: the corner loaders are skipped on both ends (the
  census must show 0 in the centre corner caches only) and bolts reach
  CONSENSUS.
STATUS 18b (2026-11-17): layout `seam4` (seedchips = [0]: only chip 0 autoseeds, via
HBF_NO_AUTOSEED in the HostFlags byte, set by CodeManager from the env var
MFMX_AUTOSEED_CHIPS that machine.py derives from the layout; unset = every chip
seeds), `config/seam4.cmd` (10-minute soak), native 4-chip planner test (16
links, both nibbles per chip, all diagonals), mock selftest ethseams_four,
cachecensus judges every seam hub except the centre corner cache.  Hardware run
FIRST RUN (seam4.out, run 20261117-115630): bring-up failed at bh0:eth3, the
fifth tile of chip 0 -- KmdEthPort allowed only 8 TLB windows and uses two per
ETH tile (L1 and register page), so a chip with 8 links needs 16.  The
T6s were (correctly) not released, so the whole run idled: bolt #1
HOLD_AT_BIRTH TIMEOUT, all lamers.  NOT a seeding or seam-logic problem.  Fixed:
MAX_WINDOWS 24, clearer messages.  Rerun pending.  Expected: stale centre
corners, no hang.
- Chip-level work: tt-smi resets and fd/TLB bring-up for four cards (more of
  the same), hc/log volumes up 2x, slowscan slower.

### 18c. Centre diagonals (a corner hub of a 2x2 array)   -- the real design work
The corner hub H of chip 0 (SE) and the corner hub F of chip 3 (NW) are
diagonal neighbors across the array centre.  The two-hop route (section 17)
now crosses BOTH hops, each on a DIFFERENT seam, through the middle chip:
  hop 1  H -> P = hub E of H = chip 1's SW corner hub, CHUB block E (E/W seam
         0-1, direction 0 -> 1, written by H's own endpoint);
  hop 2  P's forwarder block E -> F = the hub S of P = chip 3's NW corner hub
         (N/S seam 1-3, direction 1 -> 3, written by P).
The four directed diagonals, 8 hop-channels in all:
  0 SE -> 3 NW   hop 1 on E/W seam 0-1 (0->1),  hop 2 on N/S seam 1-3 (1->3)
  3 NW -> 0 SE   hop 1 on E/W seam 2-3 (3->2),  hop 2 on N/S seam 0-2 (2->0)
  1 SW -> 2 NE   hop 1 on N/S seam 1-3 (1->3),  hop 2 on E/W seam 2-3 (3->2)
  2 NE -> 1 SW   hop 1 on N/S seam 0-2 (2->0),  hop 2 on E/W seam 0-1 (0->1)
So every seam gets exactly TWO extra directed storages (and they are the
virtual source slots the current dealing lacks: slot HI of the last hub and
slot LO of "hub E" on the corner that touches the centre; diagForwarderSlot
returns false there today because eSrc is out of range).
Design decisions to make:
1. Dealing.  Append the extras AFTER the existing ordinals (ordinal >= 3E-2),
   so every existing assignment is unchanged when the extras are absent:
   channelsTwoCards(E) = 3E-2 (+2 when the seam has centre diagonals).  E/W:
   15 channels, 4 links -> 4 each, 2 links -> 8 (exactly NCH_MAX), 1 link
   refused.  N/S: 21, 4 links -> 6, 3 -> 7, 2 refused.  Each extra costs a
   channel on both ends even though it is one-way (the E1 channel carries both
   rings); accepted.
2. Who knows a centre diagonal exists?  The HostBlock mask has no spare bit
   (16 bits = 4 directions x 4 links, and the HostBlock stays frozen).  Rule:
   a chip with BOTH an E/W and an N/S seam live assumes the centre diagonal
   at that corner, and the host REFUSES (declareSeam / bringUpSeams) any
   layout where a chip has two perpendicular seams but the four seams do not
   close the square.  This keeps the T6 side free of topology guesses.  An
   L-shape (three chips) is rejected with a clear message; it would otherwise
   hang (H sends hop 1, P has no live forwarder).
3. Routing code.  diagRoute / diagSend / diagForwardDest gain the card-corner
   case: crossEdge, recvEdge and recvIndex as in the hop table above, the
   forwarder slot named by a virtual (eSrc = E or -1) hub.  The oracle test
   (check_diag_seam) is extended from "two chips side by side" to the 2x2
   global grid and must find exactly these 8 boundary crossings (plus the
   2(E-1) per seam of section 17), for every live-link count.
4. The T6 forwarder at P (a card-corner hub) chains a car that arrived
   through an E1 into the OTHER axis's E1: the forwarder already reads from its
   CHUB storage and writes to an E1 (slice 2), so the only change is the
   channel lookup; no new data path.
5. Cache loaders at the four centre corners are then enabled through the same
   diagSend answer.  Check with .ac (the NE/SE/SW/NW cache at each centre
   corner hub).

### 18c SCOPE v2 (2026-11-17, after the 3b hardware run)
STATUS 18b: PROVEN (run 20261117-142417): 16 links up, retrain 0, uncorrected
FEC 0, all four chips full of atoms from the single seed on chip 0 (chip 3 only
through chips 1 or 2), 44 seam hubs ok, 72 bolts CONSENSUS, no TIMEOUT, cars
out == in == noc == 17586, txfail 0.  Stale centre corners do NOT hang a phase.

Corrections to the design above, found by tracing the code (EthSeam.h PART 5,
InterHub.cpp):
1. The extras are ONE-WAY and rotate.  InterHub always picks P = the hub CCW
   of the corner, so all four centre diagonals go clockwise round the array:
   0SE->3NW is 0->1 (E) then 1->3 (S); 3NW->0SE is 3->2 (W) then 2->0 (N);
   1SW->2NE is 1->3 (S) then 3->2 (W); 2NE->1SW is 2->0 (N) then 0->1 (E).
   So each seam carries its two extras in ONE direction only: 0-1 eastward,
   1-3 southward, 3-2 westward, 2-0 northward.  Each chip sends its extras on
   one seam and receives them on the other: chip 0 sends E / receives N, chip
   1 sends S / receives E (its W seam), chip 3 sends W, chip 2 sends N.  Rule
   the T6 and the host can both compute from the seam tables alone: an end
   SENDS the extras on seam d iff its table has a live seam nextCW(d), and
   RECEIVES them iff it has prevCW(d).
2. Names of the extras.  In the seam's own indexing the virtual source slots are
   (E-1, HI) and (E, LO) on the E and N seams, (0, LO) and (-1, HI) on the S
   and W seams.  Do not use those names in code: define the two extras by what
   they ARE, appended after the existing ordinals:
     ordinal 3E-2  CENTRE_FIRST   hop 1: the corner hub's own endpoint
                                  (hub at edge index E-1 on E/N seams, 0 on
                                  S/W seams) writes to the E1; lands in the
                                  CHUB block of the far corner hub, index =
                                  the seam Dir4.
     ordinal 3E-1  CENTRE_SECOND  hop 2: the forwarder at the corner hub P
                                  (same edge index), CHUB block prevCW(seam),
                                  writes to the E1; lands in the far corner
                                  hub's InterHub storage, corner index as in
                                  diagRoute's SECOND_HOP.
   channelsFullRing(E) = 3E is already exactly this count.  Existing
   assign() results are unchanged; only invert() and the channel count grow.
3. The E1 needs no change.  Channel legs are per end and per channel
   (e1fwd16.c: L_OUT / L_IN_NOC are tested separately), so an extra channel
   gets LEG_OUT only at the sending end (no T6 target) and LEG_IN_NOC only at
   the receiving end.  planChannel today gives every channel both legs and
   insists both ends have a receiver (it errors "seam ends disagree"); that
   is the one place that must learn one-way channels.
4. The T6 change is confined to EthSeam.h (header only):
   - diagSend: at a hub that is on TWO edges whose two seams are both live,
     currently false.  New: the corner Dir8 whose first hop (dir8CornerTo-
     CCWDir8Edge) is a live seam edge gives DIAG_FIRST_HOP on that seam with
     ordinal CENTRE_FIRST; any other corner there stays false.
   - diagForwardDest: when diagForwarderSlot fails and the hub is on both the
     seam nextCW(block) and the edge opposite(block), both live: CENTRE_SECOND
     on that seam.
   - InterHub.cpp, CacheLoader.cpp: NO change expected.  configureSeamCornerEP
     already handles a FIRST_HOP answer; the forwarder already falls back to
     diagForwardDest when chs.open() fails off-card; the loader already asks
     diagSend; parity already counts hubs globally in both axes (DemoGlobal.h).
5. Host: channelsTwoCards(E) (+2 when the seam carries extras) in
   planLinkLocked; recvCentre (host receive plan for the two ordinals) next to
   recvDiag; planChannel one-way legs; a closure check (see 6).  The mock
   fabric and the native planner test gain the centre channels.
6. Closure rule, now cheap: for every chip X with an E/W seam to Y and an N/S
   seam to Z, Y must have an N/S seam (same direction) to some W and Z an E/W
   seam to that same W.  Otherwise declare/bringUpSeams REFUSES with a message
   naming the chip (an L-shape would hang: H sends hop 1, P has no forwarder
   target).  The check uses only the declarations, so it also runs in the mock.
   Two-chip layouts (seam, seamns) have no perpendicular pair: unchanged.
7. Channel budget with extras (NCH_MAX = 8): E/W 15 storages: L=4 -> 4 per
   link, L=3 -> 5, L=2 -> 8 (just fits), L=1 refused.  N/S 21: L=4 -> 6,
   L=3 -> 7, L=2 refused.  seam4 uses four links everywhere, so it fits.

Verification gap that needs a decision.  The cache census counts non-empty
atoms, and the caches that face OFF the array are non-empty too (chip 3's south
caches show 1380 with no neighbor at all; the four centre corner caches already
show 90 with the diagonals unwired).  So .ac cannot tell a loaded centre corner
from a boundary fill, and 3c would "pass" the census before it works.  Options:
  (a) a per-cache content hash in .ac plus the same hash of the SOURCE region
      on the sending hub, compared offline at end of events (needs a T6
      logging change and a check that the cache is quiescent then);
  (b) a host-side check through the existing HostBlock dump path;
  (c) accept counters only: the diagonal channels' out/in/noc counters (the
      `seams` report) moving, which proves cars flow but not their contents.
This is the same question as the pending "do atom CONTENTS match across a
seam" item, so one solution should serve both.  Recommend (c) for 3c itself
and (a) as its own slice afterwards.

Work list (each step native-tested before the next; no hardware until 1-5):
  1. EthSeam.h: CENTRE_FIRST / CENTRE_SECOND, sendsCentre/recvsCentre,
     centreRoute, diagSend + diagForwardDest cases, invert() learns the two
     ordinals, recvCentre.
  2. ethseam_test.cpp: oracle on the 2x2 GLOBAL hub grid (today: two chips side
     by side); must find exactly the 8 centre boundary crossings on top of the
     per-seam 2(E-1); diagSend/diagForwardDest checked against the planner's
     dealing for every live-link count 1..4.
  3. EthSeamHost: channel count, one-way legs, closure check; ethlinks_test.cpp
     four-chip planner test (centre channels per link, legs, receiving hubs)
     and the L-shape refusal.
  4. Image rebuild only (T6 headers changed): run_native_tests.sh, mock
     selftest ethseams_four extended, config/seam4.cmd unchanged except its
     header comment.
  5. cachecensus: judge the centre corners too, but only as "non-empty" (see the
     gap above), and print the diagonal channel counters.
  6. Hardware: seam4.cmd again.  Expect everything of the 3b run plus: `seams`
     shows the extra channels moving cars on all four seams, one direction
     each; bolts still CONSENSUS.
Size: about the same as slice 2 (one header, two test files, one host file);
the T6 images only rebuild.  Main risk: a mistake in the one-way leg plumbing
would show as a stuck channel with txfail or partial counts rising on ONE
seam only; a wrong ordinal on one end would mis-land cars silently, which the
native oracle (step 2) is there to prevent.

### Order and what I need from you
a. 18a first (cheap, config only, exercises the untested N/S links and the
   parity), then 18b (the full array without centre diagonals, a real test of
   16 links and of "stale corners do not hang"), then 18c (design above).
b. Hardware questions: are the N/S and the chip 2-3 E/W cables all installed
   (the cabling table above says so, but only x=3 N/S was ever exercised)?
   Is it fine to run four cards under the 16-link bring-up (a longer boot)?
