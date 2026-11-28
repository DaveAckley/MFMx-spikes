#pragma once  /* -*- C++ -*- */
/* BytePhys.h -- a tiny bytecode machine for physics (SPIKE, 2026-11-26).

   Manual: BYTEPHYS.md (repo root; keep it in step with this file).

   Does interpreting physics cost too much on a T6 baby core
   (CLOSED_ISSUES 'BytePhys spike')?  This is the
   smallest machine that can run FB1 and FB4 (scripts/bytephys/bpasm.py
   assembles them into BytePrograms.h), so the EWP can run native and
   bytecode versions side by side (types 4/5 native, 7/8 bytecode) and
   time both (the `.pt` mark, ewp/src/Physics.cpp).

   THE MACHINE.  A u32 operand stack and a u16 return-address stack, both
   fixed arrays in the Machine object (on the EWP it lives in hart B's
   fast RAM beside the event window), so bytecode recursion never touches
   the native stack: the interpreter is one loop and never calls itself.
   The Machine also holds a copy of the dispatch table (mOps), so the
   table load of every dispatch comes from fast RAM, not L1.
   A physics is checked ONCE by verifyBCP() (opcodes, immediates, jump
   targets, fields, member ids, chunk ends, the blob's tables; bpasm's
   headers do it at compile time, openBCP again when a blob is opened), then run() checks only what depends on the run: every push,
   pop, call, return, frame, site number and member type.  Every
   instruction burns one unit of fuel, compared at backward jumps and
   calls.  Any violation calls Env::fault(Fault), which must not return
   (the EWP FAILs into the physics RecoverPoint; the host test records
   it and throws).

   Env (a template parameter, so the EWP and the host test share this
   exact code) provides:
     P4Atom & atom(u32 sn)            site sn of the window, sn < 41
     u32 between(u32 lo, u32 hi)      uniform in [lo, hi]
     bool oneIn(u32 n)
     [[noreturn]] void fault(u32 f)   f is a Fault

   ENCODING.  One byte opcode, then its immediates, little-endian.
   Binary ops pop b then a and push (a op b).  See bpasm.py for the
   assembler.

   IMAGES, FUNCTIONS, CHUNKS (2026-11-27).  All the code of a set of .bp
   files (one physics) is one Image: its FUNCTIONS (each program's entry,
   every call target) cut into CHUNKS of at most CHUNK_MAX bytes, stored
   back to back.  A chunk is the unit the fast-RAM code cache holds
   (ByteCache.h), so nothing inside one depends on where it sits: JMP,
   JZ and JNZ take a u8 offset WITHIN their chunk; XJMP, XJZ and XJNZ (u16
   chunk, u8 offset) go to another chunk, which is how a function bigger
   than a chunk continues (bpasm puts an XJMP at the end of a chunk that
   would fall through); CALL takes a u16 FUNCTION index (its code starts
   at offset 0 of the function's first chunk) and saves (chunk, return
   offset, frame).  So the interpreter asks where code is only at those
   transfers, through its Res(olver) template parameter: InPlace reads
   the image where it lies (L1 on the EWP), the code cache copies chunks
   into fast RAM.  A Program is a handle: an image and an entry
   function.

   BCP: BYTECODE PHYSICS BLOBS (2026-11-28; the name is provisional).
   An image is stored as ONE pointer-free blob, so a physics can later be
   chosen at start time (appended after an EWP image's HostBlock, like
   MFMs loading a .so; BYTEPHYS.md section 3).  Everything in it is
   little-endian and found by byte offsets from the blob's start, every
   table 4-aligned:

     header (BCP_HEADER_BYTES = 64): magic 'BCP1', u16 BCP_VERSION,
       u16 OP_COUNT, u32 total size (a multiple of 4), u32 checksum
       (FNV-1a over the whole blob with these 4 bytes read as 0), u16
       chunk count, u16 function count, u16 member refs, u16 member ats,
       u16 first type, u16 type count, then u32 offsets of: code, chunk
       starts (u16 x chunks+1), function chunks (u16 x functions),
       MemberRefs, MemberAts (4 bytes each), type -> entry function
       (u16 x types, NO_FUNC = none); 3 reserved words, 0.
     then the code and the tables, each padded to 4 bytes.

   verifyBCP(bytes, size) is constexpr (byte reads only), so the
   generated headers static_assert it.  openBCP(bytes, room, view) runs
   the same verify and fills a BCPView: plain pointers into the blob,
   for run() and the cache.  Nothing outside a blob carries code or
   tables.

   ARITHMETIC SATURATES.  ADD and SUB clamp to [0, 2^32-1] instead of
   wrapping, and SETM clamps a value to its member's declared width.

   DECLARED MEMBERS.  bpasm source declares elements and quarks (named
   members of given bit widths, packed into the atom's 80 STATE BITS: bits
   0..31 are mStg[0], 32..63 mStg[1], 64..79 mData0).  GETM/SETM name a
   member by a u16 id; the program's Layout maps the id to one (type,
   pos, len) per element type that has it (a quark member can live at
   different places in different elements).  A site whose type has no
   such member is a fault (BF_TYPE).

   FRAMES.  Local variables live in mLocals, u32 slots, numbered from the
   current frame's base.  ENTER nargs nlocals (normally a function's
   first instruction, or the program's) opens a frame of nlocals slots
   at the top of mLocals, moves the top nargs operands into slots
   0..nargs-1 (the deepest operand into slot 0) and zeroes the rest.
   LOADL/STOREL n reach slot n of the current frame only.  CALL saves
   the return address and the caller's frame; RET restores both, so a
   callee's frame vanishes with it.  Results come back on the operand
   stack.
*/
#include "itype.h"
#include "P4Atom.h"

