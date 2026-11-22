# ERISC / eth-seam notes (Blackhole QuietBox, MFMx)

See also: /CLAUDE.md (start here, formatting rules), ERISC_SEAM_DESIGN.md (design, slices 1-3), ../python/EWD/HANDOFF.md (mfmxctl), ../../../scripts/ptrace/README.txt.

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

Batch runner: tools/expts10.sh (bash) builds ethfwd15 + both images and runs the 9.5 follow-ups (idle/route/reflect x fence/csr x e1fwd13/14, with --regen) with && chaining, stopping with a banner on the first failure; output in tools/out10/ (do not commit).

### 9.6 What out10 (expts10.sh, 2026-11-16) says, 15 steps all PASS

Idle sweep (ns/sweep, nch=1 / nch=8): e1fwd13 fence 184/1204; e1fwd14 fence 164/1130; e1fwd13 csr 168/1099; e1fwd14 csr 149/1019.
Per extra channel (2 leg visits): 146 (e13 fence), 138 (e14 fence), 133 (e13 csr), 124 ns (e14 csr).  So the local-RAM hoisting saves
~7% (~9 ns/channel, although it removes ~6 of ~8 L1 loads per channel) and dropping the fences ~8-10%.  Most probable reading: the
in-order E1 core overlaps independent loads, so a leg visit costs ~ONE dependent L1 latency (~50-60 ns incl. branch/call), not one per
load; hoisting independent config loads therefore bought little.  (Hypothesis from the numbers, not measured directly.)  If idle
latency ever matters, the lever is batching: issue all 2*nch ring-header loads back to back before testing any (software
pipelining), expected to cut an 8-channel sweep several-fold.  Not done: it does not seem to matter (below).
Reflect without regen, K=4 (hops/s total over both E1s, 2 channels): 64 B: e13f 7.39 M, e14f 7.97, e13c 7.60, e14c 8.23; 4160 B:
5.54 / 5.77 / 5.66 / 5.90 M.  => ~243 ns per hop per E1 (e14 csr, <=576 B) and ~339 ns for 4160 B (3 chunks); ~12 GB/s per E1 of car
payload.  The rate is IDENTICAL for 1, 2 and 8 channels (8 ch regen: same totals as 2 ch) and fair across channels (min~max): the
E1 core's per-hop cost (TXQ2 status poll + 4 MMIO stores + bookkeeping), not the link or the ring depth, is the cap.  The clang
e1fwd11 stage 1b number (5.8 M hops/s at 4160 B, K=4) equals the gcc numbers: gcc code is as good as clang here.
Reflect with --regen (E1 regenerates + every 16th verifies payload, 0 BAD in ~1.3 M checks over all sizes/K/channel counts/fence
and csr): hop rate is bounded by E1 payload regen, ~11 ns per word (~9 cycles): 4160 B 0.17 M hops/s total (~0.7 GB/s).  The 2.3 M
hops/s at K=1/64 B is regen-bound too, so it is NOT a latency figure; ethfwd16/expts11 adds a no-regen K=1 latency run.
Route (host-as-hub) on e1fwd14: PASS at 2 ch fence, 2 ch csr, 8 ch csr; 64/576/4160 B; 0 bad / 0 timeouts; E1 counters exact.
OPEN QUESTION FOR THE USER: what car rate does one seam actually need?  ~4 M cars/s per E1 (before the NoC leg) is likely far more
than 35 hubs per card per step need; if so, further E1 speed-ups are not on the critical path and the work should move to the T6
side and the seam table.

### 9.7 e1fwd15 / ethfwd16 / expts11.sh (written 2026-11-16 while the user was away; NOT run on hardware)

e1fwd15.c = e1fwd14 + flag F_VCSTATIC (8): the NoC leg writes with command word 0x82 (NOC_CMD_VC_STATIC) instead of 0x2; image id
0xE1F00015; hosttest15.c exercises both command words (odd tcms cases use the flag) and passes.  ethfwd16.cpp = ethfwd15 + --noc
(card A E1 L_OUT, card B E1 L_IN_NOC with per-channel T6 node/base = --t6-addr + c*0x4100; host injects at A and verifies the T6
L1 of B word for word; tool owns the T6 tile exactly like ethfwd12 stage 2: cores in reset, region zeroed/read back, restored) +
--t6/--t6-addr + --vcstatic; it accepts image ids 13/14/15.  expts11.sh runs: noc on e1fwd14 (1/2/8 ch, csr and fence), e1fwd15
plain vs --vcstatic, two 3000-round 4160 B stress runs (plain vs VC_STATIC), then idle/route/reflect regressions and the K=1
latency run.  The mock models the NoC leg only to exercise the tool.

### 9.8 out11 (expts11.sh, 2026-11-16): all 12 hardware steps PASS

NoC leg on the multi-channel image works on hardware: host -> A OUT rings -> A E1 -> link -> B E1 -> NoC -> T6 (1,2) rings @0x100000,
host-verified word for word, 0 bad / 0 timeouts, E1 counters exact (fwd = noc-writes = in-seen = n, no noc-fail/txfail), for e1fwd14
with 1, 2 and 8 channels (64/576/4160 B), csr and fence.  VC_STATIC A/B (e1fwd15): plain and --vcstatic both PASS at 500 rounds and in
the 12000-cars-per-channel x 2 channels 4160 B stress (23.1 s vs 23.2 s, host-bound): no ordering failure without the flag, no hang
or failure with it, so there is no reason to turn it on.  (Absence of failures is evidence, not proof; the flag's performance /
deadlock behaviour with other NoC features remains uncharacterised.)
One-way E1 hop latency (reflect, no regen, K=1, 1 channel, e1fwd15 csr): 16 B 543 ns, 64 B 554, 576 B 600, 4160 B 649 ns (the
size-dependent part is only ~100 ns for 4160 B).  K=2: 267-326 ns/hop, K=4: 131-171 ns/hop aggregate.  Idle e1fwd15 csr: 139 ns/sweep
at 1 ch, 983 ns at 8 ch (matches e1fwd14 within noise).  Regressions (route, reflect --regen 0 BAD) still PASS on e1fwd15.
Net: T6-bypass data path A-E1 -> B-E1 -> T6 is functionally proven with the host standing in for the sending T6; what is missing for a
true T6 -> T6 path is the sending T6's NoC write into the E1 OUT ring (ERISC_SEAM_DESIGN sec 11).

### 9.9 Stage 3a: bare-metal T6 ping-pong (t6pp, ethfwd17 --pp, expts12.sh) -- written 2026-11-16, NOT run on hardware

