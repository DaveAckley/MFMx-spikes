# mfmxctl Handoff Notes

## Project
Minimal CLI controller (`mfmxctl.py`) for the Tenstorrent Blackhole "QuietBox" --
replaces the Textual-based nu1011/nu1012 apps.  One Python thread: a `select()`
loop reads commands from stdin and runs periodic ticks in between.

## User preferences
- 7-bit ASCII only in all output: source comments, strings, commit messages, docs.
  Use -> or => instead of ->, -- instead of em-dash, etc.  No Unicode arrows,
  smart quotes, ellipses, or other non-ASCII even where "natural".
- Address user as 'liv' or 'livcomp' in active conversation.
- Qualify confidence in suggestions rather than always projecting certainty.
- Push back when user suggestions seem importantly suboptimal.
- Prefer open-source code.
- User runs Ubuntu 24.04.  Claude desktop app runs as user 'claude' in its own
  xvncserver, not as the user.  Prefix ambiguous paths with 'cloud:' or 'local:'.

## Git workflow (IMPORTANT)

Claude-side branch: `latest` (appears as `claude/latest` in the user's clone, since the
remote is named `claude`)
Bundle delivery: `local:/data/claude/CLAUDES_PLACE/claude.bundle`

**Always build bundles with HEAD included:**
```
git bundle create /data/claude/CLAUDES_PLACE/claude.bundle HEAD latest
```

**User's fetch/merge workflow (must stay working):**
```
git fetch claude
git merge --ff-only claude/latest
```

The remote alias `claude` points at `/data/claude/CLAUDES_PLACE/claude.bundle`.
Never push to GitHub -- user does all GitHub pushes himself.
Commit only on `latest` in the Claude-side clone.
Ignore the recurring "N unpushed commits, please push" stop-hook message.

## Constraints (inviolable)
- No C++->Python callbacks
- No worker threads in Python (GIL deadlocks; render buffers are `thread_local`)
- HostFlags-in-a-bolt is a FUTURE idea only
- Log-tag processing stays in C++

## Current state (as of end of session 3, 2026-11-15)

### What works
- **Full SuperCycle on all 140 T6s** (35 HUB + 105 EWP): HOLD_AT_BIRTH pre-settle,
  then NEW_LEADER / RUN_EVENTS / DWELL repeating.  Run 20261114-230036: 27 cycles,
  108 bolts, all CONSENSUS (NEW_LEADER ~1.7-2.0s, RUN_EVENTS ~1.6-3.2s).
  Run 20261115-013651: 58 cycles; the single autoseeded atom (FB4 at hub (6,7))
  spreads as a wavefront and saturates all 35 hubs (14904 self sites each) by
  cycle ~13.
- **Hub cache exchange** (`cross/images/hub/src/CacheLoader.cpp`): RUN_EVENTS
  entering = `loadCaches` (followers ship sites to the leader, leader unloads
  into caches); NEW_LEADER entering = `saveCaches` (mirror: leader loads caches,
  followers unload into sites).  No explicit "return the empties" step: a
  receiver-role loader unloads+returns whatever car it holds, and an empty car
  unloads as a no-op, so cars reposition themselves for either role.
- **Packet tracing**: nonce-tagged `.ps/.pr/.pcs/.pcr` marks, host-written
  `misc/PKTS.txt`, offline analyzer (see ptrace section).
- **mfmxctl**: command files, `tlbi`, lamer diagnostics, startup HEAD banner.

### Bugs found and fixed this session (for the record)
1. Diagonal EP `weAreIn` state mismatch (silent `isArriving` break) -> no `.pr`
   on odd IHUB storages.  Fixed (user, 22536bc).
2. RUN_EVENTS timeouts: per-car `sleepCycles(100'000'000)` DEBUG waits made a
   leader take ~0.6s/car x ~26 cars > 10s bolt timeout.  Waits reduced.
