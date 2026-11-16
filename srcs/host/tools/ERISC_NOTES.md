# ERISC / eth-seam notes (Blackhole QuietBox, MFMx)

Written at the end of the eth-seam spike session, 2026-11-15.  Everything here
was measured on the user's QuietBox (4 Blackhole cards, Ubuntu 22.04.5, TT-KMD
2.3.0, FW bundle 18.8.0.0 on all cards, core clock ~800 MHz) unless marked
UNVERIFIED.  Nothing here uses tt-metal or tt-umd; only tt-kmd ioctls + PCIe TLB
windows, and a bare-metal RISC-V image for the Ethernet tile's second core (E1).

GOAL: fuse the four cards' CA grids into one 4x grid over the QSFP-DD 800GbE
links with no PCIe transactions in the data path.  MFMx packets ("cars") move
T6 -> NoC -> ETH tile -> link -> ETH tile -> NoC -> T6.

## 1. Hardware facts established

- ETH tiles are NoC0 Y=1, X=1-7 and 10-16 (E5, E8 harvested).  Port map:
  E4 X=3, E6 X=4, E10 X=6, E12 X=7, E13 X=10, E11 X=11, E9 X=12, E7 X=13.
- Cabling: x-seam links dev0<->dev1 and dev2<->dev3 use X=6,7,10,11; y-seam
  links dev0<->dev2 and dev1<->dev3 use X=3,4,12,13.  Ring 0-1-3-2; cards 0 and
  3 are NOT directly cabled, so a diagonal is two ERISC hops.
- Each ETH tile has two cores: E0 (base firmware, leave alone) and E1 (customer
  core, held in reset).  E1 is released by: write image to L1 0x4000, set
  E1 PC/END regs (0xFFB14008 / 0xFFB1400C), clear soft-reset bit 12 of
  0xFFB121B0 (bit 11 = E0).  Default soft-reset value 0x00047000.
  tt-kmd ioctl SET_NOC_CLEANUP (0xFA0E) writes the reset value back when the fd
  closes, so a persistent forwarder needs a long-lived owner process.
- E1: sp = 0xFFB02000, 8 KiB local RAM, no .data/.bss in images (linker discards
  them): no statics.  Build with the sfpi gcc cross toolchain (riscv32-tt-elf-gcc -march=rv32im_zicsr
  -mabi=ilp32 -O2 -ffreestanding -nostdlib -mno-relax; see e1fwd/build.sh, which also
  rejects any .data/.bss).  Images first spiked under clang; gcc builds are to be
  re-verified on hardware (e1fwd11 first).
- Tensix soft-reset register is the same address (0xFFB121B0); all five RISC-V
  cores in reset = 0x47800 (BRISC 0x800, TRISC 0x7000, NCRISC 0x40000).
  L1 is NOT zeroed at boot (T6 (1,2) held 0x99fff5d7... at 0x100000).
- Firmware: stay on 18.8.0.0.  19.0 changes the DMFW bootloader, needs
  tt-flash >= 3.4.7, ~60 s reset, no documented rollback.  Working; do not touch.

## 2. TT-link queue 2 (host- or E1-programmed L1-write packets)

- TXQ2 0xFFB92000: ctrl +0x00 (1 = TT-link), CMD +0x04 (2 = L1 write), STATUS
  +0x08 (bit16 = ongoing), max packet +0x0C (1500), SRC +0x14, SIZE +0x18
  (multiple of 16), DEST +0x1C, xfer counter +0x30 (CUMULATIVE since boot, not
  per run), CFG_SEL_SW +0x80 (header entry 1 = 1<<8), SEL_HW +0x84.
- RXQ2 0xFFB96000: ctrl +0x00 = 2, buffer start +0x0C (RXBUF_L1 0x7E000 >> 4),
  size +0x10 (0x100), header ctrl +0x18 (0xe), seq +0x40, remote seq +0x44,
  drops +0x4C.  TXHDR[1] at 0xFFB98280 (+0x10 SA0, +0x14 SA1, +0x18 DA0,
  +0x1C DA1): unicast to the peer MAC.  Base FW uses only queue 0 (broadcast):
  NEVER send with header entry 0.
- Max TT-link packet 1500 B incl. 32 B headers: <= 1456 B payload per CMD.
  Larger transfers split in hardware (1472/1488 B verified); the E1 forwarder
  sends cars in <= 1456 B chunks (4160 B car = 3 chunks, verified).
- Never write CMD while STATUS bit16 is set.  All 16 links verified both
  directions, drops = 0 everywhere, through ~100M packets.

