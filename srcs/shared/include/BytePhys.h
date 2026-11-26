#pragma once  /* -*- C++ -*- */
/* BytePhys.h -- a tiny bytecode machine for physics (SPIKE, 2026-11-26).

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
   Every push, pop, call, return, jump target, site number and field is
   checked, and every instruction burns one unit of fuel.  Any violation
   calls Env::fault(Fault), which must not return (the EWP FAILs into the
   physics RecoverPoint; the host test records it and throws).

   Env (a template parameter, so the EWP and the host test share this
   exact code) provides:
     P4Atom & atom(u32 sn)            site sn of the window, sn < 41
     u32 between(u32 lo, u32 hi)      uniform in [lo, hi]
     bool oneIn(u32 n)
     [[noreturn]] void fault(u32 f)   f is a Fault

   ENCODING.  One byte opcode, then its immediates, little-endian.  Jump
   and call targets are absolute u16 offsets into the program.  Binary
   ops pop b then a and push (a op b).  See bpasm.py for the assembler.
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
      OP_ADD,                           //< a b -> a+b (u32 wrap)
      OP_SUB,                           //< a b -> a-b (u32 wrap)
      OP_EQ,                            //< a b -> a==b
      OP_LT,                            //< a b -> a<b (unsigned)
      OP_JMP,                           //< imm16 target
      OP_JZ,                            //< imm16 target: pop, jump if 0
      OP_JNZ,                           //< imm16 target: pop, jump if != 0
      OP_CALL,                          //< imm16 target: push return
      OP_RET,                           //< pop return address
      OP_GETF,                          //< imm8 field: sn -> value
      OP_SETF,                          //< imm8 field: sn value ->
      OP_COPY,                          //< src dst -> (atom dst = atom src)
      OP_EMPTY,                         //< sn -> (atom sn = empty)
      OP_BETWEEN,                       //< lo hi -> between(lo, hi)
      OP_ONEIN,                         //< n -> oneIn(n)
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
      BF_COUNT
    };

    enum : u32 {
      SITES = 41,                       //< EventWindow::ATOM_COUNT
    };

    struct Program {
      const u8 * mCode;
      u16 mLength;
    };

    struct Machine {
      enum : u32 {
        STACK_SIZE = 16,                //< operand stack, u32 each
        CALL_SIZE = 8,                  //< return addresses
      };
      u32 mStack[STACK_SIZE];
      u16 mCalls[CALL_SIZE];
      u32 mExecuted;                    //< instructions run, last event

      template <class Env>
      bool run(const Program & p, Env & env, u32 fuel) {
        const u8 * code = p.mCode;
        const u32 len = p.mLength;
        u32 pc = 0;
        u32 sp = 0;                     // next free operand slot
        u32 cp = 0;                     // next free call slot
        u32 used = 0;

#define BP_NEED(n) do { if (sp < (n)) env.fault(BF_STACK_UNDER); } while (0)
#define BP_ROOM(n) do { if (sp + (n) > STACK_SIZE) \
                          env.fault(BF_STACK_OVER); } while (0)
#define BP_IMM(n) do { if (pc + (n) > len) env.fault(BF_PC); } while (0)

        while (true) {
          if (pc >= len) env.fault(BF_PC);
          if (++used > fuel) env.fault(BF_FUEL);
          const u8 op = code[pc++];
          switch (op) {
          case OP_HALT0:
            mExecuted = used;
            return false;
          case OP_HALT1:
            mExecuted = used;
            return true;
          case OP_PUSH8:
            BP_IMM(1);
            BP_ROOM(1);
            mStack[sp++] = code[pc++];
            break;
          case OP_PUSH32: {
            BP_IMM(4);
            BP_ROOM(1);
            const u32 v = (u32) code[pc] |
              ((u32) code[pc + 1] << 8) |
              ((u32) code[pc + 2] << 16) |
              ((u32) code[pc + 3] << 24);
            pc += 4;
            mStack[sp++] = v;
            break;
          }
          case OP_DUP:
            BP_NEED(1);
            BP_ROOM(1);
            mStack[sp] = mStack[sp - 1];
            ++sp;
            break;
          case OP_DROP:
            BP_NEED(1);
            --sp;
            break;
          case OP_SWAP: {
            BP_NEED(2);
            const u32 t = mStack[sp - 1];
            mStack[sp - 1] = mStack[sp - 2];
            mStack[sp - 2] = t;
            break;
          }
          case OP_OVER:
            BP_NEED(2);
            BP_ROOM(1);
            mStack[sp] = mStack[sp - 2];
            ++sp;
            break;
          case OP_ADD:
          case OP_SUB:
          case OP_EQ:
          case OP_LT: {
            BP_NEED(2);
            const u32 b = mStack[--sp];
            const u32 a = mStack[sp - 1];
            mStack[sp - 1] = op == OP_ADD ? a + b
                           : op == OP_SUB ? a - b
                           : op == OP_EQ ? (u32) (a == b)
                           : (u32) (a < b);
            break;
          }
          case OP_JMP:
          case OP_JZ:
          case OP_JNZ:
          case OP_CALL: {
            BP_IMM(2);
            const u32 t = (u32) code[pc] | ((u32) code[pc + 1] << 8);
            pc += 2;
            if (t >= len) env.fault(BF_PC);
            bool go = true;
            if (op == OP_JZ || op == OP_JNZ) {
              BP_NEED(1);
              const u32 v = mStack[--sp];
              go = op == OP_JZ ? v == 0u : v != 0u;
            } else if (op == OP_CALL) {
              if (cp >= CALL_SIZE) env.fault(BF_CALL_OVER);
              mCalls[cp++] = (u16) pc;
            }
            if (go) pc = t;
            break;
          }
          case OP_RET:
            if (cp == 0) env.fault(BF_CALL_UNDER);
            pc = mCalls[--cp];
            break;
          case OP_GETF: {
            BP_IMM(1);
            const u8 f = code[pc++];
            BP_NEED(1);
            const u32 sn = mStack[sp - 1];
            if (sn >= SITES) env.fault(BF_SITE);
            const P4Atom & a = env.atom(sn);
            mStack[sp - 1] = f == F_TYPE ? a.getType()
                           : f == F_DATA0 ? (u32) a.mData0
                           : f == F_STG0 ? a.mStg[0]
                           : f == F_STG1 ? a.mStg[1]
                           : (env.fault(BF_FIELD), 0u);
            break;
          }
          case OP_SETF: {
            BP_IMM(1);
            const u8 f = code[pc++];
            BP_NEED(2);
            const u32 v = mStack[--sp];
            const u32 sn = mStack[--sp];
            if (sn >= SITES) env.fault(BF_SITE);
            P4Atom & a = env.atom(sn);
            if (f == F_DATA0) a.mData0 = (u16) v;
            else if (f == F_STG0) a.mStg[0] = v;
            else if (f == F_STG1) a.mStg[1] = v;
            else env.fault(BF_FIELD);
            break;
          }
          case OP_COPY: {
            BP_NEED(2);
            const u32 dst = mStack[--sp];
            const u32 src = mStack[--sp];
            if (src >= SITES || dst >= SITES) env.fault(BF_SITE);
            env.atom(dst) = env.atom(src);
            break;
          }
          case OP_EMPTY: {
            BP_NEED(1);
            const u32 sn = mStack[--sp];
            if (sn >= SITES) env.fault(BF_SITE);
            env.atom(sn) = P4Atom::makeEmptyAtom();
            break;
          }
          case OP_BETWEEN: {
            BP_NEED(2);
            const u32 hi = mStack[--sp];
            const u32 lo = mStack[sp - 1];
            mStack[sp - 1] = env.between(lo, hi);
            break;
          }
          case OP_ONEIN:
            BP_NEED(1);
            mStack[sp - 1] = env.oneIn(mStack[sp - 1]) ? 1u : 0u;
            break;
          default:
            env.fault(BF_OPCODE);
          }
        }
#undef BP_NEED
#undef BP_ROOM
#undef BP_IMM
      }
    };
  }
}
