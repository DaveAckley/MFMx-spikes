# mfmxctl Handoff Notes

## Project
Minimal CLI controller (`mfmxctl.py`) for the Tenstorrent Blackhole "QuietBox" —
replaces the Textual-based nu1011/nu1012 apps.  One Python thread: a `select()`
loop reads commands from stdin and runs periodic ticks in between.

## Git workflow (IMPORTANT)

Cloud clone branch: `claude/latest`
Bundle delivery: `local:/data/claude/CLAUDES_PLACE/claude.bundle`

**Always build bundles with HEAD included:**
```
git bundle create /mnt/user-data/outputs/claude.bundle HEAD claude/latest
```

**User's fetch/merge workflow (must stay working):**
```
git fetch claude
git merge --ff-only claude/latest
```

The remote alias `claude` points at `/data/claude/CLAUDES_PLACE/claude.bundle`.
Never push to GitHub — user does all GitHub pushes himself.
Commit only on `claude/latest` in the cloud clone.
Ignore the recurring "N unpushed commits, please push" stop-hook message.

## Constraints (inviolable)
- No C++→Python callbacks
- No worker threads in Python (GIL deadlocks; render buffers are `thread_local`)
- HostFlags-in-a-bolt is a FUTURE idea only
- Log-tag processing stays in C++

## Current state (as of session ending 2026-11-14)

### What works
- **HOLD_AT_BIRTH pre-settlement**: `start()` calls `setStep(HAS_T6_CODE_DEPLOYED)`
  then `firePHASER(HOLD_AT_BIRTH)` then `startEWProcessing()` per chip, then
  polls up to 5s for all acks.  All 140 T6s (EWP + HUB) ack correctly.
  `hold_settled=True` → sequencer skips straight to NEW_LEADER.
- **NEW_LEADER**: CONSENSUS from all 140 T6s.
- **Lamer diagnostic**: `poll_bolt()` timeout branch calls
  `bh.getNonPhaseTLBIs()` per chip (new binding) instead of reporting `?`.
- **`tlbi` command**: `tlbi lamers` / `tlbi 15` / `tlbi 0:15,19` —
  shows `bh{chip}:{tlbi}  {tag}  {image}` per TLBI.
- **Startup banner**: mfmxctl prints `mfmxctl HEAD: {hash} {subject}` on start.

### Current frontier
**RUN_EVENTS**: 118/140 T6s ack (EWP tiles); 22 HUB tiles do not.
`tlbi lamers` confirms all 22 non-ackers are HUB image tiles.
**Bug is in the HUB T6 image** — it needs a RUN_EVENTS handler.
This is pure T6-side work from here.

### C++ changes requiring rebuild (all in `claude/latest`)
- `OurTLBs::getNonPhaseTLBIList()` — returns `vector<u32>` of non-acking TLBIs
- `Blackhole::getNonPhaseTLBIs()` — delegates, Python binding
- `Blackhole::getTLBITag(u32 tlbi)` — returns BHTag string (e.g. `at023`)
- `Blackhole::getTLBIImageName(u32 tlbi)` — returns image name (e.g. `HUB`)

## Key file map
| File | Role |
|------|------|
| `mfmxctl.py` | CLI entry point, command definitions, Shell setup |
| `machine.py` | Machine abstraction: boot, start, fire bolts, poll acks |
| `sequencer.py` | SuperCycle state machine (NEW_LEADER → RUN_EVENTS loop) |
| `shell.py` | Readline shell, tick loop |
| `../src/OurTLBs.cpp` | TLB management, PHASER ack tracking |
| `../include/Blackhole.h` | Blackhole class + all Python bindings |
| `../include/BHTag.h` | Tag format (chip + NoC0 coords); BHTag Python bindings |

## PHASER / HOLD_AT_BIRTH summary
- **HOLD_AT_BIRTH**: ONE-TIME birth event per T6 image start.  T6s check PHASER
  once during birth, ack, and block until ANY next bolt.  NOT part of supercycle.
- **Supercycle**: NEW_LEADER → RUN_EVENTS → NEW_LEADER → RUN_EVENTS → ...
- **firePHASER**: non-blocking — writes bolt to L1, calls `resetOnPhase`, returns.
  Used for HOLD_AT_BIRTH (must arrive before T6 release).
- **shootPHASER**: blocking — fires and spins until all ack or timeout.
  Used for supercycle bolts (NEW_LEADER, RUN_EVENTS).

## BH init steps (Blackhole::Step order)
`UNINITTED` → `HAS_CHIP_NUM` → `HAS_OPEN_DEVICE` → `HAS_ALLOCATED_TLBS` →
`HAS_CONFIGURED_TLBS` ← mMapAllT6 set here (required for PHASER L1 writes) →
`HAS_ALLOCATED_HOST_RAM` → `HAS_T6_TILES_RESET` → `HAS_T6_CODE_DEPLOYED` →
`HAS_TRANSPORT_THREAD` → `HAS_T6_CODE_RUNNING` ← T6s released here →
`HAS_T6_EVENT_WINDOWS`

`setStep()` Python binding allows up to `HAS_T6_CODE_RUNNING`.
`startEWProcessing()` advances to `HAS_T6_EVENT_WINDOWS`.