## 3. Memory-ordering facts (the expensive lessons)

- Baby RISC-V L0 data cache: 4 lines x 16 B, NOT coherent with L1 writes by other
  clients (NoC, ETH RX, host).  Flushed only by `fence`/atomics.  Present on
  E1 (measured: unfenced poll missed 84.5-87.3% of host-written values; fenced
  0%).  Disable it with CSR 0x7c0 bit 3 (DisLowCache): WORKS on E1 (readback
  0x80020008 with our tag bit; 0% missed without fence).  Same trick MFMx
  cross/src/_BUD.S line ~108 uses on the T6 harts.  Poll cost: ~20 ns stale-
  cached (meaningless), ~35 ns L0-off, ~40 ns fenced.
- POLICY (user, 2026-11-16): the MFMx codebase turns the L0 data cache OFF (CSR 0x7c0 bit 3) on its
  T6 harts (cross/src/_BUD.S) and the E1 experiment above showed E1 honors the same bit.  So the
  L0 data cache is NOT assumed to be an issue for MFMx or E1 images: run with the CSR route
  (e1fwd F_CSR; no per-poll fence needed) and treat the stale-L0 numbers above as history.
  The fence route (F_FENCE) remains only as a fallback / for the older stage-0 tests.
  (Instruction cache is separate and unaffected by this bit.)