namespace MFM {
  namespace BytePhys {

    enum Op : u8 {
      OP_HALT0 = 0,                     //< event over, physics said no
      OP_HALT1,                         //< event over, commit the window
      OP_PUSH8,                         //< imm8: push it
      OP_PUSH32,                        //< imm32: push it
      OP_DUP,                           //< a -> a a
      OP_DROP,                          //< a ->
      OP_SWAP,                          //< a b -> b a
      OP_OVER,                          //< a b -> a b a
      OP_ADD,                           //< a b -> a+b (saturates at 2^32-1)
      OP_SUB,                           //< a b -> a-b (saturates at 0)
      OP_EQ,                            //< a b -> a==b
      OP_LT,                            //< a b -> a<b (unsigned)
      OP_JMP,                           //< imm8 offset in this chunk
      OP_JZ,                            //< imm8 offset: pop, jump if 0
      OP_JNZ,                           //< imm8 offset: pop, jump if != 0
      OP_CALL,                          //< imm16 function: save return+frame
      OP_RET,                           //< restore return+frame
      OP_GETF,                          //< imm8 field: sn -> value
      OP_SETF,                          //< imm8 field: sn value ->
      OP_COPY,                          //< src dst -> (atom dst = atom src)
      OP_EMPTY,                         //< sn -> (atom sn = empty)
      OP_BETWEEN,                       //< lo hi -> between(lo, hi)
      OP_ONEIN,                         //< n -> oneIn(n)
      OP_GETM,                          //< imm16 member: sn -> value
      OP_SETM,                          //< imm16 member: sn value ->
      OP_ENTER,                         //< imm8 nargs, imm8 nlocals
      OP_LOADL,                         //< imm8 slot: -> local
      OP_STOREL,                        //< imm8 slot: value ->
      OP_XJMP,                          //< imm16 chunk, imm8 offset
      OP_XJZ,                           //< imm16 chunk, imm8 offset: pop, if 0
      OP_XJNZ,                          //< imm16 chunk, imm8 offset: pop, if != 0
      OP_COUNT
    };

    /* Atom fields GETF/SETF can name.  F_TYPE is read-only (setting it
       would need new parity: use COPY or EMPTY). */
    enum Field : u8 {
      F_TYPE = 0,
      F_DATA0,                          //< P4Atom::mData0 (u16)
      F_STG0,                           //< P4Atom::mStg[0]
      F_STG1,                           //< P4Atom::mStg[1]
      F_COUNT
    };

    enum Fault : u32 {
      BF_STACK_OVER = 1,
      BF_STACK_UNDER,
      BF_CALL_OVER,
      BF_CALL_UNDER,
      BF_OPCODE,
      BF_PC,                            //< ran off the end or bad target
      BF_SITE,                          //< site number >= 41
      BF_FIELD,                         //< bad or read-only field
      BF_FUEL,                          //< instruction budget used up
      BF_MEMBER,                        //< no such member id (or no Layout)
      BF_TYPE,                          //< the site's type lacks that member
      BF_FRAME,                         //< ENTER: no room, or nargs > nlocals
      BF_LOCAL,                         //< LOADL/STOREL outside the frame
      BF_COUNT
    };

    enum : u32 {
      SITES = 41,                       //< EventWindow::ATOM_COUNT
      STATE_BITS = 80,                  //< mStg[0], mStg[1], mData0
      MAX_MEMBER_BITS = 32,             //< widest member (fits the stack)
    };

    /* All ones in the low len bits, 1 <= len <= 32. */
    inline u32 lenMask(u32 len) {
      return len >= 32u ? 0xffffffffu : (1u << len) - 1u;
    }

    /* State word i of an atom: 0 mStg[0], 1 mStg[1], 2 mData0. */
    // always_inline: Machine::run is built O2 inside -Os images
    inline __attribute__((always_inline)) u32 stateWord(const P4Atom & a, u32 i) {
      return i == 0u ? a.mStg[0] : i == 1u ? a.mStg[1] : (u32) a.mData0;
    }

    inline __attribute__((always_inline)) void putStateWord(P4Atom & a, u32 i, u32 v) {
      if (i == 0u) a.mStg[0] = v;
      else if (i == 1u) a.mStg[1] = v;
      else a.mData0 = (u16) v;
    }

    /* len state bits at pos (pos + len <= STATE_BITS, len <= 32). */
    inline u32 getBits(const P4Atom & a, u32 pos, u32 len) {
      const u32 w = pos >> 5;
      const u32 off = pos & 31u;
      if (off + len <= 32u)             // fast path: inside one word
        return (stateWord(a, w) >> off) & lenMask(len);
      u64 v = stateWord(a, w);
      if (w < 2u) v |= (u64) stateWord(a, w + 1u) << 32;
      return (u32) (v >> off) & lenMask(len);
    }

    inline void setBits(P4Atom & a, u32 pos, u32 len, u32 val) {
      const u32 w = pos >> 5;
      const u32 off = pos & 31u;
      if (off + len <= 32u) {           // fast path: inside one word
        const u32 m = lenMask(len) << off;
        putStateWord(a, w, (stateWord(a, w) & ~m) | ((val << off) & m));
        return;
      }
      u64 v = stateWord(a, w);
      if (w < 2u) v |= (u64) stateWord(a, w + 1u) << 32;
      const u64 m = (u64) lenMask(len) << off;
      v = (v & ~m) | (((u64) val << off) & m);
      putStateWord(a, w, (u32) v);
      if (w < 2u) putStateWord(a, w + 1u, (u32) (v >> 32));
    }

