# START HERE (for Claude sessions on MFMx-spikes)

Read this first.  It is auto-loaded by Claude Code from the repo root.
Everything below is 7-bit ASCII on purpose (the user wants only ASCII in
source, comments, docs and commit messages).  Use AMERICAN spelling in
all new or edited text (center, color, neighbor, behavior; not centre,
colour, neighbour, behaviour).  Old identifiers such as CENTRE_FIRST stay
unless the user asks for a rename.

## 1. CODE FORMATTING (the user cares a lot; applies to all NEW or
##    substantially edited C/C++/Python code)

- About 80 columns, rarely over.
- ONE enum value per line.  ONE data member per line.  One thing per line:
  no packed multi-declarations, no long `&&` chains of writes.
- A trailing one-line `//<` comment on a member or enum value is encouraged
  (try to make it fit in 80 columns).  Longer comments go on the lines
  BEFORE the member.
- NEW or substantially edited MFMx C++ lives in `namespace MFM` and uses the
  itype.h aliases (u8 u16 u32 u64, s8 s16 s32 s64, volatile ones uv32 sv32 ...),
  never `unsigned`/`int`/uint32_t for sized data.  A header that cannot see
  itype.h types is a bug in the header: include "itype.h".  Exceptions:
  extern "C" symbols (declare them INSIDE namespace MFM, the symbol stays
  unmangled) and the bare-metal C images in srcs/host/tools/t6pp.
- Reformat ONLY when asked.  Do not reformat old files on the side.
  Docs and shell scripts are not covered, but keep them readable.
- Do not bend MFMx code to support programs that build without MFMx; pull
  such tools into the tree instead (see srcs/host/tools/run_native_tests.sh).
- HostBlock (srcs/shared/include/HostBlock.h): change it only extremely
  reluctantly.  _BUD.S hard-codes byte offsets 23/24/25 and +4; _BUD.ld.in
  places .hostblock last.  The eth-seam mask lives in the old RSV2/RSV3
  bytes (mEthSeamLo/Hi) and layout/size are static_asserted.
- Never reuse tool or image names: give every new version a new name.

## 2. WORKFLOW RULES

- Branch `eth-seam`.  Claude commits on LOCAL branch `latest` and rebuilds
  the bundle:  sg claude -c 'git bundle create /data/claude/CLAUDES_PLACE/claude.bundle HEAD latest'
  `latest` is ALWAYS Claude's current work (user's choice, 2026-11-25):
  whatever line of work is active is committed on `latest`, so the
  user's  git merge --ff-only claude/latest  always gets it.  The
  diagonal-tiles work (EW lattice, GEOMETRY.md sec 10) is on latest
  since 25a522d; branch `diagonal-tiles` is kept in the bundle too
  (bundle: ... claude.bundle HEAD latest diagonal-tiles, both at the same
  tip while that work is active).  CAUTION for the user: merging
  claude/latest into eth-seam brings the diagonal-tiles commits with it.
  then ALWAYS verifies  git bundle list-heads <bundle>  equals
  `git rev-parse HEAD` (a silent failure once left it stale).
- The user runs `git fetch claude`, `git merge --ff-only claude/latest`,
  pushes to GitHub; the QuietBox (QB) builds from GitHub (build_all.sh) and
  the user runs mfmxctl there.  Results reach Claude ONLY if copied into
  /data/claude/CLAUDES_PLACE/ (seamcycle*.out, MFMX-<date>-<time>/ dirs).
- Merged work is effectively published: never rewrite it; fix forward.
- Stage EXPLICIT paths only; never `git add -A` (build outputs are
  untracked and once got committed).
- Do not start new work (new slices, reformatting) without a go-ahead.
- The host pybind glue (EthSeamHostPy.h, QuietBox.cpp, CodeManager.cpp) cannot
  be compiled on the Claude machine (no pybind11); the QB build is its first
  compile.  T6/E1 images CAN be cross-built here: rsync the tree to a scratch
  dir and `make` in its build_cross.
- When running mfmxctl on the QB include --rtmp so the user can watch.
- HUB DIAGNOSTIC MARKS: LOGPTAG (and friends) go to the log block, which
  the hn hart delivers, and slowscan does NOT capture them.  Only HBPTAG
  (and friends) go into the HostBlock buffer that slowscan reads, so it still
  works when hn is sick.  That ring keeps the LATEST
  marks (packString drops the oldest words when full; a torn mark at the head is OK).  When a mark must survive a hang, emit LOGPTAG and
  HBPTAG back to back.  Slowscan prints with Eprintf (stderr): capture
  stderr too when a run may hang.
- DEFINITION OF DONE for a commit (docs are part of the change):
  * Fixed a bug or finished an item: MOVE its entry from OPEN_ISSUES.md to
    CLOSED_ISSUES.md (dated, measured numbers kept, story cut), or just
    update it if still open; any note in HANDOFF.md too, in the SAME commit.
  * Found a bug or idea and did not fix it: add it to OPEN_ISSUES.md before
    the turn ends; chat is not a record.
  * Changed behaviour, an option or a command: update the CLAUDE.md map or
    run record and any .cmd header that mentions it.
  * Before committing, grep the docs for the touched symbols/options, and
    say in the commit message which docs changed (or "no doc impact").

## 3. DOCUMENT MAP (every file below links to the others)

- GEOMETRY.md  how the grid is tiled onto the box: terms (cell vs
  supercell, SELF, ring, band), the derivation of 137 x 108 / 177 x 148,
  the T6Grid map, caching policies and event bounds, dead bands,
  permanent edges, and every place a geometry change would touch.
- srcs/shared/include/EWLattice.h  (branch diagonal-tiles) the perfect
  radius-T diamond tilings (template EWLatticeT<T>; EWLattice = T 4,
  EWLattice10 = T 10) and rect-snap hub geometry: residue table, site -> EW center,
  CenterIter (row-major walk of the centers in a rectangle), hubRect,
  eventArea, band, storeBox; SHIFT = 18 (two EW layers).  Native test
  srcs/host/tools/ewlattice_test.cpp (in run_native_tests.sh).
- POLICY EWSNAP (branch diagonal-tiles, 2026-11-25, BUILT, not run):
  `policy ewsnap` (or --policy ewsnap; CachePolicyId 2) = four corners on
  the EW lattice: every owned/borrowed/lent set is whole EWs
  (EWLattice.h), bands are minimal and disjoint (CacheLoader walks them
  with EWLattice::BandWalk; the car's startsAt carries the walk ordinal,
  BADC on mismatch), event centers are drawn uniformly from five center
  tables built at hub init (GridManager::buildCenterTables; mark
  `.ct:N0:N1:N2:N3:N4.` hex live centers, no corner / SE / SW / NW / NE,
  logged with `.cp`), a site is reported by the hub owning its EW
  (GridManager::ownsSite), elision keeps a band only when the corner
  repeats.  On this branch the T6Grid ring is 28 for EVERY policy
  (193 x 164; 22 before 2026-11-26), so compare fourcorners and ewsnap on
  the same build.  `policy ewsnap10` (CachePolicyId 3, 2026-11-26,
  BUILT, not run) is the same on radius-10 tiles (EWLattice10, 221
  sites): EWLattice.h is a template on the tile radius (EWLatticeT<T>),
  the hub picks T at init (GridManager::mEWTile, EWAnyBandWalk); events
  keep radius-4 windows.  GEOMETRY.md sec 10 'EWSNAP10'.
  Design and checks: GEOMETRY.md sec 10.  Hub + host change: ./build_all.sh.
  `grid edges [on|off]` (any time, no chips needed): white single-pixel
  hub ownership edges painted on a COPY of each frame
  (srcs/host/python/EWD/ewoverlay.py; zig-zag under ewsnap, rectangles
  otherwise); zoom scans the unpainted image, so the lines never count
  as occupied.  geomlab --crosscheck checks its owner map against
  EWLattice.h.
- scripts/celllab/celllab.py  host-only lab for CELL shapes (CW x CH
  T6s, hub NW or SE): hubs/EWPs per chip, SELF to cover HD, stored grid
  per policy ring, hub L1 fit (model calibrated on the hub image) and
  how much of HD each shape could cover; `--check`, `--fits`,
  `--atom-bytes`, `--no-side-tables`, `--l1`.  GEOMETRY.md sec 9.
- scripts/geomlab/geomlab.py  host-only lab comparing tilings (rect,
  diamonds in brick rows / rotated / clumped, EW-lattice blocks) on dead
  area, ring, exchange, hub balance, neighbor hops, EW packing, with
  pictures; `--check` self-test; `--crosscheck` compares EWLattice.h
  (layout `ewlattice`) with the lab's rect-snap site by site.  Results
  in GEOMETRY.md sec 10.
- srcs/host/tools/ERISC_SEAM_DESIGN.md  design of the eth-seam, sections
  1-18.  Sec 15 Dir4 wiring, 16 slice 1, 17 slice 2 (diagonals), 18 slice 3
  (N/S seam, 2x2 array, centre diagonals).
- srcs/host/tools/ERISC_NOTES.md  hardware facts, ordering lessons, the
  running hardware record (sec 9.x; 9.14 = mfmxctl runs).  Cabling: ring
  0-1-3-2; E/W links x=6,7,10,11; N/S links x=3,4,12,13.
- scripts/feed/keepalive_test.py  simulates the RTMPFeed keepalive pacing
  (old vs new rule, repeats per 30 s).
- DISPLAY CROP (DemoGlobal.h gridCoordToPixel): hub T6Grids are drawn into the
  1920x1080 image minus their N/W cache rings (pixel = hub origin + grid coord
  - OW, where OW is the cache ring width, now 15), so hub origins sit at multiples of 137 x 108 (the cross marks) and the
  permanent-edge rings of the first hub row/column are not shown.  Base width is
  now 137 (FLOOR of 960/7; was 138, the ceiling): chip pitch 959, a full
  QuietBox is 1918 sites wide, so the last 2 image columns stay blank and
  nothing falls off the right; 5 x 108 x 2 = 1080 fits exactly.  Display only:
  qb.setSimAtom keeps its old coordinates.
- CACHE WIDTH / L1 (2026-11-21): ring OW = OH = 20 (15 in ef39772..44b7998, 10 before): hub grid 177 x 148,
  T6Grid 314356 bytes (BlockCode.h literal, the hub static_asserts it) plus the
  4 bytes/site DLGridList; the hub linker L1 is now 3/4 of L1 (_BUD.ld.in; was
  1/2, ~42 KB free at OW=10, hub.map: LiveB 321 KB, EP_InterHub 200 KB, .text
  160 KB).  Dead area is OW-independent (the lent band is lost, the borrowed one
  gained); the cost is L1 and exchange packets (~+50 percent per band).