- NoC packet size (user, 2026-11-16): the NoC has a 16 KiB single-packet limit; anything larger
  must be split by the sender or by the NoC's autosplit logic.  MFMx embraces the 16 KiB single-
  packet limit in ALL contexts (a 4160 B car is one NoC packet).  So the multi-packet caveat below
  applies to the Ethernet hop (1456 B TT-link chunks), not to NoC writes of cars.  (The ethdump
  sample's 12288 B figure is its own conservative choice.)
- NoC->L1 and ETH-RX->L1 writes are atomic per 16 B lane (per user/Tenstorrent).
- IMPORTANT (per user/Tenstorrent): the L1 write of a single packet is carried out
  by TWO INDEPENDENT STREAMS, and the in-order guarantee applies only WITHIN a
  stream, not between them.  A word arriving in one stream says nothing about
  whether earlier-addressed words carried by the other stream have landed yet.
  Which addresses go to which stream (WormholeB0/NoC/Ordering.md in the user's
  tt-isa-documentation fork; TT engineers (via the TT Discord bot) say Blackhole A0
  behaves the same, a structural property of the NoC's two L1 write ports; we assume
  this for BH T6 and ETH tiles until proven otherwise): the receiving NIU splits
  L1 writes by address into 16-byte granules.  Stream 0 = byte addresses 0..15 mod
  32, stream 1 = 16..31 mod 32.  Each 16-byte write is atomic; within a stream one
  packet's writes occur in ascending address order; NOTHING is stated about order
  between the streams.  (The same page also says generically that the units of a
  packet "can be processed in any order" and that software should not assume order
  among individual accesses; we rely on the specific per-stream ascending-order
  statement.  This is the weakest link of the argument below.)
- Measured: E1 reading its own L1 (fenced) saw ETH RX writes commit in address
  order on both directions of two links, and never saw an older word behind a
  matching footer.  That is an observation, not a guarantee: given the two
  streams it may only mean the streams happened to stay in step in these tests,
  and zero bad cars does not prove the absence of cross-stream reordering.
- Therefore a car is COMPLETE only when header == ankle == footer (ankle = word
  (words-5), tcms >= 2; see section 4), and a footer match alone is NOT enough.
  Keep the ankle on the ETH hop as well as the NoC hop.
- WHY header+ankle+footer is enough for ONE packet (derived from the stream rule
  above; checked for slot bases 0 and 16 mod 32 and tcms 0,1,2,3,10,66): car
  packets are 16 B, 32 B or a multiple of 64 B and ring slots keep src%16 == dst%16
  (stride 0x1040 = 130*32).  For tcms >= 2 the header (word 0) and the ankle (end-20,
  in the last stream-0 granule of the packet) are in the SAME stream, and the footer
  (end-4) is in the OTHER stream.  Header == ankle therefore proves that stream's
  last granule landed, and by in-stream ascending order everything before it in that
  stream; footer == header proves the same for the other stream.  That covers both
  streams, which is exactly what TCBase.h's "5w back to get on other stream" is for.
  tcms 1 (32 B): header and footer are in different streams, one granule each.
  tcms 0 (16 B): one atomic granule.  The test needs header values to differ between
  consecutive cars in a slot (nonce+1 per departure; 8-bit nonce, 4-slot ring).
- NOT covered by that argument: a car that arrives as SEVERAL packets.  A 4160 B car
  is 3 TT-link chunks on the ETH hop (1456 B payload each: note 1456 mod 32 = 16, so
  chunk boundaries fall mid 32 B block, harmless for the address-based stream rule; a NoC
  write of a car is a single packet, 4160 B << 16 KiB).  The header is in chunk 1 and the ankle +
  footer are in chunk 3, so a match says nothing about chunk 2.  Cross-packet landing
  order at the receiver is not documented for the ETH RX queue; if NoC packets ever did have to be ordered, packets
  on the same route with the same VC at every shared hop do not reorder (NOC_CMD_VC_STATIC
  pins the first-hop VC), but the page states commit ordering between packets only for
  overlapping bytes (and write-before-read).  Hardware evidence so far: --regen payload
  verification at 4160 B (3 chunks) saw ~20k checks, zero bad (section 5); that is evidence, not proof.  Open test: long
  runs with per-word stamps at tcms 66, plus VC_STATIC on the NoC leg.
- The early "out of order" results in ethlinktest12/13 were L0 staleness, not
  hardware reordering.  UNVERIFIED: a T6/NoC-reader observer (should read the
  footer first or recheck markers).
- After storing a car's words and before a TXQ/NIU CMD store, read one stored
  word back (stores to different regions may be processed out of order).

## 4. MFMx car model as the E1 sees it (shared/include/TCMarker.h, TCBase.h)

- Marker word = 0x2C | nonce<<8 | tcms<<16 | state<<24 (state 3 INBOUND_DEPARTED,
  4 OUTBOUND_DEPARTED).  Packet bytes: 16 (tcms 0), 32 (1), (tcms-1)*64 (>=2);
  4160 B = tcms 66 (InterHubBlock; InterHubStorage = 4 cars).  Footer = last word,
  ankle = word (words-5) if tcms >= 2.  Complete = header == ankle == footer (all
  three; see the two-streams caveat in section 3).
- Flow control IS the car exchange: a shipped car's slot is owned by the far end
  until a complete car with a changed header arrives; FIFO slot order mod ring.
  So the E1 is a transparent wire: it adds no credits.  A "new" car = complete
  and header word != last forwarded for that slot.

## 5. Tools and results (all in srcs/host/tools; versions are never reused)

- ethprobe*/ethlinktest11..14: link bring-up, queue 2 config, order experiments.
- e1fwd/e1fwd1.c + ethfwd1.cpp: stages 0, 1a, 1b.   e1fwd/e1fwd11.c +
  ethfwd11.cpp: adds stage 2 and the stop-flag fix.  ethfwd12.cpp: stage 2 owns
  its T6 tile (reset, zero, readback).  e1fwd/hosttest*.c: native harness for
  the E1 logic (simulated TXQ2/NIU, two cores as threads).  Tools default to a
  dry run, write only through allowlists, `--mock` exercises logic offline.
- e1fwd/e1fwd13.c + e1fwd/hosttest13.c (session 5): multi-channel bidirectional router
  image for the seam (see ERISC_SEAM_DESIGN.md); passes the native harness, not yet
  built for RISC-V or run on hardware.
- Stage 0: CSR 0x7c0 bit 3 works on E1 (x-seam 0:1:6 and y-seam 0:2:3).
- Stage 1a (host as hubs, E1 FWD role): 8000 cars/dir at 64/576/4160 B, zero bad,
  both directions, both seams.
- Stage 1b (both E1s REFLECT, no host in loop): one-way E1 hop latency ~0.56-0.68
  us nearly independent of size; hops/s scale ~linearly with cars in flight K
  (K=4, 4160 B: 5.8 M hops/s = ~24 GB/s aggregate both directions, far below line
  rate; the 4-car ring is the limit, not the link).  --regen verified payload
  over ~20k checks at 4160 B, zero bad (E1 regen costs ~7-15 us/hop).
- Stage 2a/2b (E1 NoC-writes cars into T6 (1,2) L1; 2b fed over the link from
  card A): 8000 cars per size, zero bad, so host -> A E1 -> link -> B E1 -> NoC
  -> T6 works with PCIe only used to inject at A and verify at the T6.
- Stage 2c throughput is NOT trustworthy: 122 ns/write at every size is the E1
  loop floor (posted-write ISSUE rate), and source-buffer reuse may tear cars in
  flight (only the drained final ring is checked).  Needs non-posted writes with
  the NIU ack counters.

## 6. Open items / next steps

1. T6 -> E1 direction: a T6 NoC-writes cars into the E1 OUT ring (needs an ETH-tile
   destination branch in T6EP::shipTCTo / NRI3, ETH node + L1 address from the
   image block table), then E1 forwards (already works).  Completes a host-free
   T6 -> T6 path across cards.  Needs a T6 test image built with sfpi.
2. Acknowledged (non-posted) NoC writes for a trustworthy 2c number; copy the
   control bits and ack-counter use from MFMx NRIUtils.
3. Corner diagonals: two ERISC hops (E1-to-E1-to-E1 through the middle card).
   User expectation: CornerHub only routes NSEW even for diagonal destinations,
   so a two-hop path should just work; UNVERIFIED.  Cards 0 and 3 are not cabled.
4. Lifetime: E1 dies with the owning process (NOC_CLEANUP).  Production needs a
   long-lived owner (daemon or the simulator host process) and a protocol for
   link retrain / E1 restart.  Link-retrain behaviour untested.
5. E1 NoC command buffer: the code uses NIU0 cmd buf 3 (as MFMx hNC does).  It
   worked.  The base FW does use dynamic NoC counters (L1 0x7D040), so it uses the
   NIU somehow; no conflict seen, but not documented for cmd buf 3.
6. Repeat stage 1/2 on the remaining links (7 not yet exercised: X=7,10,11 on the
   x seam; X=4,12,13 on the y seam) and on cards 1-3 pairs (1:3, 2:3).
7. Leader election / SuperCycle across cards, channel counts per ETH tile
   (mfmx routing: how many EP channels per link), 4 cars/ring depth vs throughput.
8. Placement hygiene: see section 7.  Our E1 image/CTL/rings sit in regions tt-metal
   reserves for its own ERISC firmware; fine while tt-metal is not loaded, but
   relocate to >= 0x15B40 if the two must ever coexist.
9. Use the retrain/FEC counters (section 7) in the tools to prove links stay clean
   during long runs; never write the retrain-force word.

## 7. Cross-check against tt-metal headers (blackhole-doc/dev_mem_map.h, eth_l1_address_map.h)

Headers supplied by the user (Tenstorrent, Apache-2.0); numbers below were computed
by compiling the macros.  They describe tt-metal's layout; the base FW 18.8.0.0 is
not guaranteed to match, but the system-engineering addresses agree with what we
measured (0x7CC00 boot results, RXQ0 buffer at 0x7F000).

- ETH L1 is 512 KiB (0x80000).  Top 64 KiB (0x70000-0x7FFFF) is SYSENG reserved:
  boot results 0x7CC00, retrain count 0x7CE00, corrected/uncorrected FEC codeword
  counters 0x7CE90 / 0x7CE98, sysENG mailbox 0x7D000, base-FW dynamic NoC counters
  0x7D040, retrain-force word at 0x1EFC.  Our RXQ2 buffer (0x7E000, empirically
  zero) lies inside the SYSENG region but at an address the map does not name.
- Reset PCs: 0xFFB14000 = E0 (active/idle ERISC), 0xFFB14008 = E1 ("subordinate"
  ERISC): confirms our E1_PC register.  E1 has no base-FW local RAM use (full
  8 KiB at 0xFFB00000); E0 base FW uses the first 0x700 bytes.
- tt-metal active-ERISC L1 map: mailbox 0x100-0x32E0, E0 firmware 0x3330-0x9330,
  E1 ("subordinate") firmware 0x9330-0xF330, scratch/bank-to-noc after that, kernel
  config 26 KiB; first free address for user data ERISC_L1_UNRESERVED_BASE =
  0x15B40; app area ends at MEM_ERISC_MAX_SIZE 0x6F260 (fabric router reserved
  0x6F260-0x6FE70, sync/routing/barrier up to 0x70000).
- OVERLAP: our E1 image (0x4000-0x5020), CTL/stat block (0x8000-0x83FF), rings
  (0x10000-0x24100) and loop buffers (0x30000-) overlap tt-metal's reserved firmware
  and scratch regions below 0x15B40.  Harmless with FW 18.8.0.0 alone (links stayed
  up, drops 0); would corrupt state if tt-metal firmware were loaded on these tiles.
  Relocating the image to the 0x15B40+ area is a small change if coexistence is
  ever needed.
- The write allowlist in the tools (scratch 0x10000-0x6FFFF) is coarser than the
  regions actually used; the E1 never writes outside its rings/CTL/loop buffers.
- Tensix L1 is 1.5 MiB (0x180000), consistent with the T6 region choice 0x100000.

## 8. Cross-check against the unofficial BlackholeA0 ISA docs (fork of tt-isa-documentation)

Source: github.com/DaveAckley/tt-isa-documentation, BlackholeA0/EthernetTile/{README,EthernetTxRx,BabyRISCV/README}.md and
BlackholeA0/TensixTile/BabyRISCV/{README,CSRs,MemoryOrdering}.md (read 2026-11-16 through a summarizer, not verbatim:
re-check exact bit numbers against the raw files before relying on them).  TT has withdrawn these pages; they were
INCOMPLETE when released.  Treat as strong hints, not guarantees.  The Ethernet pages say the E0/E1 cores are like the
Tensix baby cores but are not a substitute for the Tensix pages (differences: PIC, 2 cores, no Tensix coprocessor); every
Tensix-page fact below that we have not confirmed on an ETH tile is marked (T).

Clock: docs say 1.35 GHz.  The user's cards currently run at 800 MHz (1.35 GHz held in reserve), so cycle-to-time
conversions in these notes use 800 MHz unless stated.

Fast RAM (local data RAM):
- Each ETH core has its own 8 KiB at 0xFFB00000-0xFFB01FFF, reachable only by that core's own loads/stores; unmapped to
  the NoC (and local-RAM/pc snapshots are listed as not accessible over the NoC).  So E0 and E1 do NOT share it, and the
  host can NOT read it: anything the host must see has to be published into L1.  Stack top 0xFFB02000 = one past the end.
- Load latency (T): local RAM 2 cycles, no contention; L1 with L0 hit 2; L1 L0-miss >= 8 (more on bank/port conflicts);
  L1 atomic >= 12; NoC/tile-control/PIC register loads >= 7-8.  A latency of N needs N-1 independent following
  instructions to hide.  The doc recommends stack, thread-local data and hot read-only globals in local RAM.
  (Tensix docs also describe a slow alias for local RAM at 0xFFB14000-0xFFB1DFFF reachable from the NoC; on the ETH tile
  that range holds PC/PIC registers, so do not assume it.)
- After reset local RAM may take up to 2048 cycles to zero (T); the core stalls automatically on its own accesses.

L0 data cache (T): 64 B = 4 lines of 16 B; caches loads from L1 only; not coherent with other writers; any fence or atomic
flushes all of it; a store flushes its line; each hit has ~0.8% chance of flushing everything unless cfg0 bit 24
(DisLowCachePeriodicFlush) is set.  CSR 0x7c0 = cfg0; bit 3 is "DisLowCash" (L0 data cache off), which matches the
0x7c0 bit 3 (value 8) used by e1fwd F_CSR.  A polling loop using fence/atomics loses the 2-cycle hit path every iteration.

Memory ordering (T): loads/stores can reorder inside the LSU (4-entry store queue, 8-entry retire queue); writes are
fire-and-forget; one request per region per cycle, so ordering is total WITHIN a region but free across regions (L1 vs
MMIO).  A fence drains the store queue (requests SENT, not processed).  Recipe for "store processed before later
request" is: read back the same address and consume it (matches our TXQ CMD dependency token).  Other tools: cfg0 bits
0 DisLdBufByp, 9 SyncAllOps, 12 DisStMerge (store merging of same-16B-block L1 stores), 20 DisRegForwarding;
pmacfg0/1 (0x7c1/0x7c2) bit 0 makes every load/store behave as if fenced (strongly ordered; whole address space due to
a hardware bug) -- UNTESTED here, possible blunt alternative to the readback.  mcycle (0xb00/0xb80) and minstret
(0xb02/0xb82) exist: usable for in-E1 timing.  No `time` CSR documented.

Ethernet TX/RX (EthernetTxRx.md): TXQ regs as in section 2 (SRC +0x14, SIZE +0x18, DEST +0x1C, CMD +0x04 =2 for TT-link
L1 write, STATUS +0x08 bit 16 CMD_ONGOING; do not reissue until clear; src/size/dest 16-byte aligned/multiples).  Max
TT-link payload and default MAX_PKT_SIZE are NOT documented (our 1456 B is empirical).  TT-link resends on timeout /
drop notices, RX discards duplicate sequence numbers; no credit flow control (the car exchange is ours).  The docs say
NOTHING about ordering between streams or within one packet's L1 write (the Ethernet pages are silent; the stream split
comes from the Wormhole NoC page, see section 3 and 8.2).  TXQ2 cannot send MMIO-write/overlay packets; RXQ2 cannot receive MMIO writes (we use L1 writes
only).  Other base facts: 3 TXQ + 3 RXQ per tile, shared 10-entry TX header table at 0xFFB98200 (+i*0x80), RX classifier
0xFFB9C000, RXQ regs 0xFFB94000/5000/6000.
Not yet read: InstructionCache.md (404 at the name tried).

