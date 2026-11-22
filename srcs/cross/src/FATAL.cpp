
#include "FATAL.h"
#include "itype.h"
#include "Printf.h"
#include "FastLocal.h"
#include "CrossUtils.h"
#include "HostBlock.h"
#include "ExtraConstants.h"

namespace MFM {
  extern HostBlock theHostBlock;

  /// CONFIRM ExtraConstants.h decls
  static_assert(spOffHB==offsetof(HostBlock, mAtFailRegSP),"Bad SP offset");
  static_assert(raOffHB==offsetof(HostBlock, mAtFailRegRA),"Bad RA offset");
  static_assert(fpOffHB==offsetof(HostBlock, mAtFailRegFP),"Bad FP offset");
}

/* setjmp / longjmp for the T6 RISC-V hart: ra, sp, s0..s11 (14 words, no FP
   on this core).  Same code as t6pp/t6sjlj.c, proven on a T6 BRISC (7/7
   tests, ERISC_NOTES 9.22).  No libc is linked into the images. */
__asm__(".pushsection .text,\"ax\"\n"
        ".globl recSetjmp\n.type recSetjmp,@function\n"
        "recSetjmp:\n"
        "  sw ra, 0(a0)\n  sw sp, 4(a0)\n"
        "  sw s0, 8(a0)\n  sw s1, 12(a0)\n  sw s2, 16(a0)\n  sw s3, 20(a0)\n"
        "  sw s4, 24(a0)\n  sw s5, 28(a0)\n  sw s6, 32(a0)\n  sw s7, 36(a0)\n"
        "  sw s8, 40(a0)\n  sw s9, 44(a0)\n  sw s10, 48(a0)\n  sw s11, 52(a0)\n"
        "  li a0, 0\n  ret\n"
        ".size recSetjmp,.-recSetjmp\n"
        ".globl recLongjmp\n.type recLongjmp,@function\n"
        "recLongjmp:\n"
        "  lw ra, 0(a0)\n  lw sp, 4(a0)\n"
        "  lw s0, 8(a0)\n  lw s1, 12(a0)\n  lw s2, 16(a0)\n  lw s3, 20(a0)\n"
        "  lw s4, 24(a0)\n  lw s5, 28(a0)\n  lw s6, 32(a0)\n  lw s7, 36(a0)\n"
        "  lw s8, 40(a0)\n  lw s9, 44(a0)\n  lw s10, 48(a0)\n  lw s11, 52(a0)\n"
        "  mv a0, a1\n  bnez a0, 1f\n  li a0, 1\n1: ret\n"
        ".size recLongjmp,.-recLongjmp\n"
        ".popsection\n");

/* FIRST thing on the FAIL path, before anything is written to the HostBlock:
   a recoverable failure must leave no fail record behind. */
void PrepareToDie(signed code, unsigned fileId, unsigned line) {
  using namespace MFM;
  recoverNow(fAll.mRecover, code, fileId, line);   // jumps, or returns if none
  PrepareToDieHard(code, fileId, line);
}

void PrepareToDieHard(signed code, unsigned fileId, unsigned line) {
  if (fileId != 0u) {
    using namespace MFM;
    HostBlock & hb = theHostBlock;
    u8 hart = fAll.mHartNum;
    hb.mPerHartFailFileID[hart] = (u16) fileId;
    hb.mPerHartFailFileLine[hart] = (u16) line;
    hb.mAtFailRegSP = getRegisterSP();
    hb.mAtFailRegRA = getRegisterRA();
    hb.mAtFailRegFP = getRegisterFP();
  }
  t6hangprep(code);
}

/*
extern "C" void bhang(void) {
  __asm__ __volatile__("1: j 1b" ::: "memory");
}
*/