    /* Where one element type keeps a member.  (may_alias: read in
       place from a BCP blob's bytes.) */
    struct __attribute__((may_alias)) MemberAt {
      u16 mType;
      u8 mPos;                          //< first state bit
      u8 mLen;                          //< width, 1..32
    };

    /* Member id -> its MemberAts (one per element type that has it). */
    struct __attribute__((may_alias)) MemberRef {
      u16 mFirst;                       //< index into Layout::mAts
      u16 mCount;
    };
    static_assert(sizeof(MemberAt) == 4u && sizeof(MemberRef) == 4u,
                  "BCP tables hold 4-byte MemberAts and MemberRefs");

    /* A u16 read in place from a BCP blob's bytes. */
    typedef u16 __attribute__((may_alias)) U16A;

    struct Layout {
      const MemberRef * mRefs;
      u16 mRefCount;
      const MemberAt * mAts;
    };

    /* The physics types run as bytecode (ewp/src/Physics.cpp PHY_BFB1,
       PHY_BFB4).  The host draws them like their native twins but
       darkened (tintBytecode), so the two can be told apart on screen.
       Add any new bytecode type here. */
    inline bool isBytecodeType(u32 type) {
      return type == 7u || type == 8u;
    }

    /* Darken one color channel of a bytecode type to 5/8. */
    inline u8 tintBytecode(u8 channel) {
      return (u8) (((u32) channel * 5u) / 8u);
    }

    enum : u32 {
      CHUNK_MAX = 256,                  //< bytes; offsets are u8
    };

    /* The BCP blob format (see 'BCP' above). */
    enum : u32 {
      BCP_MAGIC = 0x31504342u,          //< 'B' 'C' 'P' '1', little-endian
      BCP_VERSION = 1u,                 //< bump on any format change
      BCP_HEADER_BYTES = 64u,
      BCP_MAX_BYTES = 0x100000u,        //< 1 MB: more than any EWP L1 left
      NO_FUNC = 0xffffu,                //< a type with no entry function
    };
    // header byte offsets
    enum : u32 {
      BCPH_MAGIC = 0u,
      BCPH_VERSION = 4u,                //< u16
      BCPH_OPCOUNT = 6u,                //< u16
      BCPH_SIZE = 8u,
      BCPH_CHECKSUM = 12u,
      BCPH_CHUNKS = 16u,                //< u16
      BCPH_FUNCS = 18u,                 //< u16
      BCPH_REFS = 20u,                  //< u16
      BCPH_ATS = 22u,                   //< u16
      BCPH_FIRST_TYPE = 24u,            //< u16
      BCPH_TYPES = 26u,                 //< u16
      BCPH_OFF_CODE = 28u,
      BCPH_OFF_CHUNK_START = 32u,
      BCPH_OFF_FUNC_CHUNK = 36u,
      BCPH_OFF_REFS = 40u,
      BCPH_OFF_ATS = 44u,
      BCPH_OFF_TYPE_FUNC = 48u,
      BCPH_RESERVED = 52u,              //< 3 words, must be 0
    };
    // what verifyBCP reports about the blob itself (its code's problems
    // are reported as the Fault they would cause)
    enum : u32 {
      BCP_BAD_SIZE = 0x101u,            //< too small, too big, not /4
      BCP_BAD_MAGIC,
      BCP_BAD_VERSION,                  //< format or opcode set differs
      BCP_BAD_CHECKSUM,
      BCP_BAD_TABLE,                    //< an offset or count out of bounds
      BCP_BAD_LAYOUT,                   //< a member ref or at is invalid
      BCP_BAD_TYPES,                    //< a type names no function
    };

    constexpr u32 bcpRd16(const u8 * b, u32 i) {
      return (u32) b[i] | ((u32) b[i + 1u] << 8);
    }
    constexpr u32 bcpRd32(const u8 * b, u32 i) {
      return bcpRd16(b, i) | (bcpRd16(b, i + 2u) << 16);
    }

    /* FNV-1a over size bytes, the checksum word read as 0. */
    constexpr u32 bcpChecksum(const u8 * b, u32 size) {
      u32 h = 2166136261u;
      for (u32 i = 0; i < size; ++i) {
        const u32 x = i - BCPH_CHECKSUM < 4u ? 0u : b[i];
        h = (h ^ x) * 16777619u;
      }
      return h;
    }

    /* A verified BCP blob, opened (openBCP): pointers into it.  Chunk c
       is mCode[mChunkStart[c] .. mChunkStart[c + 1]); function f starts
       at offset 0 of chunk mFuncChunk[f]; element type t (mFirstType <=
       t < mFirstType + mTypeCount) runs function mTypeFunc[t -
       mFirstType] unless that is NO_FUNC. */
    struct BCPView {
      const u8 * mBase;                 //< the blob
      const u8 * mCode;
      const U16A * mChunkStart;         //< mChunkCount + 1 entries
      const U16A * mFuncChunk;          //< mFuncCount entries
      const U16A * mTypeFunc;           //< mTypeCount entries
      Layout mLayout;                   //< for GETM/SETM
      u16 mChunkCount;
      u16 mFuncCount;
      u16 mFirstType;
      u16 mTypeCount;
    };