- HUB GEOMETRY has ONE source: DemoGlobal.h (DG::T6GRID_BASE_WIDTH etc, with
  static_assert guards).  C++ derives from it (EWControl cross marks too).
  Python derives from srcs/host/python/EWD/HubGeometry.py (SITES_PER_HUB,
  base/ring sizes; used by LiveAER, aer.py, RTMPFeed13, the mock).  Never write
  137/108/157/128 as literals; run scripts/hubgeom/hubgeom_check.sh (build_all.sh runs it first and stops; fails if
  Python and C++ disagree).  Numbers quoted in docs (137, 959, 1918, 14796) are
  descriptive of the current width, not inputs.  EXCEPTION, guarded: the T6GRID
  size literal in BlockCode.h (hub image static_assert fails until updated).
- NO AER CHEATING mode (RUN TIME since 2026-11-21; mfmxctl `events honest`
  or `--events honest`, default `shortcut`; set before `start`; host flag
  HBF_EVERY_EVENT, Blackhole.setEveryEvent): the hub then draws ONE random
  center per event request, empty or not, and ships it to an EWP (EWP physics
  already returns an EMPTY-center window unchanged), instead of drawing up to
  100 and crediting each empty hit as a free event (mEWsEmptyCenters).
  mEWsEmptyCenters stays 0, so aer.py/live AER (committed + empty) count only
  round-tripped events: no empty-grid AER inflation after the seed.  The hubs
  log `.ee:1` (0 in shortcut mode) next to `.cp`.  Configs seam11..seam17 say
  `events honest`.  Before the autoseed fires nothing is shipped in either mode.
  RESULT, run 20261119-121915 (seam11, 4 cards, 1918x1080 = 2,071,440 sites (140 hubs x 14796; the stream image is 1920x1080, 2 blank columns), hub
  width 137, fourcorners, FB1, `.ee:1` on all 140 hubs, 14.3 min): live AER
  final 0.973 (1 min), 0.976 (5 min), 0.979 (whole run), flat from the first
  reading (0.982) to the end; aer.py (110 of 140 hubs kept) wall 0.996, running
  1.022, duty 97%, obsolete 0.0%, events per hub spread 0.8% (35.7% in the
  old-mode run 113217).  Old mode, run 113217 last 60 s: wall 1.52 but
  NON-EMPTY-ONLY 1.007 at 72% fill, so the honest bound was always about
  1.0 and the extra was free empty-draw credit (about 1/fill).  Honest
  reading: ~1 AER on a 1918x1080 (nearly HD) grid, with cross-hub and cross-card events,
  FB1 physics (cheap; heavier physics lowers it).  NOTE the grid fills MUCH
  more slowly now (fill 3-9% after 14 min, versus 72% by ~12 min in old mode):
  the old mode gave sparse grids a 1/fill time warp.  Use `seed` or a longer
  run (seam12) to reach a full grid.  Link time: 64 B per NoC tick, so an
  EWPC_EMPTY placeholder (state word only) saves little on the wire.
