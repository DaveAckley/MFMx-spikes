#pragma once  /* -*- C++ -*- */
/* BytePhys.h -- a tiny bytecode machine for physics (SPIKE, 2026-11-26).

   Manual: BYTEPHYS.md (repo root; keep it in step with this file).

   OPEN_ISSUES 'physics as bytecode with explicit, bounded stacks': does
   interpreting physics cost too much on a T6 baby core?  This is the
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
   A program is checked ONCE by verify() (opcodes, immediates, jump
   targets, fields, member ids, its end; bpasm's headers do it at compile
   time), then run() checks only what depends on the run: every push,
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

    /* Where one element type keeps a member. */
    struct MemberAt {
      u16 mType;
      u8 mPos;                          //< first state bit
      u8 mLen;                          //< width, 1..32
    };

    /* Member id -> its MemberAts (one per element type that has it). */
    struct MemberRef {
      u16 mFirst;                       //< index into Layout::mAts
      u16 mCount;
    };

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

    /* One physics' code (see IMAGES above): chunk c is mCode[
       mChunkStart[c] .. mChunkStart[c + 1]); function f starts at offset
       0 of chunk mFuncChunk[f]. */
    struct Image {
      const u8 * mCode;
      const u16 * mChunkStart;          //< mChunkCount + 1 entries
      u16 mChunkCount;
      const u16 * mFuncChunk;           //< mFuncCount entries
      u16 mFuncCount;
      const Layout * mLayout;           //< for GETM/SETM (may be null)
    };

    /* What to run: an image and its entry function (an element's
       behave, or a microbenchmark). */
    struct Program {
      const Image * mImage;
      u16 mFunc;
    };

    /* Code where the image lies (L1 on the EWP): the resolver run() uses
       when no cache is given. */
    struct InPlace {
      const u8 * chunk(const Image & img, u32 c) {
        return img.mCode + img.mChunkStart[c];
      }
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
         came from (each run<Env> has its own); null = no copy yet.  The
         EWP zeroes it in initB (fast RAM is not zeroed at boot); a host
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

      /* Run program p of a VERIFIED image (verify(img) == 0, below; the
         generated images are checked at compile time), so opcodes,
         immediates, jump targets, chunk and function numbers, fields,
         member ids and chunk ends need no checks here.  What only the
         run can know is still checked: stack depths, frames, sites,
         member types, and fuel, which is counted per instruction but
         compared only at backward jumps, cross-chunk jumps and calls
         (straight-line code in a chunk always reaches one, or a halt or
         a return).  res.chunk(image, c) says where chunk c's bytes are;
         it is asked at entry and at every XJMP/XJZ/XJNZ taken, CALL and
         RET, never in between. */
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
        const Image & img = *p.mImage;
        u32 chunk = img.mFuncChunk[p.mFunc];
        const u8 * code = res.chunk(img, chunk);
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
                              chunk = c_; code = res.chunk(img, c_); \
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
        BP_GOCHUNK(img.mFuncChunk[BP_IMM16], 0u);
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
        const MemberAt & at = memberAt(*img.mLayout, id, a.getType(), env);
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
        const MemberAt & at = memberAt(*img.mLayout, id, a.getType(), env);
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

    /* Bytes in chunk c. */
    constexpr u32 chunkLength(const Image & img, u32 c) {
      return (u32) img.mChunkStart[c + 1u] - (u32) img.mChunkStart[c];
    }

    /* Does an instruction start at offset t of chunk c? */
    constexpr bool isInsnStart(const Image & img, u32 c, u32 t) {
      const u8 * code = img.mCode + img.mChunkStart[c];
      u32 pc = 0;
      while (pc < t) {
        const int n = immBytes(code[pc]);
        if (n < 0) return false;
        pc += 1u + (u32) n;
      }
      return pc == t;
    }

    /* 0 if Machine::run may run any function of img, else the Fault it
       would hit: no chunk or function, chunk starts out of order or a
       chunk empty or over CHUNK_MAX, a function or call naming no
       function, an unknown opcode (BF_OPCODE), an immediate past its
       chunk's end, a jump target off its chunk's end or inside an
       instruction, a cross-chunk jump to no chunk, or a chunk whose last
       instruction is not halt0/halt1/jmp/xjmp/ret, so control could run
       off it (BF_PC; a call needs a next instruction to return to), a
       bad or read-only field (BF_FIELD), a member id with no Layout or
       past it (BF_MEMBER), enter with nargs > nlocals or more than all
       the local slots (BF_FRAME).  constexpr: bpasm's headers
       static_assert it for every image. */
    constexpr u32 verify(const Image & img) {
      if (img.mChunkCount == 0u || img.mFuncCount == 0u) return BF_PC;
      if (img.mChunkStart[0] != 0u) return BF_PC;
      for (u32 c = 0; c < img.mChunkCount; ++c) {
        if (img.mChunkStart[c + 1u] <= img.mChunkStart[c]) return BF_PC;
        if (chunkLength(img, c) > CHUNK_MAX) return BF_PC;
      }
      for (u32 f = 0; f < img.mFuncCount; ++f)
        if (img.mFuncChunk[f] >= img.mChunkCount) return BF_PC;
      for (u32 c = 0; c < img.mChunkCount; ++c) {
        const u8 * code = img.mCode + img.mChunkStart[c];
        const u32 len = chunkLength(img, c);
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
            if (i0 >= len || !isInsnStart(img, c, i0)) return BF_PC;
          } else if (op == OP_XJMP || op == OP_XJZ || op == OP_XJNZ) {
            const u32 c2 = i0 | (i1 << 8);
            if (c2 >= img.mChunkCount || i2 >= chunkLength(img, c2) ||
                !isInsnStart(img, c2, i2)) return BF_PC;
          } else if (op == OP_CALL) {
            if ((i0 | (i1 << 8)) >= img.mFuncCount) return BF_PC;
          } else if (op == OP_GETF) {
            if (i0 >= F_COUNT) return BF_FIELD;
          } else if (op == OP_SETF) {
            if (i0 == F_TYPE || i0 >= F_COUNT) return BF_FIELD;
          } else if (op == OP_GETM || op == OP_SETM) {
            if (img.mLayout == nullptr ||
                (i0 | (i1 << 8)) >= img.mLayout->mRefCount)
              return BF_MEMBER;
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
  }
}