    /* What to run: an opened image and its entry function (an element's
       behave, or a microbenchmark). */
    struct Program {
      const BCPView * mImage;
      u16 mFunc;
    };

    /* Code where the image lies (L1 on the EWP): the resolver run() uses
       when no cache is given.  A resolver has two calls: bind(img), once
       at run()'s entry, and chunk(c), the address of chunk c's bytes in
       the image bound, at entry and at every transfer.  bind() keeps the
       image's table pointers here so a transfer need not reread them
       from the Image (an L1 struct on the EWP). */
    struct InPlace {
      const u8 * mCode = nullptr;
      const U16A * mStart = nullptr;
      void bind(const BCPView & img) {
        mCode = img.mCode;
        mStart = img.mChunkStart;
      }
      const u8 * chunk(u32 c) { return mCode + mStart[c]; }
    };

    struct Machine {
      enum : u32 {
        STACK_SIZE = 16,                //< operand stack, u32 each
        CALL_SIZE = 8,                  //< return addresses
        LOCALS_SIZE = 32,               //< local slots, all frames
      };
      struct Call {
        u16 mChunk;                     //< the caller's chunk
        u8 mReturn;                     //< offset after the CALL in it
        u8 mBase;                       //< caller's frame base
        u8 mTop;                        //< caller's frame end
      };
      u32 mStack[STACK_SIZE];
      Call mCalls[CALL_SIZE];
      u32 mLocals[LOCALS_SIZE];
      u32 mExecuted;                    //< instructions run, last event
      /* The dispatch table, copied here from run()'s kOps (in L1 on the
         EWP) so each dispatch's table load comes from fast RAM along
         with the rest of the Machine.  mOpsFrom names the kOps the copy
         came from (each run<Env> has its own); null = no copy yet.  On
         the EWP fast RAM is zero at reset (nothing calls initB); a host
         Machine must be value-initialized (`Machine vm{};`) or call
         resetDispatch().  The table has OPS_SLOTS entries, the power of
         two at or above OP_COUNT; dispatch masks the opcode byte to it
         and the entries past OP_COUNT fault BF_OPCODE, so even a
         corrupted opcode (a bit flipped in a cached copy, ByteCache.h)
         reaches a handler or a fault, never a wild jump.  It costs one
         AND per instruction.  (A flip that stays below OP_COUNT runs
         another opcode: the mask is safety, not detection.) */
      enum : u32 {
        OPS_SLOTS = 32,                 //< power of two >= OP_COUNT
      };
      static_assert((u32) OP_COUNT <= (u32) OPS_SLOTS &&
                    (OPS_SLOTS & (OPS_SLOTS - 1u)) == 0u,
                    "the dispatch mask needs a power of two >= OP_COUNT");
      const void * mOpsFrom;
      void * mOps[OPS_SLOTS];

      void resetDispatch() { mOpsFrom = nullptr; }

      /* Run p from its image where it lies (InPlace). */
      template <class Env>
      bool run(const Program & p, Env & env, u32 fuel) {
        InPlace res;
        return run(p, env, fuel, res);
      }

