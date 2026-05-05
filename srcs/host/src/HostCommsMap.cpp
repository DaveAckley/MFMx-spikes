#include "HostCommsMap.h"
#include "SimConstants.h" // for MIN_GTEED_HOST_RAM_PER_BH
#include "OurTLBs.h" // for AHAX_TLBI_L1_LAST_UNI

namespace MFM {
  void HostCommsMap::requireCommBlockForBlockCode(BlockCode b) {
    MFM_API_ASSERT(b > 0 && b < BlockCode::BC_BLOCKCODE_COUNT, ILLEGAL_ARGUMENT);

    MFM_API_ASSERT(mHostRAMChunkOffsets[b] == U8_MAX, DUPLICATE_ENTRY);

    u32 size = getStorageSizeFromBlockCode(b);
    MFM_API_ASSERT(size <= U8_MAX * HOST_COMMS_MAP_CHUNK_SIZE, OUT_OF_RESOURCES);

    u32 thisChunks = (size + HOST_COMMS_MAP_CHUNK_SIZE - 1u)/HOST_COMMS_MAP_CHUNK_SIZE;
    u32 totalChunks = thisChunks + mNextFreeChunk;
    MFM_API_ASSERT(totalChunks*HOST_COMMS_MAP_CHUNK_SIZE <= MIN_GTEED_HOST_RAM_PER_BH, OUT_OF_ROOM);
    MFM_API_ASSERT(totalChunks < U8_MAX, ARRAY_INDEX_OUT_OF_BOUNDS);
      
    mHostRAMChunkOffsets[b] = mNextFreeChunk;
    mNextFreeChunk += thisChunks;
    HTprintf("rCBFBC hcm=%p, bc=%u, sz=%uB, offset=%uch, size=%uch, tot=%uch/%uB\n",
             (void*) this,
             b,
             size,
             mHostRAMChunkOffsets[b],
             thisChunks,
             mNextFreeChunk,
             mNextFreeChunk*HOST_COMMS_MAP_CHUNK_SIZE);
  }

  std::string HostCommsMap::to_repr() const {
    std::string ret = "<HostCommsMap:";
    ret.append(getName()=="" ? "<UNINIT>" : getName());
    bool first = true;
    for (u32 i = 1u; i < sizeof(mHostRAMChunkOffsets); ++i) {
      if (mHostRAMChunkOffsets[i] != U8_MAX) {
        ret.append(first ? "+" : ",");
        first = false;
        ret.append(getNameFromBlockCode((BlockCode) i));
        u32 at = mHostRAMChunkOffsets[i]*HOST_COMMS_MAP_CHUNK_SIZE;
        ret.append("@0x");
        ret.append(toHex(at));
      }
    }
    ret.append(">");
    return ret;
  }

  bool HostCommsMap::configureT6ImageForHostComms(T6Image & t6i) const {
    // for each IBA after t6i ImageBlockHeader
    //  check if iba.mBlockCode is allocate in mHostRAMChunkOffsets
    //  set iba.mHostChunkOffsetOpt to offset, if so, else to 255
    ImageBlockHeader ibh = t6i.getImageBlockHeader();
    MFM_API_ASSERT(ibh.isValid(),ILLEGAL_STATE);
    u32 ec = ibh.getEntriesCount();
    for (u32 e = 0u; e < ec; ++e) {
      ImageBlockAddr * piba = t6i.getImageBlockAddrAtIndexIfAny(e);
      MFM_API_ASSERT_NONNULL(piba);
      BlockCode bc = (BlockCode) piba->getBlockCode();
      u8 chunkOffset = getHostRAMChunkOffset(bc);
      piba->mHostChunkOffsetOpt = chunkOffset; // whether chunkOffset == U8_MAX or not
      Eprintf("IM: BC %s(%d) @ 0x%02x == 0x%x\n",
              getNameFromBlockCode(bc), bc,
              chunkOffset,
              chunkOffset == U8_MAX ? 0u : chunkOffset * HOST_COMMS_MAP_CHUNK_SIZE);
    }

    return true;
  }

  void * HostCommsMap::getHostBlockAddress(u8 tlbi, BlockCode b) const {
    /*
    XXXX write me
      sanity check args and state
      void * pert6base = compute mHostBaseAddress + tlbi * hostbuffersize
      u32 offset = host offset for blockcode b
      return pert6base + offset
    or something like that.                                               
    */    
    MFM_API_ASSERT(tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI, ILLEGAL_ARGUMENT);
    MFM_API_ASSERT(b < BlockCode::BC_BLOCKCODE_COUNT, ILLEGAL_ARGUMENT);
    MFM_API_ASSERT(mHostMemoryBaseAddress != 0u, ILLEGAL_STATE);

    char * pert6base = ((char*)mHostMemoryBaseAddress) + tlbi * HOST_RAM_PER_BH;
    u8 chunkoff = getHostRAMChunkOffset(b);
    MFM_API_ASSERT(chunkoff < U8_MAX,ILLEGAL_STATE);
    return (void*) (pert6base + chunkoff * HOST_COMMS_MAP_CHUNK_SIZE);
  }

}