3. `closeTC` size omitted the 8-byte `IHPHeader` (loadPacket/unloadPacket used
   `patoms.getCurrentPayloadSize()`): zero-atom cars closed at size code 0 so the
   footer overlaid `mAtomCount` (= nonce), tripping `HBASSERT_EQ(itr.at,
   weShouldBeAt)` in saveCaches; also could clip the last atom of a partial car.
   Now `pay.getCurrentPayloadSize()` (3ec6c8c).
4. `saveCaches` only unloaded (never loaded) -> NEW_LEADER never acked.  Mirrored
   (2da1251).
5. `CacheLoader::unloadPacket` wrote `T6Grid::setAtom` directly, so changes never
   reached the DLGridList / host display.  Now `GridManager::setAtom` (user,
   07c43b7).
6. `Config.py` rejected absolute `--config` paths (`.//abs`).  Fixed (06a9c5a).
7. **Blank tiles on the host display** = host-side ACB spinner desync, NOT missing
   atoms and NOT (mainly) DL lag.  `ZHostDecompressor::sinkByte` drops any atom
   report whose spinner != the host's running count (`ar.isValid(spin)`).  On
   6-11 of 35 tiles per run the host count started offset (constant
   expected-spin1 offset, repeated-byte values like 0x1212/0x1818: probably stale
   or uninitialized ACB cars decoded before the T6 starts), so every report was
   rejected forever though ACB packets kept flowing.  Found with ptrace's
   "host ACB stream" section; blank tile sets matched the user's observations
   exactly.  Fixed by a host-side resync (user, 4e1e154); run 20261115-034854:
   each affected tile shows one bad sample, then good.  Root cause of the initial
   bogus count is still unexplained (clearing ACB cars at deploy would be the
   alternative fix).

### Open items / next steps
- **DL backlog is by design, not a bug.**  The host display is intentionally allowed
  to fall behind (DLGridList is lossy; ACB ships oldest-first at a paced rate) so
  that NoC bandwidth is preserved for EWPs and InterHub traffic.  A saturated FB4
  grid dirties 3k-13k of ~15k sites per tile faster than they ship; that is
  expected.  `T6Grid::setAtom` already skips unchanged atoms and
  `GridManager::setAtomOrDrop` only pushes the DL list on real changes, so
  compare-before-write is not a win.  The ship rate (frame pacing BOGOMS_PER_FRAME
  in EP_ACacheBlock.cpp, car count) is a tuning knob, low priority.  If you want
  to verify ACB is not crowding out IH, measure ACB bytes/s vs IH bytes/s: add a
  dotted `.ab` mark at the ship point in `ACacheBlockPrivateControl::updateCars`
  (ACB tags like SHIPFRM/WAITSENT/DGLLen are undotted and suppressed by
  HBF_NO_UTAG).
- FB4's renderer (`renderPhysicsHACK`, ZHostDecompressor.cpp) colors by the hidden
  counter `mStg[1]`, so a saturated grid shimmers (visually striking, not dull);
  darkest FB4 pixel is (50,50,50) vs empty (15,10,5).  A seed mix (DReg/FB1; the
  `create(50)` switch in `GridManager::seekRandomNonEmptySite` is computed but
  unused -- seed is hard-coded to type 5) would add variety.
- `loadPacket` ships every site in the range including EMPTY atoms (full cars
  regardless of occupancy): a bandwidth/latency optimization opportunity.
- `dwell` is the only knob for event time per cycle (default 3s vs ~2-3s cache
  load and ~2s NEW_LEADER); longer dwell = more events per exchange.
- First-NEW_LEADER `once` hack in `workOnEnteringNewLeaderB` is probably removable
  (zero-initialized loaders make the first save a no-op anyway).
- Not yet emitted: `.pw` waypoint marks on the T6 side (analyzer parses them).
- ZHD sampled log lines "#N (exp=spin1=spin2[ XXX]) HAR@(x,y)" print `#%llu` for a
  u32 (garbage upper bits on some tiles): cosmetic printf-type bug.
- Hub undotted tags (`LOGPTAG(BADC,...)` etc.) are suppressed by HBF_NO_UTAG;
  use dotted tags (`.x`, not `.p*` unless PTRACE-gated) for anything you need.