      /* Run program p of a VERIFIED image (opened by openBCP, which
         runs verifyBCP; the generated blobs are also checked at compile
         time), so opcodes,
         immediates, jump targets, chunk and function numbers, fields,
         member ids and chunk ends need no checks here.  What only the
         run can know is still checked: stack depths, frames, sites,
         member types, and fuel, which is counted per instruction but
         compared only at backward jumps, cross-chunk jumps and calls
         (straight-line code in a chunk always reaches one, or a halt or
         a return).  res.bind(image) once, then res.chunk(c) says where
         chunk c's bytes are; it is asked at entry and at every XJMP/XJZ/
         XJNZ taken, CALL and RET, never in between.  The image's tables
         are read through locals, not the Image, after entry. */
      template <class Env, class Res>
      __attribute__((optimize("O2")))  // the EWP builds -Os, which would
      // merge every handler's dispatch into one shared indirect jump
      bool run(const Program & p, Env & env, u32 fuel, Res & res) {
        // computed-goto dispatch (GCC): each handler jumps straight to
        // the next one; kOps lists every Op in enum order
        static void * const kOps[] = {
          &&L_HALT0, &&L_HALT1, &&L_PUSH8, &&L_PUSH32, &&L_DUP, &&L_DROP,
          &&L_SWAP, &&L_OVER, &&L_ADD, &&L_SUB, &&L_EQ, &&L_LT, &&L_JMP,
          &&L_JZ, &&L_JNZ, &&L_CALL, &&L_RET, &&L_GETF, &&L_SETF,
          &&L_COPY, &&L_EMPTY, &&L_BETWEEN, &&L_ONEIN, &&L_GETM, &&L_SETM,
          &&L_ENTER, &&L_LOADL, &&L_STOREL, &&L_XJMP, &&L_XJZ, &&L_XJNZ,
        };
        static_assert(sizeof(kOps) / sizeof(kOps[0]) == OP_COUNT,
                      "kOps must list every Op, in order");
        if (__builtin_expect(mOpsFrom != (const void *) kOps, 0)) {
          for (u32 i = 0; i < OPS_SLOTS; ++i)
            mOps[i] = i < OP_COUNT ? kOps[i] : &&L_BADOP;
          mOpsFrom = (const void *) kOps;
        }
        void * const * const ops = mOps;
        const BCPView & img = *p.mImage;
        const U16A * const funcChunk = img.mFuncChunk;
        const Layout * const layout = &img.mLayout;
        res.bind(img);
        u32 chunk = funcChunk[p.mFunc];
        const u8 * code = res.chunk(chunk);
        u32 * const stk = mStack;
        u32 pc = 0;
        u32 sp = 0;                     // next free operand slot
        u32 cp = 0;                     // next free call slot
        u32 fb = 0;                     // current frame: mLocals[fb..ft)
        u32 ft = 0;
        u32 used = 0;

#define BP_NEED(n) do { if (__builtin_expect(sp < (n), 0)) \
                            env.fault(BF_STACK_UNDER); } while (0)
#define BP_ROOM(n) do { if (__builtin_expect(sp + (n) > STACK_SIZE, 0)) \
                          env.fault(BF_STACK_OVER); } while (0)
#define BP_IMM16 ((u32) code[pc] | ((u32) code[pc + 1] << 8))
// (c and t are evaluated before code changes)
#define BP_GOCHUNK(c, t) do { const u32 c_ = (c); const u32 t_ = (t); \
                              chunk = c_; code = res.chunk(c_); \
                              pc = t_; } while (0)
#define BP_NEXT do { ++used; goto *ops[code[pc++] & (OPS_SLOTS - 1u)]; } \
  while (0)
#define BP_FUEL_CHECK do { if (__builtin_expect(used > fuel, 0)) \
                              env.fault(BF_FUEL); } while (0)

        BP_NEXT;

      L_BADOP:                          // only reachable by corruption
        env.fault(BF_OPCODE);
        return false;
      L_HALT0:
        mExecuted = used;
        return false;
      L_HALT1:
        mExecuted = used;
        return true;
      L_PUSH8:
        BP_ROOM(1);
        stk[sp++] = code[pc++];
        BP_NEXT;
      L_PUSH32:
        BP_ROOM(1);
        stk[sp++] = (u32) code[pc] | ((u32) code[pc + 1] << 8) |
          ((u32) code[pc + 2] << 16) | ((u32) code[pc + 3] << 24);
        pc += 4;
        BP_NEXT;
      L_DUP:
        BP_NEED(1);
        BP_ROOM(1);
        stk[sp] = stk[sp - 1];
        ++sp;
        BP_NEXT;
      L_DROP:
        BP_NEED(1);
        --sp;
        BP_NEXT;
      L_SWAP: {
        BP_NEED(2);
        const u32 t = stk[sp - 1];
        stk[sp - 1] = stk[sp - 2];
        stk[sp - 2] = t;
        BP_NEXT;
      }
      L_OVER:
        BP_NEED(2);
        BP_ROOM(1);
        stk[sp] = stk[sp - 2];
        ++sp;
        BP_NEXT;
      L_ADD: {
        BP_NEED(2);
        const u32 b = stk[--sp];
        const u32 a = stk[sp - 1];
        stk[sp - 1] = a + b < a ? 0xffffffffu : a + b;
        BP_NEXT;
      }
      L_SUB: {
        BP_NEED(2);
        const u32 b = stk[--sp];
        const u32 a = stk[sp - 1];
        stk[sp - 1] = a < b ? 0u : a - b;
        BP_NEXT;
      }
      L_EQ: {
        BP_NEED(2);
        const u32 b = stk[--sp];
        stk[sp - 1] = (u32) (stk[sp - 1] == b);
        BP_NEXT;
      }
      L_LT: {
        BP_NEED(2);
        const u32 b = stk[--sp];
        stk[sp - 1] = (u32) (stk[sp - 1] < b);
        BP_NEXT;
      }
      L_JMP: {
        const u32 t = code[pc];
        if (t < pc) BP_FUEL_CHECK;      // backward: a loop
        pc = t;
        BP_NEXT;
      }
      L_JZ: {
        BP_NEED(1);
        const u32 t = code[pc];
        if (stk[--sp] == 0u) {
          if (t < pc) BP_FUEL_CHECK;
          pc = t;
        } else pc += 1;
        BP_NEXT;
      }
      L_JNZ: {
        BP_NEED(1);
        const u32 t = code[pc];
        if (stk[--sp] != 0u) {
          if (t < pc) BP_FUEL_CHECK;
          pc = t;
        } else pc += 1;
        BP_NEXT;
      }
      L_XJMP:                           // to another chunk: fuel always
        BP_FUEL_CHECK;
        BP_GOCHUNK(BP_IMM16, code[pc + 2]);
        BP_NEXT;
      L_XJZ:
        BP_NEED(1);
        if (stk[--sp] == 0u) {
          BP_FUEL_CHECK;
          BP_GOCHUNK(BP_IMM16, code[pc + 2]);
        } else pc += 3;
        BP_NEXT;
      L_XJNZ:
        BP_NEED(1);
        if (stk[--sp] != 0u) {
          BP_FUEL_CHECK;
          BP_GOCHUNK(BP_IMM16, code[pc + 2]);
        } else pc += 3;
        BP_NEXT;
      L_CALL: {
        BP_FUEL_CHECK;                  // recursion is a loop too
        if (cp >= CALL_SIZE) env.fault(BF_CALL_OVER);
        Call & c = mCalls[cp++];
        c.mChunk = (u16) chunk;
        c.mReturn = (u8) (pc + 2u);
        c.mBase = (u8) fb;
        c.mTop = (u8) ft;
        BP_GOCHUNK(funcChunk[BP_IMM16], 0u);
        BP_NEXT;
      }
      L_RET: {
        if (cp == 0) env.fault(BF_CALL_UNDER);
        const Call & c = mCalls[--cp];
        fb = c.mBase;
        ft = c.mTop;
        BP_GOCHUNK(c.mChunk, c.mReturn);  // the callee may have evicted it
        BP_NEXT;
      }
      L_GETF: {
        const u8 f = code[pc++];
        BP_NEED(1);
        const u32 sn = stk[sp - 1];
        if (sn >= SITES) env.fault(BF_SITE);
        const P4Atom & a = env.atom(sn);
        stk[sp - 1] = f == F_TYPE ? a.getType()
                    : f == F_DATA0 ? (u32) a.mData0
                    : f == F_STG0 ? a.mStg[0]
                    : a.mStg[1];
        BP_NEXT;
      }
      L_SETF: {
        const u8 f = code[pc++];
        BP_NEED(2);
        const u32 v = stk[--sp];
        const u32 sn = stk[--sp];
        if (sn >= SITES) env.fault(BF_SITE);
        P4Atom & a = env.atom(sn);
        if (f == F_DATA0) a.mData0 = (u16) v;
        else if (f == F_STG0) a.mStg[0] = v;
        else a.mStg[1] = v;
        BP_NEXT;
      }
      L_COPY: {
        BP_NEED(2);
        const u32 dst = stk[--sp];
        const u32 src = stk[--sp];
        if (src >= SITES || dst >= SITES) env.fault(BF_SITE);
        env.atom(dst) = env.atom(src);
        BP_NEXT;
      }
      L_EMPTY: {
        BP_NEED(1);
        const u32 sn = stk[--sp];
        if (sn >= SITES) env.fault(BF_SITE);
        env.atom(sn) = P4Atom::makeEmptyAtom();
        BP_NEXT;
      }
      L_BETWEEN: {
        BP_NEED(2);
        const u32 hi = stk[--sp];
        const u32 lo = stk[sp - 1];
        stk[sp - 1] = env.between(lo, hi);
        BP_NEXT;
      }
      L_ONEIN:
        BP_NEED(1);
        stk[sp - 1] = env.oneIn(stk[sp - 1]) ? 1u : 0u;
        BP_NEXT;
      L_ENTER: {
        const u32 nargs = code[pc];
        const u32 nlocals = code[pc + 1];
        pc += 2;
        if (ft + nlocals > LOCALS_SIZE) env.fault(BF_FRAME);
        BP_NEED(nargs);
        fb = ft;
        ft = fb + nlocals;
        sp -= nargs;
        for (u32 i = 0; i < nargs; ++i) mLocals[fb + i] = stk[sp + i];
        for (u32 i = nargs; i < nlocals; ++i) mLocals[fb + i] = 0u;
        BP_NEXT;
      }
      L_LOADL: {
        const u32 n = code[pc++];
        if (fb + n >= ft) env.fault(BF_LOCAL);
        BP_ROOM(1);
        stk[sp++] = mLocals[fb + n];
        BP_NEXT;
      }
      L_STOREL: {
        const u32 n = code[pc++];
        if (fb + n >= ft) env.fault(BF_LOCAL);
        BP_NEED(1);
        mLocals[fb + n] = stk[--sp];
        BP_NEXT;
      }
      L_GETM: {
        const u32 id = BP_IMM16;
        pc += 2;
        BP_NEED(1);
        const u32 sn = stk[sp - 1];
        if (sn >= SITES) env.fault(BF_SITE);
        P4Atom & a = env.atom(sn);
        const MemberAt & at = memberAt(*layout, id, a.getType(), env);
        stk[sp - 1] = getBits(a, at.mPos, at.mLen);
        BP_NEXT;
      }
      L_SETM: {
        const u32 id = BP_IMM16;
        pc += 2;
        BP_NEED(2);
        const u32 v = stk[--sp];
        const u32 sn = stk[--sp];
        if (sn >= SITES) env.fault(BF_SITE);
        P4Atom & a = env.atom(sn);
        const MemberAt & at = memberAt(*layout, id, a.getType(), env);
        const u32 max = lenMask(at.mLen);
        setBits(a, at.mPos, at.mLen, v > max ? max : v);
        BP_NEXT;
      }
