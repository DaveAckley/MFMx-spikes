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
| How to add an opcode | section 4.1 |
| ulam-lite sources (FB1, FB4, quark Aging) | scripts/ulamlite/*.ulam |
| ulam-lite compiler (.ulam -> .bp) | scripts/ulamlite/ulamlite.py |
| bpasm programs and declarations (the .ulam ones GENERATED) | scripts/bytephys/*.bp |
| The hand-written version 1 programs (reference, not built) | scripts/bytephys/hand/*.bp |
| Assembler | scripts/bytephys/bpasm.py |
| Assembled programs and layout tables (GENERATED, do not edit) | srcs/shared/include/BytePrograms.h |
| Native twins of the programs | srcs/shared/include/NativeFB.h |
| EWP glue (types 7, 8; timing marks) | srcs/cross/images/ewp/src/Physics.cpp |
| Host test (bytecode == native, faults) | srcs/host/tools/bytephys_test.cpp |
| Results per step | sections 10 and 12 here; CLOSED_ISSUES.md 'BytePhys spike' |
| Open items, next work | OPEN_ISSUES.md |

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
- **Size:** the Machine object is 360 bytes on the EWP (228 before the
  dispatch table copy, 84 in version 1) and lives in hart B's fast RAM
  beside the event window (`Physics::mVM`).  132 of those bytes are the
  copy of run()'s dispatch table (`mOps`, 32 entries: the 28 handlers
  and 4 BF_OPCODE fillers, so a masked opcode never jumps wild) and the
  pointer it came from (`mOpsFrom`), filled by the first run() (EWP
  fast RAM is zero at reset; nothing calls initB, section 12).  Beside it, the code cache
  (`Physics::mCodeCache`, ByteCache.h, ~1.1 KB).

An event runs one program from offset 0 until HALT0 or HALT1:

- **HALT1:** commit the window (the event happened).
- **HALT0:** physics declined; the window is discarded.

Everything else ends the event with a **fault** (section 6).


## 3. Encoding

One opcode byte, then its immediates, little-endian.  Binary
operations pop b, then a, and push (a op b), so `push 7 push 2 sub`
leaves 5.

**Images, functions, chunks** (since 2026-11-27).  All the code of one
set of .bp files (one physics) is one Image (BytePhys.h `Image`): its
FUNCTIONS (each program's entry, every call target, every `func`) cut
into CHUNKS of at most 256 bytes (CHUNK_MAX), stored back to back, with
a chunk table and a function table (function -> first chunk).  The
chunk is the unit the fast-RAM code cache holds, so nothing inside one
depends on where it sits: jmp/jz/jnz take a u8 offset WITHIN their
chunk; xjmp/xjz/xjnz (u16 chunk, u8 offset) reach another chunk (bpasm
uses them for a jump to a label in another chunk, and appends an xjmp
where a chunk would fall through into the next); call takes a u16
FUNCTION index and saves (chunk, return offset, frame), and ret goes
back through the cache, since the callee may have evicted the caller.
A Program is a handle: (image, entry function).  Until 2026-11-27 each
element was its own program with absolute u16 targets.

**BCP blobs** (since 2026-11-28; "BCP" = bytecode physics, the name is
provisional).  An image is stored as ONE pointer-free blob, so a physics
can be chosen at start time instead of compiled in, the analog of MFMs
loading a .so: `--physics NAME` or the `physics` command writes a .bcp
file after the EWP image in L1 (section 9 'Loadable physics').  Layout
(BytePhys.h 'BCP'; little-endian; every table 4-aligned and found by its
byte offset from the blob's start):

| bytes | field |
|---|---|
| 0 | magic 'BCP1' |
| 4, 6 | u16 BCP_VERSION (1), u16 OP_COUNT |
| 8 | u32 total size, a multiple of 4 |
| 12 | u32 checksum: FNV-1a over the blob, these 4 bytes read as 0 |
| 16..27 | u16 counts: chunks, functions, member refs, member ats, first type, types |
| 28..51 | u32 offsets: code, chunk starts (u16 x chunks+1), function chunks (u16 x functions), MemberRefs, MemberAts (4 bytes each), type -> entry function (u16 x types, 0xffff = none) |
| 52..63 | reserved, 0 |
| 64.. | the code, then the tables |

`verifyBCP(bytes, size)` checks the header (BCP_BAD_SIZE, _MAGIC,
_VERSION, _CHECKSUM, _TABLE, _LAYOUT, _TYPES) and then the code exactly
as before (reporting the Fault a bad instruction would cause); it reads
bytes only, so it is constexpr and every generated header
static_asserts it.  `openBCP(bytes, room, view)` runs it again and fills
a `BCPView` (plain pointers into the blob) for run() and the code cache;
the EWP opens its blobs at the first event that needs one
(Physics::openImages; nothing calls initB; a failure marks `BCPBAD` and
runs no bytecode; `.bv:TRIED:OPEN:BENCH:TYPES:LOADED:SUM.` with the
100k-event marks).  `entryFor(view, type)` replaces
the old generated programFor table.

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
| 0c | jmp | u8 offset | ( -- ) | in this chunk |
| 0d | jz | u8 offset | ( x -- ) | jump if x == 0, in this chunk |
| 0e | jnz | u8 offset | ( x -- ) | jump if x != 0, in this chunk |
| 0f | call | u16 function | ( -- ) | saves (chunk, return offset) and the caller's frame |
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
| 1c | xjmp | u16 chunk, u8 offset | ( -- ) | to another chunk |
| 1d | xjz | u16 chunk, u8 offset | ( x -- ) | to another chunk if x == 0 |
| 1e | xjnz | u16 chunk, u8 offset | ( x -- ) | to another chunk if x != 0 |
| 1f | new | u16 type | ( sn -- ) | atom sn = a new atom of that type (verify refuses INACCESSIBLE); `new NAME` or `new N` |

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

### 4.1 Adding an opcode

The steps, in order (worked out adding `new`, 2026-11-28; ffa15bf is
a complete example to diff against).  BytePhys.h's `enum Op` points
here.

**First, the slot limit.**  The dispatch table has `OPS_SLOTS` = 32
entries, indexed by the opcode byte masked to 5 bits, and since `new`
all 32 are opcodes.  So the next opcode needs one of:
- 64 slots: `OPS_SLOTS = 64`.  The Machine's copy of the table (mOps,
  hart B fast RAM) grows by 128 bytes, and the mask widens to 6 bits.
  Entries 32..63 get L_BADOP, so a masked corrupt byte can fault
  BF_OPCODE again.
- an escape opcode whose next byte selects one of a second table:
  fast RAM stays the same, but each instruction it covers costs an
  extra byte and dispatch.

Then:

1. **srcs/shared/include/BytePhys.h**
   - Append the name to `enum Op`, just before `OP_COUNT`.  The numbers
     are the ISA: never insert or reorder.
   - Add `&&L_NAME` at the same position in `kOps` (in run()); a
     static_assert checks the count.
   - Write the `L_NAME:` handler in run(): `BP_NEED(n)` before popping,
     `env.fault(BF_...)` for run-time checks, end with `BP_NEXT`.
     Immediates: read with `BP_IMM16` / `code[pc++]`, then advance pc.
   - Immediates: add it to `immBytes()`.
   - Anything about an immediate that can be checked once goes into
     verifyBCP's instruction loop (as `new`'s type), not the handler.
   - A new `Fault` only if no existing one fits (append; section 6).
2. **scripts/bytephys/bpasm.py**: the opcode numbers are read from the
   enum, so a plain opcode needs nothing.  One with immediates needs a
   case in `insn_size()` and in `encode()`.
3. **scripts/ulamlite/ulamlite.py**: its operand stack effect in
   `EFFECT`, and whatever syntax emits it (for `new`: `ew[n] =
   ELEMENT;` in s_assign).
4. **scripts/bytephys/benchgen.py** (optional): a timing pattern in
   the list, if one can run in a loop without wrecking the window
   (`empty` and `new` have none).
5. **Regenerate**, in order: `scripts/ulamlite/ulamlite.py`,
   `scripts/bytephys/benchgen.py`, `scripts/bytephys/bpasm.py`.  This
   rewrites every generated header and every physics/*.bcp.  The blob
   header records OP_COUNT, so every old blob is refused from now on.
6. **srcs/host/tools/bytephys_test.cpp**: a run test, a fault test for
   each run-time check (`faultOf`), a verify test for each immediate
   check (`RawImage(...).bad`); and recheck the dispatch-mask tests in
   testCodeCache, whose static_assert fails when the slot count
   changes.
7. **BYTEPHYS.md**: a row in the section 4 table, an entry in section
   12; section 13 if ulam-lite got syntax for it.
8. **Check**: `srcs/host/tools/run_native_tests.sh` (includes `bpasm
   --check` and `ulamlite --check`); a cross build (`__fastram_b_end`
   must leave 2 KB of hart B stack; _BUD.ld.in fails the link if not).
   On the QB it needs `build_all.sh` (shared headers changed), and
   expected checksums in experiment.cmd change with the blobs.


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

Since 2026-11-27 (VERIFY ONCE, section 12) the verifier (verifyBCP
since 2026-11-28) checks an image before it runs (at compile time for
the generated ones, and again when a blob is opened) and run() trusts
it; the run-time checks that remain are the ones only the run
can decide.


## 7. Assembler (.bp files)

`scripts/bytephys/bpasm.py` assembles every `*.bp` in its directory into
ONE image in BytePrograms.h, stored as the BCP blob `BP_BCP` (section
3; static_assert(verifyBCP(BP_BCP, sizeof(BP_BCP)) == 0)), one
`BP_FUNC_<NAME>` (its entry function) per file (bfb1.bp ->
BP_FUNC_BFB1), `BP_PROG_FUNCS` / `BP_PROG_NAMES` in source order, and
`BP_TYPE_<ELEMENT>`, `BP_FIRST_TYPE`, `BP_TYPE_COUNT`; the layout and
the type table live inside the blob.  `bpasm.py --list [tests|bench]`
prints a set's listing.  A file's functions are its first instruction, each call
target and each `func`; each runs to the next.  bpasm cuts a function
into chunks of at most 256 bytes, preferring a cut at a label, and picks
the short jump form when the target is in the same chunk.
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
- `scripts/bytephys/bpasm.py --list [tests|bench]`: the whole image's
  listing, chunk by chunk, with offsets and bytes (the physics set by
  default).

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
  BCP's type table (`entryFor(mBCP, type)`)
  (`PHY_BFB1`, `PHY_BFB4` in Physics.cpp; static_asserts tie them to the
  types the .bp files declare).  They behave like native types 4 and 5
  (except that counters saturate).  Seed them with e.g. `seed 25 7`.
- **Loadable physics (2026-11-28):** the physics blob may come from the
  host instead of the image.  Images whose layout config says `physics
  = true` ([image.EWP] in config/nu1012.dtoml) get, at 'start', the
  chosen .bcp file (mfmxctl `--physics NAME`, or `physics NAME` in a
  .cmd before `start`; NAME = srcs/host/python/EWD/physics/NAME.bcp or
  a path) or, with none chosen, four zero words.  The host verifies the
  file with verifyBCP and checks it fits (T6Image::attachPhysics; a bad
  file stops the run before any code is sent), then writes it at the
  end of the bin file rounded up to 16; the bin file and HostBlock are
  unchanged.  The EWP looks at `__hostblock_end` rounded up to 16 (room
  up to `__l1_image_limit`, 0x120000): a nonzero magic is a loaded blob,
  opened (verified again) once; zero means the compiled-in BP_BCP.
  `.bv` LOADED = 1 and SUM = the blob's checksum say which ran.  The
  bench stays compiled in.  bpasm.py writes physics/bfb.bcp (the same
  bytes as BP_BCP).  Details: physics/README.md there.
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

Loop optimization (run 20261127-094705, 7e10820, events shortcut): FB4
24788 cycles/event (native 3266, 7.59x, 374 insns), FB1 3013 (native
473, 6.37x, 30.0 insns).  Per instruction FB4 58, FB1 85 cycles.

**Microbenchmark (2026-11-27, run 20261127-110936).**  `seed N 31` puts down
Bench atoms (type 31, PHY_BENCH).  Each event on one runs the next of
26 programs written by scripts/bytephys/benchgen.py (bench/*.bp ->
srcs/shared/include/ByteBench.h, BytePhys::Bench): open a frame, push
two dummies, then 50 passes of a body of 8 copies of one opcode
pattern, counter on the operand stack.  `base` has an empty body, so
(cycles - base) / (insns - base insns) is the cost of one pattern
instruction.  Each program runs twice per event: from its array in L1
(as real physics runs) and copied into a stack buffer (HB fast RAM).
The base event also times 64 native byte loads from each place.  After
4 rounds the atom erases itself.  Marks `.pb:NAME:V:TICKS:INSNS.` (V =
L, F, or N for a probe); runsum.py's 'bytecode microbenchmark' section
tabulates them.  bytephys_test checks every bench program commits,
touches only the center's state, and runs base + whole pattern copies.

RESULT (about 62 runs per program and place; cycles per instruction,
code in L1 / in fast RAM): the cheapest ops cost 42-48 / 36-40 (swap,
over, dup, push8), jumps and pushes ~55 / ~45, so DISPATCH is ~40 of
them, and fetching code from L1 instead of fast RAM adds ~4 per code
byte (~10 per instruction).  The expensive ops, net of their pushes
and drops: getm ~175, setm ~215 (~275 straddling), copy ~150, between
and onein ~100, enter ~85, getf ~70.  Native byte loads: L1 6.6,
fast RAM 5.3 cycles (loop included).  Why dispatch is slow (Physics.cpp
built -Os, rv32 riscv32-tt-elf-g++): run() spills the fuel counter,
its limit and both jump-table addresses to the stack, so each dispatch
does 4 stack loads + 1 store, the pc < len and fuel checks, a table
load and `jr`; every handler then takes two more jumps (to a shared
`mv s1,s11`, then to the loop head): ~28 RV instructions, 4 of them
taken jumps, for a DUP.  For FB4 (374 insns): 40 copies ~6,000 cycles,
the other ~334 instructions ~17,000.

**New dispatch timed (run 20261127-141156, 6b7842d, events
shortcut; section 12 'Dispatch rework').**  About 50 runs per bench
program.  Cycles per instruction, code in L1 / in fast RAM (old run
110936 in parentheses, L1):

    swap 27.2 / 21.9 (41.9)    over 26.3 / 21.0 (45.5)
    dup 25.3 / 20.0 (47.0)     push8 27.3 / 19.9 (48.7)
    jmp 35.6 / 23.5 (55.2)     jz 31.6 / 23.9 (61.1)
    jnz 36.2 / 25.4 (60.4)     push32 32.8 / 24.4 (56.6)
    add 33.5 / 25.9 (54.1)     loadl 30.3 / 21.9 (53.3)
    storel 34.0 / 25.5 (57.7)  call 35.5 / 27.4 (58.6)
    enter 49.2 / 40.0 (67.8)   getf 33.2 / 25.4 (55.1)
    setf 41.0 / 30.9 (58.8)    getm1 57.1 / 47.0 (90.9)
    getm2 58.1 / 48.1 (106.0)  setm1 69.4 / 57.5 (105.5)
    setm2 73.7 / 61.8 (125.8)  copy 41.3 / 32.0 (84.5)
    between 46.7 / 39.2 (63.1) onein 41.7 / 35.1 (64.0)

So the cheapest instruction is ~25 cycles in L1 (was 42-48), ~20 in
fast RAM: the rework took ~20 cycles off every dispatch.  Net of their
pushes and drops: getm ~120 (was ~175), setm ~155-165 (~215-275),
copy ~70 (~150), between ~105, onein ~75.  Taken jumps cost ~10 more
in L1 than in fast RAM (the fetch restarts at the target).  Native
byte loads unchanged (L1 6.8, fast 5.3).  Real physics: FB1 bytecode
1975 cycles/event vs native 459 (4.30x; was 3013, 6.37x), FB4 14388 vs
3270 (4.40x; was 24788, 7.59x), i.e. 38.5 cycles per FB4 instruction
(was 66), and FB4 is now faster than the hand-written v0 (21886).
No `.pf`, all 8 seed batches grew, FB4 share 0.35 (native ahead, as
expected at 4.4x).  What is left of a dispatch: the opcode byte load
from L1 (~7), the kOps table load from L1 (~7) and the indirect jump;
putting the table (or a pre-translated program) in fast RAM would
attack the second.


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
- **Where programs come from:** one whole physics, compiled in or loaded
  at start (section 9); several to choose among at start, or one per
  type, later.
- **A verifier** at load time, for early error reports only (the run-time
  checks stay).
- **Host-side bytecode** to pick display colors per type.
- **Stack sizes:** 16 operands, 8 calls and 32 locals are guesses; real
  programs (and a compiler) will tell.
- **Versioning:** a program header with a version and the type it is
  for.


## 12. Change log

- **Dispatch rework (2026-11-27):** verify() checks a program once
  (opcodes, immediates, jump targets on instruction starts, fields,
  member ids, enter sizes, no running off the end; a call needs a next
  instruction); bpasm's headers static_assert it for every program, so
  run() trusts its program and drops those per-instruction checks.
  Fuel is still counted per instruction but compared only at backward
  jumps and calls.  Dispatch is computed goto, each handler jumping to
  the next; run() is built optimize("O2") because the EWP's -Os merges
  all the dispatches into one shared jump (stateWord/putStateWord are
  always_inline for the same reason).  A SWAP went from ~28 RISC-V
  instructions with 4 taken jumps and 5 stack spills to 14 with one
  jump.  Timed on the QB in run 20261127-141156: cheapest ops 25
  cycles (was 42-48), FB4 14388 cycles/event (was 24788), section 10.
- **Dispatch table in fast RAM (2026-11-27, run 20261127-151122):**
  each dispatch is a dependent chain (opcode byte, then table entry,
  then jump), so L1's latency is paid in full, unlike the independent
  loads of the native probe (L1 6.8 vs fast 5.3).  Every MFMx image
  runs with the L0 data cache OFF (_BUD.S sets DisLowCash, CSR 0x7c0
  bit 3), so each L1 load is >= 8 cycles (ERISC_NOTES section 8)
  against 2 for fast RAM: both the opcode fetch and the table load
  paid that before this change.  run() now copies its
  kOps table (static, in L1) into the Machine (`mOps`, HB fast RAM)
  once, remembered by `mOpsFrom`, and indexes that; every dispatch in
  the EWP build is `sh2add; lw` off the Machine.  initB clears
  mOpsFrom (fast RAM is not zeroed at boot); host Machines are
  value-initialized.  Machine 228 -> 344 B; HB fast-RAM data ends at
  0x3e0, so ~7.2 KB stays for the stack.  The opcode bytes still come
  from L1 (a fast-RAM code cache is the next idea, OPEN_ISSUES).
  MEASURED (run 151122, de5a0a3, ~70 runs per bench program): about 6
  cycles off every instruction, as predicted from the latency (8+ vs 2).
  Cheapest ops in L1 20-22 cycles (dup 19.9, over 20.9, swap 21.8,
  push8 22.0; were 25-27), with code in fast RAM 13.5-15.6 (were 20-22);
  getm1 51.8 (57.1), setm1 63.7 (69.4), jmp 30.2 / 17.0.  FB1 bytecode
  1815 cycles/event, 3.98x native (was 1975, 4.30x); FB4 12199, 3.74x
  (was 14388, 4.40x), 32.6 cycles per FB4 instruction.  The opcode fetch
  is now the one L1 load left in every dispatch: code in fast RAM is
  worth another ~6-13 per instruction (the L/F columns).  FB4 share
  0.13-0.16 (native ahead).
- **Code cache in fast RAM (2026-11-27, run 20261127-200923):**
  ByteCache.h's CodeCache keeps the programs in use in fixed fast-RAM
  slots (EWP: Physics::mCodeCache, 4 x 256 bytes): use(p) returns p
  with mCode pointing at a copy, copied into the least recently used
  slot on a miss, or p itself if it is bigger than a slot (tooBig, runs
  from L1).  Keyed by the code pointer (the generated programs are
  static); a copy of a verified program needs no re-verification.  For
  flaky memory: each slot is re-copied from L1 every 4096 uses, and
  run() now masks each opcode byte to its 32-entry table (entries past
  OP_COUNT fault BF_OPCODE), one AND per instruction, so even a corrupt
  opcode cannot jump wild (a flip that stays below OP_COUNT runs
  another opcode: safety, not detection).  Bench programs run uncached
  (their L and F columns stay comparable).  Marks every 100,000 events
  per EWP: `.bc:HITS:MISSES:TOOBIG:REFRESHES:BYTES.` and the hart B
  stack `.sw:USED:HEADROOM.` (hex); runsum 'code cache and hart B
  stack'.  HB fast-RAM data now ends at 0x840 (~6 KB of stack).
  bytephys_test: cached runs identical to uncached, LRU, tooBig, the
  refresh repairing a corrupted copy, and the mask.  Next (OPEN_ISSUES):
  function-granular caching and calls across elements.
  MEASURED (run 200923, bebd68e): FB4 bytecode 9468 cycles/event, 2.89x
  native (was 12199, 3.74x; 25.3 cycles per instruction), FB1 1633,
  3.49x (was 1815, 3.98x).  Cache: 560.7M uses on 420 EWPs, 99.9999%
  hits (607 misses, ~1.4 per EWP: the first loads), 0 too big, 136,585
  refreshes (one per 4096 uses, as designed).  The mask costs ~1 cycle
  per instruction (bench L column: dup 19.9 -> 20.8, over 20.9 -> 21.8,
  push8 22.0 -> 23.0; F the same).  Hart B stack high-water max 1008
  bytes (median 816) of ~6 KB, headroom min 5072 bytes: room for a
  cache several times this size.  BUDGET (Liv, 2026-11-27): the stack
  will grow as more powerful bytecodes arrive, so plan hart B's 8 KB as
  ~2 KB stack reserve + ~3 KB code cache + ~1 KB other data, not
  around today's 1 KB high-water.
- **Images, function chunks, chunk cache (2026-11-27, run
  20261127-224448; Liv's 'hybrid'):** see section 3.  A whole
  physics is one image of functions cut into <= 256-byte chunks;
  jmp/jz/jnz take a u8 in-chunk offset, xjmp/xjz/xjnz (new, 1c-1e) a
  chunk and offset, call a function index; fuel is compared at backward
  jumps, every cross-chunk jump and calls.  verify(Image) checks the
  tables and every chunk (static_assert in each generated header).  The
  cache (ByteCache.h) now holds CHUNKS: 12 slots of 256 bytes (~3 KB),
  fully associative, most recently used slot tried first, LRU, refresh
  every 4096 hits; run() asks it only at entry, taken cross-chunk jumps,
  calls and returns.  `.bc:HITS:MISSES:SCANS:REFRESHES:BYTES.` (SCANS =
  hits outside the MRU slot; the third field was TOOBIG before).  The
  bench F column now runs through the cache (an untimed warm run first),
  so a `call` pattern includes a cache lookup each way.  The FB1+FB4
  image is 170 bytes in 3 chunks (FB1's kabam is its own function); the
  test physics has a function cut in two.  _BUD.ld.in now fails the link
  if hart B's fast-RAM data leaves under 2 KB for the stack (data ends at
  0x10b0 with the cache, ~3.8 KB of stack).  Next: if small functions
  waste slots, packed 64-byte units (allocator only); cross-element
  calls in ulam-lite.
  MEASURED (run 224448, 1b19246): FB4 9423 cycles/event, 2.88x native
  (was 9454); FB1 1695, 3.39x.  Cache 99.9997% hits (2118 misses on 420
  EWPs: first loads), 0.4% of hits outside the MRU slot, refreshes one
  per 4096 hits.  Stack high-water 880 bytes, headroom min 3040.  But a
  transfer costs: bench `call` (call + ret) ~278 cycles in fast RAM (was
  ~44 when whole programs were cached) and ~105 from L1 (was ~62), so
  ~117 cycles per transfer through the cache (call and ret alternate
  chunks: MRU miss, tag scan) and ~21 in place (the Image's tables are
  L1 loads); `enter` the same plus its own cost.  Open (OPEN_ISSUES):
  table pointers in locals, a second MRU try or a caller slot saved for
  RET.
- **DReg and Res, a second physics (2026-11-28, host-tested,
  cross-built, not yet run):** scripts/ulamlite/dreg/{dreg,res}.ulam,
  the same steps and draws as native types 2 and 3 (a native swap with
  an empty site is a copy and an erase); bytephys_test compares 24000
  events site by site against a copy of the native code.  New opcode
  OP_NEW (0x1f, `new TYPE16`, ( sn -- )): a new atom of a type fixed at
  verify time (INACCESSIBLE refused); ulam-lite `ew[n] = Res;`, and `is
  Inaccessible`.  OP_COUNT is now 32 = OPS_SLOTS, so no masked byte
  faults BF_OPCODE any more (each runs some opcode; still never a wild
  jump).  Blobs record OP_COUNT, so the old ones are refused and every
  blob was regenerated (bfb.bcp checksum now 0xea8b444b).  ulamlite.py
  SETS carry a first type each (dreg from 9); bpasm builds the set into
  srcs/host/tools/ByteDReg.h and physics/dreg.bcp (208 bytes).  The host
  draws 9 and 10 as darker native DReg / Res (isBytecodeType 7..10).
  EWP image 144852 bytes (blob at 0x235e0).
- **Loadable physics, step 3 (2026-11-28, measured, run
  20261128-083650: every EWP ran the loaded blob, `.bv:1:0:0:2:1:
  7ed764ec`; timing as before, FB4 9403 cycles/event, FB1 1681, bench
  call 43.3 F):** the start-time load (section 9 'Loadable physics').  Host:
  the layout-config flag `physics = true`, mfmxctl `--physics` and the
  `physics` command (applied at `start`, Machine._apply_physics),
  T6Image::attachPhysics (reads, verifyBCP, fit check against
  L1_IMAGE_LIMIT) / attachNoPhysics (the empty marker), and
  CodeManager::deployPhysics writing the words after the code (multicast
  and unicast alike).  EWP: openImages takes the blob at
  `__hostblock_end` rounded to 16 if its magic is nonzero, else BP_BCP;
  `.bv` grew LOADED and SUM (runsum reads both forms); hart B data ends
  at 0x10e0.  bpasm.py also writes physics/bfb.bcp (and --check checks
  it).  Mock MFMx: attachPhysics checks the header only.
- **BCP blobs (2026-11-28, measured, runs 022755 and 030540):**
  Liv's start-time loadable physics, step 2: an image is one
  pointer-free blob (section 3), still compiled in (BP_BCP).
  verifyBCP(bytes, size) replaces verify(Image) (constexpr, byte reads);
  openBCP fills a BCPView (Image is gone; Program is (view, function));
  entryFor(view, type) replaces programFor.  bpasm.py packs the blob
  (pack_bcp) and emits BP_FUNC_<NAME>, BP_PROG_FUNCS, BP_PROG_NAMES.
  The EWP opens the physics and bench blobs once (mBCP, mBenchBCP in
  hart B fast RAM, +80 bytes: data ends at 0x10c0); `BCPBAD` mark if one
  fails.  RUN 20261128-014339 RAN NO BYTECODE: the open sat in initB,
  which nothing calls (since bb5c983); the cache and dispatch resets
  there never ran either, harmless because fast RAM is zero at reset
  and bind() empties the cache.  Now opened at the first event that
  needs it (f1e7905's late reopen proved the open itself works on the
  hardware: `.bv:0:0:1:0:0:2`).  MEASURED with that reopen (run
  20261128-022755): same as before the blobs: bench call 43.3 F / 50.6
  L, enter 49.5 / 58.6; getm1 46.3 / 56.5 (was 48.6 / 58.7: the
  Layout now sits in the view in fast RAM); FB4 9376 cycles/event
  (2.87x), FB1 1663 (3.60x); 1.13 cache misses per million events, 0
  scans.  bytephys_test: hand-made images are packed by a C++ packBCP,
  which must give bpasm's exact bytes for the physics; damage of every
  kind is refused (magic, version, opcode count, size, checksum, a
  misaligned or out-of-bounds table, reserved words, a member over 32
  bits or a ref past the ats, a type naming no function, a bad opcode
  under a fixed checksum, room too small, a misaligned blob).  FB1/FB4
  blob: 280 bytes (170 of code).
- **Cheaper cache hits (2026-11-28, host-tested, cross-built, not yet
  run):** Liv: no tuning for leaf calls, do as little as possible on a
  hit.  The resolver is now bind(img), once per run() (a different image
  empties the cache: one physics at a time, which one may be chosen at
  start), then chunk(c).  A HIT: mSlotOf[c & 63] names a slot and its
  u16 tag (the chunk number) confirms it: two byte loads, a compare,
  nothing written.  A stale hint (another chunk with the same low bits,
  or the slot reused) scans the 12 tags out of line and rewrites the
  hint; a chunk in no slot is a miss, copied into the next slot in turn
  (FIFO, since hits leave no use record).  The refresh is off the hit
  path: Physics re-copies one slot from L1 every 1024 events, between
  events (refreshOne, round robin), so each slot every ~12,000.  run()
  reads the image's tables through locals after entry (InPlace keeps
  mCode / mChunkStart from bind).  `.bc:MISSES:SCANS:REFRESHES:BYTES.`
  (no hit count); runsum prints misses and scans per million events.
  HB fast-RAM data ends at 0x1070.  bytephys_test: hint hits count
  nothing, a shared hint entry falls back to a scan, FIFO order,
  rebinding, refreshOne round robin and repair.
  MEASURED (run 20261128-003440, 954157f): bench `call` 43.3 cycles per
  insn F / 50.7 L (was 138.8 / 52.3), `enter` 49.5 / 59.3 (was 114.3 /
  58.5): code in fast RAM now beats L1 for calls, ~20 cycles per
  transfer above the old whole-program cache; the locals barely moved
  L.  Other patterns unchanged.  `.bc`: 1.75 misses per million
  events, 0 scans.  FB4 9436 cycles/event (2.88x), FB1 1764 (3.61x;
  FB1 has moved ~5% between runs).  Stack high-water 880, headroom
  3104 bytes.

- **Microbenchmark (2026-11-27):** Bench atoms (type 31) run 26
  per-opcode programs from benchgen.py, from L1 and from fast RAM, plus
  native load probes; `.pb` marks, runsum section (section 10).  bpasm
  emits BP_PROG_COUNT / BP_PROG_LIST / BP_PROG_NAMES for every set.
  Not yet run on the QB.

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
Each SET is one physics: scripts/ulamlite/*.ulam (BFB1, BFB4: the
compiled-in one and physics/bfb.bcp), tests/ (bytephys_test only), and
dreg/ (DReg, Res: physics/dreg.bcp, loaded with `physics dreg`).
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
agrees).  Elements are numbered from a set's first type (ulamlite.py
SETS; 7 = `FIRST_TYPE`, above the native types 0..6) in order of name:
BFB1 is 7, BFB4 is 8; the dreg set starts at 9, so DReg is 9 and Res
10 and no two physics share a number.  Adding an element can renumber
the ones after it.  bpasm turns the numbers into a table,
inside the BCP blob (type -> entry function; `BP_FIRST_TYPE` /
`BP_TYPE_COUNT` in BytePrograms.h), and the EWP runs any type it finds
there (entryFor) (Physics.cpp's
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
`ew[e] = Empty;`, `ew[e] = ELEMENT;` (a new atom of that type, OP_NEW);
if/else, while, for, break, continue, return; calls.

**Expressions:** unsigned literals (decimal, 0x), true/false,
constants, locals, self's members (`a.age`), `+ -` (saturating),
`== != < <= > >=`, `&& || !` (short-circuit, so random draws happen
only when reached), method calls, `random.between(lo, hi)`,
`random.oneIn(n)`, `ew[e] is TYPE` (`is Empty` for empty sites, `is
Inaccessible` for sites off the edge).

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