Consequences for e1fwd13 (planned changes, not yet made): hoist per-channel config into local RAM/stack once at start;
keep counters local and publish to L1 with plain stores (no read-modify-write); poll the stop word in L1 only.  Verify by
the existing CTL+0x30 sweep counter (idle sweeps/second) and by mcycle deltas.

### 8.1 ethdump sample (EthernetTile/Samples/ethdump/{README.md,ethdump.c}; Apache-2.0, Tenstorrent 2025)

Read 2026-11-16 via a summarizer (not verbatim).  A host program that runs a ~400-byte RV32 program on E1 which drains an
ETH RX queue (queue 2, RAW mode) into a ring in L1 and NoC-writes it to a pinned host ring through the PCIe tile.
Independent confirmation of several things we found by experiment:
- E1 start recipe agrees with ours: code via L1, E1 PC/END at 0xFFB14008/0xFFB1400C, soft reset 0xFFB121B0 with E0 = 0x800
  (bit 11) and E1 = 0x1000 (bit 12); sp = 0xFFB02000; tt-kmd SET_NOC_CLEANUP (0xFA0E) so E1 is re-reset if the owner dies
  (older tt-kmd returns EINVAL: ignorable).  Boot results 0x7CC00, boot params 0x7C000.
- TXQ quieting used there: ETH_TXQ_CTRL = 8 (DIS_DROP: no drop-notice resends) and ETH_TXQ_REMOTE_SEQ_TIMEOUT (+0x48) = ~0
  on queues 0-2.  (Not what we do on TXQ2 in TT-link mode; recorded for reference.)
