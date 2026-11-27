# BYTEPHYS -- reference manual for the physics bytecode (version 0, SPIKE)

A small, growing reference for BytePhys, the bytecode that MFMx physics
can be written in and run on the EWPs.  Version 0 is a spike ("pretend
bytecode"): just enough machine to run FB1 and FB4 and measure what
interpreting costs on a T6 baby core.  This manual is meant to be
fleshed out as the design grows; section 11 collects what is not
decided yet.

Where things live:

| What | Where |
|------|-------|
| The machine (opcodes, fields, faults, interpreter) | srcs/shared/include/BytePhys.h |
| Programs (assembly source) | scripts/bytephys/*.bp |
| Assembler | scripts/bytephys/bpasm.py |
| Assembled programs (GENERATED, do not edit) | srcs/shared/include/BytePrograms.h |
| Native twins of the programs | srcs/shared/include/NativeFB.h |
| EWP glue (types 7, 8; timing marks) | srcs/cross/images/ewp/src/Physics.cpp |
| Host test (bytecode == native, faults) | srcs/host/tools/bytephys_test.cpp |
| Design history and results | OPEN_ISSUES.md 'physics as bytecode' |

BytePhys.h is the single source of truth: the assembler reads the opcode
and field numbers out of it, so this manual describes it and must follow
it, never the other way around.


## 1. Why bytecode

The tight limit for heavy physics is hart B's 8 KB fast RAM: the event
window (mFastEW) and the native stack both live there, and the stack
must stay there (moving it to L1 kills performance).  Compiled native
physics that recurses or uses big locals can overflow it with no warning.
A bytecode machine keeps its own stacks as fixed arrays, so physics
cannot grow the native stack, every stack access is checked, and a
program that misbehaves FAILs cleanly into the physics RecoverPoint
instead of corrupting the tile.  Bytecode is also small (FB1 is 129
bytes) and could someday be loaded per type from the host.  The main
hope for winning back speed is RICH primitives (section 11): one
instruction doing the work of a native loop.


## 2. The machine

- **Operand stack:** 16 entries of u32 (`Machine::STACK_SIZE`).
- **Call stack:** 8 return addresses of u16 (`Machine::CALL_SIZE`).
  Bytecode may call and recurse freely within that depth; the
  interpreter itself is one loop and never recurses natively.
- **Program counter:** a byte offset into the program, starting at 0.
- **The window:** 41 sites, numbered as everywhere in MFM (MDist.h):
  sorted by squared distance from the center, site 0 the center, 1-4 the
  four nearest, 5-8 the four diagonals, out to 40 at Manhattan distance
  4.  Instructions name a site by its number, taken from the stack.
- **Fuel:** every instruction executed costs 1; the EWP gives each event
  `BP_FUEL` = 10,000.  Running out is a fault, so an infinite loop ends
  the event instead of the tile.
- **Size:** the Machine object is 84 bytes and lives in hart B's fast RAM
  beside the event window (`Physics::mVM`).

An event runs one program from offset 0 until HALT0 or HALT1:

- **HALT1:** commit the window (the event happened).
- **HALT0:** physics declined; the window is discarded.

Everything else ends the event with a **fault** (section 6).


## 3. Encoding

One opcode byte, then its immediates, little-endian.  Jump and call
targets are absolute u16 byte offsets into the program.  Binary
operations pop b, then a, and push (a op b), so `push 7 push 2 sub`
leaves 5.

Stack effects below are written `( before -- after )`, top of stack on
the right.


## 4. Instruction reference

| Hex | Mnemonic | Immediate | Stack effect | Notes |
|-----|----------|-----------|--------------|-------|
| 00 | halt0 | | ( -- ) | End: discard the window |
| 01 | halt1 | | ( -- ) | End: commit the window |
| 02 | push8 | u8 | ( -- v ) | `push N` assembles to this when N < 256 |
| 03 | push32 | u32 | ( -- v ) | `push N` for larger N |
| 04 | dup | | ( a -- a a ) | |
| 05 | drop | | ( a -- ) | |
| 06 | swap | | ( a b -- b a ) | |
| 07 | over | | ( a b -- a b a ) | |
| 08 | add | | ( a b -- a+b ) | u32, wraps |
| 09 | sub | | ( a b -- a-b ) | u32, wraps |
| 0a | eq | | ( a b -- a==b ) | 1 or 0 |
| 0b | lt | | ( a b -- a<b ) | unsigned; 1 or 0 |
| 0c | jmp | u16 target | ( -- ) | |
| 0d | jz | u16 target | ( x -- ) | jump if x == 0 |
| 0e | jnz | u16 target | ( x -- ) | jump if x != 0 |
| 0f | call | u16 target | ( -- ) | pushes the return offset on the call stack |
| 10 | ret | | ( -- ) | pops the call stack |
| 11 | getf | u8 field | ( sn -- value ) | read a field of atom sn (section 5) |
| 12 | setf | u8 field | ( sn value -- ) | write a field of atom sn |
| 13 | copy | | ( src dst -- ) | atom dst = atom src (whole atom) |
| 14 | empty | | ( sn -- ) | atom sn = the empty atom |
| 15 | between | | ( lo hi -- r ) | uniform random in [lo, hi] |
| 16 | onein | | ( n -- flag ) | 1 with probability 1/n, else 0 |

Notes:
- Every jump, call and return target is checked against the program
  length; every site number against 41.
- The random draws (between, onein) use the EWP's PRNG; the host test
  replays the same stream through native and bytecode code and checks
  the draws come in the same order.


## 5. Atom fields (getf / setf)

The fields of the 12-byte P4Atom (locked for this round) that version 0
can name:

| Number | Name | Meaning | Writable |
|--------|------|---------|----------|
| 0 | type | the atom's type | NO (its parity would need redoing; use copy or empty) |
| 1 | data0 | `P4Atom::mData0`, u16 | yes (low 16 bits stored) |
| 2 | stg0 | `P4Atom::mStg[0]`, u32 | yes |
| 3 | stg1 | `P4Atom::mStg[1]`, u32 | yes |

These are raw storage words, not typed fields.  How a type declares
named fields of its own is open (section 11).


## 6. Faults

Any violation calls the environment's fault handler, which never
returns.  On the EWP it FAILs into the physics RecoverPoint: the event is
undone (the window is committed with its center erased) and the tile
logs `.pf` (what failed) and `.pw` (file and line).  Each fault has its
own FAIL line, so `.pw` names the fault.

| Code | Name | Cause |
|------|------|-------|
| 1 | BF_STACK_OVER | pushed past 16 operands |
| 2 | BF_STACK_UNDER | popped an empty operand stack |
| 3 | BF_CALL_OVER | call nested deeper than 8 (e.g. runaway recursion) |
| 4 | BF_CALL_UNDER | ret with no call |
| 5 | BF_OPCODE | not an opcode |
| 6 | BF_PC | ran off the end, a truncated immediate, or a bad target |
| 7 | BF_SITE | site number 41 or more |
| 8 | BF_FIELD | unknown field, or a write to `type` |
| 9 | BF_FUEL | more than the event's fuel (10,000 instructions) |

No checking happens before a program runs.  A verifier that rejects bad
programs at load time would only report problems earlier; the run-time
checks are what keep the tile safe.


## 7. Assembler (.bp files)

`scripts/bytephys/bpasm.py` assembles every `*.bp` in its directory into
BytePrograms.h, one `BP_PROG_<NAME>` per file (fb1.bp -> BP_PROG_FB1).

Syntax:
- One instruction per line; `;` starts a comment.
- `name:` defines a label (may share a line with an instruction).
- Mnemonics are the opcode names in lower case, without `OP_`.
- `push N` takes decimal or 0x hex and picks push8 or push32.
- `jmp jz jnz call` take a label.
- `getf setf` take a field name: type, data0, stg0, stg1.

Commands (from the repo root):
- `scripts/bytephys/bpasm.py`: regenerate BytePrograms.h.
- `scripts/bytephys/bpasm.py --check`: exit 1 if BytePrograms.h is out of
  date (run_native_tests.sh runs this).
- `scripts/bytephys/bpasm.py --list scripts/bytephys/fb4.bp`: listing with
  offsets and bytes.

After editing a .bp: rerun bpasm.py, then
`srcs/host/tools/run_native_tests.sh bytephys` (bytecode must still match
native), then ./build_all.sh on the QB.


## 8. An example: FB4

FB4 bumps a counter in the center atom, then copies the center onto all
40 neighbors.  Native (NativeFB.h):

```
ca.mStg[1]++;
for (sn = 1; sn <= 40; ++sn) atom(sn) = ca;
return true;
```

Bytecode (scripts/bytephys/fb4.bp), with its listing:

```
   0: 02 00     push 0         ; ( 0 )         site 0, for setf
   2: 02 00     push 0         ; ( 0 0 )       site 0, for getf
   4: 11 03     getf stg1      ; ( 0 stg1 )
   6: 02 01     push 1         ; ( 0 stg1 1 )
   8: 08        add            ; ( 0 stg1+1 )
   9: 12 03     setf stg1      ; ( )           ca.mStg[1]++
  11: 02 01     push 1         ; ( sn )        sn = 1
  13: 02 00 loop: push 0       ; ( sn 0 )
  15: 07        over           ; ( sn 0 sn )
  16: 13        copy           ; ( sn )        atom sn = atom 0
  17: 02 01     push 1
  19: 08        add            ; ( sn+1 )
  20: 04        dup
  21: 02 29     push 41
  23: 0b        lt             ; ( sn+1 flag )
  24: 0e 0d 00  jnz loop       ; ( sn+1 )      while sn+1 < 41
  27: 05        drop           ; ( )
  28: 01        halt1          ;               commit
```

29 bytes; 369 instructions per event (9 per neighbor).  FB1 (fb1.bp,
129 bytes) shows branches, random draws and the kabam loop.


## 9. On the EWP

- **Bytecode physics types:** type 7 runs fb1.bp, type 8 runs fb4.bp
  (`PHY_BFB1`, `PHY_BFB4` in Physics.cpp).  They behave exactly like
  native types 4 and 5.  Seed them with e.g. `seed 25 7`.
- **On screen:** the host draws a bytecode type as a darker copy (5/8
  brightness) of its native twin's color.  `BytePhys::isBytecodeType`
  lists the bytecode types.
- **Timing:** every EWP logs `.pt:TYPE:EVENTS:TICKS:INSNS.` (hex) every
  4096 events of types 4, 5, 7, 8: AI clock cycles spent in physics and
  bytecode instructions executed.  runsum.txt's "physics timing" section
  turns them into cycles per event and bytecode/native ratios.
- **The Env interface** (a template parameter, so the EWP and the host
  test run the same interpreter):
  - `P4Atom & atom(u32 sn)`: window site sn.
  - `u32 between(u32 lo, u32 hi)`.
  - `bool oneIn(u32 n)`.
  - `[[noreturn]] void fault(u32 f)`.


## 10. Measured cost (QB, 2026-11-26)

Runs 20261126-141643, -163422 and -165929, honest events, AI clock 800
MHz, means over all EWPs:

| Physics | Native cycles/event | Bytecode cycles/event | Ratio | Insns/event |
|---------|---------------------|-----------------------|-------|-------------|
| FB1 (4 / 7) | ~500 | ~2260 | 4.5x | ~30 |
| FB4 (5 / 8) | 3320 | 21886 | 6.6x | 369 |

That is about 50 cycles per bytecode instruction: slow for a switch
dispatch.  The likely cost is that the program bytes and the switch's
jump table sit in L1, not fast RAM (unmeasured).  No faults in any run.


## 11. Open questions and wish list (to be fleshed out)

- **Rich primitives**, where the real win is: one instruction doing a
  native loop's work.  First candidate (Liv): "find a closest atom of
  type X, breaking ties uniformly at random", pushing its site number.
  Others: count atoms of a type in a radius, pick a random empty site,
  swap two sites, apply a symmetry (rotate or flip site numbers).
- **Types and fields:** how a type names its own data fields (bit
  ranges in the P4Atom storage) and how getf/setf address them; the
  type field itself (creating atoms of a new type, with parity).
- **Speed:** copy the running program into fast RAM; threaded dispatch;
  superinstructions (e.g. `getf` on site 0); measure where the 50
  cycles go.
- **Where programs come from:** compiled in today; later per type, loaded
  from the host into EWP L1.
- **A verifier** at load time, for early error reports only (the run-time
  checks stay).
- **Host-side bytecode** to pick display colors per type.
- **Stack sizes:** 16 and 8 are guesses; real programs will tell.
- **Versioning:** a program header with a version and the type it is
  for.


## 12. Change log

- **Version 0 (2026-11-26):** the spike.  23 opcodes, 4 fields, 9
  faults; FB1 and FB4 written by hand; proven identical to native on the
  host; timed on the QB (section 10).
