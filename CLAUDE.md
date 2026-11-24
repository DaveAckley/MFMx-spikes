# START HERE (for Claude sessions on MFMx-spikes)

Read this first.  It is auto-loaded by Claude Code from the repo root.
Everything below is 7-bit ASCII on purpose (the user wants only ASCII in
source, comments, docs and commit messages).

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
- SPECKS OWNER LOOKUP (2026-11-24, built, not yet run; host C++ + Python, ./build_all.sh): module `pixelOwner(x,y)` -> None or (chip, tlbi, gridX, gridY): the hub whose SELF sites map onto pixel (x,y) (QuietBox::getT6GridInfoFor, site = pixel - hub origin + SELF_ORIGIN).  `zoom specks` rows whose last painter is a RING report now also read that OWNER hub's SELF site (one more L1 dump per distinct owner) and append `OWNER atCXY: EMPTY | type N`.  Why: for a RING painter `HUB:` is only the borrower's cache copy (cleared after the SAVE), so specks10-16's 17 RING EMPTY rows could not be judged.  Reading: OWNER EMPTY with the host still showing an atom = a real turd (owner emptied it, host never heard); OWNER type N = the pixel is correct (no change, so no report).  Mock: $MFMX_MOCK_OWNER; selftest specks_who.
- PIXEL HISTORY (2026-11-24, built, not yet run; host C++ + Python, ./build_all.sh): QuietBox::pixelHistory keeps the last 4 reports that reached each pixel (accepted or dropped as stale, SELF/RING, atom type, stream epoch), newest first, written in ZHostDecompressor next to acceptPixelEpoch (33 MB, lock-free shift, a torn entry is possible and fine).  Module `pixelHistory(x,y)` -> [(accepted, ring, type, epoch)...]; `zoom specks` rows end with `HIST: R4@e150 < S0@e150 < -S4@e148` (newest first; R/S = RING/SELF; type 0 = empty; leading '-' = dropped as stale).  Why: specks10-17 found turds whose owner SELF site is EMPTY while the pixel shows an atom from a RING report.  Hypothesis to test: owner's empty report and a stale borrower RING atom report arrive in either order with epoch labels of emission, not of change (so equal or later label wins).  Reading: `S0@eN` before (older than) `R4@eM` with M >= N = the stale-RING hypothesis, then fix by stamping epoch-of-change; no empty report at all in the history = the owner never reported (silent hub write, refresh needed).  Mock: $MFMX_MOCK_HIST; selftest specks_who.
- SELF WINS A TIE (2026-11-24, built, not yet run; host C++, ./build_all.sh): QuietBox::ringLosesTie + ZHostDecompressor: a RING report is dropped (counted in `stale`, shown as `-R4@eN` in HIST) when the pixel's last accepted painter is a SELF report of the SAME epoch.  Why: specks10-18 HIST showed 19 of 27 turds were the owner's empty (S0@eN) followed by a stale borrower atom (R4@eN) in arrival order; the other 8 had the RING label newer than the owner's (5) or a preceding ring-empty (3) and are NOT fixed by this (needs the epoch-of-change stamp).  Cost: a borrower's live ring change in an epoch where its pixel already got a SELF report is hidden until the next epoch (about one epoch, only for such pixels).  Check: rerun the quiet-pause specks; expect about 8 fewer-than-27 turds and `stale` > 0.
- ACB FRAME 25 ms EXPERIMENT (2026-11-24, built, not yet run): EP_ACacheBlock.h BOGOMS_PER_FRAME is now ACB_BOGOMS_PER_FRAME, default 25 (was 50).  Why: specks10-15's `.dl` marks show the DL never empties at RUN_EVENTS exit (LEN averages ~9.7k, max ~15k of ~26k sites) and the hub pops ~10.8k sites per 3.3 s epoch (~3.3k/s), about what one ~986 byte car per 50 ms (20 cars/s) carries; host pixels were still changing 55-70 s after a pause.  Test: compare `.dl` LEN at exit and pops per epoch against the 50 ms runs (LEN should fall, pops/epoch rise if pacing was the cap); if nothing moves, the compressor or car returns are the limit (next: `.fr:FRAMES:FULL:TIMEOUT:CARWAIT` marks).  To revert build with -DACB_BOGOMS_PER_FRAME=50u or edit the default.  Hub change: ./build_all.sh.
- LIVE LINE SHOWS SCY (2026-11-24, built, not looked at on the stream): the stream's `LIVE <elapsed>` overlay now reads `LIVE <elapsed>  scy N` once the supercycle number is nonzero.  RTMPFeed13.scyFn (set each frame by Machine.frame from Machine.scy_fn, which mfmxctl points at the SuperCycle) is called at render time, so the keepalive redraw shows it too; any error there drops the suffix, never the frame.  Python only.
- IDLE-TIME REFRESH is currently OFF (ACB_REFRESH_PER_FRAME default 0 in EP_ACacheBlock.h) while hunting turds: with it on, a held cycle repaints the specks (SELF, current epoch, empty) before `who` can name the real painter.  Restore the default 20u when the hunt is over, or build with -DACB_REFRESH_PER_FRAME=20u.
- IDLE-TIME REFRESH (2026-11-23, built, not yet run): ACacheBlockPrivateControl::tryToSendFrame (T0) now, when the change list is empty mid-frame, reports up to REFRESH_PER_FRAME (20, EP_ACacheBlock.h; 0 = off; was 8) random SELF sites per 50 ms frame, reading their current atoms from the grid, so a stale host pixel ('turd') is repainted from truth gradually (about 400 reports/s/hub when idle; random, so half the sites in ~26 s, 90% in ~85 s).  Only idle time is used; self sites only (the owner has the last word).  Mark `.rf:N` every 100k refresh reports.  Prompted by ghost islands at nuke hole edges: all four hubs under them were empty in `dumpl1`/l1grid, zhd clean (0 bad reports, 0 desyncs); cause still unknown, with the quiet cache load/save (b5e50c0) in the build.
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
  `status` (`live:`): `[140] est. AER: 1.57, 1.58, 1.57`
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

## 4. WHERE THINGS STAND (2026-11-17)

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