Goal: take the host out of car management without touching MFMx.  t6pp/t6pp.c is a tiny BRISC-only image (sfpi gcc, sh t6pp/build.sh,
2268 B, linked at L1 0 = BRISC's fixed reset address, no .data/.bss).  Startup mirrors the parts of srcs/cross/src/_BUD.S that matter for one
hart: sp = 0xFFB02000 (top of the 8 KiB local RAM, = __stack_top8K), CSR 0x7c0 bit 3 set (L0 data cache off, as _BUD.S does on every hart);
it does NOT do gp setup, bss zeroing (it has none), C++ ctors, t6inithostblock, or the other-harts release (_BUD.S's hartB releases T0-T2/NC by
editing soft-reset 0xFFB121B0; here the host tool holds the other four cores in reset and releases bit 11 alone).  Eventual convergence into
_BUD.S: the t6pp loop is what a hartB/NC task would do for the seam (poll receive slot -> NoC write to the E1), the NoC sequence being
NRI3::initiateWriteToT6 (NIU0 cmd buf 3) byte for byte.
Protocol: each tile keeps a receive ring of K cars per channel in its OWN L1; the local E1 (ROUTER, L_OUT|L_IN_NOC, ring depth K) NoC-writes
arriving cars into it and ships whatever the T6 NoC-writes into the E1 OUT ring (slot index preserved end to end).  Initiator seeds K cars
(nonce 1, state 3, payload payload_word(1,w,(c<<2)|i)); both ends handle a NEW COMPLETE car (header == ankle == footer, header != last) by
optionally verifying payload (every --ppve), flipping nonce+1 / state 3<->4 in place (ankle, footer, header order), reading the header back
as a dependency token, and NoC-writing the slot to the E1 OUT slot.  Control block at T6 L1 0x8000 (layout in t6pp.c header).
Native check: t6pp/hosttest.c (two tiles + a fake E1 delivering word by word in ascending order, i.e. header first) passes for tcms 0,1,2,3,
10,66 x ring depth 1,2,4 x verify off/every car (payload errors 0, partial path exercised).
ethfwd17.cpp = ethfwd16 + --pp --t6-image FILE --t6a X:Y --t6b X:Y --t6-addr --ppve: it owns the two tiles (all five cores in reset, image
readback-verified, control block + rings zeroed, BRISC released alone, everything restored at exit; soft-reset writes allowlisted to
{orig, orig|0x47800, that with only bit 11 clear}).  Tile validation: Tensix columns x 1..7 and 10..16, rows y 2..11 (others are refused
with exit 2 -- never write soft-reset on an ARC/DRAM/PCIe tile); a HARVESTED tile gives exit code 4 ("tile unavailable") after restoring,
which expts12.sh treats as a warning.  tt-kmd NOC_CLEANUP covers only the E1; if the tool is KILLED the T6 BRISC keeps spinning harmlessly.
expts12.sh: first car (1 ch, K=1), sizes/K sweeps, 2 and 8 channels, verify-every-car, fence-mode E1, then six tile-pair flex runs (near,
far, cross-corner, mid) chosen to vary NoC route lengths.  Reported: round trips/s, per-car round trip = K*nch/(A handled per s), payload
checks/BAD, noc-fail/txfail, partial sightings, stop acks.  Unknowns until the first run: BRISC start details (reset at 0 per _BUD.S; release
of bit 11 alone per CodeManager::releaseTheHounds), whether a Tensix NoC write into the E1's L1 behaves like the host's PCIe-NoC write, NoC1
vs NoC0 routing (t6pp always uses NoC0; MFMx's NRI3 picks per route length via preferNoC).

### 9.10 Hart / NIU-buffer probing (t6pp2, ethfwd18, expts13.sh) -- written 2026-11-16, NOT run on hardware

Prompted by the user's review of 9.9: (1) NRI3::initiateWriteToT6 asserts it runs on hart NC (NRIUtils.cpp: "nri3 reserved for hNC").  That is
a MFMx software convention; whether hardware restricts which harts may drive which NIU command buffers is not established here, so t6pp2.c has
entry points for BOTH harts (BRISC at address 0, NCRISC at address 16 like _BUD.S) and a control word (+0x48) selecting the NIU command buffer.
ethfwd18 adds --t6-hart b|nc (nc: NCRISC_RESET_PC 0xFFB12238 = 16, override 0xFFB1223C = 1, release soft-reset bit 18 alone; both registers
restored at exit, cores held first) and --nri-buf 0..3.  expts13.sh probes NC/3 (what MFMx does), B/3, NC/0, B/0, B/1, B/2 with one car, uses the
first variant that works for the full suite and stops with a banner if none does.
(2) OurTLBs.cpp:966+ (resetTheFleet) sets TRISC0-2 reset PCs 4/8/12, NCRISC 16, with overrides, leaving B at its fixed 0: exactly the table t6pp2
and ethfwd18 reproduce for NC; the eventual flow into _BUD.S inherits it.
(3) Idea, not implemented: give the E1 image a real crt0: .data with VMA in the 8 KiB local RAM (0xFFB00000..) and LMA in L1, copied at start
before the queue/CTL space is zeroed.  Feasible; notes: the LMA should sit right after .text inside the 0x4000..0x7FFF image window (objcopy -O binary
fills gaps, so an LMA far away in queue space would bloat the .bin) unless that window gets tight; build.sh's .data/.bss refusal would become a
local-RAM budget check (data + bss + worst-case stack <= 8 KiB); today's e1fwd14/15 keep all hot state on the stack already.

### 9.11 out13 (expts13.sh, 2026-11-16): stage 3a WORKS on hardware -- all 17 steps PASS, first try

Bare-metal t6pp2 on a Tensix tile of EACH card (card 0 and 1, link 0:1:6) ping-pongs MFMx-format cars T6 -> E1 -> link -> E1 -> T6 -> E1 -> link -> E1 -> T6 with
no host in the data path.  All six probes worked: hart NC and hart B, NIU command buffers 0, 1, 2 and 3 (NC/3 = what MFMx does), identical rates (~2.6 us per
64 B K=1 round trip each).  So on this hardware nothing restricts BRISC from driving cmd buffer 3 (only one hart per tile was running, so sharing between
harts is untested).  Full suite ran on NC/3.  Totals over the 51 measurement points: ~29 M round trips completed at A, 4,401,468 payload checks, 0 BAD,
0 noc-fail, 0 txfail, all stop acks received.
Latency (per car, one full round trip = two seam crossings; 1 channel): 16 B 2.59 us, 64 B 2.60, 576 B 2.92, 4160 B 6.00 us at K=1.  One-way T6->T6 across the
seam is therefore ~1.3 us (64 B) to ~3.0 us (4160 B).  Of the 2.6 us: 2 x ~0.55 us E1-to-E1 hops (9.5), the rest is 4 NoC writes + T6/E1 polling.
Throughput (round trips/s, total): 1 ch K=4: 16 B 1.36 M, 64 B 1.30 M, 576 B 0.90 M, 4160 B 0.278 M; 2 ch K=4: 64 B 1.78 M, 576 B 1.16 M, 4160 B 0.30 M;
8 ch K=4: 64 B 2.12 M, 4160 B 0.547 M (~4.5 GB/s of car payload across the link, both directions combined).  K=2 doubles K=1 until the E1 saturates.
Tile position does NOT matter at this precision: near (1,2)/(1,2), (7,2)/(7,2), far (16,11)/(16,11), cross (1,11)/(16,2) and mid (10,6)/(4,6) all gave 2.59-2.61 us
(64 B, K=1) and 5.97-6.00 us (4160 B, K=1); NoC distance is in the noise next to the E1/polling costs.  Fence-mode E1 equals --csr.  Verifying EVERY car's payload
in the T6 (--ppve 1) cuts 4160 B K=4 on 2 ch from 0.30 M to 0.045 M round trips/s: the check loop costs more than the transport, so production code should not.
"Partial sightings" (a slot whose header has landed but header/ankle/footer do not all match yet) are COMMON: 6.3 M over the run, roughly 35-40% of 64 B cars.  So cars
really do become visible piecewise on the T6 and the header == ankle == footer rule is doing real work; with 4.4 M verified payloads and 0 bad it has never let a
torn car through.  This is the strongest evidence yet for the two-stream argument (ERISC_NOTES sec 3), still evidence rather than proof.
What this does NOT yet cover: MFMx's own protocol layers (EPs, PLL, phase handling, SuperCycle), many hubs at once, the sending T6 being MFMx code (shipTCTo ETH branch),
E1 management from the MFMx host, link retrain/failure handling, the y seam and diagonals.

### 9.12 e1fwd16 / ethfwd19 / expts14.sh: BH-55 workaround, E1 over NoC1, NIU dump, link health -- written 2026-11-16, NOT run on hardware

Prompted by the user's pointer to boopdotpng/tenstorrent-docs hardware/erisc-cores-and-ethernet-launch.md and tt-metal issue #28366 (read via a summarising
fetch tool, so details are the summarised content).  What the doc confirms: E1 = subordinate ERISC (DM1) with reset PC 0xFFB14008 (DM0: 0xFFB14000); 8 KB
local RAM at 0xFFB00000; tt-metal's boot copies the data image from L1 scratch into local memory (supports the crt0 idea in 9.10); syseng reserves
0x70000-0x7FFFF (boot results 0x7CC00, heartbeat 0x7CC70, mailbox 0x7D000, base-FW dynamic NoC counters 0x7D040); TXQ sequence as ours (STATUS bit 16 =
command ongoing; 16 B minimum); ETH cores cannot receive NoC multicast (unicast only); ETH translated coords are (20+channel, 25) while we use raw NoC0
(x,1), which works.  New: BH-55 -- "polling ETH_TXQ_CMD for busy is unreliable on BH; do a dummy read of ETH_TXQ_CMD, then check STATUS bit 16".  We poll STATUS
bit 16 but never did the dummy read.  Issue #28366: tt-metal moved its ERISC fabric to NoC1 on Blackhole because base firmware on E0 "may use NoC 0 at any
time"; its hang was a software NoC counter out of step with the NIU hardware counter (fabric skips the nonposted-write counter); closed Sep 2025.  Our E1
keeps no such counters (posted writes, only the command buffer's busy bit is polled), but it DOES share NIU0 and command buffer 3 with whatever base FW does.
e1fwd16.c = e1fwd15 + F_TXQFIX (16: dummy TXQ2 CMD read before every STATUS poll), F_NOC1 (32: NoC writes over NIU1 0xFFB30000, node ids still raw NoC0
coordinates -- UNPROVEN) and CTL+0x58 = NIU command buffer (0 default 3, 0xFF = buffer 0, 1..3); image id 0xE1F00016; hosttest16.c covers the flag paths logically
(not the register bases) and passes.  ethfwd19.cpp adds --txqfix, --e1-noc 0|1, --e1-nri-buf N, --dump-niu (READ-ONLY NIU0/NIU1 command-buffer registers
of the ETH tile on both cards: run before any E1 use; non-zero buffers show what base FW has driven) and, on every run, read-only link-health snapshots before
and after (port status, heartbeat 0x7CC70, retrain count 0x7CE00, FEC corrected/uncorrected 0x7CE90/0x7CE98, dyn NoC words 0x7D040..) with deltas; it never
writes them.  expts14.sh: NIU dump; BH-55 A/B on the same image (idle, reflect speed, route, reflect --regen, T6 ping-pong); an E1-over-NoC1 probe (larger NoC1
steps only if it works; tile 1:2 is mirror-safe: its NoC1 mirror (15,9) is another Tensix tile); two 60 s soaks with link-health deltas.
Interpretation guide: a slower --txqfix run measures the BH-55 cost; any BAD/timeout only under --txqfix would mean the workaround itself matters; a NoC1 probe
that times out means NIU1 does not deliver raw-NoC0-coordinate writes from an ETH tile as configured (T6->ETH over NoC1 is a separate, still-untested question).

### 9.13 out14 (expts14.sh, 2026-11-16): all 15 hardware steps PASS

BH-55 (--txqfix, e1fwd16, same image A/B): functionally identical -- no BAD, no timeout, txfail 0 in route, reflect --regen, reflect, T6 ping-pong -- so there is no
evidence the dummy CMD read is needed.  Cost: idle sweep unchanged (139 ns, no TX in that path); reflect K=4 2 ch: 64/576 B 7.95 -> 7.60 M hops/s (-4%),
4160 B 5.69 -> 5.09 M (-10.5%, three chunks = three dummy reads); T6 ping-pong 64 B 1.80 -> 1.71 M round trips/s (-5%), 4160 B unchanged (not E1-bound).  Default stays off.
E1 NoC writes over NIU1 (--e1-noc 1, raw NoC0 coordinates, T6 tile 1,2): WORKED -- noc test 1, 2 and 8 channels (64/576/4160 B) 0 bad / 0 timeouts, and the
T6 ping-pong with the E1 on NoC1 matched NoC0 exactly (64 B K=4 1.803 M round trips/s both ways).  So firmware's coordinate translation makes NIU1 accept the same
node ids for ETH -> T6 as MFMx assumes for T6 -> T6; E1 can avoid sharing NIU0 with base FW at no cost.  (T6 -> ETH over NoC1 is still untested.)
NIU dump before any E1 use: NIU0 and NIU1 command buffers 0..3 all zero on both cards (base FW leaves no trace at rest).  Not yet shown that the dump is sensitive:
run ethfwd20 --dump-niu-after after a --noc run (card B's E1 should leave its last command in the buffer it used; card A, which does no NoC writes, is the control).
Link health: retrain +0, FEC uncorrected +0, port stays 1, heartbeat advances, over ~2 minutes of E1 traffic including two 60 s soaks.  FEC corrected rises at a steady
~170 k/s (card A) and ~280 k/s (card B) -- corrected codewords are normal on this link and the rate did not visibly depend on traffic (idle runs vs soaks).  NOTE: ethfwd19
printed every delta with the wrong sign (before - after) because its 'after' call swapped the snapshots, so out14 shows FEC "decreases": the absolute values in the 'before:'/
'after:' lines are right and rise.  Fixed in ethfwd20.  The words at 0x7D040.. did not change in any run, but they look like high-entropy data, not counters, so "base-FW
dynamic NoC counters" is only the tt-metal header's name for them; no conclusion about base-FW NoC activity from them.
Soaks (60 s each): reflect K=4 2 ch 576 B 7.947 M hops/s, identical to the 1 s run, ~238 M hops per channel, txfail 0; T6 ping-pong K=4 2 ch 576 B 1.157 M round trips/s
(69.4 M round trips), identical to the short run, 0 bad / 0 noc-fail.  No degradation, retrain or uncorrectable errors.

### 9.14 mfmxctl eth-seam runs (2026-11-17): the T6-side wiring runs on hardware, T6s idle

Five runs of `mfmxctl.py --layout seamtest --boot -s config/seamtest.cmd` (chips 0 and 1, chip 1 east of chip 0, links at eth x = 6, 7, 10, 11, T6s idle across the
seam, 60 s soak with `seams` three times, then stop and `quit close`).  Each run: tt-smi -r, start (declare seams, deploy both chips, bring up all four links, release
the T6s, 140/140 T6 acks on both chips), soak, stop (links torn down), close.  Run 1 aborted inside 'stop' (bug 1 below), before teardown; runs 2-5 tore
the links down; runs 2 and 3 still aborted at process exit (bugs 2 and 3); runs 4 and 5 (5 = the slice 1 build) exit clean.
- Bring-up: all four links passed every stage on every run: validate (link Up, queue 2 pristine), queue-2 smoke test both ways (64 B each way), E1 deploy with the image
  id check, channel setup from the planner, go.  Links 7, 10 and 11 had never been used before this; they behave like 6.
- Health, 8 link ends x 3 samples x 5 runs: port 1, retrain 0, FEC uncorrected 0, heartbeat words keep changing (0xABCD0000 | a wrapping 16-bit counter).  FEC corrected
  per second, steady between samples and between runs: eth6 ~243 k (card 0) / ~332 k (card 1); eth7 ~447 k / ~680 k; eth10 ~611 k / ~437 k; eth11 ~945 k / ~375 k.  So the
  other links correct 2-4x more than eth6 (card 0's eth11 is ~4x eth6).  Normal for a FEC link and uncorrected stays 0, but eth11 is the one to watch in a longer soak.
- Hub side: the hub log shows the seam endpoint configured at the top-row edge hubs of both cards: chip 0 hub NoC (16,3) Dir8 ET8 and chip 1 hub (2,3) WT8, both on ETH tile
  (6,1) channel 0, weAreIn 1 / 0 (opposite, as required).  Only those two of the ten edge hubs appear: mfmxctl's log collection (slowscan) reaches few tiles in a short
  run and HB / LOG output is gappy, so the other eight are unobserved, not failing.  No FAIL / ASSERT lines anywhere.
- Bugs this found, none in the seam code: (1) CodeManager::newFails() began with FAIL(INCOMPLETE_CODE) although its body is #if 0, and Blackhole::stepRetreat calls it on
  every retreat, so any 'stop' or 'close' aborted (fixed 192a548); (2) QuietBox exited with its InterHub hack thread joinable (~QuietBox empty, _stopInterHubHackThread never
  defined; fixed a4e7c41, qb.shutdown is now bound); (3) every hub's ZHostDecompressor thread was never stopped (fixed c942e6f).
- Not shown: cars crossing.  config/seamtest.cmd never starts a SuperCycle, and the cache loaders only run on NEW_LEADER, so the global supercell parity and the Dir4
  cache loaders of slice 1 (ERISC_SEAM_DESIGN.md section 16) were not exercised.  The `seams` channel counters were 0 throughout, as expected.

### 9.15 t6pp3 / ethfwd21 / expts15.sh: T6 -> ERISC over NoC1 -- run 2026-11-21: FAILED, mis-addressed (see 9.16)
- Question: does a Tensix NoC write INTO an ETH tile work over NIU1 (0xFFB30000), node ids still raw NoC0 coordinates (as MFMx passes them to NIU1 for T6 ->
  T6)?  NRI3::initiateWriteToEthL1 is forced to NoC0 (NRIUtils.cpp `usenoc = 0u`) only because NoC1 -> ETH was never tried.  The wish: reflect the spatial
  layout in the communications design, i.e. let preferNoC choose (the torus wraps in Y: upper Tensix rows are nearer the ETH row y=1 over NoC1, lower rows over
  NoC0).  E1 -> T6 over NoC1 is proven (9.13); this is the other leg.
- Files (all new, nothing existing changed): t6pp/t6pp3.c (t6pp2 + control word +0x4C: 0 = NIU0, 1 = NIU1 for the image's writes to the E1; id 0x76500003),
  t6pp/hosttest3.c (native harness, in run_native_tests.sh), ethfwd21.cpp (ethfwd20 + `--t6-noc 0|1`, needs t6pp3), expts15.sh.
- expts15.sh: build, read-only NIU dump, dry run, CONTROL (T6 over NoC0), PROBE (T6 over NoC1, one channel, 64 B; a failing probe is reported and the batch
  ends with the control-only summary); if it worked: 64/576/4160 B on two channels with the E1 reply over NoC0 and over NoC1, three tile pairs (3:2 upper row,
  5:6 middle, 12:10 lower row; a harvested tile is a WARNING), a lower-row control, and a 60 s soak, each with link-health deltas.
- Hazard: a write that does not reach the ETH tile may land on another tile (the NoC1 mirror of ETH (x,1) is a Tensix tile in row 10) or nowhere; use FREE
  tiles.  Expected failure mode: a timeout or bad payload in the RESULT line.
- If it passes: make initiateWriteToEthL1 use preferNoC (keep the ETH asserts), as a constant, not a flag (the host flags byte is full); if it fails, keep
  NoC0 and record why here.

### 9.16 ethfwd22 / expts16.sh: T6 -> ERISC over NoC1 with the mirrored ETH node -- run 2026-11-21: PASSED
- Why 9.15 failed: it sent the ETH node as NoC0 (6,1).  The NIU config has coordinate translation ON (0x6005, bit 14), which gives Tensix tiles one shared
  coordinate system on both NoCs, but rows Y=0/1 (PCIe/ARC/ETH) are NOT translated: there X is the raw coordinate of the NoC in use.  NoC1 mirrors raw X
  (x1 = 16 - x0) and leaves Y alone (tt-isa-documentation BlackholeA0/NoC/Coordinates.md; NoC_BH_1_Y01Coords.svg).  So NoC1 (6,1) is E13, not our E1 (NoC0
  (6,1) = E10, NoC1 (10,1)).  The 9.15 probe wrote one 64 B car into E13's L1 at 0x10000 (link counters unaffected) and nothing arrived: 0 round trips.
  (The 9.15 hazard note about a Tensix tile in row 10 assumed Y flips too; for rows 0/1 it does not.)
- ethfwd22 (ethfwd21 + node X sent to the T6 image is 16 - x with --t6-noc 1; `--t6-e1x N` overrides; the unmirrored node over NIU1 is REFUSED), expts16.sh
  (same steps as expts15.sh; failed probe stops the batch).  Result (out16): control and every NoC1 step PASS with 0 BAD payloads, 0 noc-fail, 0 txfail:
  probe 0.37 M round trips/s (1 ch, 64 B); 64/576/4160 B on 2 ch about 1.80 / 1.16 / 0.30 M round trips/s, equal to the NoC0 control; E1 replies over NoC0
  and NoC1; tile rows y=2/6/10; 60 s soak 69.5 M round trips, 8.7 M payload checks.  No retrains, no uncorrected errors.
- All four T6 <-> ETH legs (T6 -> E1, E1 -> T6, each over NoC0 and NoC1) now work.  NRI3::initiateWriteToEthL1 now uses preferNoC on NoC0 coordinates and
  sends the mirrored X when NoC1 is chosen (NRIUtils.cpp, 40864a7).  OBSERVED in MFMx 2026-11-22: seam21-13, one hour on the full QB, 432,184 seam cars
  out = in = written to the T6s, txfail/nocfail 0, AER 0.968, consensus unchanged (see OPEN_ISSUES).

### 9.17 arcpeek: read-only look at the ARC tile and the AI clock -- written 2026-11-22, RUN 2026-11-22 (all four cards)
- Goal: find the current AICLK (the chip runs at 800 MHz as far as the user knows) before anyone thinks about changing it.  Tool: `srcs/host/tools/arcpeek.cpp`
  (`g++ -std=c++17 -O2 -Wall -Wextra -Werror arcpeek.cpp -o arcpeek`; `./arcpeek`, `--dev N`, `--aiclk`, `--mock`).  READ-ONLY: it sends no ARC message, rings no
  doorbell and writes nothing; it maps 2 MiB tt-kmd TLB windows at NoC0 (8,0) (the ARC tile) like the ethfwd tools.  Run it ONLY on an idle card
  (no mfmxctl, no other tool): running it beside a live run is untested.  The sysfs files tt_aiclk / tt_axiclk / tt_arcclk are safe any time.
- How (from tt-kmd blackhole.c and the tt-system-firmware telemetry docs, read not run): reset-unit scratch registers 0x80030400 + 4N: N=2 ARC_BOOT_STATUS (bit0
  READY_FOR_MSG, bits2:1 HW_INIT), N=4 ARC_ERROR_STATUS0 (bit1 cable fault), N=11 ARC_MSG_QCB_PTR, N=12 ARC_TELEMETRY_DATA, N=13 ARC_TELEMETRY_PTR.  The table at
  PTR: word0 version (major bits 23:16), word1 entry count, then u32 entries from PTR+8, low 16 bits tag id, high 16 bits offset in words from the data base; value =
  u32 at data base + 4*offset.  Tag ids from tt-kmd telemetry.h: AICLK 14, AXICLK 15, ARCCLK 16, AICLK_LIMIT_MAX 63, TIMER_HEARTBEAT 32, POWER 7, ASIC_TEMP 11, ...
- The AICLK word's layout is NOT documented in what we read.  arcpeek prints it raw and, as a hint only, low16 (expected: MHz) and high16.  Check against what the
  kernel reports: `cat /sys/class/tenstorrent/tenstorrent!0/tt_aiclk` (tt-kmd sysfs attribute `tt_aiclk`, decimal; exact path varies).
- Messages (NOT used by arcpeek; for later): queue control block pointer in SCRATCH 11 (first word queue base, low 8 bits of the second word = entry count),
  doorbell = write 0 to ARC_MSI_FIFO 0x800B0000 over the NoC; types seen in the kernel driver: 0xA0 ASIC state 0, 0xA3 state 3, 0xC1 watchdog timeout, 0x56 reset,
  0x90 test, 0x21 power setting, 0xC7 firmware log.  The kernel driver never sends AICLK busy/idle or force.  Firmware (aiclk_ppm.c): target = highest enabled min
  arbiter capped by the lowest enabled max arbiter (voltage-derived, host fmax), a forced clock overrides all but cannot leave [fmin,fmax] from the firmware
  table, GO_BUSY sets the busy arbiter to fmax (idle: fmin) or when the AICLK power domain is active.
- RESULT (arcpeek10.out, idle QB, all four cards, same on each): ARC_BOOT_STATUS 0x5 (ready_for_msg=1, hw_init=2), ERROR_STATUS0 0, telemetry table
  version 0x100 (major 0), 65 entries, the heartbeat tag advances (+2 per 200 ms) on every card.  AICLK raw 0x320 = 800: the low 16 bits ARE plain MHz and
  high16 = 0; this equals the kernel's sysfs tt_aiclk (800).  AXICLK 960, ARCCLK 800 (tags 17..20 also read 800, other clock domains, not identified).
  AICLK_LIMIT_MAX (tag 63) = 1350 on all four: a 1.3 GHz request is inside the firmware's range, though thermal/TDP arbiters may still clip it.
  VCORE 740 (mV), POWER 36..37 (tt-smi "Core Power (W)"), ASIC_TEMP (tag 11) is signed 16.16 fixed point degrees C: 0x0023899a = 35.54, card 3
  0x0023ec0a = 35.92, matching tt-smi "Core Temp (C)" 35.1..36.1.  FAN_RPM 1882.  THM_LIMIT_THROTTLE 90, THM_LIMIT_SHUTDOWN 110, TDC_LIMIT_MAX 200.
- tt-smi -s snapshot (tti-snapshot.json, idle QB, tt-smi 3.0.27, pyluwen 0.7.11, TT-KMD 2.3.0, FW bundle 18.8.0.0, boards p150b): same telemetry words as arcpeek,
  and tt-smi device N = arcpeek card N = /dev/tenstorrent/N (BOARD_ID words match).  Tags 17..20 are L2CPUCLK0..3 (all 800).  BOARD_POWER_LIMIT 0x12c = 300 W,
  INPUT_POWER 0x83..0x86 = 131..134 W (whole board, idle), core POWER 34..37 W at VCORE 0.74 V, VDD range 0.70..0.90 V (VDD_LIMITS 0x038402bc), thermal throttle
  90 C vs 36 C now, THM limit 110.  Its limits block shows tdp_limit, tdc_limit and asic_fmax as 0: those tags are not populated in this firmware, so they say
  nothing.  Rough upper bound for 1.3 GHz at the 0.90 V ceiling: core P ~ f V^2 gives about 37 * 1.625 * 1.48 = 89 W idle-load-equivalent, far under 300 W
  (real load power unknown: no snapshot yet taken during a run).  ENABLED_ETH 0x3edf, TENSIX_ENABLED_COL 0x3fff (14 columns), DDR 16G.
- Not done: reading the ARC from a T6 (a t6pp-style image doing a NoC read of the ARC scratch/CSM).  Tested: native mock walk
  (run_native_tests.sh arcpeek_mock).

### 9.18 arcmsg: first ARC message, TEST + GET_AICLK -- written 2026-11-22, RUN OK on card 0
- Tool `srcs/host/tools/arcmsg.cpp` (`g++ -std=c++17 -O2 -Wall -Wextra -Werror arcmsg.cpp -o arcmsg`; `./arcmsg --dev N [--test] [--aiclk] [--queue Q]`,
  `--mock`).  New name: arcpeek stays strictly read-only.  It WRITES (request slot, queue pointers, doorbell) so it needs `--dev N` and an IDLE card
  (no mfmxctl, tt-smi or other tool: no locking, the kernel and tt-smi use the same queues).  Default queue 3, `--queue` picks another.
- Whitelist of two harmless messages (firmware source, tt-zephyr-platforms msgqueue.c and aiclk_ppm.c): TEST 0x90 (reply data[1] = word1 + 1, data[2] =
  last serial + 1) proves the transport; GET_AICLK 0x34 (reply data[1] = AICLK in MHz, data[2] = mode 1 uncontrolled / 2 PPM forced / 3 PPM unforced).  No
  FORCE_AICLK / GO_BUSY in this tool; a changing tool gets its own new name.
- Protocol (read in the firmware, not yet run): scratch 11 -> 4-word info block {queues base, 4 | 4 << 8, 0, 0}; each queue 72 words: header 8 (0 req_wptr, 1
  resp_rptr host-written; 4 req_rptr, 5 resp_wptr, 6 serial ARC-written), then 4 request slots of 8 words (word0 low byte = command), then 4 response slots of 8
  (word0 low byte = status/exit code).  Pointers run 0..7 (equal = empty, differ by 4 = full), slot = pointer % 4.  Host: write the request, bump req_wptr,
  READ IT BACK (the doorbell is in a different TLB window), write 0 to ARC_MSI_FIFO (0x800B0000, tt-kmd blackhole.c), poll resp_wptr, read the slot, bump
  resp_rptr.  The tool refuses a queue that is not empty, a geometry other than 4x4, a not-ready ARC, and times out after 2 s leaving the request queued.
- Expected on this QB: TEST data[1] = 0x1235 for 0x1234, GET_AICLK 800 MHz (compare tt_aiclk and arcpeek).  The mode value tells whether the firmware's
  DVFS is on (2/3) or the clock is uncontrolled (1); that decides whether FORCE_AICLK can do anything.  Tested: native mock (run_native_tests.sh arcmsg_mock).
- RESULT (card 0, idle, queue 3): info block at 0x10017a08 -> queues at 0x1001b19c, geometry 0x404 (4 queues x 4 slots); queue empty (all pointers 0).  TEST
  data[1] = 0x1235 for 0x1234 (TRANSPORT OK, status 0), then pointers 1/1/1/1; GET_AICLK: status 0, AICLK 800 MHz (agrees with telemetry and sysfs), mode 3 =
  PPM UNFORCED.  So the message transport works as read in the firmware, the firmware's DVFS is on (not "uncontrolled"), and the clock is NOT forced: it is 800
  because the arbiters pick 800 (target = highest enabled min arbiter, capped by lowest enabled max arbiter), not because the chip is stuck.  FORCE_AICLK (which
  overrides the arbiters within [fmin,fmax]) is therefore a meaningful lever.  Only card 0 tried; cards 1-3 not yet.

### 9.19 t6arc / t6arcrun: a Tensix tile reads the ARC telemetry over the NoC -- written 2026-11-22, WORKS (t6arc4, NIU0)
- Goal: prove the T6 -> ARC NoC read path (and give a way to observe AICLK from inside the chip) before any MFMx image depends on it.  Reconnaissance only:
  the ARC is only READ.  Image `srcs/host/tools/t6pp/t6arc.c` (`sh t6pp/build.sh t6arc`, 2456 bytes, image id 0x76500010, runs on BRISC at L1 0), host runner
  `srcs/host/tools/t6arcrun.cpp` (`g++ -std=c++17 -O2 -Wall -Wextra -Werror t6arcrun.cpp -o t6arcrun`).
- Image: NoC read commands on NIU0 (or NIU1, control word) command buffer 3, exactly the NRI3::blockingL1ReadNoC0 sequence (target node = ARC (8,0) = node 8,
  read command, tag 0xe << 10, complete when the NIU outstanding counter for id 0xe is 0, return address = our L1 with the same address mod 16).  One pass =
  4 scratch reads (boot status, queue info, data base, table pointer), the table header (2 words), the 65 table entries in 12-word reads, then one read per
  wanted tag: AICLK 14, AXICLK 15, ARCCLK 16, TIMER_HEARTBEAT 32, AICLK_LIMIT_MAX 63, ASIC_TEMP 11, POWER 7, VCORE 6, tag 17 (L2CPUCLK0).  20 reads per pass.
  Results go to a control block at T6 L1 0x8000 (layout in the t6arc.c header); one pass per rise of `go`.  Status 0 ok, 1 bad pointer, 2 read timeout.
- Host runner: default is a DRY RUN (reads the tile's soft-reset, NIU cfg, prints the plan).  `./t6arcrun --dev 0 --go` (default tile 1:2 NoC0 coordinates,
  `--tile X:Y`, `--noc 0|1`, `--passes N`, default 3, `--image FILE`, default t6pp/t6arc.bin relative to the cwd: run it from srcs/host/tools).  It refuses a
  harvested tile (exit 4) and a tile whose five cores are not all in reset, writes ONLY the soft-reset register (allowlisted values), the image and the control
  block, releases BRISC alone, runs the passes (300 ms apart, so the heartbeat must move), then holds all cores and restores the original soft-reset value;
  tt-kmd is asked (NOC_CLEANUP ioctl 0xFA0E) to restore it if the process dies.  IDLE CARD ONLY.  Verdict line: `T6 READ OK: 3 of 3 passes`.
- Expected on this QB: AICLK raw 0x320 = 800, AXICLK 960, ARCCLK 800, AICLK_LIMIT_MAX 1350, tag 17 = 800, heartbeat moving, same numbers as arcpeek (9.17).
  Then try `--noc 1`: the ARC is in the one column (x = 8) that NoC1 maps to itself, so node 8 should work unchanged.  If a pass reports status 2 the NoC read
  never completed: record the status and which NoC, do not retry in a loop.
- Tested here: native `t6arc_hosttest` (the image compiled for the host against a fake NoC and fake ARC memory laid out like arcpeek10.out: 20 reads, 12 words
  max, node 8, alignment, bad-pointer and stuck-NIU paths) and `t6arcrun_mock` (run_native_tests.sh t6arc); the real image cross-builds with the sfpi gcc.
  Not tested: anything on hardware (does the T6 NIU read of the ARC SRAM/scratch work like the host's; is the 16-byte alignment rule right for reads).
- FIRST HARDWARE RUN (card 0, tile 1:2, NIU0, 2026-11-22): the image starts (ready word, id 0x76500010, csr 0x7c0 0x80020008 = L0 data cache off), the tile
  restores cleanly, 4 NoC reads complete with none failed, and the FIRST one is RIGHT: scratch 2 (boot status) = 0x5, the value arcpeek sees.  But scratch 11,
  12, 13 (queue info, data base, table pointer) came back 0, so the image stopped with status 1 (bad pointer).  So the T6 -> ARC NoC read path works at least for
  one word; why the next three read 0 is NOT known.  Hypotheses: (a) data of a read lands somewhere other than the return address I computed (the mod-16 rule
  for reads); (b) the completion counter drops before later responses arrive; (c) back-to-back reads to the same node.  Next: t6arc2 (image id 0x76500011) +
  t6arcrun2: a debug log of the first 8 reads (address, buffer, the 16 buffer words afterwards) and `--probe` reads (12 words from 0x80030400, 4 from 0x80030430,
  scratch 11 three times, scratch 2 then scratch 12).  Run: `sh t6pp/build.sh t6arc2`, `g++ -std=c++17 -O2 -Wall -Wextra -Werror t6arcrun2.cpp -o t6arcrun2`,
  `./t6arcrun2 --dev 0 --go --passes 1 --probe`.  t6arc and t6arcrun stay as they are.
- t6arc2 probe run (same tile): a 12-word read at 0x80030400 DELIVERED CORRECTLY at the return address: scratch 0 = 0x00110000, scratch 2 = 5, scratch 10 =
  0x0c024a6d, scratch 11 = 0x10017a08 (the queue info pointer arcpeek sees) at buffer words 0, 2, 10, 11.  So block reads, the node id, the NIU0 command
  sequence and the buffer placement are right.  But single-word reads of scratch 11 (0x8003042c, three times) returned 0, a 4-word read at 0x80030430 (scratch
  12..15) returned 0, 0, 0 (and once 0, 0, 5, 0), while a single-word read of scratch 2 (0x80030408) returned 5 each time.  So the failures are not "reads after the
  first".  Open: single-word reads at addresses that are not 8-byte aligned (0x...2c), and reads at/after 0x80030430, return 0 though the data is nonzero when
  the host reads it.  Hypotheses: alignment of small reads in register space; a range limit; timing.  Next: t6arc3 (id 0x76500012) + t6arcrun3 `--probe`: single-word
  reads of scratch 0..15 (also with a delay before the copy), 12-word blocks at 0x...400/410/430, and ARC SRAM reads (table header at 0x1001a094, data at
  0x1001a1a0), `./t6arcrun3 --dev 0 --go --passes 1 --probe` after `sh t6pp/build.sh t6arc3` and `g++ -std=c++17 -O2 -Wall -Wextra -Werror t6arcrun3.cpp -o t6arcrun3`.
- t6arc3 sweep result and DIAGNOSIS (same tile, NIU0): single-word reads of scratch 0..15 right only for N=0 and N=2; 12 words @0x80030400 right; 12 words @0x80030410
  had scratch 10, 11 at buffer words 10, 11 (should be 6, 7: shifted +16 bytes) and zeros elsewhere; 12 words @0x1001a1a0 (telemetry data) had BOARD_ID 0x412 and
  0x3191113f at words 9, 10 (should be 1, 2: shifted +32 bytes); 12 words @0x80030430 all 0 (the data fell outside the window).  All shifts equal source address
  mod 64 (0x410 -> 0x10, 0x1a0 -> 0x20, 0x400 -> 0, 0x428 -> 0x28, 0x42c -> 0x2c).  So the NIU puts returned read data at (return address rounded down to 64) +
  (source address mod 64): the return buffer must match the source MODULO 64 (the Blackhole NoC is 64 bytes wide), not modulo 16 as NRI3::blockingL1ReadNoC0
  assumes (its comment cites the Wormhole rule).  A fake NoC with this landing rule reproduces the t6arc3 failure (status 1) and passes t6arc4.  Fix in
  t6arc4 (image id 0x76500013): buffer = RB + (source mod 64), RB 64-aligned; t6arcrun4 `--probe` repeats the sweep as the check.
  Run: `sh t6pp/build.sh t6arc4 && g++ -std=c++17 -O2 -Wall -Wextra -Werror t6arcrun4.cpp -o t6arcrun4 && ./t6arcrun4 --dev 0 --go --passes 3` (without --probe
  for the real walk; with --probe for the sweep).  Evidence is for reads FROM THE ARC TILE; reads from a T6 L1 may differ (see OPEN_ISSUES).
- t6arc4 RESULT (card 0, tile 1:2, NIU0, 3 passes, no probe): `T6 READ OK: 3 of 3`.  20 NoC reads per pass, 0 failed; the walk found the table at 0x1001a094
  (version 0x100, 65 entries), data base 0x1001a1a0, AICLK tag at offset 14.  From the T6: AICLK raw 0x320 = 800 MHz, AXICLK 960, ARCCLK 800, L2CPUCLK0 800,
  AICLK_LIMIT_MAX 1350, POWER 37, VCORE 739/740, ASIC_TEMP 36.2 C, heartbeat 37653 -> 37656 -> 37659 (moving, 3 per 300 ms pass gap), identical to arcpeek
  and tt-smi.  The mod-64 return rule is confirmed for reads FROM THE ARC TILE (scratch registers and ARC SRAM).  NIU1 (`--noc 1`, same tile, 1 pass) ALSO WORKS: 20 reads, 0 failed, AICLK 800, AXICLK 960, ARCCLK 800, L2CPUCLK0 800, limit 1350, with node (8,0) unchanged (the ARC column maps to itself on NoC1).  Tip: the
  debug log makes the output long; filter with `| grep -E "pass|AICLK|READ|image|restored"`.

### 9.20 hub `.ck` clock mark -- written 2026-11-22, WORKS on all 140 hubs (run 20261122-121809)
- Each hub reads its chip's ARC telemetry (AICLK 14, AXICLK 15, ARCCLK 16, AICLK_LIMIT_MAX 63) once at init and logs `.ck:STATUS:AICLK:AXICLK:ARCCLK:LIMIT.`
  (hex, MHz in the low 16 bits; trailing '.' = end marker; STATUS 0 ok) with the `.cp` mark.  Code: `srcs/cross/src/ArcTelemetry.cpp` / `include/ArcTelemetry.h`
  (built into the hub image only, `codes/hub.cmake`; the ewp and zot images do not contain it), same sequence as the proven t6arc4 (9.19): 3-4 scratch reads,
  the table header, the entries in 12-word chunks, then one read per tag; ~14 NoC reads per hub, NRI3 on hart NC, NoC0 to the ARC at raw (8,0), the return
  buffer 64-aligned and offset by the source address mod 64, bounded spins (a failure leaves a STATUS, never FAIL).  `NRI3::blockingL1ReadNoC0` is untouched
  (OPEN_ISSUES "return buffer alignment").  Read at HE_BORN1 (InterHub.cpp HTOC_INIT) and logged at the first NEW_LEADER (init-time marks do not reach the log).
- Expect on this QB: `.ck:0:320:3c0:320:546.` on all 140 hubs.  Later, after any clock change, the same mark shows the clock the hubs actually run at.
  Cost: 0x660 bytes of hub text plus 128 + 24 bytes of data.  Cross-build of all images passes here; the hub image was NOT run.
- RESULT (t6arc-10.out, MFMX-20261122-121809, seam4, 140 tile logs): all 140 hubs logged `.ck:0:320:3c0:320:546.`: status 0, AICLK 800, AXICLK 960, ARCCLK 800 MHz,
  AICLK_LIMIT_MAX 1350, identical on every chip.  So the hub-side NoC read of the ARC works in MFMx (hart NC, NRI3, mod-64 return buffer) on every hub, with
  the run otherwise normal enough to boot and shut down ("quietbox shutdown: ok").

### 9.21 t6l1rd / t6l1rdrun: how does a NoC read from another T6's L1 land? -- written and RUN 2026-11-22 (card 0, NIU0): reads from a T6's L1 land at the return address
- Question (OPEN_ISSUES "return buffer alignment"): reads from the ARC tile land at (return & ~63) + (source & 63), but NRI3::blockingL1ReadNoC0 matches its buffer
  to the source only modulo 16, and MFMx uses it to read image block headers at 0xb0 (48 mod 64) into privateL1ReadBuffer, which sits at 16 mod 64 in the hub image
  (hub.map 0xc9d50) -- under the ARC rule that read would land 32 bytes off, yet neighbor discovery works.  So the rule for an L1 source probably differs.  This tests it.
- Image `srcs/host/tools/t6pp/t6l1rd.c` (`sh t6pp/build.sh t6l1rd`, id 0x76500020; read-only toward the peer) on tile A reads tile B's L1: B is pre-filled by the host
  with val(a) = a * 0x9E3779B1 ^ 0x5A5A5A5A at 0x20000..0x20100.  Method A = the NRI3 rule: buffer = base + (src & 15), with 4 buffer alignments (base = RB + 16 k),
  16 source offsets (src mod 64 = 0, 4 .. 60) and word counts 1, 4, 12; method B = the ARC rule, buffer = RB + (src & 63).  240 reads; each is judged against val() and,
  if wrong, where the first word landed is searched.  Host tool `t6l1rdrun.cpp` prints the shift tables and a RESULT line: "AT THE RETURN ADDRESS" (mod 16 is enough, the
  hub is fine), "MOD-64 rule too" (blockingL1ReadNoC0 is only right by luck of alignment), or neither.  It also prints the hub's exact case (k=1, source 48 mod 64).
- Run (idle card, from srcs/host/tools): `sh t6pp/build.sh t6l1rd && g++ -std=c++17 -O2 -Wall -Wextra -Werror t6l1rdrun.cpp -o t6l1rdrun && ./t6l1rdrun --dev 0` (dry
  run), then `./t6l1rdrun --dev 0 --go` (tile A 1:2 reads tile B 2:2; `--tile`, `--peer`, `--noc 0|1`).  Touches: A's soft-reset (as t6arcrun), A's L1 image and
  control block, and ONLY B's L1 0x20000..0x20100; restores A on exit; refuses a harvested tile or an A that is not all in reset.
- Tested here: native `t6l1rd_hosttest` (image vs a fake NoC under both landing rules; 240 reads, verdicts and shifts as predicted) and `t6l1rdrun_mock` (both
  rules through the host tool's analysis); the image cross-builds with the sfpi gcc (1248 bytes).  Nothing on hardware yet.
- RESULT (card 0, tile 1:2 reads 2:2, NIU0): 240 reads, 0 failed; method A (NRI3 rule, 4 buffer alignments) 192/192 right, method B 48/48 right, every shift-table cell ok, hub case (k=1, source 48 mod 64) ok.  Reads from another T6's L1 land AT THE RETURN ADDRESS, so NRI3::blockingL1ReadNoC0's mod-16 match is enough; the mod-64 rule is specific to the ARC tile.  No code change needed.  Only NIU0 tested.

### 9.22 t6sjlj / t6sjrun: RISC-V setjmp / longjmp on a T6 hart -- written 2026-11-22, NOT run on hardware
- Why: step 2 of the parked "physics failure recovery" design (OPEN_ISSUES): PHYSICS_FAIL(code) would longjmp to a guard set once per event in Physics::doTransition.  The hub images link no libc, so this tests a hand-written setjmp/longjmp in the build the images use (rv32im_zicsr, ilp32, -O2, -ffreestanding).  No NoC traffic; only the tile's own L1 and stack.
- Image `srcs/host/tools/t6pp/t6sjlj.c` (`sh t6pp/build.sh t6sjlj`, id 0x76500030, 3148 bytes): `t6_setjmp`/`t6_longjmp` in asm save and restore 14 words (ra, sp, s0..s11; no FP on this core), setjmp declared `returns_twice`, longjmp `noreturn`; longjmp(jb,0) returns 1 as in C.  Seven tests: (0) return value 5, (1) 0 maps to 1, (2) asm: s0..s11 patterned, setjmp, all s regs garbaged, longjmp, every s reg and sp checked, (3) unwind from 20 recursion frames (672 bytes of stack), sp back and a volatile changed after setjmp seen, (4) nested guards, (5) 20000 setjmp/longjmp rounds with sp stable, (6) eight C locals held in callee-saved registers survive a longjmp out of a callee.  Timing: 1000 rounds of empty loop / setjmp / setjmp+longjmp read from the wall clock 0xFFB121F0 (low word), baseline subtracted.
- Host tool `t6sjrun.cpp`: `./t6sjrun --dev 0` (dry run), `./t6sjrun --dev 0 --go` (tile 1:2, `--tile X:Y`).  Touches only that tile: soft-reset (BRISC only released, restored on exit, NOC_CLEANUP armed), L1 image and control block 0x8000..0x8300.  Idle card only, never kill -9.  Prints one line per test, the stack used and ticks per round, and a RESULT line.
- Tested here: native `t6sjlj_hosttest` (the same C tests with libc setjmp, -O0 and -O2; the asm and test 2 exist only on the target) and `t6sjrun_mock` (pass and a forced failure).  The image cross-builds with the sfpi gcc.  The asm itself has NOT been executed anywhere yet: hardware is the first test.
- Not covered by this test: a wild pointer or illegal instruction (needs a trap handler), infinite loops, a longjmp across a frame holding a lock, the hub's actual hart configuration (this runs on BRISC of an idle tile, stack at the top of 0xFFB02000).