- NUKE (2026-11-22, run on the QB, works; PHASER arg-count table was misaligned until 132ce06): `nuke [big] [X Y R]` in mfmxctl queues a blast (global site coords, disc) that rides the next NEW_LEADER bolt (words 2..4; NEW_LEADER always carries 5 words) and every hub erases it from its whole grid, caches included, after SAVE and before LOAD; mark `.nk:SCY:SELF:CACHE.` (hex) per touched hub; (The old racy host-side `nuke host` command is gone; EWControl::doNuke stays in the C++ host for the old nu101x scripts.)  Random radius: plain 10-100, `big` 100-1000 (hub ignores blasts outside the grid or with radius over 2000).  `autonuke [on|off|status] [DLO DHI [RLO RHI]]` repeats: after each random delay (default 5-300 s) it queues a blast of random radius (default 40-400) at a random place, then picks the next delay; fires at the next NEW_LEADER, only while the cycle runs.  Test: config/nuke10.cmd.  Details: OPEN_ISSUES "physics failure recovery, nuke, xray".
- QUIET CACHE LOAD/SAVE (2026-11-23, EXPERIMENT, not yet run): CacheLoader::unloadPacket now writes with GridManager::setAtomQuiet (grid only, no DLGridList push), so cache loads and saves no longer paint; only events (writeEW), autoseed and nuke do.  Reason: the host image is a pixel per site and each hub also paints its S/E cache ring (20 sites, pixel = gridc-20) onto its neighbor's pixels, so the owner and a borrower both reported the same site and a stale cache copy could overwrite the owner's report (suspected cause of the 'turds' at nuke-hole edges, found with `zoom map`'s x mod 137 histogram).  Back out by reverting the commit if turds persist or the display stops tracking.
- SELF UNLOADS REPORT (2026-11-24, built, not yet run): CacheLoader::unloadPacket now writes through GridManager::setAtomOrDrop (DL push on change) for sites inside this hub's SELF region, and still setAtomQuiet for cache-ring sites.  A follower's SAVE unload lands in its own SELF; a leader's LOAD unload lands in its cache ring, so in effect only the SAVE path changes.  Why: the control run (no nukes, specks10-13) and the nuke run (specks10-12) showed turds with HUB: EMPTY and all 21 definite SELF turds within 15 sites of the SELF edge (inside the 20-site overlap band; chance about 3e-8), with the DL fully drained and no report loss on the host (.nl/.dl, zhd: 0 desynced, 0 stale).  The quiet SAVE emptied a SELF site with no report from its owner.  Expected: no SELF turds in zoom specks; if turds persist, build the host-directed refresh.  Hub change: ./build_all.sh.
- CYCLE HOLDAT / WAITPAUSE (2026-11-24, built, selftest only): `cycle holdat STEP [SCY]` (STEP = NEW_LEADER, RUN_EVENTS or DWELL) arms a pause for the moment STEP is next once scy >= SCY, then disarms; `cycle holdat off` clears, plain `cycle holdat` shows it, `cycle status` lists it, `cycle stop` and reset clear it.  `cycle waitpause [SECS]` runs ticks until the cycle is paused (or frozen, or SECS elapse; default 3600) so a command file can continue.  `cycle waitpause [SECS] quiet [QSECS]` then also waits (cap 60s) until the host's total report count (zhdSnapshot, summed over hubs) has not changed for QSECS (default 1): the DLs and ACBs have drained, since the hubs keep reporting through the DWELL.  The reply says how long it waited and how many reports arrived meanwhile (sequencer.wait_quiet).  Plain waitpause does NOT wait for the drain.  Scripted turd check: `cycle start`, `cycle holdat RUN_EVENTS 150`, `cycle waitpause quiet`, `zoom specks`.  WHICH PAUSE POINT (2026-11-24, from a live run): the DWELL is the time the hubs spend INSIDE RUN_EVENTS (the RUN_EVENTS bolt's consensus means they have entered it), so a pause with `next: NEW_LEADER` leaves the hubs running events: `waitpause quiet` then saw 16.5M reports in 60 s and never went quiet.  Only a pause with `next: RUN_EVENTS` (holdat RUN_EVENTS) has the hubs out of events, SAVE done, and the DLs/ACBs able to drain, so hub L1 dumps and the picture can agree.  Specks data taken at `next: NEW_LEADER` (specks10-12..15) compared a live picture with L1 dumps taken seconds later.  Python only: no ./build_all.sh.  INTERRUPTING: while `cycle waitpause` (and its quiet wait) runs, typed lines execute as with `wait` (poll_input): `quit` / `q c` ends the wait and shuts down, `cycle stop` ends it (cycle idle), `cycle holdat off` disarms (the wait then lasts to its cap, so follow with `cycle stop` or `cycle hold`), `cycle hold` pauses after the current step.  Ctrl-C also ends the command ('interrupted').  Piped stdin is executed during the wait too, so command files must be given with -s (selftests use run_script).
- SPECKS OWNER LOOKUP (2026-11-24, built, not yet run; host C++ + Python, ./build_all.sh): module `pixelOwner(x,y)` -> None or (chip, tlbi, gridX, gridY): the hub whose SELF sites map onto pixel (x,y) (QuietBox::getT6GridInfoFor, site = pixel - hub origin + SELF_ORIGIN).  `zoom specks` rows whose last painter is a RING report now also read that OWNER hub's SELF site (one more L1 dump per distinct owner) and append `OWNER atCXY: EMPTY | type N`.  Why: for a RING painter `HUB:` is only the borrower's cache copy (cleared after the SAVE), so specks10-16's 17 RING EMPTY rows could not be judged.  Reading (CORRECTED 2026-11-24, see TURD VERDICT): OWNER EMPTY alone is NOT a turd.  Mock: $MFMX_MOCK_OWNER; selftest specks_who.
- PIXEL HISTORY (2026-11-24, built, not yet run; host C++ + Python, ./build_all.sh): QuietBox::pixelHistory keeps the last 4 reports that reached each pixel (accepted or dropped as stale, SELF/RING, atom type, stream epoch), newest first, written in ZHostDecompressor next to acceptPixelEpoch (33 MB, lock-free shift, a torn entry is possible and fine).  Module `pixelHistory(x,y)` -> [(accepted, ring, type, epoch)...]; `zoom specks` rows end with `HIST: R4@e150 < S0@e150 < -S4@e148` (newest first; R/S = RING/SELF; type 0 = empty; leading '-' = dropped as stale).  Why: specks10-17 found turds whose owner SELF site is EMPTY while the pixel shows an atom from a RING report.  Hypothesis to test: owner's empty report and a stale borrower RING atom report arrive in either order with epoch labels of emission, not of change (so equal or later label wins).  Reading: `S0@eN` before (older than) `R4@eM` with M >= N = the stale-RING hypothesis, then fix by stamping epoch-of-change; no empty report at all in the history = the owner never reported (silent hub write, refresh needed).  Mock: $MFMX_MOCK_HIST; selftest specks_who.
- TURD VERDICT, ELISION, TIE RULE REVERTED (2026-11-24, host Python only): the specks runs use `policy fourcorners` + `elide on`.  With elision a band shared by consecutive corners stays with the borrower across SAVE and the next LOAD (CachingPolicy.h keepAcross): the owner's SELF is stale BY DESIGN and the borrower's ring holds the truth, so `OWNER: EMPTY` with a RING atom is probably a correct pixel, not a turd (specks10-17/18/19 over-counted).  `zoom specks` now appends `=> TURD` only when every copy that could hold the truth is EMPTY: a SELF painter's own site, or for a RING painter BOTH the borrower's ring (`HUB:`) and `OWNER:`; `(ring holds it: kept band?)` marks ring-atom + owner-empty.  Count with `grep -c '=> TURD'`.  The SELF-wins-a-tie host rule (b21d7a7) is REVERTED (8ad9137): the report epoch is set once at ENTERING NEW_LEADER (workOnEnteringNewLeaderB), so the SAVE, the LOAD and the whole event phase share one label; `S4@eN` then `R0@eN` is normal (SAVE report, then a live event report) and the rule dropped the live truth, creating 4 SELF turds in specks10-19 (`-R0@e149 < S4@e149`).  Measured: specks10-18 19 of 27 same-epoch S0-then-R4 pairs went away with the rule, so those were probably elision-kept bands, i.e. not turds.  If an epoch-of-change stamp is ever built it needs a SAVE/LOAD-vs-events phase bit besides the cycle number (age + phase bit fit in the unused high byte of AtomReport.mCoord.x, record stays 20 B; 52,392 B of L1 for a u16 per site, about 331 KB free in the hub map).
- EPOCH-OF-CHANGE STAMP (2026-11-24, built, NOT yet run; hub + host + wire, ./build_all.sh for both): every report now says when its site was last WRITTEN, not just when it was emitted.  Hub: `theSiteStamps[177][148]` (u16 per site, 52 KB of L1; the hub map after the change ends at 0xdc054 of the 0x120000 linker L1 limit, about 278 KB free; the linker gives the hub three quarters of the 1.5 MB L1, _BUD.ld.in) is set by `stampSiteWritten` in GridManager::setAtomOrDrop and setAtomQuiet on every actual change (SELF, ring, quiet or reported) to (epoch & 0x7fff) << 1 | phase.  Phase (L1GridManagerControl::mEventsPhase): 0 = SAVE/LOAD, set by setReportEpoch at ENTERING NEW_LEADER; 1 = events, set when RUN_EVENTS entering is ready (writes while draining stay phase 1 so a site's stamps never go backwards).  So SAVE/LOAD(N) < events(N) < SAVE/LOAD(N+1).  Reporter (EP_ACacheBlock) puts `siteStampByte` ((changeEpoch & 0x7f) << 1 | phase, age clamped to STAMP_MAX_AGE 120 epochs, a stamp newer than the announced epoch reads as (now, events)) into the HIGH BYTE OF AtomReport.mCoord.x (sites have x < 256; the epoch marker x = 0xffff is untouched; record stays 20 B, so reports per car is unchanged, the byte is nearly constant so it should compress well: measure pops per epoch vs the .dl marks).  Host (ZHostDecompressor): splits the byte off, AtomReport::changeKey(streamEpoch, byte) rebuilds (changeEpoch << 1 | phase), `acceptPixelEpoch` orders pixels on that key (pixelEpochs now hold change keys), painter word epoch = change epoch.  `zoom specks` HIST entries are now `R4@e86v` / `S0@e85s`: change epoch and phase (v events, s SAVE/LOAD).  Why: the elision/in-flight finding (a stale borrower ring report emitted after the owner's SAVE report carried an equal or newer EMISSION epoch and won; specks10-210: 4 of 5 definite turds had an owner S0 in their history that the stamp orders correctly; the 5th, a lone R4 whose ring later went empty with no report, is not fixed by this).  Hub and host MUST be built together (an old hub's stamp byte reads as garbage ages).  The arithmetic was checked exhaustively in a standalone C++ test (no mismatch over wraps and ages 0..300).  Expect: far fewer `=> TURD` in the quiet-pause specks, zhd `stale` counts above 0.
- HOST-DIRECTED REFRESH (2026-11-24, built, link-checked, selftest; host C++ not compiled here): the host asks a hub to re-report a site, in the 6 spare bytes of the empty car it returns (RESTART_REQ clear): up to 3 slots of (x+1,y+1) in hub grid coords (PT_ACacheBlock.h setRefreshSlot/getRefreshSlot).  Hub: recvTC latches them in ACacheBlockL1Control (queue of 16, `.hr` mark), T0 tryToSendFrame pushFrontC's each onto the DL, so the next report carries the site's current atom and change stamp.  Host: ZHostDecompressor::requestRefresh(chip,tlbi,x,y), stampFeedback fills slots (restart counters only when a restart is requested), the report path counts answers and "caught" (answer empties a pixel that showed an atom); python MFMx.refreshPixel(x,y) asks the last painter (ring or self) AND the owner, refreshStats() = (asked, answered, caught).  `zoom specks refresh` runs it on the listed specks after the evidence is printed, waits 3 s, marks rows REPAIRED.  config/refreshcheck.cmd is the short run.  Also fixed: notePainter stored gy in 7 bits, so whoPainted gy was wrong for hub rows >= 128 (bottom ring); bit 56 now holds the 8th bit.  Turd rate before this: 8 of 249 specks (turdhunt10-10, stamp build).
- IDLE CLOSE CAUSED STREAM RESTARTS, FIXED (2026-11-25, link-checked, NOT yet run): `zhd` restarts done per run: turdhunt10-10 0, -11 0 (before idle close), -12 40 (idle close), -13 11 (+pad), fastframe10-10 6 (+10 ms).  Cause: closeIfData set CLOSABLE on the car being filled, but writeByteToCurrentACB kept adding T1's bytes to it until T0 sealed and shipped it, so bytes written after the seal were lost from the packet and the host decoder desynced (a full car never had this: addByte refuses).  Fix: writeByteToCurrentACB refuses (returns false; the encoder retries) once the car's closable flag is set.  Check: `zhd` restarts back to 0, and the quiet-pause turd count stays low.  Other fastframe10-10 numbers (1142 s, 10 ms frames, 2 nukes): BOLT consensus mean 0.14/0.15 s, 2-4 of 366 over 0.3 s, max 0.32; feed real 133 of 150 frames per 30 s on average, maxgap 0.52 s, maxwrite 0.27 s (so the stream buffering near scy 250 was not a feed gap); AERse 147 at the start to about 66 over the run (not comparable, shortcut mode).
- ACB IDLE PAD (2026-11-24, built, link-checked, not yet run): lzmfmx::encode is LZSS with groups of 8 items staged in code[17] and an 18-byte lookahead, so the last ~7-8 reports before the DL runs dry stay inside the encoder and idle close has nothing to ship.  A true sync flush would need a terminator opcode (encoder + decoder + format, flag day) and the restart path (EOF + reset) is too heavy.  So: when the DL is empty after reports were packed (mDirty), tryToSendFrame PACK (2a) appends PAD_RECORDS = 12 epoch-marker records (idempotent on the host, ZHostDecompressor.cpp:205), once per idle transition; idle close then ships the car.  `.ip` mark every 256 pads.  Host "reports arrived" counters include the markers.  -DACB_IDLE_PAD=0u reverts.  Check: refresh answers inside the quiet pause (`refresh totals` answered == asked of the same pause) and turd counts.
- ACB IDLE CLOSE (2026-11-24, built, RUN turdhunt10-12 on f410d687e8: 0 definite turds in 275 specks, 22 kept-band; earlier runs 8/249 (stamp only), 1/281 (+refresh); refresh STILL answered one pause late (0 answered inside each 3 s), so the idle close did not free a car in a quiet pause: the encoder probably holds a tail, or no car ships when the car is empty.  Next idea: flush the encoder at end of RUN_EVENTS / start of NEW_LEADER): a car ships only when full or marked closable (setRCloseFlag was commented out in tryToSendFrame WAIT, "fill the foggencar"; only the restart EOF path set it), so when the DL ran dry the stream's tail (up to a car of reports) sat unshipped in the hub until traffic resumed.  That may itself make quiet-pause "turds" (the last reports never leave the hub) and it leaves the host no car to return a refresh request in.  Now WAIT, at frame time, marks the car being filled closable if the DL is empty and the car has any bytes (ACacheBlockL1Control::closeIfData, under mLock because T1 adds bytes under it); `.ic` mark every 256.  Under load nothing changes.  -DACB_IDLE_CLOSE=0u reverts.  Unknown: whether lzmfmx holds a few bytes of lookahead that still would not ship.  Check: quiet-pause turd counts and the refresh answering within the 3 s.
- HOST REFRESH FIRST RUN (turdhunt10-11, 97c767f539): 1 definite turd in 281 specks (pause 1, before any refresh had been asked), 0 in pauses 2-8; the earlier run had 8 in 249, so the drop is probably run-to-run variance, not proven to be the refresh.  The refresh does NOT answer inside a quiet pause: `refresh asked N ... 0 repainted empty after 3.0s`, and the totals show each batch answered only at the NEXT pause (answered lags asked by one batch).  Cause: a car is returned to the hub only after the host consumes a shipped car, and the hub ships a car only when it is full (the RClose-every-frame line in tryToSendFrame is commented out), so with the DL empty no cars flow and requests wait on the host.  So `caught` (66 of 325) mixes real repairs with sites that physics emptied after events resumed; do not read it as a repair rate.  Works under traffic; a quiet-pause test needs a hub heartbeat car (not built).  Idle refresh (ACB_REFRESH_PER_FRAME) still 0.  Low priority: see whether t6-exports.h (mfmx_wrapper.cpp:14) can go.
- ACB FRAME 25 ms EXPERIMENT (2026-11-24, 25 ms ran fine; now 10 ms, built, not yet run: is the picture smoother or less laggy?): EP_ACacheBlock.h BOGOMS_PER_FRAME is now ACB_BOGOMS_PER_FRAME, default 25 (was 50).  Why: specks10-15's `.dl` marks show the DL never empties at RUN_EVENTS exit (LEN averages ~9.7k, max ~15k of ~26k sites) and the hub pops ~10.8k sites per 3.3 s epoch (~3.3k/s), about what one ~986 byte car per 50 ms (20 cars/s) carries; host pixels were still changing 55-70 s after a pause.  Test: compare `.dl` LEN at exit and pops per epoch against the 50 ms runs (LEN should fall, pops/epoch rise if pacing was the cap); if nothing moves, the compressor or car returns are the limit (next: `.fr:FRAMES:FULL:TIMEOUT:CARWAIT` marks).  To revert build with -DACB_BOGOMS_PER_FRAME=50u or edit the default.  Hub change: ./build_all.sh.
- LIVE LINE SHOWS SCY (2026-11-24, built, not looked at on the stream): the stream's `LIVE <elapsed>` overlay now reads `LIVE <elapsed>  scy N` once the supercycle number is nonzero.  RTMPFeed13.scyFn (set each frame by Machine.frame from Machine.scy_fn, which mfmxctl points at the SuperCycle) is called at render time, so the keepalive redraw shows it too; any error there drops the suffix, never the frame.  Python only.
- FEED PACER (2026-11-25, RUN 4x and good: 150.4-150.6 frames per 30 s, 0 resyncs, owed <= 2.4, no gap over 1 s): the Firefox player fell minutes behind the sim over hours (a reload fixed it).  Cause: fastframe10-11 wrote 5.48 frames/s (real ~4.4 + keepalive repeats ~1.1) to ffmpeg, which stamps frames by COUNT at 5 fps, so the stream timeline ran ~10 percent slow against the wall clock.  RTMPFeed13 now has ONE writer, `pacer()`: exactly fps frames per second on absolute deadlines; each tick writes the newest real frame (sendGraphicsFrame only stores it in `pendBytes`; an unwritten older one is dropped and counted) else repeats the last bytes as is.  A repeat is re-rendered only once stale >= 1.5 s (NO DATA banner), at most once a second.  FEEDSTAT now ends with `drop N` per 30 s; real+dup should total 150 per 30 s.  Check: sum of real+dup per window == 150, scy shown in the player stays within a few of the terminal after hours.  LONG-TERM RATE (pacer v3, built, not run): every frame time is fractional.  owed = wall*fps - frames written; next interval = 1/(fps*(1+slew)), slew = 0.05*owed clamped to +-0.25 (SLEW_GAIN, MAX_SLEW), so 5 Hz falls out when owed is 0 and corrections are smooth (time constant 4 s) rather than a burst of repeats that would displace real frames.  A stall owing more than MAX_OWED=5 s resyncs.  FEEDSTAT ends `drop N resync N owed X` (max frames owed in the window).  Offline sim with wake jitter and a 3 s stall: 5.001 Hz overall, recovers in about 20 s.  (v1 cd0cc1a resynced after one late period and drifted; v2 b5fa58e84c bursted.)  The first version resynced after one period late, which let the stream lag grow a bit per stall.  repeatBase() and keepAlive() are gone; scripts/feed/keepalive_test.py still models the old scheme.
- PYTHON CLEANUP (2026-11-25): nu1010/1011/1012.py (the old Textual apps) are deleted (git history has them; scripts/AutoWash.sh and A1AAutoWash.sh still name nu1011.py and are stale).  mfmxctl.main() no longer holds the commands: they live in srcs/host/python/EWD/cmd_machine.py, cmd_bolts.py, cmd_cycle.py, cmd_view.py, cmd_mock.py as module-level `c_xxx(ctx, args)` functions plus a `register(ctx)` that adds them to the shell; cmdutil.py has `Ctx` (sh, m, cyc, args, MFMx, mock, state), `need` and `kw`.  Add a command by writing a function in the right cmd_*.py and a sh.command line in its register().  `help NAME` now accepts an abbreviation.  The selftest ignores the `mfmxctl HEAD:` banner when looking for words (a commit subject containing 'superseded' once failed a check).  machine.py (was 1372 lines) is now the Machine core (init, logging, status; 131 lines) plus mixins, one class still: machine_boot.py (BootMixin: configure, preflight_links, init_box, boot, start, stop, close, shutdown), machine_seams.py (SeamMixin: eth-seams, PKTS.txt), machine_watch.py (WatchMixin: slowscan, dumpl1, specks, who, zhd), machine_frames.py (FrameMixin: live AER line, frame, idle_frame), machine_bolts.py (Bolt, BoltMixin: fire/poll/diagnose bolts), machine_util.py (fmt_ints, NullFeed, layout and option parsers).  machine.py re-exports the old module-level names, so `from machine import ...` and `machine.Machine.method(fake, ...)` still work.  Method bodies were moved verbatim; the mock selftest passes, the real-hardware paths (preflight, PKTS, seams) are untested beyond a name check.
- RUN SUMMARIES, SMALL TRANSFERS (2026-11-25): run directories are huge (2.8 GB for a 3 h run; the analysis only needs misc/*.txt and the console).  mfmxctl now writes, at shutdown, `MFMX-.../runsum.txt` (a 3-8 KB summary) and `MFMX-.../send-MFMX-<date>-<time>.tgz` (about 0.3 MB for a 3 h run: runsum.txt, console.txt = the shell transcript without mark lines, every misc/*.txt up to 8 MB each, and the last 2 MB of all.mfmk as all.mfmk.tail): SEND THE send-MFMX-<date>-<time>.tgz (named for its run so several can sit in one directory) (plus the raw .out only if asked), not the run directory.  It reports `run summary: ... send ...tgz (N KB)` as its last line; failures are reported and never break shutdown.  The code is srcs/host/python/EWD/runsum.py (Shell keeps a transcript deque of everything said/echoed); `scripts/runsum/runsum.py NAME.out MFMX-.../ [NAME.sum] [--grep REGEX --max N] [--bundle]` runs it by hand on an old run (ON the QB): the summary has build/HEAD, command line, elapsed, consensus mean/max/over-0.3 s per bolt kind and every timeout, feed pacing (real+dup per 30 s, drops, resyncs, owed, gaps), AER trend (about 12 samples), nukes/seeds, trouble lines, console tail; `--grep` adds capped matching lines of the console and all.mfmk (e.g. `--grep 'FROZEN|\.sc'`).  Since 2026-11-25 (diagonal-tiles) runsum.txt also has a "hub marks" section: for each tag in runsum.HUB_MARK_TAGS (`.cp:`, `.ct:`) how many hubs logged it in tiles/*.mfmk, its commonest values and, for colon-separated hex values, each field's min..max in decimal.  Mock selftest scenario `runsum` covers the shutdown hook.
- ONE SCRIPT, ONE FILE BACK (2026-11-25): every experiment runs `python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error` from srcs/host/python/EWD; Claude rewrites srcs/host/python/EWD/config/experiment.cmd for each experiment and ships it in the bundle (its first comment line is the experiment's name; runsum.txt shows it as `script:`).  The user sends back only send-MFMX-<date>-<time>.tgz.  Do not rely on the user's own dave*.cmd files (they are not in the repo).  LESSON: dave13.cmd had `#decay 4` commented out, so both 'FB4' and 'FB1 control' runs of 2026-11-25 were really the default autoseed type; and it had `autonuke on 1 100 100 1000` (random nukes of radius 100-1000 every 1-100 s), which changes the fill from run to run (8 vs 11 nukes) and probably explains AERre 0.65-0.75 falling vs 0.916 flat for the same config.  AER comparisons need NO nukes and the same length; runsum.txt prints `decay: NOT SET` when no decay command ran.
- AER GAP, PARKED (2026-11-25, decided to let it go): honest FB1 AERre today reads 0.94 flat (dwell 4 8, no seeds, run 165051), 0.92 with `seed 25 4`, 0.885-0.894 with dwell 2 3; the 3 h soak 124337 (build f8ddc81+, MFMX_EVERY_EVENT 1, seam12.cmd, 11-19) read 0.987 at 180 s and 0.977-0.979 at 900 s with the SAME LiveAER method (only the label changed).  Ruled out: BOGOMS (10 nuke-disturbed, 25, 50 all give the same curve to 0.003), dwell (worth +0.03: fixed ~0.2 s/scy overhead), seeds/fill (worth ~0.015 over 15 min, none early), the host cycle (dwell share 0.962 now vs 0.953 in the soak).  What is left: the hubs finish ~5% fewer events per dwell second than on 11-19, so it is a hub-side change in the last week (event path, marks/instrumentation, runtime honest vs the old compile flag, elide).  NOT bisected: too costly for a 0.04 difference.  If it matters later: build 0258dd9 with `#define MFMX_EVERY_EVENT 1` (Grid.h) in a worktree, run the first 900 s of seam12.cmd (no seeds, dwell 4 8), bundle with the current runsum; or first make runsum ship a per-hub last `.ev` table (the tgz has no tiles/*.mfmk, so hub event counts cannot be compared today).  Soak run dir with tiles: ../MFMX-20261119-124337.
- SEED ABORT FIXED (2026-11-25, `seed 25 4` ran clean in 5 runs): `seed 25 4` once core-dumped (QuietBox.cpp:30 FATAL OUT_OF_ROOM).  doSeed drew sites over the whole 1920x1080 display space, but the chips cover only 1918 columns (the last two are blank), and DG::isValidChipCoord compared the chip coordinate against the HUB counts (7x5) instead of the chip array (2x2): x=1918/1919 mapped to chip column 2, i.e. chip number 2 (wrong chip, bogus offset) in the top half or 4 (fatal) in the bottom half, about 1 draw in 1000 per seed.  Now: DemoGlobal.h has ACTIVE_GRID_WIDTH/HEIGHT (1918x1080, chips x per-chip sites) and getActiveGridSize(); isValidChipCoord checks the 2x2 array; mapCoordToAddress rejects x/y at or beyond the active size; doSeed draws from the active size; storeP4Atom returns false (not a fatal) for a chip number >= MAX_BLACKHOLES.  isValidDGC is unchanged (CodeManager uses it as an array-bounds check against the 1920x1080 mFullSimGrid).  Shared header: needs build_all.sh.  Checked offline: (1917,y) valid on chips 1/3, (1918|1919,y) invalid.
- `.sc` SCY MARK (2026-11-25, in every run since; content not yet inspected): hb (BRISC) marks each new scy once, in workOnEnteringNewLeaderB (HBPTAG and LOGPTAG `.sc`, value = scy low 16 bits, same value as the report epoch).  Use it to place a hang by cycle number (fastframe10-12 froze at scy 3621 with bh2, the leader chip, 120 of 140 hubs not acking RUN_EVENTS).  The report epoch IS the scy (setReportEpoch from the NEW_LEADER arg 1); `.em` just logs it sparsely (every 16th marker).  Hub change: needs build_all.sh.
- t6-exports.h GONE (2026-11-24): its only live content was the version string (the symbol-address part was #if 0, deprecated).  The host no longer includes it; mfmx_wrapper.cpp mfmxVersionString() builds the same leximited form formatSymbols.pl made ("MFMx-" + digit-count-prefixed year-2000, 0-based day-of-year and minute-of-day, e.g. MFMx-226.3277.3409) from __DATE__/__TIME__, so it is the compile time of that one file (shows in the RTMP feed label); checked against the old header and leap years.  The cross build no longer runs formatSymbols.pl (deleted) or the objdump symbol-table pipeline, and no longer copies anything to srcs/generated_headers (include path and build_all.sh references removed; the .gitignore line can go).  hub.s / bin files are still made.
- IDLE-TIME REFRESH RETIRED (2026-11-24): the random SELF idle refresh (ACB_REFRESH_PER_FRAME, nextRefreshSite, the .rf mark) is removed from the hub.  It was off during the turd hunt and the stamp, host refresh and idle close/pad made it unnecessary.  The host-directed refresh (`zoom specks refresh`) stays.
- STALE EWTs + EW BOOKS (2026-11-23, built, not yet run): hub-side only.  An EWT now carries the epoch it was shipped in (GridManager::applyEWT: mHiddenYPos = y | epoch<<16, epoch = L1GridManagerControl::getReportEpoch()); a return from another epoch is dropped unread and counted STALE (the EWP only runs in RUN_EVENTS, so an EWT in flight at the exit returns in a later epoch).  Books: every non-empty EWT shipped (OFFERED) comes back once and lands in COMMITTED, OBSOLETED, STALE or UNAPPLIED (EWs suspended, or came back with no result), so RETURNED = COMMITTED + OBSOLETED + STALE + UNAPPLIED and OUTSTANDING = OFFERED - RETURNED (GridManager::getEWsOutstanding).  Marks, hex: `.eo:OFFERED:RETURNED:STALE:UNAPPLIED.` at the RUN_EVENTS exit and `.en:` the same at the nuke; `.ev:C:R:E.` is unchanged (aer.py: obsolete = R - C now includes stale).  `.st:x,y` (every 1000th) names a stale site.  workOnExitingRunEventsB still does not wait (`if (true /* DONE DRAINING EWPS? */)`): OUTSTANDING is the basis to build that on.  Expected: turds gone and STALE > 0 in .eo.  Hub change: ./build_all.sh.
- DL BACKLOG MARKS (2026-11-24, built, not yet run): hex, hub-side, LOG and HB.  `.dl:LEN:POPPED.` at every RUN_EVENTS exit (LEN = DLGridList::getLength(), sites still queued for reporting; POPPED = DLGridList::getPopped(), total sites popped and reported since init).  `.nl:BEFORE:AFTER:POPPED.` at each nuke (LEN before applyNuke, LEN after, POPPED then).  Use: after a big nuke AFTER should be about the erased count; later `.dl` LEN should fall to ~0 and POPPED rise by about AFTER.  If it does and turds remain, reports are lost downstream of the hub (AER path or host accept); if LEN stays high or POPPED stalls, the loss is in the hub.  The DL pops from the back and sites push to the front (FIFO), and applyNuke walks x outer, y inner, so a nuke's erasures reach the screen column by column, left to right, top to bottom, over several frames: expected, not a bug.  Hub change: ./build_all.sh.
- WHO (2026-11-23, built, not yet run): `who X Y` names the hub that last painted picture pixel (x,y), for ghost-pixel ("turd") hunting.  Host: `QuietBox::painters` keeps one u64 per pixel (valid, epoch known, RING = the report came from the painting hub's S/E cache ring rather than its SELF sites, chip, tlbi, atom type, epoch), written by the decoder threads where a report is drawn; `QuietBox::newestEpoch` is the newest epoch any stream has announced; `whoPainted(x,y)` in the module returns (chip, tlbi, ring, epochKnown, epoch, atomType, newestEpoch|-1) or None.  `who` prints the hub tag (atCXY), self vs ring, atom type, the epoch and its lag behind the newest, and the hub-relative pixel (x%137, y%108).  Use: freeze (`cycle hold`, `cycle step` to RUN_EVENTS), find a turd pixel (`zoom specks` lists lone occupied sites, image coordinates), `who X Y`.  Reading it: RING painter = a borrower's report is the ghost; SELF painter with an old epoch = the owner's last report was non-empty and it never reported the later empty; SELF painter in the current epoch = suspect the hub's grid-to-report path.  The pixel coordinates are those of the unzoomed 1920x1080 picture.  Memory 16 MB host side, no wire change.
  GRID SITE + HUB TRUTH (2026-11-23): `who` also prints `hub grid site (x,y)`, the painter's own grid coords of the report (the hub-relative pixel is NOT the grid coord); `zoom specks` does the whole check itself, no manual dumpl1: per speck it prints one who line (Machine.who_row: `(x,y) RING|SELF type T eN lag L atTAG site (gx,gy)`), then dumps the L1 of the painting hubs (Machine.specks_who -> dumpl1, decoded with scripts/l1dump/l1grid.py; hub.map from $MFMX_HUB_MAP or build_cross/srcs/cross/hub.map) and appends `HUB: EMPTY` (the hub emptied the site and the host never heard: a turd), `HUB: type N` (the hub still holds an atom; for a RING painter it is the borrower's cache copy) or `HUB: ?` (that dump failed); no hub.map gives one 'hub truth unavailable' line.  One L1 read per distinct hub, so use it paused.  After the hub reads it rescans the picture and marks each speck `GONE` or `STILL` (with refresh off, a GONE speck was a report still arriving, i.e. a backlog, not a turd).  By hand: `dumpl1 TAG` then `l1grid.py DUMP.l1 --site X,Y`.  Painter word stores atom type in 9 bits.  First turd run (refresh off, 9f42e63): 8/8 specks type 4, last painted epochs 38-48 vs newest 51, 6 SELF + 2 RING, so not a ring race: no accepted report since.
- ZOOM COST AND CONSENSUS (2026-11-23, built, not yet run): with `zoom auto` on, NEW_LEADER/RUN_EVENTS consensus read 0.44-0.66 s; `zoom off` in the same run gave 0.052/0.075 s and `zoom auto` again 0.44-0.66 (the user's A/B inside one run).  Consensus is HOST-observed and mfmxctl is one thread, so a slow frame() delays noticing a finished bolt (same mechanism as the slowscan artifact in OPEN_ISSUES).  Now: `ZoomView.apply(img, between, scan)`: Machine.frame() passes `between=poll_bolt` (a bolt is noticed after the scan and after the step, not only at the next 5 ms tick) and `scan=False` while a bolt is pending (box and view just stay as they were; counted in `scans_skipped`); each stage is timed (scan, step, render, and the whole frame()) as [calls, total, max]; `zoom status` (and bare `zoom`) now shows `cost: cv2|numpy; avg/max scan .., step .., render .., frame ..` and the skipped-scan count.  The render itself is one uninterruptible call, so a long render still delays the poll once per frame: the cost line says which stage that is.  If consensus is still high with zoom on, read that line.  FIRST QB RESULT (d182d3d): scan avg 463 ms (max 576), render 34 ms, step 0.3: the SCAN was the cost, not the render.  Cause: `img.max(axis=2)` (numpy's slow case: reduction over a 3-byte axis).  Fixed: one shared `ZoomView.occupied_mask` built from per-channel compares (identical mask, about 6x faster here, zoom_test 82 -> 28 ms/frame); frame_box and occupancy_map now use it.  Re-read the cost line on the QB.
- REPORT EPOCHS (2026-11-23, built, not yet run): fixes the suspected ghost race (hub A, borrower, reports a cache-ring site X non-empty; later hub B, the owner, runs an event that empties X and reports it; B's report reaches the host first and A's old one lands after it, so the host keeps a stale non-empty pixel nobody will repaint).  The report stream is ONE continuous LZ-compressed byte stream (records straddle cars), so a car cannot carry an epoch.  Instead the hub puts an EPOCH MARKER record in the stream (AtomReport.h: mCoord.x == EPOCH_MARK_X 0xffff, mCoord.y = epoch = scy low 16 bits; same record size; consumes a spin like any record): `workOnEnteringNewLeaderB` sets `L1GridManagerControl::mReportEpoch` (scy, from the incoming bolt word 1) as a changeover begins; T0's PACK loop (EP_ACacheBlock.cpp, step 1b) emits a marker at the next record boundary when it differs from the last one sent (and first thing in every stream, incl. after a restart).  A report reads its atom at pop time, so it belongs to the epoch current at that moment and every record after a marker was read in its epoch (no drain, no barrier).  Host: `ZHostDecompressor` tracks the stream epoch (`mStreamEpoch`, -1 until a marker, reset on restart) and `QuietBox::acceptPixelEpoch` keeps a u16 newest-epoch-per-pixel map (reset by `clearT6GridImage`); a record older than its pixel (wraparound difference) is dropped (not drawn, not setSimAtom'd) and counted: `zhd` shows 'N stale reports ignored', `zhd all` per hub.  Ties go to arrival order.  Test of the race story: stale count rises and turds stop.  Also: the seal-time Fletcher recompute in `sealTrailer` is now compile-time optional, `ACBP_SEAL_VERIFY` (PT_ACacheBlock.h; default 0 = off; -DACBP_SEAL_VERIFY=1 for the .zn/.zw/.zx corruption hunt).
- ZOOM (2026-11-22, host-side, built, NOT yet run on the QB): `zoom [off|auto|reset|SCALE]` crops and scales the 1920x1080 site image on its way to the feed (srcs/host/python/EWD/zoomview.py, hooked in machine.frame).  auto fits the box of every non-empty site ever seen (grows only, zoom only moves outward); the goal is recomputed only when the box comes within 5% of the view edge, then with 14% headroom, and the view eases to it (smoothstep, 15 frames = 3 s) so there is no per-frame creep; the view is centered on the box and may extend past the image (drawn very dark purple, OUTSIDE_BGR 20,4,12, vs the empty-site 5,10,15), so a corner card is centered not pinned to the edge; fractional scale, sites kept square, min box 120x68 sites (cap ~14.7x); `zoom shrink on` makes the box the union of the last 25 frames (5 s) instead of everything ever seen, and the view zooms back in when the tight fit (with headroom) is 1.3x tighter than the shown goal; `zoom shrink off` returns to ever-seen.  `zoom region X0 Y0 X1 Y1` prints an occupancy map of a pixel rectangle in the same 3x4-cell layout as `scripts/l1dump/l1grid.py --profile` (a hub's whole grid in pixels: hx*137-20..hx*137+157 by hy*108-20..hy*108+128), to compare the picture with a hub's real grid from `dumpl1`.  `zoom specks [R]` lists occupied sites with no other occupied site within R (default 5): lone ghost pixels, with their x mod 137 / y mod 108 histograms and first coordinates.  `zoom map` prints a 48x18 map of occupied sites per cell, occupied counts by x mod 137 and y mod 108 in 20-wide steps (a spike in the first bucket = cache-ring copies at hub edges), and whether grid drawing is on to find what holds the box open; the grid's white crosses/labels count as occupied.  SCALE (1..64) is fixed.  Upscale = nearest to the next whole factor then cv2.warpAffine bilinear down (numpy nearest fallback without cv2).  Off by default; use with the grid off (crosses count as occupied).  A zoom exception turns zoom off, never the stream.  Test: tools/zoom_test.py (the cv2 branch only runs where cv2 is installed, e.g. the QB venv).
- PHYSICS FAILURE MARKS `.pf:V.` and `.pw:W.` (2026-11-22; boom10 run 144848 PROVED the first design: 7 of 10 seeded PHY_BOOM atoms logged `.pf:19974.`, 140 hubs alive, AER 0.999; the redesign to recoverable plain FAIL PROVED too: run 20261122-162107, 8 of 10 booms logged `.pf:19974.` and `.pw:852141.` (file 13 = Physics.cpp, line 173 = the BOOM FAIL), HB stack high-water 784 bytes at failure, all 140 hubs alive to the end, AER 0.98-0.99): a FAIL during Physics::doTransition on HB no longer hangs the hart: PrepareToDie longjmps to the hart's RecoverPoint (fAll.mRecover, RecoverPoint.h), doTransition commits the pre-event window with the center erased and logs `.pf:V.` (DECIMAL in the .mfmk; V = ((FAILCode+100)<<9)|center type) and `.pw:W.` (W = (file id << 16)|line) in LOG and HB, first 8 then every 1024th.  ENGINE_FAIL always hangs (engine broken).  Self-test: config/boom10.cmd seeds PHY_BOOM (type 6), expect `.pf:19974.` (= 0x4e06).  Count: `grep -h '\.pf:' MFMX-*/tiles/*.mfmk | sed 's/.*\.pf://' | sort | uniq -c`.  Locks (AtomicScopeLock), the mark chokepoints and the wrong-hart asserts never recover (RecoverSuspend / ENGINE_FAIL); [canary fill PROVED: run 20261122-170519, 9 of 10 booms, all 140 hubs alive, AER ~1.0; every recovery showed `.sk:784` and `.sh:6816` = 7600 - 784, i.e. HB's true stack high-water is 784 bytes of 7600] a recovery also logs `.sk:N.` (stack high-water bytes, whole stack is canary-filled at boot since 2026-11-22) and `.sh:N.` (stack headroom above the data, bytes; recovery is refused under 64; not `.ps`, that is the packet-send tag).  Details: OPEN_ISSUES "physics failure recovery".
- CLOCK MARK `.ck:STATUS:AICLK:AXICLK:ARCCLK:LIMIT.` (2026-11-22, PROVEN: run 20261122-121809, all 140 hubs `.ck:0:320:3c0:320:546.`): each hub reads
  its chip's ARC telemetry table over the NoC once at init (hart NC, InterHub.cpp manageInterHubNC ->
  srcs/cross/{include,src}/ArcTelemetry.*, hub image only) and logs it with `.cp` (CacheLoader.cpp).  Hex, clocks in MHz
  (`.ck:0:320:3c0:320:546.` = 800/960/800 MHz, limit 1350), STATUS 0 ok (1 not read, 2 ARC not ready or bad pointer, 3 NoC
  read failed, 4 bad table, 5 tag missing).  See it: `grep -h '\.ck:' MFMX-*/tiles/*.mfmk | sed 's/.*\.ck://' | sort | uniq -c`.
  The NoC read return buffer must match the source modulo 64 (ERISC_NOTES 9.19).
- scripts/l1dump/l1grid.py  decodes the T6Grid out of a hub L1 dump (mfmxctl
  `dumpl1 at023` -> <run>/dumps/N/at023.l1) and checks that the PERMANENT-edge
  cache rings are EMPTY (exit 1 if not).  Finds theT6Grid via the hub image's
  linker map (build_cross/srcs/cross/hub.map, or --map/--addr; the address moves
  with every hub build: 0x7276c in the first width-137 build, 0x7274c in the honest
  one, so use the map of the build that made the run) and the .pe mask from <run>/tiles/TAG.mfmk
  (or --pe).  Parity-checks every atom, so a wrong address shows as WARNING.
  Test: python3 scripts/l1dump/l1grid_test.py.
- RESULT, honest (no AER cheating) 3 h soak seam12, run 124337 (10929 s): LIVE AER
  0.956/0.956/0.958 (1m/5m/all) at the end; aer.py over ALL 140 hubs (time-aligned
  since 2026-11-19, see CLOSED_ISSUES): wall AER 0.959 total, running 0.986, duty
  97%, flat across the three hours (running total 0.973 at 30 min, 0.965 at 1 h,
  0.960 at 2 h, 0.959 at the end; trailing 0.954-0.957 after the first hour);
  events per hub spread 0.2%; fill 28% in the first half hour, then 66% and
  steady; no bolt stalls, no hang; hub time
  max 0.03 s.  The honest AER settles about 0.02 below the early 0.98 as the grid
  fills, no drift after that.  4 of 140 hubs desynced their report stream (display
  only; CLOSED_ISSUES "Compressed report stream breaks").
- Report stream health (srcs/host/{include,src}/ZHostDecompressor.*): the per-hub
  compressed atom-report stream has no resync, so one bad byte freezes that hub's
  tile on screen forever while the hub keeps running (run 124337: 4 of 140 hubs
  in 2.2 h; OPEN_ISSUES).  The decompressor now counts bad spins, logs FIRST-BAD
  (last 48 decoded bytes + last 16 packets) in the tile log, declares DESYNCED after
  20 in a row.  `zhd [all]` in mfmxctl, MFMx.zhdSnapshot(), misc/*-ZHD.txt on change.
  The spin now advances for every intact report (bad atoms are skipped and counted).
  STREAM RESTART (2026-11-19; hub image + host changed, untested on hardware): a
  desynced hub is asked to restart its compressed stream.  Host -> hub in the empty
  cars the host returns (PT_ACacheBlock.h: RESTART_REQ and EPOCH flags + 6 bytes
  ACBHostFeedback; EP_ACacheBlock.cpp recvTC latches them BEFORE it wipes the car).
  Hub: T0 restartStep (finish the record in flight, set draining), T1 source ends
  the stream (EOF) when drained, encoder flushes, last car EOF|CLOSABLE, next car
  SOF+epoch (setupNewCar), T1 waits for it to ship then lzmfmx::reset() and
  streamRestarted().  Host: ZHostDecompressor requestRestart() on DESYNCED, throws
  RestartStream from sourceByte on the matching SOF, the thread resets and decodes
  again, adopts the hub's spin (mSpinSync).  Level-triggered request + 1 bit epoch,
  so late or lost returns are harmless.  lzmfmx: reset(); getNextByteBlocking
  returns -1 at EOF instead of spinning; encode() on a dirty ring HANGS, always
  reset().  mfmxctl: `zhd restart on|off`, `zhd break TAG[,TAG..]` (TEST: drop one
  compressed byte).  config/seam15.cmd breaks hubs on purpose (12 min).  Host
  test of the compressor: scripts/lzrestart/run.sh.  Hub marks `.zq:` (asked)
  and `.zd:` (done).
  PACKET TRAILER (2026-11-19, after seam14 broke 3 hubs within 6 bytes of a packet
  start; hub image + host changed, untested on hardware): every car the hub closes
  now ends with u16 seq + u16 Fletcher-16 of the data (PT_ACacheBlock.h sealTrailer,
  called by T0 before closeTC; data space 986 B, len still 990).  The host
  (notePacket) checks them: tile log `ZHD PKTSUM` (checksum bad: packet damaged in
  transit or host copy) and `ZHD PKTSEQ` (a packet lost/repeated), first 20 per hub,
  counts in the DESYNC/FIRST-BAD lines.  Reading the next break: PKTSUM/PKTSEQ before
  it = damage at the packet (NoC/host side); neither, yet still desynced = the bytes
  were wrong before packetizing (T0->T1 ring, compressor) or the decoder.  Fixed
  also: the `ZHD BADPKT` spam (a full packet was flagged).  Flag day: hub and host
  must both be the new build.
  SEAM14-11 (2 h, trailer build): 2 natural breaks, 0 bad checksum/seq, first bad
  record 11 and 1 byte into a packet: damage is at the packet join, before sealing.
  So the trailer sum is now a SHADOW sum: the hub folds each byte in as the sink
  accepts it (ACacheBlockL1Control mShadowA/B, reset in setupNewCar) and seals THAT,
  not a recompute; hub also marks `.SHADOWBAD` if the car differs at seal time.  A
  PKTSUM now means damage anywhere after the sink accepted the byte.  Flag day again.
  config/seam14.cmd: two hour soak (seam13 shape, feed started late, no --rtmp) that runs
  `zhd` after every 10 minute block and `zhd all` at the end; the 10 s `zhd` tick is its
  own tick (the seam configs turn slowscan off).
  STREAM BREAK CAUSE FOUND AND FIXED (2026-11-21, PROVISIONAL, CLOSED_ISSUES): the host's
  return write of an ACB car could land out of order (write-combining window), so the
  hub reused a car and a late payload write zeroed its length mid-fill.  Host fix
  OurTLBs::writeToWordsOrdered (payload, store fence, footer, header); 7 clean stress
  hours.  Marks .zl/.zr/.zs/.zn/.zw and host DUPRET stay in.
- CACHE ELISION (four corners, 2026-11-21, UNPROVEN): with `elide on` a hub does not
  save and reload the bands whose role is unchanged between the previous and the
  next corner (CachingPolicies::keepAcross, CacheLoaders::mKeepMask, `.xk` mark);
  `cycle repeat on` picks the corner uniformly 0..3.  Off by default.
- srcs/shared/include/HubEdges.h  PERMANENT array edges: HubEdges::neighborExists
  (on chip, or across a live seam, Dir4 or two-hop diagonal; CacheLoader uses
  it too) and permanentEdges() -> PE_N/W/S/E bits for the sides with no
  neighbor hub.  GridManager::init stores them in mPermEdges (no HostBlock
  field, no HostFlags bit) and logs `.pe:MASK` (hex) once per hub (from
  configureCacheLoaders next to `.cp`: init-time marks do not reach the log); the cache
  ring on such a side is INACCESSIBLE (GridManager::isAccessible /
  getAtomOrInaccessible: events read INACCESSIBLE_TYPE, setAtomOrDrop drops
  writes, so those caches stay EMPTY from init), and
  CachingPolicies::withPermanentEdges() lets event centers run to the last
  real site on such a side (a temporary dead band keeps the window-radius
  clearance).  scripts/hubedges/hubedges_test.cpp checks all nine layouts
  (g++ line in its header; needs srcs/shared/src/S8C.cpp).
- srcs/shared/include/CachingPolicy.h  the CACHING POLICY: per Dir8 whether a
  hub sends its edge sites, receives into its cache, or does nothing, and the
  event bounds; CP_SUPERCELL (original: NEW_LEADER arg = leader code in each
  2x2 supercell) and CP_FOURCORNERS (every hub alike; the arg is a Corner4,
  MDist.h: the corner the hubs expand toward, borrowing the two edge bands
  and the corner cache there and lending the opposite three).  No virtuals
  (T6): CRTP base + static functions, picked by a switch.  The host chooses
  per run with `policy [fourcorners|supercell]` (or --policy; DEFAULT is
  fourcorners since 2026-11-19, supercell is the opt-out), before
  'start': Blackhole.setCachePolicy -> CodeManager -> two HostFlags bits
  (HBF_CACHEPOL0/1, no HostBlock layout change); hubs log `.cp:ID` once.
  scripts/cachepolicy/cachepolicy_test.cpp checks the invariants on the host
  (g++ line in its header): pair agreement, non-overlapping event reach,
  never touching a lent band or an unexchanged cache.
- Syntax-checking host C++ without the QB (no tt libraries needed): pybind11
  headers from `pip install --target DIR pybind11`, the generated FileIDs.h
  and t6-exports.h from a cross build (build_cross), then from srcs/host:
  `g++ -std=c++17 -fsyntax-only -Iinclude -I../shared/include -I../cross/include
  -IDIR/pybind11/include -I/usr/include/python3.12 -I<build_cross>
  -I<build_cross>/srcs/cross -DPROJECT_SOURCE_DIR=/x -DBUILD_HOST
  -DSPIKE_DIR='"x"' src/FILE.cpp`  (checked: CodeManager, OurTLBs, LiveAER,
  mfmx_wrapper).  A hint only: the pybind11 version here may differ from the
  QB's.
- scripts/xchg/xchg.py  cache exchange timing per hub from the `.xb`/`.xe`
  marks (SAVE in NEW_LEADER, LOAD in RUN_EVENTS: time and packets), with
  --bolts to compare against the host bolt times.  Also prints STALLS from
  the `.xw` heartbeat marks (a hub waiting over 1 s).  `.xe:K:P:HEX16` also
  carries the packets moved per Dir8 (8 bytes); `--dirs` tabulates them by
  direction and by whether the `.xk` keep mask held that direction.
  Self-test xchg_test.py.
- OPEN_ISSUES.md  the one list of open bugs, unproven things and ideas.
- CLOSED_ISSUES.md  what got done, newest first (entries move here from OPEN_ISSUES.md).
- srcs/host/python/EWD/HANDOFF.md  mfmxctl usage, command files (seamtest,
  seamcycle, seamlong, seamns), selftest, ptrace and eth-seam sections.
- scripts/ptrace/README.txt  packet-trace analyzer (reads RUNDIR/tiles).
- scripts/aer/aer.py  average event rate (AER, mAER) of a run from the hub
  `.es`/`.ev` marks: cumulative (64-bit) committed:returned:empty event
  counters, logged when events start and stop each supercycle.  AER is per
  grid SITE per second and counts EVERY event, including the ones at empty
  centers (the hubs draw random sites until one is occupied; each empty draw
  is an act of physics that did nothing and counts), so a sparse grid has a
  HIGHER AER.  Events = committed + empty (GridManager::mEWsCommitted,
  mEWsEmptyCenters in Grid.cpp).  Reports wall and running AER, duty, obsolete
  fraction, trailing windows, --series, and a diag (non-empty-only mAER, fill
  from the `.ae` census).  The window starts at the autoseed (`.as`), or the
  first `.es` with --from-start.  The hub ends .es/.ev with a '.'
  (`.ev:C:R:E.`, never hex): a mark without it was cropped in transit (aer.py
  then treats its counts as bad; runs from older hub images have no dots and
  are read as before; the live AER needs the dot, so host and hub image go
  together).  Torn counter marks (a hub log now and then has a truncated hex
  field) are repaired first (repair_counters: longest
  non-decreasing run, previous counts carried over), else one makes a ~2**64
  delta and the totals read 1e13 mAER.  Hubs lose marks (8% of '.es', a few '.ev'),
  so cycles are lined up across hubs BY TIME (align_hubs: per chip, clusters of the
  hubs' '.ev' times; a lost mark is filled in, counters interpolated, totals exact);
  only a hub that stopped logging early is dropped.  Older versions lined up by mark
  number and kept as few as 1 of 140 hubs.  scripts/aer/aer_test.py is its self-test.
  Needs the hub image from this commit or later.
- LIVE AER on the stream (rough reference, not a measurement): the host
  transport thread hands every parsed tile mark to LiveAER::noteMark
  (srcs/host/include/LiveAER.h, called from OurTLBs::updateLogBlocks), which
  keeps only the latest cumulative `.ev` events (C+E) of each hub with the
  hub's own clock; `.as` sets a seeded flag.  pybind: MFMx.liveAERSnapshot().
  srcs/host/python/EWD/LiveAER.py turns snapshots (once a second, from
  Machine._live_line) into AER over the last 1 and 5 min and since the
  autoseed, using each hub's own clock between its marks, so lost or late
  marks only blur it.  It is a line of its own in the video overlay (between
  the LIVE line above and the version line below; RTMPFeed13 `extra`) and in
  `status` (`live:`): `[140] AERre: 1.57, 1.58, 1.57` (since 2026-11-24 the label names the counting method, no "est. ": AERse = Shortcut Empties, events shortcut, empty draws credited free; AERre = Run Empties, events honest, empties shipped and run; machine.AER_LABELS, set from the mode applied at `start`)
  ([N] = hubs reporting, 140 = every hub: 35 per chip; then the last 1 minute,
  last 5 minutes and all since the autoseed, in that order like top's load
  averages; three significant digits).  mfmxctl logs the line once a minute to misc/mfmxctl-LIVEAER.txt
  and the last one as `live AER final:` in the shutdown output (so it is in
  the .out), to compare with aer.py afterwards.  AER is aer.py's wall AER
  /1000.  It ignores counters that go backwards (torn marks) and rates above
  1000 AER, and takes the PLAIN mean over hubs (a trimmed mean read low:
  while the grid fills, sparse hubs run at 50+ AER and full ones at 1.5).  While the grid fills the figure is HIGH (empty
  draws are cheap: about 30 AER at 54% fill after 12 min, about 1.6 at steady
  state).
  Self-tests: scripts/aer/liveaer_test.py (python), mock selftest (status
  line), and a standalone g++ check of LiveAER.cpp.
- scripts/cachecensus/cachecensus.py  per-neighbor cache occupancy from the
  `.ac` marks; `--seam CHIPA:DIR:CHIPB` checks seam hubs.
- srcs/host/tools/run_native_tests.sh  all native tests (ASAN=1, name filter).
- srcs/shared/include/EthSeam.h  the shared routing/dealing header (parts 1-5
  plus the T6 and host lookups); the single source of truth for layout.
- Claude's own memory notes live outside the repo
  (~/.claude/projects/.../memory/); this file and the docs above are the
  durable record.

## 4. WHERE THINGS STAND (2026-11-25 summary first; the 11-17 history follows)

LATEST (2026-11-25): HEAD builds and runs the FB1 honest-AER experiment end to
end in 15 minutes: `python mfmxctl.py --layout seam4 --boot -s
config/experiment.cmd --script-exit --abort-on-error` (the one script every
experiment runs; Claude rewrites it per experiment and ships it in the bundle),
and the user sends back ONE file, send-MFMX-<date>-<time>.tgz (30-50 KB,
runsum.txt + console + misc).  Health on the QB: 140 report streams, 0
desynced, 0 bolt timeouts, consensus about 0.11 s, feed 150.5 frames per 30 s.
Python was split into mixins and cmd_*.py modules (see PYTHON CLEANUP).  Open
threads: the AER GAP vs the 11-19 soak (parked, see the bullet), the one
unexplained single hang at scy 3621 in fastframe10-12 (OPEN_ISSUES), RTMPFeed13
cleanup, shorter HLS latency, hub heartbeat car for in-pause refresh answers.
No experiment is queued: experiment.cmd currently holds the last one run (no
seed, dwell 4 8) and need not be rerun.

---- 2026-11-17 history ----

PROVEN ON HARDWARE (chips 0,1; links eth 6,7,10,11): E1 bring-up and
teardown, Dir4 crossings, diagonal crossings (slice 2), cache loaders across
the seam, global supercell parity, 55 bolts CONSENSUS over a 5-minute cycle,
cross-seam cars in == out on all 13 channels, retrain 0 and FEC uncorrected 0,
clean exit.  Corner caches of all seam hubs are full (seamlong10 / run
20261117-102406, cachecensus ok).  Not yet checked: atom CONTENTS match across
the seam.

Startup speed (2026-11-22): CodeManager's full-image word-by-word SPOT CHECK READBACK of every tile (one PCIe read
per word, whole image, per tile) is OFF by default (it never found anything); `MFMX_SPOTCHECK=1` in the environment
turns it back on.  The HostBlock magic/cigam/base-address check after each download always runs.

PROVEN (cont.): slice 3a, the N/S seam (chips 0 and 2, links x=3,4,12,13):
run 20261117-110545, 53 bolts, cars in==out, 14 seam hubs ok.

PROVEN (cont.): slice 3b, the 2x2 array (layout seam4, 16 links, only chip 0
autoseeds via layout key seedchips -> MFMX_AUTOSEED_CHIPS -> HBF_NO_AUTOSEED):
run 20261117-142417: all four chips full, 44 seam hubs ok, 72 bolts CONSENSUS,
no TIMEOUT (stale centre corners do not hang), cars out == in == noc.
  python mfmxctl.py --layout seam4 --boot --rtmp -s config/seam4.cmd --script-exit
  scripts/cachecensus/cachecensus.py MFMX-<date>-<time> \
    --seam 0:E:1 --seam 2:E:3 --seam 0:S:2 --seam 1:S:3
(Never kill a run with -9: a hung mfmxctl blocks tt-smi and can leave a link
untrained.  Use `quit`, or SIGTERM from another terminal.)

PROVEN (cont.): slice 3c, the centre diagonals (EthSeam.h PART 6, one-way
centre channels, closed-square check; ERISC_SEAM_DESIGN.md 18c): run
20261117-163033 (10cb99a): 16 links up, retrain 0, uncorrected FEC 0, 17908
cars out == in == noc, txfail 0 on all 144 channel rows, 34+34 bolts CONSENSUS,
no TIMEOUT.  The 8 extra channels (E/W eth7 ch3 and eth10 ch3; N/S eth13 ch4
and eth3 ch5) each moved cars in ONE direction only: 0->1, 3->2, 2->0, 1->3.
The four centre corner caches read 100 (a full corner), up from about 90 with
the diagonals unwired.  That is consistent with loaded but NOT proof of atom
contents; a content-hash slice is still open.
Performance (run 20261117-165203, slowscan off): the periodic slowscan WAS the
cause of slow frames and slow bolts.  Bolts 1.8 s (NEW_LEADER) / 2.3 s
(RUN_EVENTS) instead of 5.6 s, 61+61 bolts in the same time, frames 153 per
30 s (5.1 fps, max gap 0.3 s) instead of one per 5-6 s.  Only the bring-up
(`start`, ~105 s) still produces no frames.  Slowscan stays available via
`scan`, the after-timeout diagnosis (which now also turns the tick ON, so a
freeze sweeps every tile; one tile per chip per tick, output goes to
all.mfmk, not stdout), and `tick slowscan on`.  Centre channels
again one-way, out == in == noc 31970, txfail 0, retrain 0, uncorrected 0.
Intermittent bring-up failure "link not Up (port status 2)" seen three times
on different tiles (bh1:eth13, bh1:eth4, bh0:eth7; also after clean runs, so
not only after a kill).  validate() now polls up to 60 s for Up (logs how long
it took), reports both ends, and bringUpSeams re-verifies every end (Up, no
retrain) before succeeding.  If it still fails after the wait, the link is
stuck, not slow: note which tile (EthLinks lines in host/hc000.mfmk).
Long run (seam5.cmd, run 20261117-175735, a67ac2c, about 35 min): all 16
links came up first try (no "Up after N ms" wait logged, so the wait is
untested on a real failure), retrain 0, uncorrected FEC 0, txfail 0, out == in
== noc at every 5-minute dump, steady 15.5k cars per 5 minutes (CA did not die
out, no seam stalled), 412 bolts CONSENSUS (median 1.8 s, max 3.4 s), centre
channels one-way, 10666 frames at about 5.1 fps, orderly SIGTERM exit.
SEED TYPE BUG: that run was still FB4.  `decay 4` reached mCommonArgs[1] but
hub Grid.cpp (seekRandomNonEmptySite) hard-coded makeAtom(5) and never read
it (only Physics.cpp START_TYPE does).  Fixed: Grid.cpp now seeds
mCommonArgs[1] when set (default stays 5); `.as` logs the real type.  Needs a
QB rebuild; check the `.as` mark / AUTOSEEDOMATIC value to confirm type 4.
Run 20261117-201302 (b450021): the 60 s wait did NOT help.  bh1:eth13
(the 16th link) stayed port status 2 for 60 s, its peer bh3:eth13 also read 2,
heartbeats live, retrain 0: the base firmware never trained it after
tt-smi -r (stuck, not slow; bh1:eth13 has now failed twice, bh1:eth4 and
bh0:eth7 once each).  Response: mfmxctl `boot` now runs a link PREFLIGHT after
tt-smi -r (machine.py preflight_links): read-only port-status polls
(EthSeamHost::portStatus) of every declared link end for --link-wait secs
(12), then up to --link-resets (5) more tt-smi -r, then boot fails naming the
links.  releaseIdlePorts drops the cached TLB windows before each reset.
If it keeps failing on the same link, reseat that cable.
Run 20261117-203012 (0c35e57, seam5, ~35 min, seed type 4): PREFLIGHT
WORKED.  Rounds 1 and 2 had links stuck at port 2 (bh1/bh3 eth13; then also
bh0/bh1 eth7: always BOTH ends of a link, a different set each reset), the
third reset brought all 32 ends Up (127 s total).  Then 16 links, retrain 0,
uncorrected FEC 0, txfail 0, out == in == noc at every dump (15.7k cars per 5
min, steady 15.7k..15.9k, no die-out), 412 bolts CONSENSUS (median 1.8 s, max
3.4 s), centre channels one-way, 10667 frames, `.as:4` in the tile logs: the
seed-type fix (b450021) is proven, the stream shows FB1.  Use --link-resets 5
for unattended runs; a reseat of the bh1<->bh3 eth13 cable may help.
mfmxctl (after run 203012): preflight defaults are now --link-wait 12 /
--link-resets 5.  New --abort-on-error: a failing command-file line (error,
unknown command; not usage text, not typed lines) or a failed --boot ends the
run, exit 1, orderly shutdown.  `wait` now runs lines typed meanwhile, ONLY on
a tty (on a pipe all input is already buffered, so polling would run the rest
of a piped script at the first wait); a typed wait inside a wait is refused.
Selftests for these use a pty (mock/selftest.py run_pty).
LINK DEFAULTS (2026-11-23): preflight defaults changed from --link-wait 12 / --link-resets 5 to --link-wait 6 / --link-resets 10 (more re-rolls for similar time; the 12 s figure was only a guess, one run had its last round up within ~8 s; use the LINK TIMING lines below to judge).
LINK TIMING (2026-11-23): each preflight round prints `link timing round R: N/32 Up, first Xs, last Ys` (seconds from just after the reset to each end's first Up poll; every end goes to the log as LINKTIME).  `--link-linger SECS` adds SECS to every round's wait (a round that comes Up late is accepted and printed as LATE) to find out whether stragglers ever come Up late, i.e. whether longer waits or more short rounds (--link-wait 6 --link-resets 10) pay.  Not yet run on the QB.
Run 20261118-020111 (bac7d62, seam5 35 min, --abort-on-error, preflight 12 s
x 5): preflight needed 3 extra resets, 44 s in all (rounds showed port 0, not
2, for a different set of ends each time: bh1/bh3 eth4+eth13, then bh0/bh1
eth10 + bh1/bh3 eth4, then bh0/bh2 eth12; 4th round all Up within ~8 s).
Then all healthy: 111130 cars (15.6k..16.2k per 5 min), out == in == noc,
txfail 0, retrain 0, uncorrected FEC 0, 419 bolts (median 1.8 s, max 3.3 s),
10664 frames, `.as:4`.  This hub image predates the AER marks, so aer.py had
nothing to read; the AER run needs the hub from b948755 or later.
FIRST AER MEASUREMENT (run 20261118-030230, 435e0e4, seam6, 12 min): duty 74%
(events running 74% of wall time), obsolete 0.8%.  That hub image counted only
COMMITTED (non-empty-center) events, which UNDERCOUNTS true AER: empty-center
events count too.  Its numbers (252 mAER total, 413 trailing, rising while the
FB1 seed filled the grid, fill 8% -> 49%) are the non-empty-only figure, not
AER; ignore them (the old C:R:O marks are not readable by the current
aer.py).  Redo with the hub from the empty-center commit.  Counters are now
u64 (a sparse grid draws many empty events: at 20 draws per event the u32
empties counter could wrap in days).
FIRST 6 HOUR RUN AND FIRST TRUE AER (run 20261118-035205, f5f7b56, seam7,
6.04 h, seam4, --abort-on-error): clean.  Preflight 30.9 s after 2 extra
resets.  4182 bolts, no TIMEOUT: NEW_LEADER 1.79 s (sd 0.06, constant),
RUN_EVENTS 2.43 s mean (1.70-3.50, sd 0.58).  Cars out == in == noc on every
channel, 1,124,360 in the last block, txfail 0, nocfail 0, retrain 0,
uncorrected FEC 0 (corrected FEC counts are in the billions per link).  Stream:
FEEDSTAT real 151.7 / dup 4.3 per 30 s (keepalive pacing fix 1e948b3 works).
aer.py: duty 74-75%, obsolete 0.7%, fill plateau 72-73%.  Trailing 300 s:
1166 mAER wall, 1553 running (non-empty-only 1008, i.e. about 1.0 AER).  The
TOTAL (4523 mAER) is inflated by the first 30 min: while the grid is nearly
empty every attempt burns 5000 cheap empty draws (first 1800 s: 40494 mAER);
use the trailing figures for steady state.  Per hub, committed events are
uniform (225-255M, +-6%); the 40% spread in total events per hub is all empty
draws (131M-2.5G; low on central chip 0 hubs near the seed, which fill first).
Since the hub image after 76b9426 seekRandomNonEmptySite gives up after 100
empty draws (was 5000): AER numbers from before and after are NOT comparable
on sparse grids (the empty events of the first half hour shrink a lot).
EXCHANGE SPEED-UP (run 20261118-123006, 3e5011e, seam6 12 min, preflight all
Up in 5.5 s): PACKET_SLEEP_CYCLES 1,000,000 instead of 10,000,000.  xchg.py:
6.4 ms per packet (about 64 ms before); slowest hub 0.17 s SAVE / 0.24 s
LOAD; host bolts NEW_LEADER 0.28 s (was 1.79), RUN_EVENTS 0.36 s (was 2.43);
duty 94% (was 74%).  At equal grid fill the committed (non-empty) running
rate is unchanged (441 mAER at 20-40% fill both runs, about 690 at 40-55%),
so wall AER is up by duty: 414 vs 329 wall at 20-40% fill, 656 vs 505 at
40-55%.  Cars out == in == noc, txfail 0, nocfail 0, retrain 0, FEC
uncorrected 0, BADC 0, no TIMEOUT.  The 100-draw cap changed the empty
share little (empties per committed event 1140 vs 1225 at 0-20% fill): the
total mAER while the grid fills is driven by sparse hubs, so compare AER at
equal fill or in steady state (full 6 h run at 73% fill: 1166 wall).
Hubs stop logging at different moments: only the cycles every hub has are used.
config/seam9.cmd: ONE HOUR version (sleep 100k experiment).  Run 125641 HUNG at cycle 250; rerun 165950 ran the full hour clean (565 cycles, steady wall AER ~1570, duty 97%). Hang is intermittent (see OPEN_ISSUES.md).
config/seam12.cmd: THREE HOUR four-corners soak (`policy fourcorners`, 18 blocks
of 10 min); the first hour compares with seam9/seam10 (aer.py --series 600).
(Default policy is fourcorners since 2026-11-19; seam6.cmd and seam10.cmd now say `policy supercell` so they stay the supercell baselines.)
config/seam17.cmd: THREE HOUR twin of seam16 (`seed 25 4` stress soak, 18 blocks of
10 min, `zhd` after each) for counting stream breaks per hour.
config/seam18.cmd: ONE hour cache ELISION soak (`events honest`, `elide on`,
`cycle repeat on`; see OPEN_ISSUES "Cache elision").  mfmxctl `elide [on|off]`
and `cycle repeat [on|off]` are new (2026-11-21), as is `events [shortcut|honest]`.
config/seam21.cmd: ONE hour elision soak at OW=OH=20, seam20's setup (compare with it).
config/seam20.cmd: ONE hour elision soak at OW=OH=15 (044edf1), seam18's setup.
config/seam19.cmd: 20 min seam18 twin whose hub `.xe` marks carry per-Dir8 packet
counts (`xchg.py RUNDIR --dirs`); seam18 showed SAVE packets -47% but LOAD only -11%.
config/seam13.cmd: seam11 (12 min) with the video feed started LATE: no `--rtmp` on
the command line, `feed on` right after `start` (the RTMP stream does not exist
during bring-up; experiment against the hls.js freezes at the NO DATA gap, see
OPEN_ISSUES.md).  Open the viewer after the `feed on` line; refresh once.
config/seam11.cmd: the seam6 twin with `policy fourcorners` (the four-corners
prototype; compare with seam6 runs 005507/012507).  Run 022401 (1159c97): CLEAN,
faster fill (69% vs 49-63% at 690 s), committed events/hub/s +26-32%, spread
cv 37% vs 58-67%, duty 98%; details in OPEN_ISSUES.md.
config/seam10.cmd: seam9.cmd for the PACKET_SLEEP_CYCLES=0 experiment (hub
build must have the 0 constant).  Run 211051: clean hour, same AER/duty as
100k (exchange is no longer the bottleneck).  Compare the same counters, STALLS and AER.
config/seam7.cmd: 6 hour unattended version of seam5/seam6 (36 blocks of
wait 600 / seams / status); edit the number of blocks to change the length.
Run directory grows ~3.4 MB/min; check df first.  Ran clean for 6 h.
Other open items: the user must merge the latest bundle.  The mock
selftest (mock/selftest.py) is fully green again (132 checks; the old 7
failures were a mock gap plus tests still using the renamed `phase` command).
