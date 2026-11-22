#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "UxC.h"

namespace MFM {

  /** The clocks of this chip as the ARC tile (the Blackhole System
      Management Controller) reports them in its telemetry table,
      read by a T6 over the NoC (hart NC only: NRI3 is NC's).  Values
      are the raw telemetry words; the low 16 bits are MHz.  The ARC
      is only READ.  The read sequence was proven on hardware by
      srcs/host/tools/t6pp/t6arc4.c (ERISC_NOTES 9.19), including the
      rule that the NoC read return buffer must match the source
      address MODULO 64, which NRI3::blockingL1ReadNoC0 does not do. */
  struct ArcClocks {
    u32 mStatus;     //< 0 ok, 1 not read yet, 2 bad pointer, 3 read failed,
                     //< 4 bad table, 5 tag missing
    u32 mAiclk;      //< telemetry tag 14 (AICLK), raw
    u32 mAxiclk;     //< tag 15
    u32 mArcclk;     //< tag 16
    u32 mLimit;      //< tag 63 (AICLK_LIMIT_MAX)
    u32 mReads;      //< NoC reads done
  };

  extern ArcClocks theArcClocks;

  /** Walk the ARC telemetry table once and fill theArcClocks.  Hart
      NC only.  Never fatal: a failed read leaves a status. */
  void readArcClocksNC(U8C usnoc0) ;

  /** "STATUS:AICLK:AXICLK:ARCCLK:LIMIT." in hex (MHz in the low 16
      bits of each clock); the final '.' is the end marker.  b needs
      at least 40 bytes. */
  const char * formatArcClocks(char * b, u32 siz, const ArcClocks & ac) ;
}
