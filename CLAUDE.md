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
- srcs/shared/include/CachingPolicy.h  the CACHING POLICY: per Dir8 whether a
  hub sends its edge sites, receives into its cache, or does nothing, and the
  event bounds; CP_SUPERCELL (original: NEW_LEADER arg = leader code in each
  2x2 supercell) and CP_FOURCORNERS (every hub alike; the arg is a Corner4,
  MDist.h: the corner the hubs expand toward, borrowing the two edge bands
  and the corner cache there and lending the opposite three).  No virtuals
  (T6): CRTP base + static functions, picked by a switch.  The host chooses
  per run with `policy [supercell|fourcorners]` (or --policy), before
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
  the `.xw` heartbeat marks (a hub waiting over 1 s).  Self-test xchg_test.py.
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
  delta and the totals read 1e13 mAER.  scripts/aer/aer_test.py is its self-test.
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
