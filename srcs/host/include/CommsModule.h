#pragma once   /* -*- C++ -*- */
#include "itype.h"
#include <string>

#include "HostUtils.h"
#include "BlockCode.h"
#include "Fail.h"

namespace MFM {
  struct CommsModule {
    // EP INTERFACE
    const char * getNameImpl() { return mName.c_str(); }
    
    std::string mName;
    bool mBlocksRequired[BlockCode::BC_BLOCKCODE_COUNT];
    std::string getName() const { return mName; }
    std::string to_repr() const {
      std::string ret = "<CommsModule:";
      ret.append(getName());
      bool first = true;
      for (u32 i = 0u; i < sizeof(mBlocksRequired)/sizeof(mBlocksRequired[0]); ++i) {
        if (mBlocksRequired[i]) {
          ret.append(first ? "+" : ",");
          first = false;
          ret.append(getNameFromBlockCode((BlockCode) i));
        }
      }
      ret.append(">");
      return ret;
    }
    void init(std::string modname) {
      mName = modname;
      memset_s(mBlocksRequired,0u,sizeof(mBlocksRequired));
    }

    void requireCommBlockNamed(std::string blockname) {
      BlockCode b = getBlockCodeFromName(blockname.c_str());
      requireCommBlockForBlockCode(b);
    }

    void requireCommBlockForBlockCode(BlockCode b) {
      MFM_API_ASSERT(b > 0 && b < BlockCode::BC_BLOCKCODE_COUNT, ILLEGAL_ARGUMENT);

      MFM_API_ASSERT(!mBlocksRequired[b], DUPLICATE_ENTRY);

      mBlocksRequired[b] = true;
    }

    bool isBlockCodeRequired(BlockCode b) {
      MFM_API_ASSERT(b > 0 && b < BlockCode::BC_BLOCKCODE_COUNT, ILLEGAL_ARGUMENT);
      return mBlocksRequired[b];
    }
  };
}
