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

- Claude commits on LOCAL branch `latest` (ALWAYS Claude's current
  work, Liv's choice 2026-11-25) and keeps `diagonal-tiles` at the same
  tip (the EW-lattice line of work lives on latest since 25a522d).  After
  each commit:  git branch -f diagonal-tiles HEAD,  then
  sg claude -c 'git bundle create /data/claude/CLAUDES_PLACE/claude.bundle HEAD latest diagonal-tiles'
  and ALWAYS check  git bundle list-heads <bundle>  equals
  `git rev-parse HEAD` (a silent failure once left it stale).  Never
  push: Liv pushes.  CAUTION for Liv: merging claude/latest into
  eth-seam brings the diagonal-tiles commits with it.
- The user runs `git fetch claude`, `git merge --ff-only claude/latest`,
  pushes to GitHub; the QuietBox (QB) builds from GitHub (build_all.sh) and
  the user runs mfmxctl there.  Results reach Claude ONLY if copied into
  /data/claude/CLAUDES_PLACE/ (seamcycle*.out, MFMX-<date>-<time>/ dirs).
- Merged work is effectively published: never rewrite it; fix forward.
- Stage EXPLICIT paths only; never `git add -A` (build outputs are
  untracked and once got committed).
- Do not start new work (new slices, reformatting) without a go-ahead.
- Command files (.cmd, experiment.cmd above all) say `feed on` by
  default, so Liv can watch the stream.
- DOC HYGIENE: at least once per session (a quiet moment, e.g. while a
  QB run is going) revisit OPEN_ISSUES.md and CLAUDE.md:
  * OPEN_ISSUES.md keeps only current work: done items move to
    CLOSED_ISSUES.md (short, numbers kept), parked items and ideas to
    DEFERRED_ISSUES.md, stale status words get fixed.
  * CLAUDE.md stays short: rules, the map, one paragraph of current
    state.  Feature notes and run records belong in the doc that owns
    them; refresh section 4.
  * Commit it on its own ("Docs: ...").  No go-ahead needed for this.
- The host pybind glue (EthSeamHostPy.h, QuietBox.cpp, CodeManager.cpp) cannot
  be compiled on the Claude machine (no pybind11); the QB build is its first
  compile.  T6/E1 images CAN be cross-built here: rsync the tree to a scratch
  dir and `make` in its build_cross.
- HUB DIAGNOSTIC MARKS: LOGPTAG (and friends) go to the log block, which
  the hn hart delivers, and slowscan does NOT capture them.  Only HBPTAG
  (and friends) go into the HostBlock buffer that slowscan reads, so it still
  works when hn is sick.  That ring keeps the LATEST
  marks (packString drops the oldest words when full; a torn mark at the head is OK).  When a mark must survive a hang, emit LOGPTAG and
  HBPTAG back to back.  LOG CARS DIED after 65.5 s with no mark before a941d4c (CLOSED_ISSUES
  2026-11-26): a tile silent that long (most EWPs; hubs during a long
  host stall) stopped logging for good.  Fixed and proven; EWP LOGPTAG
  marks now reach tiles/ (560 files per run).  Slowscan prints with Eprintf (stderr): capture
  stderr too when a run may hang.
- DEFINITION OF DONE for a commit (docs are part of the change):
  * Fixed a bug or finished an item: MOVE its entry from OPEN_ISSUES.md to
    CLOSED_ISSUES.md (dated, measured numbers kept, story cut), or just
    update it if still open; any note in HANDOFF.md too, in the SAME commit.
  * Found a bug or idea and did not fix it: add it to OPEN_ISSUES.md before
    the turn ends; chat is not a record.  Parked on purpose (Liv says
    leave it) or an idea nobody is working on: DEFERRED_ISSUES.md.
    OPEN_ISSUES.md stays short and current (trimmed 2026-11-28).
  * Changed behavior, an option or a command: update the doc that owns it
    (BYTEPHYS.md, GEOMETRY.md, HANDOFF.md, ERISC_NOTES.md ...), the
    one-line entry in the CLAUDE.md map if there is one, and any .cmd
    header that mentions it.  Keep CLAUDE.md short (it is loaded into
    every session): no run records or feature essays here.
  * Before committing, grep the docs for the touched symbols/options, and
    say in the commit message which docs changed (or "no doc impact").

## 3. DOCUMENT AND TOOL MAP

Details of every feature below (how it works, marks, run records) are
in CLAUDE_ARCHIVE.md under the CAPITALIZED name given in brackets.

Issue lists:
- OPEN_ISSUES.md  what needs work NOW (open bugs, runs awaited, next
  work awaiting a go-ahead).  Short on purpose.
- DEFERRED_ISSUES.md  parked bugs, design explorations, ideas.
- CLOSED_ISSUES.md  what got done, newest first.

Reference docs:
- BYTEPHYS.md  the physics bytecode: machine, instruction set, chunks
  and images, code cache, ulam-lite compiler, measured cost, change log.
- GEOMETRY.md  how the grid is tiled onto the box: cells, rings, bands,
  T6Grid map, caching policies and event bounds, dead bands, permanent
  edges, EW lattice tilings (sec 10), what a geometry change touches.
- srcs/host/python/EWD/HANDOFF.md  mfmxctl usage, command files,
  selftest, ptrace, eth-seam operation.
- srcs/host/tools/ERISC_SEAM_DESIGN.md  eth-seam design (secs 1-18).
- srcs/host/tools/ERISC_NOTES.md  hardware facts, ordering lessons,
  running hardware record (9.x).  Cabling: ring 0-1-3-2; E/W links
  x=6,7,10,11; N/S links x=3,4,12,13.
- CLAUDE_ARCHIVE.md  the old long CLAUDE.md sections 3 and 4.

Running experiments [ONE SCRIPT, ONE FILE BACK; RUN SUMMARIES]:
- Every experiment, from srcs/host/python/EWD:
  python mfmxctl.py --layout seam4 --boot -s config/experiment.cmd --script-exit --abort-on-error
  Claude rewrites config/experiment.cmd per experiment (first comment
  line = its name) and ships it in the bundle.  Liv sends back ONE file,
  send-MFMX-<date>-<time>.tgz (runsum.txt + console + misc/*.txt + the
  tail of all.mfmk); it lands in /data/claude/CLAUDES_PLACE/.
  runsum.txt sections: bolts, feed, AER, nukes/seeds, hub marks, physics
  timing, code cache and stack, microbenchmark, corner notches, lamers
  (on a bolt timeout), trouble lines (srcs/host/python/EWD/runsum.py).
- Experiments use `events shortcut` (AERse) unless studying AER itself
  [NO AER CHEATING mode]; AER comparisons need no nukes and equal length.
- The host loads raw .bin images (L1 from address 0), never ELF.
- Never kill a run with -9 (a hung mfmxctl blocks tt-smi and can leave
  a link untrained): `quit close`, or SIGTERM.

Builds and tests:
- QB builds from GitHub with ./build_all.sh (hub + EWP + host).  Shared
  header or hub/EWP change => build_all.sh.
- T6/E1 images cross-build here: rsync the tree to a scratch dir and
  `make` in its build_cross.  Host pybind glue does not compile here;
  syntax-check host C++ with the recipe in the archive [Syntax-checking
  host C++].
- srcs/host/tools/run_native_tests.sh  all native tests (ASAN=1, name
  filter).  mock selftest: srcs/host/python/EWD/mock/selftest.py.
- HUB GEOMETRY has ONE source, DemoGlobal.h (Python mirror
  HubGeometry.py); never write 137/108/... as literals;
  scripts/hubgeom/hubgeom_check.sh (build_all.sh runs it).  Guarded
  exception: the T6GRID size literal in BlockCode.h.

Physics on the EWP (BYTEPHYS.md):
- Bytecode types 7, 8 = FB1, FB4 (native twins 4, 5, NativeFB.h);
  source scripts/ulamlite/*.ulam -> ulamlite.py -> scripts/bytephys/*.bp
  -> bpasm.py -> GENERATED srcs/shared/include/BytePrograms.h, which
  holds the physics as ONE BCP blob, BP_BCP (also the tests set
  BytePhysTests.h and the bench set ByteBench.h).  Edit the
  source, rerun the tools, never edit generated files;
  `bpasm.py --list [tests|bench]` prints an image listing.
- Machine: srcs/shared/include/BytePhys.h (verify once at compile time,
  computed goto); chunk cache ByteCache.h (Physics::mCodeCache, 12 x 256 B
  in hart B fast RAM); EWP glue srcs/cross/images/ewp/src/Physics.cpp.
  Hart B budget (Liv): ~2 KB stack reserve + ~3 KB cache + ~1 KB other;
  _BUD.ld.in fails the link under 2 KB of stack.  P4Atom (12 B) is locked.
- Marks: `.pt` physics time per type, `.pb` microbenchmark, `.bc` code
  cache, `.sw` hart B stack, `.pf`/`.pw` recovered physics failures
  [PHYSICS FAILURE MARKS].  Bench atoms: type 31.
- Loadable physics plan: OPEN_ISSUES 'Next work'.

Grid, caching and display:
- srcs/shared/include/CachingPolicy.h  caching policies: fourcorners
  (default) [FOUR-CORNERS TRIMMED BANDS], supercell, ewsnap/ewsnap10
  (branch work) [POLICY EWSNAP]; `policy ...` before `start`.
  `elide on` keeps bands on a repeated corner [CACHE ELISION].
  scripts/cachepolicy/cachepolicy_test.cpp checks the invariants.
- srcs/shared/include/HubEdges.h  PERMANENT array edges (INACCESSIBLE
  rings, `.pe` mask).  srcs/shared/include/EWLattice.h  EW-lattice
  tilings.  srcs/shared/include/EthSeam.h  seam routing (single source).
- Display: picture = hub T6Grids minus N/W rings, hub origins at
  multiples of 137 x 108 [DISPLAY CROP].  Reports carry epoch-of-change
  stamps; host keeps per-pixel newest stamps [EPOCH-OF-CHANGE STAMP,
  REPORT EPOCHS]; stream health and restarts: `zhd` [Report stream
  health].
- Ghost-pixel tools: `who X Y`, `zoom specks [refresh]`, `dumpl1`,
  scripts/l1dump/l1grid.py [WHO, SPECKS OWNER LOOKUP, PIXEL HISTORY,
  TURD VERDICT, HOST-DIRECTED REFRESH].
- `notch on` corner-notch detector, notchwatch.py [NOTCH DETECTOR].

mfmxctl commands worth knowing (HANDOFF.md, `help NAME`):
`nuke [big] [X Y R]`, `autonuke` [NUKE]; `seed N TYPE`, `decay TYPE`;
`events shortcut|honest`; `policy`; `elide`; `cycle start|stop|hold|
step|repeat|holdat|waitpause` [CYCLE HOLDAT / WAITPAUSE]; `zoom
auto|off|SCALE|region|specks|map`, `zoom shrink on` [ZOOM]; `grid
on|off|edges`; `feed on`; `zhd [all|restart|break]`; `notch ...`;
`status`; `quit close`.  Commands live in srcs/host/python/EWD/cmd_*.py
[PYTHON CLEANUP].

Hub diagnostics and analysis scripts:
- scripts/aer/aer.py (AER from `.es`/`.ev`), live AER on the stream
  [LIVE AER]; scripts/xchg/xchg.py (exchange timing, `--dirs`);
  scripts/cachecensus/cachecensus.py; scripts/ptrace/README.txt;
  scripts/celllab/celllab.py and scripts/geomlab/geomlab.py (geometry
  labs, GEOMETRY.md); scripts/runsum/runsum.py (runsum by hand on the QB).
- Marks seen most: `.cp` policy, `.pe` permanent edges, `.ck` ARC clocks
  [CLOCK MARK], `.sc` scy, `.ev`/`.es` event counters, `.eo` EW books
  [STALE EWTs + EW BOOKS], `.dl`/`.nl` report backlog [DL BACKLOG
  MARKS], `.nk` nuke.
- Claude's own memory notes live outside the repo
  (~/.claude/projects/.../memory/); the repo docs are the durable record.

## 4. WHERE THINGS STAND (2026-11-28)

The work is the BytePhys bytecode physics on the EWPs (BYTEPHYS.md; run
20261127-224448: FB4 9423 cycles/event, 2.88x native, FB1 1695, 3.39x,
chunk cache 99.9997% hits).  Cache hits made cheap (run
20261128-003440: bench call 43 cycles per insn from fast RAM, was
139); next the start-time loadable physics (OPEN_ISSUES 'Next work').
Every experiment: config/experiment.cmd, `events shortcut`, `policy
fourcorners`, `elide on`.  Health: recent runs boot clean (the
HOLD_AT_BIRTH init race fix is counting clean boots), 0 bolt timeouts,
consensus ~0.13 s, 140 report streams with 0 desynced, feed 150 frames
per 30 s.  Proven history (seams, 2x2 array, AER, soaks): CLAUDE_ARCHIVE.md
section 4.