- RX queue 2 raw mode: CTRL(+0)=0, BUF_START_WORD(+0x0C), BUF_SIZE_WORDS(+0x10) = bytes>>4, HDR_CTRL(+0x18)=0 keeps all
  headers, BUF_PTR(+8)=0.  Valid RX data is bounded by BUF_PTR - OUTSTANDING_WR_CNT(+0x50)*96 (the firmware uses *128 as a
  faster safe overapproximation; the quantity is non-monotonic, track a high-water mark).  Classifier: override-decision
  0xFFB9D000 (1 = drop all, 0 = accept), MAC_RX_ROUTING 0xFFB98150 = 2 (from actions), NO_MATCH_ACTIONS 0xFFB9CD04 =
  0x40 (prepend HW metadata) + rxq index, TCAM flush 0xFFB9CD60.
- NoC from E1: NIU(i) = 0xFFB20000 + i*0x10000, ethdump uses NIU 1 (we use NIU 0).  NOC_CMD_WR = 2; ordering of successive NoC
  writes is kept with the NOC_CMD_VC_STATIC flag (1<<7 in the command word, offset 0x1C), which the sample says preserves
  order through to the PCIe tile.  e1fwd13's noc_write sends command word 2 WITHOUT VC_STATIC (as e1fwd11 / NRI3 do); worth
  testing VC_STATIC on the NoC leg if the ordering question (two streams) shows up there.  The sample also treats 12288 B
  as a safe per-NoC-transaction limit (real limit "just under 16 KiB"); our 4160 B car is well inside it.
