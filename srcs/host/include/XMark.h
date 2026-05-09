#pragma once  /* -*- C++ -*- */
//#include "PT_LogBlock.h"
#include "UxC.h"
#include <string>
#include <iostream>
#include "XUtils.h" // for memset_s

namespace MFM {
  struct XMark {              //< eXpandedMark
    u64 mTickStamp;
    U16C mFidLin;
    U8C mNoC;
    u8 mChipNum;
    u8 mHartNum;
    u8 mCmd;
    bool mValid;
    std::string mMsg;
    bool isValid() { return mValid; }
    void reset() { memset_s(this,'\0',sizeof(*this)); }

    /** leaves the stream mangled on \return false !*/
    bool parseFromIStream(std::istream& is, U8C noc0, u8 chipnum, u32 ticksbase) ;
    void formatToOStream(std::ostream& os) ;
  };
}
