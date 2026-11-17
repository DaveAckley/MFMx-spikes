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

## 3. DOCUMENT MAP (every file below links to the others)

- srcs/host/tools/ERISC_SEAM_DESIGN.md  design of the eth-seam, sections
  1-18.  Sec 15 Dir4 wiring, 16 slice 1, 17 slice 2 (diagonals), 18 slice 3
  (N/S seam, 2x2 array, centre diagonals).
- srcs/host/tools/ERISC_NOTES.md  hardware facts, ordering lessons, the
  running hardware record (sec 9.x; 9.14 = mfmxctl runs).  Cabling: ring
  0-1-3-2; E/W links x=6,7,10,11; N/S links x=3,4,12,13.
- srcs/host/python/EWD/HANDOFF.md  mfmxctl usage, command files (seamtest,
  seamcycle, seamlong, seamns), selftest, ptrace and eth-seam sections.
- scripts/ptrace/README.txt  packet-trace analyzer (reads RUNDIR/tiles).
- scripts/cachecensus/cachecensus.py  per-neighbor cache occupancy from the
  `.ac` marks; `--seam CHIPA:DIR:CHIPB` checks seam hubs.
- srcs/host/tools/run_native_tests.sh  all native tests (ASAN=1, name filter).
- srcs/shared/include/EthSeam.h  the shared routing/dealing header (parts 1-5
  plus the T6 and host lookups); the single source of truth for layout.
- Claude's own memory notes live outside the repo
  (~/.claude/projects/.../memory/); this file and the docs above are the
  durable record.

## 4. WHERE THINGS STAND (2026-11-17, bundle head 8758b01)

PROVEN ON HARDWARE (chips 0,1; links eth 6,7,10,11): E1 bring-up and
teardown, Dir4 crossings, diagonal crossings (slice 2), cache loaders across
the seam, global supercell parity, 55 bolts CONSENSUS over a 5-minute cycle,
cross-seam cars in == out on all 13 channels, retrain 0 and FEC uncorrected 0,
clean exit.  Corner caches of all seam hubs are full (seamlong10 / run
20261117-102406, cachecensus ok).  Not yet checked: atom CONTENTS match across
the seam.

BUILT, NOT YET RUN: slice 3a (N/S seam, chips 0 and 2):
  python mfmxctl.py --layout seamns --boot --rtmp -s config/seamns.cmd --script-exit
  scripts/cachecensus/cachecensus.py MFMX-<date>-<time> --seam 0:S:2
Expect 4 links UP (x=3,4,12,13; 4,12,13 never tried), ~5 channels per link,
bolts CONSENSUS, every SEAM hub "ok".

NEXT: slice 3b (2x2 array, four seams, centre diagonals still off) then 3c
(centre diagonals through the middle chip, 2 extra channels per seam,
host refuses non-closed squares) -- see ERISC_SEAM_DESIGN.md section 18.
Other open items: the user must merge the latest bundle; branch
`backup-before-untrack` can be deleted with the user's say-so; 7 unrelated
selftest failures are pre-existing (legacy lamer/timeouts, guards, slowscan
gate, three phase-name checks).