- Ordering inside the firmware: dummy `lw x0, ...` loads and `fence` instructions force stores out before later loads
  (e.g. after the NoC command store, before reading PACKET_DROP_CNT).  It tolerates occasional stale metadata (benign for
  its use, NOT for car completeness).
- NIU registers ROUTER_CFG_2 / ROUTER_CFG_4 (0x10C / 0x114) are used as host<->E1 mailboxes (host-writable registers that
  need no L1 traffic).  E1 loads of NIU registers cost >= 7 cycles like L1 misses, so this is a convenience, not a speedup.
- No cache or reset warnings in the sample; address map facts match section 7 (tile coords: ETH tiles at NoC y=1, x 1-7 and
  10-16; X=8,9 are not ETH).

### 8.2 WormholeB0/NoC/Ordering.md (assumed to hold for BH T6 and ETH tiles; see section 3)

Read 2026-11-16 via a summarizer.  Facts used: L1 reads/writes are split into aligned 16 B units, each atomic, processed in any
order EXCEPT the stream rule (stream 0 = addr 0..15 mod 32, stream 1 = 16..31 mod 32; ascending within a stream for one
packet; nothing between streams).  Between packets: same route + same VC at every shared hop => no reordering or
interleaving there.  Request VC: NOC_CMD_VC_STATIC (software picks first-hop VC; routers may flip only the dateline bit),
NOC_CMD_VC_LINKED (hardware picks; multi-request transactions, other side effects).  Order of NOC_CMD_CTRL writes sets
first-hop VC order and, on a shared VC, NIU departure order.  Receiving NIU: up to 12 request + 4 response packets in flight,
one per final-hop VC; packets on different VCs can be reordered or interleaved arbitrarily; on the same VC guarantees are:
write-then-read (all writes of packet 1 commit before reads of packet 2), overlapping bytes get packet 1's write first
(per byte), MMIO writes in order, L1 write then MMIO write has NO guarantee.  Responses/acks cannot be ordered (use counters).
Stronger ordering = issue, wait for response, then issue the next.  The page says nothing on max packet size, barriers or
write-data-then-flag patterns.

