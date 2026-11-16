# T6 -> E1 seam design (rough draft, session 5, 2026-11-16)

STATUS: rough design only.  Nothing here is built or tested.  Written from a read of
cross/include/T6EPs.{h,tcc}, cross/src/NRIUtils.cpp, cross/images/hub/src/{InterHub,
EP_InterHub}.cpp, shared/include/UxC.h and tools/e1fwd/e1fwd11.c.  Items marked
CHECK are things I did not read or verify; UNVERIFIED are hardware assumptions.

## 1. Goal and the big simplification

Complete a host-free car path across a card seam:

    T6(A) --NoC--> E1(A) OUT ring --TXQ2/link--> E1(B) IN ring --NoC--> T6(B)

and the mirror path for the same EPs (cars come back the same way).  Most of the
pieces exist: E1 FWD (OUT ring -> link) and E1 NOCFWD (IN ring -> NoC -> T6) both
pass on hardware.  What is missing is only: (a) the T6 can NoC-write a car to an ETH
tile, (b) the T6 knows where, (c) one E1 image that runs both legs at once, (d) the
host plumbing that wires addresses together.  Flow control needs nothing new: the
car exchange already is the flow control (ERISC_NOTES section 4), and the E1 stays a
transparent wire.

## 2. How the T6 side works today (what we hook into)

- InterHub.cpp (HTOC config): for each Dir8 it finds the neighbor hub's NoC0 coords
  via `ngb.init(ournoc0, ngbct6off)`, then
  `ihep.configureDest(ournoc0, firstDestHubNoC0C, {blockCode, firstDestBlockIndex})`.
  An off-board neighbor takes the `!NGB@` path: the EP is never initted and never
  ships.  That is exactly the seam case, so the seam hook goes there.
- `T6EP::configureDest` has two branches: PCIe tile (host chunk address) and "dest is
  T6" (NRI3::findBlockCodeInNoC0 reads the dest tile's ImageBlockHeader over the
  NoC, then mDestBlockAddr = iba.mBlockAddr + blockindex*storage size).
- `T6EP::shipTCTo` (T6EPs.tcc) branches on `U8C::isNoC0CoordAT6(destnoc0)` (->
  NRI3::initiateWriteToT6) and `destnoc0 == PCIeTILE_NOC0` (-> initiateWriteToHost);
  anything else falls to "NO-DEST?".  An ETH tile is NoC0 y=1, so isNoC0CoordAT6
  (y in 2..11) already rejects it.  InterHubEP::shipTC computes
  `destcaraddr = mDestBlockAddr + CAR_SIZE*carindex` and logs `.ps` before shipTCTo.
- The E1 ring geometry already matches: InterHubStorage = 4 cars of 4160 B (0x1040),
  and the E1 rings are RING_CARS=4 slots of CAR=0x1040, slot index = car index.

## 3. T6-side changes

1. `UxC.h`: add `isNoC0CoordAnETH(UxC)` (y == 1, x in 1..7 or 10..16).  Harvested
   columns (E5, E8) should not be addressed; the seam table (below) only ever holds
   coordinates the host verified, so a coarse predicate is enough.
2. `T6EP::shipTCTo`: new branch `else if (U8C::isNoC0CoordAnETH(destnoc0))` calling
   `NRI3::initiateWriteToT6` unchanged (it is just an L1->L1 posted NoC write; its
   asserts are isInL1(destaddr) and src%16 == dst%16).  CHECK: isInL1() range covers
   an ETH L1 address (ETH L1 is 512 KiB, T6 L1 1.5 MiB, so it should), and
   preferNoC() handles a y=1 destination.  Keep the `.ps` mark so ptrace still sees
   the send.
3. `T6EP::configureDest`: new ETH branch.  There is no ImageBlockHeader on an ETH
   tile, so skip findBlockCodeInNoC0 and set
   `mDestBlockAddr = seam.outRingBase(chan)` (the E1 OUT ring for that channel).
   `shipTC` then adds CAR_SIZE*carindex as it does now.
4. `InterHub.cpp`: when the neighbor in a Dir8 is off-board and the seam table has an
   entry for that Dir8 (and blockindex), call `configureDest` with the ETH tile's
   NoC0 coords and the channel instead of taking the `!NGB@` path.
