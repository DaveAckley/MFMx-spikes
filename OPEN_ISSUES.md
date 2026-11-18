# OPEN ISSUES (bugs, tuning, unproven things)

One short list; newest first within a section.  Rules (see CLAUDE.md sec 2):
fix something -> delete or move its entry in the SAME commit; find something
and do not fix it -> add it here before the turn ends.  Fixed items are not
kept here (git log is the record).  Design history stays in HANDOFF.md and
ERISC_NOTES.md.

## Bugs

- mfmxctl `status` line prints api=legacy under firePHASER (mislabel).
- `create(50)` seed switch in `GridManager::seekRandomNonEmptySite` is computed
  but unused (the autoseed type follows `decay N`, default 5).

## Unproven on hardware

- Empty-center counters and u64 `.es`/`.ev` marks (f5f7b56 era image):
  first reading comes from the seam7 run.
- Keepalive pacing fix (1e948b3) on the real stream (FEEDSTAT dup near 153
  per 30 s during `start`; if it stays near 76-92 the stall is media-server
  or player side).
- ZHD counter zero-init (61cb6de): `#N` in HAR lines should now be small.
- Atom CONTENTS match across seams (content-hash slice; user: pass on it
  indefinitely while things look good).
- pybind `portStatus`/`releaseIdlePorts`, `--abort-on-error` and typed `wait`
  beyond one clean run.

## Speed / measurement ideas (not started)

- Find the constant 1.8 s in NEW_LEADER: timestamp each T6 ack, log the last
  acker per bolt (needs a rebuild).
- Bolt poll tick 0.05 s instead of 0.2 s (about 2 percent).
- Longer `dwell` (script only; physics trade-off).  `dwell` is the only knob
  for event time per cycle; duty is about 74 percent.
- `loadPacket` ships every site including EMPTY atoms (full cars regardless
  of occupancy): bandwidth/latency opportunity in the RUN_EVENTS cache load.
- AER phase 2: live mAER in the lower-left text area (tail the marks once
  per cycle; verify log files are flushed in real time first).
- FEEDSTAT per-tick timing.
- ACB ship rate (BOGOMS_PER_FRAME, EP_ACacheBlock.cpp) is a low-priority
  knob.  To check ACB is not crowding IH, add a dotted `.ab` mark at the
  ship point in `ACacheBlockPrivateControl::updateCars` (undotted tags such
  as SHIPFRM/WAITSENT/DGLLen are suppressed by HBF_NO_UTAG).  The DL backlog
  itself is by design (lossy DLGridList), not a bug.

## Cleanup / nice to have

- First-NEW_LEADER `once` hack in `workOnEnteringNewLeaderB` is probably
  removable (zero-initialized loaders make the first save a no-op).
- `.pw` waypoint marks on the T6 side are not emitted (analyzer parses them).
- Inter-T6 destinations are not in PKTS.txt (analyzer learns them from marks).
- Possible `sleep SECS` mfmxctl command (offered, not requested).
- FB4's renderer colours by hidden counter mStg[1]; a seed mix (DReg/FB1)
  would add variety.