## 9. First gcc-built E1 test on hardware (RAN 2026-11-16, PASS; procedure below)

Tool: ethfwd13 (ethfwd12 + --image FILE.bin + --boot-only).  Build the tool: g++ -std=c++17 -O2 -Wall -Wextra -Werror
ethfwd13.cpp -o ethfwd13.  Build the image: sh e1fwd/build.sh e1fwd11 (sfpi gcc; leaves e1fwd/e1fwd11.bin, do not commit it).
Run on the QuietBox, in this order, stopping at the first surprise:
  1. ./ethfwd13 --link 0:1:6 --boot-only --image e1fwd/e1fwd11.bin           (dry run: reads only, prints what it would write)
  2. ./ethfwd13 --link 0:1:6 --boot-only --image e1fwd/e1fwd11.bin --go      (deploy on card 1 only, stage 0, reset E1)
Boot-only writes: E1 image at L1 0x4000.., CTL block 0x8000..0x83FF, a few ring/CTL words (scratch), E1 PC/END regs, soft-reset
bit 12 (E1 only).  No queue-2/TXHDR/RXQ writes, no link traffic.  Pass criteria: image readback has no mismatch; ready word
0xE1E1E1E1 appears; stage 0 reports (as under clang) no-fence misses >> fence 0% ~ CSR-l0off 0%; CSR readback has bit 3 and the bit-31 tag set (0x80000008 expected; hw showed 0x80020008, bit 17 presumably a default bit);
"E1 back in reset" with soft-reset 0x00047000 and E1 PC/END at their original values.  Compare ns/poll with the clang numbers in
section 3 (~40 ns fenced, ~35 ns L0-off at the user's 800 MHz).  Only after this passes: rerun --stage1a/1b/2 with the gcc image
(without --boot-only), then e1fwd13 once an ethfwd14 exists.  --mock results are FAKE (they exercise the tool, not the image).

### 9.1 Result (ethfwd13 --boot-only --image e1fwd/e1fwd11.bin --go, link 0:1:6, card 1, 800 MHz)

gcc-built e1fwd11 (3660 B) booted (ready word), image readback clean, 0 write problems, E1 back in reset (soft-reset 0x00047000,
PC 0x4000 / END 0x7fff).  Stage 0: no fence 14.3 ns/poll, MISSED 82.7%; fence 37.1 ns/poll, 0%; CSR 0x7c0 bit 3 (no fence)
32.1 ns/poll, 0%; CSR readback 0x80020008.  So the gcc image shows the same L0 behaviour as the clang one and is slightly faster
on this loop (37.1 vs ~40 fenced, 32.1 vs ~35 L0-off; loops are compiled differently, so not a clean comparison).
32 ns = ~26 cycles per L1 poll with the L0 off, vs ~2 cycles documented for local RAM: motivates e1fwd14.

### 9.2 e1fwd14 (local-RAM hot state; NOT yet run on hardware)

e1fwd/e1fwd14.c = e1fwd13 with per-channel config + node id copied to the stack at run start, stats kept on the stack and
published to L1 with plain stores (zeroed at run start), stop word polled every 16 sweeps.  Same CTL/channel layout and wire
behaviour.  gcc build: 3320 B (e1fwd13: 2880 B as a .bin); router frame ~1168 B of the 8 KiB local RAM.  hosttest14.c = hosttest13.c
retargeted; passes for the 6 car sizes.  Host contract change: config words must be final before go is raised.  Host tool for
its host tool is ethfwd14 (written, section 9.3).  Hardware plan: with ethfwd14, run e1fwd13 and e1fwd14 on the same link test and compare
S_FWD rate / sweeps per second (CTL+0x30 with mcycle if needed).

### 9.3 ethfwd14 (host tool for e1fwd13/e1fwd14; mock-tested only, NOT run on hardware)

tools/ethfwd14.cpp = ethfwd13 plumbing + the 8-channel layout.  --image is required (no embedded image); it checks the image id word
(CTL+0x54: 0xE1F00013 / 0xE1F00014) and refuses otherwise.  CTL write window is 0x600 now.  Rings: OUT(c) = 0x10000 + c*0x4100, IN(c) =
0x30800 + c*0x4100 (same addresses on both cards).  Tests: --idle (sweep rate, no cars: the e1fwd13-vs-14 comparison), --route
(host-as-hub both directions on --nch channels, word-for-word verify, E1 counters cross-checked), --reflect (autonomous ping-pong on
all channels, per-channel min/max hops).  No NoC (L_IN_NOC) leg yet.  Procedure on the QuietBox (stop at the first surprise):
  g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd14.cpp -o ethfwd14 ; sh e1fwd/build.sh e1fwd13 ; sh e1fwd/build.sh e1fwd14
  ./ethfwd14 --link 0:1:6 --image e1fwd/e1fwd13.bin                       (dry run)
  ./ethfwd14 --link 0:1:6 --image e1fwd/e1fwd13.bin --go --idle --nch 1   (first real run: just the sweep rate)
  then --route, then --reflect, then the same with e1fwd14.bin, then --csr, --nch 8.  Compare sweeps/s and reflect hops/s.

### 9.4 First ethfwd14 hardware run (e1fwd13.bin, --go --idle --nch 1, link 0:1:6, 800 MHz): PASS

Queue-2 setup, 64 B smoke test both ways, deploy of the 2880 B gcc e1fwd13 on both cards (image id 0xE1F00013 read back), E1 reset
on exit.  Idle router, 1 channel, fence mode: 5.438 M sweeps/s on both cards = 184 ns/sweep (~147 cycles).  (An earlier version of this paragraph blamed ~ten fenced L1 loads per sweep and predicted e1fwd14 would be far faster; the 9.5 result
shows that explanation was wrong.)  Not yet run: e1fwd14 idle, --route, --reflect, --csr, --nch 8.

### 9.5 ethfwd14 hardware runs, e1fwd13 vs e1fwd14 (link 0:1:6, 800 MHz, fence mode, --nch 2 unless noted)

route (host-as-hub, 2 ch, both directions, 500 rounds x 4 cars per size): PASS for both images at 64, 576 and 4160 B (3 TT-link chunks):
2000 cars per direction per channel, 0 bad, 0 timeouts, E1 fwd/in-seen counters exactly equal (no TXFAIL).
More evidence (not proof) that cross-chunk landing order is not a problem on the Ethernet hop.  Host time is PCIe-dominated: no speed info.

idle sweep, 1 ch: e1fwd13 184 ns/sweep (5.44 M/s); e1fwd14 164 ns/sweep (6.10 M/s): only ~11% faster.
reflect, 2 ch, K=4: e1fwd13 7.39 M hops/s (64 B), 7.38 (576 B), 5.54 (4160 B); e1fwd14 7.97, 7.97, 5.77 M hops/s: +8%, +8%, +4%.
Per-channel hops were equal to within ~0.01% (fair).  Payload was NOT verified in these runs (no --regen); the 4160 B figure is ~23 GB/s
total, so rerun with --regen before trusting it.
CONCLUSION: moving config/stats/stop-poll into local RAM bought ~10%, not the big win expected, so the sweep/hop cost is NOT dominated
by those L1 loads.  Remaining suspects: the two FENCEs per sweep (each flushes the L0), the per-sweep L1 store of the sweep counter,
and the TXQ2/NoC register traffic.  Next experiments: idle and reflect with --csr (no fences), then publishing the sweep counter
only every N sweeps.
BUG found in ethfwd14 (not the images): reflect K=1 stalled for BOTH images because ethfwd14 always set the E1 ring depth to 4 while
REFLECT needs depth = K (cars cycle through slots in order).  Fixed in ethfwd15 (e1_start takes the ring depth).  K=4 results above are valid.
