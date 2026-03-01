#pragma once /* -*- C++ -*- */
#include "BlockCode.h"
#include "HostBlock.h" // for HOST_COMMS_MAP_CHUNK_SIZE 
#include "T6Image.h" 
#include "CommsModule.h" 

namespace MFM {
  struct HostCommsMap {
    std::string mName;
    u8 mNextFreeChunk;
    u8 mHostRAMChunkOffsets[BlockCode::BC_BLOCKCODE_COUNT];
    void * mHostMemoryBaseAddress;

    std::string getName() const { return mName; }
    std::string to_repr() const ;

    void init(std::string modname) {
      mName = modname;
      mNextFreeChunk = 0u;
      memset_s(mHostRAMChunkOffsets,U8_MAX,sizeof(mHostRAMChunkOffsets));
      mHostMemoryBaseAddress = 0u;
    }

    void setHostMemoryBaseAddress(void * hostbase) {
      MFM_API_ASSERT(hostbase, ILLEGAL_ARGUMENT);
      if (!mHostMemoryBaseAddress)
        mHostMemoryBaseAddress = hostbase;
      else
        MFM_API_ASSERT(mHostMemoryBaseAddress == hostbase, ILLEGAL_STATE);
    }

    void requireCommBlockNamed(std::string blockname) {
      BlockCode b = getBlockCodeFromName(blockname.c_str());
      requireCommBlockForBlockCode(b);
    }

    void requireCommBlockForBlockCode(BlockCode b) ;

    bool isBlockCodeMapped(BlockCode b) const {
      MFM_API_ASSERT(b > 0 && b < BlockCode::BC_BLOCKCODE_COUNT, ILLEGAL_ARGUMENT);
      return mHostRAMChunkOffsets[b] != U8_MAX;
    }

    u32 getRequiredHostRAMBytes() const {
      return mNextFreeChunk * HOST_COMMS_MAP_CHUNK_SIZE;
    }
    
    u8 getHostRAMChunkOffset(BlockCode b) const {
      if (b == 0 || b >= BlockCode::BC_BLOCKCODE_COUNT) return U8_MAX;
      return mHostRAMChunkOffsets[b];
    }

    void * getHostBlockAddress(u8 tlbi, BlockCode b) const ;

    bool configureT6ImageForHostComms(T6Image & t6i) const ;

    void addCommsModule(CommsModule & cm) {
      for (u8 b = 1u; b < BlockCode::BC_BLOCKCODE_COUNT; ++b) {
        BlockCode bc = (BlockCode) b;
        if (cm.isBlockCodeRequired(bc) &&
            !isBlockCodeMapped(bc))
          requireCommBlockForBlockCode(bc);
      }
    }

  };
}