#undef BP_NEED
#undef BP_ROOM
#undef BP_IMM16
#undef BP_GOCHUNK
#undef BP_NEXT
#undef BP_FUEL_CHECK
      }

      /* Where element type `type` keeps member id (BF_TYPE if it has
         none); id < the layout's mRefCount (verified). */
      template <class Env>
      static const MemberAt & memberAt(const Layout & lay, u32 id,
                                       u32 type, Env & env) {
        const MemberRef & r = lay.mRefs[id];
        for (u32 i = 0; i < r.mCount; ++i) {
          const MemberAt & c = lay.mAts[r.mFirst + i];
          if (c.mType == type) return c;
        }
        env.fault(BF_TYPE);
      }
    };

    /* Immediate bytes after opcode op, or -1 if op is no opcode. */
    constexpr int immBytes(u32 op) {
      return op == OP_PUSH8 || op == OP_GETF || op == OP_SETF ||
             op == OP_LOADL || op == OP_STOREL || op == OP_JMP ||
             op == OP_JZ || op == OP_JNZ ? 1
           : op == OP_PUSH32 ? 4
           : op == OP_XJMP || op == OP_XJZ || op == OP_XJNZ ? 3
           : op == OP_CALL || op == OP_GETM || op == OP_SETM ||
             op == OP_ENTER ? 2
           : op < OP_COUNT ? 0
           : -1;
    }

    /* Bytes in chunk c of an opened image. */
    inline u32 chunkLength(const BCPView & img, u32 c) {
      return (u32) img.mChunkStart[c + 1u] - (u32) img.mChunkStart[c];
    }

    /* The entry function of element type `type`, or NO_FUNC. */
    inline u32 entryFor(const BCPView & img, u32 type) {
      const u32 i = type - img.mFirstType;      // wraps below the first
      return i < img.mTypeCount ? (u32) img.mTypeFunc[i] : (u32) NO_FUNC;
    }

    /* The verifier works on the raw blob, byte by byte (constexpr). */
    struct BCPBytes {
      const u8 * b;
      u32 code;                         //< offset of the code
      u32 starts;                       //< offset of the chunk starts
      constexpr u32 start(u32 c) const { return bcpRd16(b, starts + 2u * c); }
      constexpr u32 length(u32 c) const { return start(c + 1u) - start(c); }
      constexpr const u8 * chunk(u32 c) const { return b + code + start(c); }
    };

    /* Does an instruction start at offset t of chunk c? */
    constexpr bool bcpInsnStart(const BCPBytes & x, u32 c, u32 t) {
      const u8 * code = x.chunk(c);
      u32 pc = 0;
      while (pc < t) {
        const int n = immBytes(code[pc]);
        if (n < 0) return false;
        pc += 1u + (u32) n;
      }
      return pc == t;
    }

    /* Is the table of n entries of w bytes at offset off inside the blob,
       after the header and 4-aligned? */
    constexpr bool bcpTableOk(u32 off, u32 n, u32 w, u32 size) {
      return off >= BCP_HEADER_BYTES && off % 4u == 0u && off <= size &&
        n * w <= size - off;
    }

    /* 0 if b[0..size) is a BCP blob Machine::run may run any function
       of, else what is wrong.  The blob: BCP_BAD_SIZE, _MAGIC, _VERSION
       (format or opcode count), _CHECKSUM, _TABLE (an offset, count or
       chunk start out of bounds), _LAYOUT (a member ref past the ats, an
       at past the state bits or 0 or over 32 bits wide), _TYPES (a type
       naming no function).  The code, as the Fault it would cause: no
       chunk or function, chunk starts out of order or a chunk empty or
       over CHUNK_MAX, a function or call naming no function, an unknown
       opcode (BF_OPCODE), an immediate past its chunk's end, a jump
       target off its chunk's end or inside an instruction, a cross-chunk
       jump to no chunk, or a chunk whose last instruction is not
       halt0/halt1/jmp/xjmp/ret, so control could run off it (BF_PC; a
       call needs a next instruction to return to), a bad or read-only
       field (BF_FIELD), a member id past the layout (BF_MEMBER), enter
       with nargs > nlocals or more than all the local slots (BF_FRAME).
       constexpr: bpasm's headers static_assert it for every blob. */
    constexpr u32 verifyBCP(const u8 * b, u32 size) {
      if (size < BCP_HEADER_BYTES || size > BCP_MAX_BYTES || size % 4u)
        return BCP_BAD_SIZE;
      if (bcpRd32(b, BCPH_MAGIC) != BCP_MAGIC) return BCP_BAD_MAGIC;
      if (bcpRd16(b, BCPH_VERSION) != BCP_VERSION ||
          bcpRd16(b, BCPH_OPCOUNT) != (u32) OP_COUNT)
        return BCP_BAD_VERSION;
      if (bcpRd32(b, BCPH_SIZE) != size) return BCP_BAD_SIZE;
      if (bcpRd32(b, BCPH_CHECKSUM) != bcpChecksum(b, size))
        return BCP_BAD_CHECKSUM;
      for (u32 r = 0; r < 3u; ++r)
        if (bcpRd32(b, BCPH_RESERVED + 4u * r) != 0u) return BCP_BAD_TABLE;
      const u32 chunks = bcpRd16(b, BCPH_CHUNKS);
      const u32 funcs = bcpRd16(b, BCPH_FUNCS);
      const u32 nrefs = bcpRd16(b, BCPH_REFS);
      const u32 nats = bcpRd16(b, BCPH_ATS);
      const u32 ntypes = bcpRd16(b, BCPH_TYPES);
      const u32 ocode = bcpRd32(b, BCPH_OFF_CODE);
      const u32 ostarts = bcpRd32(b, BCPH_OFF_CHUNK_START);
      const u32 ofuncs = bcpRd32(b, BCPH_OFF_FUNC_CHUNK);
      const u32 orefs = bcpRd32(b, BCPH_OFF_REFS);
      const u32 oats = bcpRd32(b, BCPH_OFF_ATS);
      const u32 otypes = bcpRd32(b, BCPH_OFF_TYPE_FUNC);
      if (chunks == 0u || funcs == 0u) return BF_PC;
      if (!bcpTableOk(ostarts, chunks + 1u, 2u, size) ||
          !bcpTableOk(ofuncs, funcs, 2u, size) ||
          !bcpTableOk(orefs, nrefs, 4u, size) ||
          !bcpTableOk(oats, nats, 4u, size) ||
          !bcpTableOk(otypes, ntypes, 2u, size))
        return BCP_BAD_TABLE;
      const BCPBytes x{b, ocode, ostarts};
      if (!bcpTableOk(ocode, x.start(chunks), 1u, size))
        return BCP_BAD_TABLE;
      if (x.start(0) != 0u) return BF_PC;
      for (u32 c = 0; c < chunks; ++c) {
        if (x.start(c + 1u) <= x.start(c)) return BF_PC;
        if (x.length(c) > CHUNK_MAX) return BF_PC;
      }
      for (u32 f = 0; f < funcs; ++f)
        if (bcpRd16(b, ofuncs + 2u * f) >= chunks) return BF_PC;
      for (u32 r = 0; r < nrefs; ++r) {
        const u32 first = bcpRd16(b, orefs + 4u * r);
        const u32 count = bcpRd16(b, orefs + 4u * r + 2u);
        if (first + count > nats) return BCP_BAD_LAYOUT;
      }
      for (u32 a = 0; a < nats; ++a) {
        const u32 pos = b[oats + 4u * a + 2u];
        const u32 len = b[oats + 4u * a + 3u];
        if (len == 0u || len > 32u || pos + len > STATE_BITS)
          return BCP_BAD_LAYOUT;
      }
      for (u32 t = 0; t < ntypes; ++t) {
        const u32 f = bcpRd16(b, otypes + 2u * t);
        if (f != NO_FUNC && f >= funcs) return BCP_BAD_TYPES;
      }
      for (u32 c = 0; c < chunks; ++c) {
        const u8 * code = x.chunk(c);
        const u32 len = x.length(c);
        u32 pc = 0;
        while (pc < len) {
          const u32 op = code[pc];
          const int n = immBytes(op);
          if (n < 0) return BF_OPCODE;
          const u32 next = pc + 1u + (u32) n;
          if (next > len) return BF_PC;
          const u32 i0 = n > 0 ? code[pc + 1] : 0u;
          const u32 i1 = n > 1 ? code[pc + 2] : 0u;
          const u32 i2 = n > 2 ? code[pc + 3] : 0u;
          if (op == OP_JMP || op == OP_JZ || op == OP_JNZ) {
            if (i0 >= len || !bcpInsnStart(x, c, i0)) return BF_PC;
          } else if (op == OP_XJMP || op == OP_XJZ || op == OP_XJNZ) {
            const u32 c2 = i0 | (i1 << 8);
            if (c2 >= chunks || i2 >= x.length(c2) ||
                !bcpInsnStart(x, c2, i2)) return BF_PC;
          } else if (op == OP_CALL) {
            if ((i0 | (i1 << 8)) >= funcs) return BF_PC;
          } else if (op == OP_GETF) {
            if (i0 >= F_COUNT) return BF_FIELD;
          } else if (op == OP_SETF) {
            if (i0 == F_TYPE || i0 >= F_COUNT) return BF_FIELD;
          } else if (op == OP_GETM || op == OP_SETM) {
            if ((i0 | (i1 << 8)) >= nrefs) return BF_MEMBER;
          } else if (op == OP_ENTER) {
            if (i0 > i1 || i1 > Machine::LOCALS_SIZE) return BF_FRAME;
          }
          const bool ends = op == OP_HALT0 || op == OP_HALT1 ||
            op == OP_JMP || op == OP_XJMP || op == OP_RET;
          if (!ends && next >= len) return BF_PC;
          pc = next;
        }
      }
      return 0u;
    }

    /* Fill v from blob b WITHOUT checking it: for openBCP (below), and
       for tests that must run unverified bytes. */
    inline void viewBCP(const u8 * b, BCPView & v) {
      v.mBase = b;
      v.mCode = b + bcpRd32(b, BCPH_OFF_CODE);
      v.mChunkStart = (const U16A *) (b + bcpRd32(b, BCPH_OFF_CHUNK_START));
      v.mFuncChunk = (const U16A *) (b + bcpRd32(b, BCPH_OFF_FUNC_CHUNK));
      v.mTypeFunc = (const U16A *) (b + bcpRd32(b, BCPH_OFF_TYPE_FUNC));
      v.mLayout.mRefs = (const MemberRef *) (b + bcpRd32(b, BCPH_OFF_REFS));
      v.mLayout.mRefCount = (u16) bcpRd16(b, BCPH_REFS);
      v.mLayout.mAts = (const MemberAt *) (b + bcpRd32(b, BCPH_OFF_ATS));
      v.mChunkCount = (u16) bcpRd16(b, BCPH_CHUNKS);
      v.mFuncCount = (u16) bcpRd16(b, BCPH_FUNCS);
      v.mFirstType = (u16) bcpRd16(b, BCPH_FIRST_TYPE);
      v.mTypeCount = (u16) bcpRd16(b, BCPH_TYPES);
    }

    /* Open a BCP blob of at most room bytes (room: what is left where
       it lies; the size comes from its header): verifyBCP, then fill v.
       0 or verifyBCP's answer (v untouched then).  b must be 4-aligned. */
    inline u32 openBCP(const u8 * b, u32 room, BCPView & v) {
      if (room < BCP_HEADER_BYTES || ((__UINTPTR_TYPE__) b & 3u) != 0u)
        return BCP_BAD_SIZE;
      const u32 size = bcpRd32(b, BCPH_SIZE);
      if (size > room) return BCP_BAD_SIZE;
      if (const u32 bad = verifyBCP(b, size)) return bad;
      viewBCP(b, v);
      return 0u;
    }
  }
}
