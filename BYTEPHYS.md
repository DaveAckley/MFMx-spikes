# BYTEPHYS -- reference manual for the physics bytecode (version 2 + ulam-lite, SPIKE)

A small, growing reference for BytePhys, the bytecode that MFMx physics
can be written in and run on the EWPs.  Version 0 is a spike ("pretend
bytecode"): just enough machine to run FB1 and FB4 and measure what
interpreting costs on a T6 baby core.  Version 1 adds declared element
and quark layouts, ulam-style (section 5), and saturating arithmetic;
version 2 adds local variables and calls with arguments (section 2).
FB1 and FB4 are now written in ulam-lite, a small ulam-like language
that compiles to bpasm (section 13).
This manual is meant to be fleshed out as the design grows; section
11 collects what is not decided yet.

Where things live:

| What | Where |
|------|-------|
| The machine (opcodes, fields, layouts, faults, interpreter) | srcs/shared/include/BytePhys.h |
| ulam-lite sources (FB1, FB4, quark Aging) | scripts/ulamlite/*.ulam |
| ulam-lite compiler (.ulam -> .bp) | scripts/ulamlite/ulamlite.py |
| bpasm programs and declarations (the .ulam ones GENERATED) | scripts/bytephys/*.bp |
| The hand-written version 1 programs (reference, not built) | scripts/bytephys/hand/*.bp |
| Assembler | scripts/bytephys/bpasm.py |
| Assembled programs and layout tables (GENERATED, do not edit) | srcs/shared/include/BytePrograms.h |
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
- **Call stack:** 8 entries (`Machine::CALL_SIZE`), each the return
  address and the caller's frame.  Bytecode may call and recurse freely
  within that depth; the interpreter itself is one loop and never
  recurses natively.
- **Locals:** 32 u32 slots shared by all frames (`Machine::LOCALS_SIZE`).
  `enter nargs nlocals` opens a frame of nlocals slots on top of the
  caller's, moves the top nargs operands into slots 0..nargs-1 (deepest
  operand first) and zeroes the rest.  `loadl`/`storel n` reach slot n
  of the current frame only.  `ret` restores the caller's frame, so a
  callee's locals vanish with it.  Results come back on the operand
  stack; a function may return any number of them.

  ```
  func sum n              ; sum(n) = n + sum(n-1), sum(0) = 0
          loadl n
          jnz rec
          push 0
          ret
  rec:    loadl n
          push 1
          sub
          call sum        ; ( sum(n-1) )
          loadl n         ; this frame's n, intact after the call
          add
          ret
  ```

- **Program counter:** a byte offset into the program, starting at 0.
- **The window:** 41 sites, numbered as everywhere in MFM (MDist.h):
  sorted by squared distance from the center, site 0 the center, 1-4 the
  four nearest, 5-8 the four diagonals, out to 40 at Manhattan distance
  4.  Instructions name a site by its number, taken from the stack.
- **Fuel:** every instruction executed costs 1; the EWP gives each event
  `BP_FUEL` = 10,000.  Running out is a fault, so an infinite loop ends
  the event instead of the tile.
- **Size:** the Machine object is 228 bytes (84 in version 1) and lives
  in hart B's fast RAM beside the event window (`Physics::mVM`).

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
| 08 | add | | ( a b -- a+b ) | saturates at 2^32-1 |
| 09 | sub | | ( a b -- a-b ) | saturates at 0 |
| 0a | eq | | ( a b -- a==b ) | 1 or 0 |
| 0b | lt | | ( a b -- a<b ) | unsigned; 1 or 0 |
| 0c | jmp | u16 target | ( -- ) | |
| 0d | jz | u16 target | ( x -- ) | jump if x == 0 |
| 0e | jnz | u16 target | ( x -- ) | jump if x != 0 |
| 0f | call | u16 target | ( -- ) | saves the return offset and the caller's frame |
| 10 | ret | | ( -- ) | restores them |
| 11 | getf | u8 field | ( sn -- value ) | read a RAW field of atom sn (section 5.3) |
| 12 | setf | u8 field | ( sn value -- ) | write a RAW field of atom sn |
| 13 | copy | | ( src dst -- ) | atom dst = atom src (whole atom) |
| 14 | empty | | ( sn -- ) | atom sn = the empty atom |
| 15 | between | | ( lo hi -- r ) | uniform random in [lo, hi] |
| 16 | onein | | ( n -- flag ) | 1 with probability 1/n, else 0 |
| 17 | getm | u16 member | ( sn -- value ) | read a declared member of atom sn (section 5) |
| 18 | setm | u16 member | ( sn value -- ) | write it, saturated to the member's width |
| 19 | enter | u8 nargs, u8 nlocals | ( a1 .. an -- ) | open a frame; args to slots 0..n-1 |
| 1a | loadl | u8 slot | ( -- v ) | push local slot |
| 1b | storel | u8 slot | ( v -- ) | pop into local slot |

Notes:
- Every jump, call and return target is checked against the program
  length; every site number against 41; every member against the
  atom's type.
- **Arithmetic saturates** (version 1): add and sub clamp instead of
  wrapping, and setm clamps to the member's declared width (a 4-bit
  member given 200 stores 15).  eq and lt are unchanged.
- The random draws (between, onein) use the EWP's PRNG; the host test
  replays the same stream through native and bytecode code and checks
  the draws come in the same order.


## 5. Atom data: declared members (getm / setm)

### 5.1 State bits

A P4Atom (12 bytes, locked for this round) has a 16-bit type-and-parity
word and 80 **state bits**, numbered for members as:

| State bits | Storage |
|------------|---------|
| 0..31 | `P4Atom::mStg[0]` |
| 32..63 | `P4Atom::mStg[1]` |
| 64..79 | `P4Atom::mData0` |

A member may straddle a storage boundary (e.g. bits 28..35).  The type
is never a member: atoms change type only by `copy` and `empty`.

### 5.2 Elements, quarks and members

Declared in .bp source (section 7), in the spirit of ulam: an
**element** is an atom type with named members; a **quark** is a named
bundle of members that elements (and other quarks) can include.
Members pack in declaration order from state bit 0.

```
quark Aging                 ; scripts/bytephys/hand/aging.bp
  unsigned fuse 32          ; bits 0..31 of whoever holds it
  unsigned age 32           ; bits 32..63

element BFB4 8              ; atom type 8
  Aging a                   ; a.fuse = bits 0..31, a.age = bits 32..63
```

Member types today: `unsigned N` (1..32 bits), `bool` (1 bit), and a
quark by name.  An element must fit in 80 bits.

getm/setm name a member by **path**:

- `BFB4.a.age`: that element only.  The site must hold a BFB4.
- `Aging.age`: through the quark.  The site may hold ANY element that
  includes an Aging (once), wherever that element keeps it.

The assembler gives each distinct path a u16 **member id** and writes
one Layout table for all programs: per id, the list of (element type,
first bit, width) where it lives.  At run time getm/setm look up the
site's type in that list; no entry is a fault (BF_TYPE), so reading a
member of the wrong kind of atom can never silently read other bits.
An element path's list has one entry; a quark path's has one per
element holding the quark (scanned in order, so a little slower).

`bpasm.py --list` prints every element's bit map and every member id.

### 5.3 Raw fields (getf / setf)

The version 0 access, still there for odd jobs: getf/setf name a whole
storage word, untyped and unchecked against the atom's type.

| Number | Name | Meaning | Writable |
|--------|------|---------|----------|
| 0 | type | the atom's type | NO (its parity would need redoing; use copy or empty) |
| 1 | data0 | `P4Atom::mData0`, u16 | yes (low 16 bits stored) |
| 2 | stg0 | `P4Atom::mStg[0]`, u32 | yes |
| 3 | stg1 | `P4Atom::mStg[1]`, u32 | yes |

`getf type` is how a program asks what is at a site before touching its
members.


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
| 10 | BF_MEMBER | getm/setm member id not in the Layout (or no Layout) |
| 11 | BF_TYPE | getm/setm on a site whose type does not have that member |
| 12 | BF_FRAME | enter: nargs > nlocals, or not enough free local slots |
| 13 | BF_LOCAL | loadl/storel slot outside the current frame (or no frame) |

No checking happens before a program runs.  A verifier that rejects bad
programs at load time would only report problems earlier; the run-time
checks are what keep the tile safe.


## 7. Assembler (.bp files)

`scripts/bytephys/bpasm.py` assembles every `*.bp` in its directory into
BytePrograms.h, one `BP_PROG_<NAME>` per file (bfb1.bp -> BP_PROG_BFB1),
plus `BP_TYPE_<ELEMENT>` for each element and the shared `BP_LAYOUT`.
A file holding only declarations (aging.bp) makes no program.
Declarations are global: every file sees every other file's.

Syntax:
- One instruction per line; `;` starts a comment.
- `name:` defines a label (may share a line with an instruction).
- Mnemonics are the opcode names in lower case, without `OP_`.
- `push N` takes decimal or 0x hex and picks push8 or push32.
- `jmp jz jnz call` take a label.
- `getf setf` take a raw field name: type, data0, stg0, stg1.
- `getm setm` take a member path: `ELEMENT.member[.member...]` or
  `QUARK.member[...]`.
- `func NAME ARG... [| LOCAL...]` defines label NAME and assembles to
  `enter nargs nlocals`; until the next func, the names stand for slots
  0, 1, ... in loadl/storel (numbers work too).  `enter N M` is the raw
  form.  The program starts at offset 0, so its first func is the
  entry; a program with no locals needs no enter.
- `quark NAME` and `element NAME TYPE` start a declaration; the member
  lines after it (`unsigned NAME BITS`, `bool NAME`, `QUARKNAME NAME`)
  belong to it, and the first other line ends it.
- Errors: unknown names, a member declared twice, an element over 80
  bits, a quark that contains itself, two elements with one type number,
  a quark path no element holds, or an element holding the quark twice
  (name the member through the element instead).

Commands (from the repo root):
- `scripts/bytephys/bpasm.py`: regenerate BytePrograms.h.
- `scripts/bytephys/bpasm.py --check`: exit 1 if BytePrograms.h is out of
  date (run_native_tests.sh runs this).
- `scripts/bytephys/bpasm.py --list scripts/bytephys/bfb4.bp`: listing with
  offsets and bytes, then the element bit maps and member ids.

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

Hand-written bytecode (scripts/bytephys/hand/fb4.bp, the version 1
program; section 13 shows what the compiler makes of the same FB4),
with its listing (member 2 is
`Aging.age`):

```
element BFB4 8
  Aging a

   0: 02 00     push 0         ; ( 0 )         site 0, for setm
   2: 02 00     push 0         ; ( 0 0 )       site 0, for getm
   4: 17 02 00  getm Aging.age ; ( 0 age )
   7: 02 01     push 1         ; ( 0 age 1 )
   9: 08        add            ; ( 0 age+1 )   saturating
  10: 18 02 00  setm Aging.age ; ( )           ca.age++
  13: 02 01     push 1         ; ( sn )        sn = 1
  15: 02 00 loop: push 0       ; ( sn 0 )
  17: 07        over           ; ( sn 0 sn )
  18: 13        copy           ; ( sn )        atom sn = atom 0
  19: 02 01     push 1
  21: 08        add            ; ( sn+1 )
  22: 04        dup
  23: 02 29     push 41
  25: 0b        lt             ; ( sn+1 flag )
  26: 0e 0f 00  jnz loop       ; ( sn+1 )      while sn+1 < 41
  29: 05        drop           ; ( )
  30: 01        halt1          ;               commit
```

31 bytes; 369 instructions per event (9 per neighbor).  FB1 (hand/fb1.bp,
139 bytes) shows branches, random draws, the kabam loop, and members
named through the element (`BFB1.a.fuse`).

Native wraps `ca.mStg[1]++` at 2^32; bytecode saturates there.  The host
test checks the two are identical away from that limit and checks the
saturation on its own.


## 9. On the EWP

- **Bytecode physics types:** type 7 runs BFB1, type 8 runs BFB4, both
  compiled from scripts/ulamlite/ (section 13) and found through the
  generated `programFor(type)` table
  (`PHY_BFB1`, `PHY_BFB4` in Physics.cpp; static_asserts tie them to the
  types the .bp files declare).  They behave like native types 4 and 5
  (except that counters saturate).  Seed them with e.g. `seed 25 7`.
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
These are version 0 numbers (getf/setf).  Version 1 (getm/setm, hand
written, run 20261127-011502, same mix): FB1 2955 cycles/event (native
529, 5.59x, 29.9 insns), FB4 23514 (native 3311, 7.10x, 369 insns).
So version 1 cost FB1 +31% and FB4 +7% for the same instruction
counts; this run cannot say how much is the getm/setm accesses (table
scan, 64-bit shifts) and how much a general slowdown of the bigger
interpreter loop.  No faults.

Compiled code + one-word fast path (run 20261127-015533, 35f19e5,
events shortcut): FB1 3167 cycles/event (native 480, 6.60x, 30.7
insns), FB4 33853 (native 3314, 10.22x, 494 insns).  Bytecode minus
native, per bytecode instruction, FB4: v0 50, v1 55, now 62 cycles; FB1:
v0 59, v1 83, now 88.  So the fast path bought nothing measurable: the
cost is not the u64 work but the interpreter itself, and it grows as the
interpreter grows (more opcodes, frames), even for instructions that
did not change.  The compiled code's 34% more instructions is the rest.
Not yet known where ~60 cycles per instruction go (instruction fetch
for a bigger loop? the switch? Machine state in memory instead of
registers?); a per-opcode microbenchmark on the QB would say.


## 11. Open questions and wish list (to be fleshed out)

- **Rich primitives**, where the real win is: one instruction doing a
  native loop's work.  First candidate (Liv): "find a closest atom of
  type X, breaking ties uniformly at random", pushing its site number.
  Others: count atoms of a type in a radius, pick a random empty site,
  swap two sites, apply a symmetry (rotate or flip site numbers).
- **Types and fields** (version 1 started this, section 5): signed
  `int N` members (saturating needs signed arithmetic ops); ulam's
  voting multi-bit Bool; making atoms of a declared type (`new BFB1`,
  with parity); an `is` test that pushes a flag instead of faulting;
  element -> program dispatch from the declarations (Physics.cpp
  still switches on 7 and 8 by hand); the host reading members through
  the same Layout (census, colors).
- **Speed:** copy the running program into fast RAM; threaded dispatch;
  superinstructions (e.g. `getf` on site 0); measure where the 50
  cycles go.
- **Where programs come from:** compiled in today; later per type, loaded
  from the host into EWP L1.
- **A verifier** at load time, for early error reports only (the run-time
  checks stay).
- **Host-side bytecode** to pick display colors per type.
- **Stack sizes:** 16 operands, 8 calls and 32 locals are guesses; real
  programs (and a compiler) will tell.
- **Versioning:** a program header with a version and the type it is
  for.


## 12. Change log

- **Loop optimization (2026-11-27):** rotated loops, for counters on the
  operand stack (with fallback to a slot), one-jump `if (c) break;` /
  `continue;`, constant conditions folded.  FB4 494 -> 374 instructions
  per event, FB1 141 -> 113 (test mix).  Not yet run on the QB.

- **ulam-lite tests (2026-11-27):** a test-only physics
  (scripts/ulamlite/tests, BytePhysTests.h) runs every compiler feature
  FB1/FB4 do not; it found one bug, now fixed: a local declared without
  an initializer inside a loop kept its last value instead of starting
  at 0 again.

- **Type numbers and fast path (2026-11-27):** ulamlite.py assigns
  element type numbers (from 7, by name; no more `= N`); bpasm emits a
  type -> program table and Physics.cpp dispatches every compiled type
  through it.  getBits/setBits take a one-word fast path (no u64) when
  a member sits inside one 32-bit word.  Not yet run on the QB.

- **ulam-lite milestone 1 (2026-11-27):** scripts/ulamlite/ulamlite.py
  compiles ulam-like source to bpasm (section 13).  FB1 and FB4 now
  come from bfb1.ulam / bfb4.ulam / aging.ulam (programs renamed
  BP_PROG_BFB1 / BP_PROG_BFB4); the hand-written .bp files moved to
  scripts/bytephys/hand/.  Host test: compiled code identical to native
  for 18,000 windows.  Cost: FB4 494 instructions per event (hand 369),
  FB1 mean 141 in the test mix (hand 112).  Not yet run on the QB.

- **Version 2 (2026-11-27):** frames.  Local slots (32, shared by all
  frames), `enter`/`loadl`/`storel` (opcodes 19-1b), call entries that
  save the caller's frame, faults 12-13; bpasm `func NAME args | locals`
  with named slots.  Host test: recursion that reads its own local after
  the recursive call, argument order, frame faults.  FB1/FB4 unchanged
  (no locals needed).  Not yet run on the QB.

- **Version 1 (2026-11-27):** declared layouts.  `quark` and `element`
  declarations in .bp source; getm/setm (opcodes 17, 18) by member id
  through a generated Layout, checked against the site's type (faults
  10, 11); add, sub and setm saturate.  FB1 and FB4 rewritten with an
  Aging quark (fuse, age) laid out like the native types' storage.
  Host test: identical to native away from 2^32, saturation, getBits /
  setBits against a bit model at every position and width.  Not yet
  run on the QB.

- **Version 0 (2026-11-26):** the spike.  23 opcodes, 4 fields, 9
  faults; FB1 and FB4 written by hand; proven identical to native on the
  host; timed on the QB (section 10).


## 13. ulam-lite: compiling ulam-like source to bpasm

`scripts/ulamlite/ulamlite.py` compiles every `scripts/ulamlite/*.ulam`
into GENERATED .bp files in scripts/bytephys (one per quark, one per
element, named after it in lower case), which bpasm then assembles.
Never edit a generated .bp (its first line says so); edit the .ulam and
rerun ulamlite.py, then bpasm.py.  `ulamlite.py --check` (in
run_native_tests.sh as ulamlite_check) fails if a generated .bp is
stale; `ulamlite.py --print FILE.ulam` shows the bpasm for one file.

The language is a small subset in ulam's spirit, grown as needed:

```
quark Aging {                       // aging.ulam
  Unsigned(32) fuse;
  Unsigned(32) age;
}

element BFB4 {                      // bfb4.ulam (type number assigned)
  Aging a;
  Void behave() {
    a.age++;                        // self's member (= ew[0]'s)
    for (Unsigned sn = 1; sn < 41; sn++)
      ew[sn] = self;                // MAX FB!
  }
}
```

**Declarations:** `quark NAME { members }`; `element NAME {
constants, members, methods }`.  Members are `Unsigned(n)` (1..32),
`Unsigned` (32), `Bool` (1 bit) or a quark; they pack as in section 5.
`constant Unsigned cNAME = 21;` is inlined.

**Type numbers:** as in culam, the compiler assigns them, which is why
a whole physics compiles at once (every `is TYPE` and every table then
agrees).  Elements are numbered from 7 (`FIRST_TYPE`, above the native
types 0..6) in order of name: BFB1 is 7, BFB4 is 8.  Adding an element
can renumber the ones after it.  bpasm turns the numbers into a table,
`BP_FIRST_TYPE` / `BP_PROGRAMS[]` / `programFor(type)` in
BytePrograms.h, and the EWP runs any type it finds there (Physics.cpp's
default case).  Seeding and timing are still by number (`seed 3 8`,
the `.pt` slots), so Physics.cpp static_asserts that BFB1 and BFB4 kept
7 and 8; a renumbering fails the build with a message saying so.

**Methods:** `Void behave()` is the event (entry, offset 0); returning
from it commits the window (halt1).  Other methods take Unsigned(n) /
Bool parameters and return Void, Unsigned(n) or Bool; they compile to
`func`-style frames (enter, loadl/storel) and `call`/`ret`.

**Statements:** blocks; locals (`Unsigned x = e;`, `Unsigned(4) n;`,
`Bool b;` -- zero if not initialized); `=`, `+=`, `-=`, `x++`, `x--` on
locals and self's members; `ew[e] = self;`, `ew[e] = ew[f];`,
`ew[e] = Empty;`; if/else, while, for, break, continue, return; calls.

**Expressions:** unsigned literals (decimal, 0x), true/false,
constants, locals, self's members (`a.age`), `+ -` (saturating),
`== != < <= > >=`, `&& || !` (short-circuit, so random draws happen
only when reached), method calls, `random.between(lo, hi)`,
`random.oneIn(n)`, `ew[e] is TYPE` (`is Empty` for empty sites).

**Semantics:** every value is a u32 on the operand stack.  Storing into
an Unsigned(n) member or local clamps to n bits (members by setm,
locals and arguments by compiled code), Bool stores are 0 or 1.
`ew[dst] = src` evaluates src first (C++17 order), so draw order
matches native C++.

**Not yet:** other atoms' members (`ew[i]` as an element, i.e. ulam's
casts and references); quark methods; signed Int(n); multiplication,
division, bit ops; arrays; making atoms of a declared type; inheritance,
templates, virtual methods.

**Testing:** FB1 and FB4 are checked against native (bytephys_test).
Everything else is checked by a separate TEST-ONLY physics,
`scripts/ulamlite/tests/*.ulam` (TMethods, TLoops, TLocals, TWindow,
TFallback: every way a stack counter falls back to a slot, and returns
from inside stack-counter loops),
compiled with its own type numbers into `scripts/bytephys/tests/*.bp`
and assembled into `srcs/host/tools/BytePhysTests.h` (namespace
`BytePhys::Tests`), which only bytephys_test includes, so none of it
reaches the EWP.  Each test element computes into its own members and
the C++ test compares them with hand-computed values: value methods,
recursion, narrow and Bool parameters, returns and locals, while /
break / continue (inner loop only), `|| && !`, comparisons as values,
shadowing, a local re-zeroed on every pass of a loop, `is`, site
copies, and that short-circuited random draws are never drawn.
ulamlite.py and bpasm.py always build both physics; their `--check`s
cover both.

**Code quality:** close to hand-written.  The compiler tracks the
operand stack depth through every instruction it emits, and:
- **rotates loops**: the test sits at the bottom, so a pass costs one
  conditional jump instead of a test, a jump out and a jump back;
- **keeps simple for counters on the operand stack**: a `for` whose
  init declares an `Unsigned` counter and whose step only changes it
  keeps it on the stack, read with `dup` / `over`, dropped at the end
  (and before any `return` inside the loop).  If the body assigns the
  counter, declares a local of the same name, or reads it deeper than
  `over` reaches (e.g. an inner loop reading an outer counter), that
  loop is compiled again with the counter in a local slot;
- turns `if (c) break;` / `if (c) continue;` into one conditional jump,
  and drops tests of constant conditions (`while (true)`).

FB4 now runs 9 instructions per neighbor, 374 per event (hand-written
369; before these optimizations 494).  FB1 averages 113 in the test mix
(hand 112, before 141).