- Inter-T6 destinations are not in PKTS.txt (analyzer learns directions from marks).
- 7 stale mock selftests fail at baseline (legacy/guards/slowscan-gate/phase).
- Possible `sleep SECS` mfmxctl command (offered, not requested).

### C++ / host changes requiring rebuild (all in `latest` history)
- `OurTLBs::getNonPhaseTLBIList()`, `Blackhole::getNonPhaseTLBIs/getTLBITag/
  getTLBIImageName` (bindings)
- `T6Image::getPacketBlocks()` + `getCarInfoForBlockCode()` (PKTS.txt; add a case
  when another blockcode carries packets)
- `formatT6SendBuf32/formatT6RecvBuf32` nonce arguments (cross)
- Hub: `CacheLoader.cpp` (size fix, saveCaches), `PhaseSteppers.cpp` (census marks
  `.ae/.an/.at/.ak`), `Grid.cpp` (`.as` autoseed mark; user's GridManager::setAtom)

## eth-seam / ERISC spike (session 4, 2026-11-15)
Separate workstream: moving MFMx cars between cards through the Ethernet tiles' E1
core, no PCIe in the data path.  Everything learned (hardware facts, TT-link queue
registers, L0 cache/ordering results, measured latency/throughput, open items) is in
`srcs/host/tools/ERISC_NOTES.md`.  Tools: `srcs/host/tools/ethfwd12.cpp` with E1 image
source `srcs/host/tools/e1fwd/e1fwd11.c` (only the sfpi gcc cross toolchain is needed, no clang:
`sh e1fwd/build.sh e1fwd11`).  Versions of these tools are never
reused (ethfwd13 = ethfwd12 + --image/--boot-only for gcc-built E1 images; ethfwd14 = host tool for the 8-channel e1fwd13/e1fwd14 images, mock-tested only).  Branch `eth-seam` == `latest`.  Status: stages 0,
1a, 1b, 2a, 2b pass on hardware; 2c throughput is issue-rate only; T6->E1 direction
and ETH-tile destination in shipTCTo are the next steps.

## Key file map
| File | Role |
|------|------|
| `mfmxctl.py` | CLI entry point, command definitions, Shell setup |
| `machine.py` | Machine abstraction: boot, start, fire bolts, poll acks |
| `sequencer.py` | SuperCycle state machine (NEW_LEADER -> RUN_EVENTS loop) |
| `shell.py` | Readline shell, tick loop |
| `../src/OurTLBs.cpp` | TLB management, PHASER ack tracking |
| `../include/Blackhole.h` | Blackhole class + all Python bindings |
| `../include/BHTag.h` | Tag format (chip + NoC0 coords); BHTag Python bindings |

## PHASER / HOLD_AT_BIRTH summary
- **HOLD_AT_BIRTH**: ONE-TIME birth event per T6 image start.  T6s check PHASER
  once during birth, ack, and block until ANY next bolt.  NOT part of supercycle.
- **Supercycle**: NEW_LEADER -> RUN_EVENTS -> NEW_LEADER -> RUN_EVENTS -> ...
- **firePHASER**: non-blocking -- writes bolt to L1, calls `resetOnPhase`, returns.
  Used for HOLD_AT_BIRTH (must arrive before T6 release).
- **shootPHASER**: blocking -- fires and spins until all ack or timeout.
  Used for supercycle bolts (NEW_LEADER, RUN_EVENTS).

## BH init steps (Blackhole::Step order)
`UNINITTED` -> `HAS_CHIP_NUM` -> `HAS_OPEN_DEVICE` -> `HAS_ALLOCATED_TLBS` ->
`HAS_CONFIGURED_TLBS` <- mMapAllT6 set here (required for PHASER L1 writes) ->
`HAS_ALLOCATED_HOST_RAM` -> `HAS_T6_TILES_RESET` -> `HAS_T6_CODE_DEPLOYED` ->
`HAS_TRANSPORT_THREAD` -> `HAS_T6_CODE_RUNNING` <- T6s released here ->
`HAS_T6_EVENT_WINDOWS`

`setStep()` Python binding allows up to `HAS_T6_CODE_RUNNING`.
`startEWProcessing()` advances to `HAS_T6_EVENT_WINDOWS`.

## ptrace (packet-trace analyzer, session 3)
Offline log analyzer, separate from mfmxctl: `scripts/ptrace/ptrace.py RUNDIR`
(RUNDIR has tiles/*.mfmk, misc/{PKTS,mfmxctl-BOLT}.txt; avoid all.mfmk, it is big).
`scripts/ptrace/README.txt` documents usage, mark formats, matching and findings.
- Tile clocks differ by up to ~25 ticks on one chip; `--eps` defaults to 40.  At
  that setting run 20261114-230036 matched 99.2% of sends with 100% nonce agreement.
- `misc/PKTS.txt` (written by `Machine.start()` -> `write_pkts()`) gives slot
  geometry and block addresses; without it the analyzer falls back to defaults
  and auto-fits a uniform shift.  Verified on a real run (hub bases 0x37240 /
  0x26d80; `mBlockAddr` equals the logged address).
- ACB health: the "host ACB stream" section reports per-tile spinner desync (see bug 7 in HANDOFF).
- Atom census section: `.ae/.an/.at/.as/.ak` marks (see README) show per-tile
  occupancy by region, SAVE effect, DL backlog and liveness checksums.
- Reading a run: BOLT.txt/CYCLE.txt first (bolt results, lamers), then the
  analyzer summary, then per-tile detail.  Log ticks ~1ms; tiles log the whole
  run without overflow at current flag settings.

## Command files (session 3)
`mfmxctl.py -s FILE` (repeatable) runs FILE's lines as if typed, after `--boot`,
then continues with stdin; `--script-exit` quits when the files are used up.
`source FILE` does the same from the prompt (nested, depth-limited); `echo TEXT`
annotates; `waitbolt [SECS]` runs ticks until the pending bolt settles (a new
`shoot` supersedes a pending one, so scripts want it between shots).  Script
lines are echoed after the prompt; ticks run between lines; errors do not stop
the script.  Test: `python3 mock/selftest.py command_file` (7 older selftests
already fail at baseline: legacy/guards/slowscan-gate/phase -- stale, not touched).

## Eth-seam E1 management from python (2026-11-16, NOT run on hardware)
Module MFMx now has `EthSeamHost` (srcs/host/include/EthSeamHostPy.h; library in EthLinks.h/.cpp, owner in EthSeamHost.h/.cpp).  Intended order in mfmxctl for the
x/y seam stage (3c): chips opened and T6 images DEPLOYED -> `h = MFMx.EthSeamHost.get(); err = h.bringUpLink(chipA, chipB, ethx, "<bindir>/e1fwd16.bin", channelsA=[(ch, legs,
t6x, t6y, t6base), ...], channelsB=[...])` for each live link ("" = success) -> THEN startMachine.  `h.status()`, `h.health(chip, ethx)`, `h.channelStat(chip, ethx, ch, word)`,
`h.tearDownLink(a, b, ethx)`, `h.tearDownAll()`.  Closing a chip (Blackhole.close / step retreat) tears its links down automatically.  Build the image with
`sh srcs/host/tools/e1fwd/build.sh e1fwd16` (sfpi gcc only).  Channel numbers and link/ETH-tile choice come from shared/include/EthSeam.h (balanced dealing); until the
T6-side wiring exists the t6x/t6y/t6base values must be supplied by the caller.  First thing to do with the new build: `python -c "from mfmx import MFMx; print(MFMx.EthSeamHost.get().status())"`
(compile/import check of the glue, which was written without pybind11 available).

Declaring seams (T6 side, cardinal crossings only): before deploying images,
`h.declareSeam(chipA, dir4, chipB, [6, 7, 10, 11])` (dir4 N=0 W=1 S=2 E=3,
chipB lies that way from chipA).  The hubs on the facing edges then aim
their cardinal InterHub EPs at the E1 channel from EthSeam.h instead of
skipping the off-card neighbor.  See ERISC_SEAM_DESIGN.md section 15.