5. New shared header `shared/include/EthSeam.h` holds the ONE copy of the layout:
   CTL/channel block offsets, ring base per channel, RING_CARS, CAR size.  The T6
   images, the E1 image and the host tools all include it, so the three cannot
   drift.  (Today the layout lives only as #defines in e1fwd11.c.)
6. How the T6 learns its (ETH node, channel) per Dir8: either a small table the host
   writes into the T6's L1 at deploy (like the HostBlock host-chunk address), or a
   field in the image block.  CHECK which is least invasive; the HostBlock route is
   the one with precedent.

## 4. The two ring directions through one E1

Per channel c (one channel = one hub EP pair = one InterHubStorage):

- OUT leg (existing role_fwd): local T6 NoC-writes car i into OUT[c][i]; E1 sends it
  to the PEER E1's IN[c][i] over TXQ2 (<= 1456 B chunks).
- IN leg (existing role_nocfwd): E1 sees a new complete car in IN[c][i] and NoC-writes
  it to the local T6's InterHubStorage slot i.

Both legs run in one loop (new image mode SEAM).  Needed changes to e1fwd11.c:
- Take the T6 node and storage base PER CHANNEL from the channel block, not from
  CTL+0x48/0x4c (nocfwd today serves channel 0 only).
- Run OUT leg then IN leg per channel per sweep (role_fwd already has the loop shape).
- Complete-car test stays header == ankle == footer (car_complete already does this).
- Per-slot "last forwarded header" arrays live on the stack (images have no .bss).
  They start at 0, so the host must ZERO the rings before release, or a stale
  complete car left in L1 (L1 is not zeroed at boot) is forwarded as new.
- One TXQ2 and one NIU cmd buf are shared by all channels; the single E1 hart
  serializes them, which is fine.  Channel count per ETH tile is a policy choice
  (image supports up to 4); start with one.

Versions are never reused: this becomes e1fwd12.c with a new host tool (the notes say
the next tool is ethfwd13).

## 5. Address plumbing (who knows what, when)

The sender cannot discover the remote T6's L1 layout (findBlockCodeInNoC0 reads over
the NoC and the far card is beyond the seam).  So the HOST fills it in:
- After both cards' T6 images are deployed, the host computes for each channel the far
  T6's InterHubStorage base (from its IBA table: iba.mBlockAddr + blockindex*storage
  size) and writes (node, base) into the LOCAL E1's channel block for the IN leg.
- It writes the peer's IN ring address (the TXQ2 DEST) into the OUT leg's channel
  block (PEERIN today).
- It writes the seam table into the T6s (section 3.6).
- Both E1s are launched and rings zeroed BEFORE the T6s are released
  (HAS_T6_CODE_RUNNING), because a T6 may ship during its first sweep.  Proposal: a
  new Blackhole::Step HAS_ETH_SEAM_UP just before HAS_T6_CODE_RUNNING.
- E1 lifetime: it dies when the owning fd closes (NOC_CLEANUP).  mfmxctl is already
  one long-lived process, so a Blackhole-owned seam is a natural fit for now; a daemon
  is later work (ERISC_NOTES open item 4).

## 6. Risks and things to CHECK

1. Header uniqueness: the E1 treats a car as new only if its header word differs from
   the last one forwarded for that slot.  CHECK that nonce/state sequencing in
   TCMarker.h/TCBase.h guarantees consecutive uses of one slot never repeat a header
   word (8-bit nonce; state flips each hop).  If not, the E1 drops real cars silently.
2. Two-stream ordering (ERISC_NOTES section 3) is established for the ETH RX path.
   The NoC leg into the E1's L1 is a different path: UNVERIFIED whether a single
   large NoC write can be observed out of order by the E1.  car_complete already
   requires the ankle; add a stress test (sequence-stamped 4160 B cars T6 -> E1,
   checked at E1) before trusting it.
3. Posted writes: initiateWriteToT6 is fire-and-forget after an NRI-busy check, same
   as T6->T6 today.  No new risk, but ack-counted writes (ERISC_NOTES item 2) would
   also harden this hop.
4. Source reuse: a T6 car slot is not reused until the far end returns the car, which
   is the existing exchange rule, so tearing in flight should not occur; the same
   assumption was NOT safe in the stage-2c loop test and is worth one explicit test.
5. Diagonals and cards 0/3 (two ERISC hops) are out of scope here; the single-seam
   path comes first.
6. tt-metal region overlap (ERISC_NOTES section 7) applies unchanged.

## 7. Staged plan (same style as stages 0..2)

- Stage 3a: one-way T6 -> E1 -> link -> E1 -> T6, using only EXISTING E1 roles: card A
  runs FWD on its E1; card B runs NOCFWD to a T6 whose L1 the host checks.  New work
  is only a small sfpi T6 test image on card A that stamps cars and calls the new
  shipTCTo branch, plus the host harness (ethfwd13).  Verifies section 3.2/3.3 and
  risk 6.2 with no new E1 code.
- Stage 3b: new SEAM image (e1fwd12); two T6 test images ping-pong cars across the seam
  with the return path; verify nonces, payload and ankle at both ends.
- Stage 3c: real InterHub EPs: seam table + InterHub.cpp hook; two adjacent hubs on
  different cards run HOLD_AT_BIRTH and one NEW_LEADER/RUN_EVENTS cycle; ptrace
  learns the E1 hop (a `.ps`/`.pr` pair across the seam).
- Stage 3d: SuperCycle across cards, channel counts, link-retrain/FEC monitoring.

## 8. Questions for the user

1. Which edge hub(s) use which ETH tile/channel?  (The ETH tiles all sit on y=1;
   which T6 edge column maps to which port is a pure policy choice.)
2. Seam table delivery: HostBlock-style L1 write, or an image block field?
3. OK to add Blackhole::Step HAS_ETH_SEAM_UP before HAS_T6_CODE_RUNNING?
4. Start with one channel per ETH tile, or go straight to several?
