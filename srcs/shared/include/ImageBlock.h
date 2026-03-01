#ifndef IMAGEBLOCK_H  /* -*- C++ -*- */
#define IMAGEBLOCK_H

#include "itype.h"
#include <string>
#include "S8C.h"

#include "HostBlock.h"
#include "BlockCode.h"

namespace MFM {
  extern void memset_s(void*, u8, u32) ;

  struct ImageBlockAddr {
    static constexpr u8 IBA_MAGIC = 0xba;
    u8 mIBAMagic;
    u8 mBlockCode;
    u8 mArrayLength;
    u8 mHostChunkOffsetOpt;     // if this != 255, host maps iba blockcode to noc + this*64
    u32 mBlockAddr;

    bool isValid() const {
      return
        mIBAMagic == IBA_MAGIC &&
        mBlockCode != 0u;
    }

    static ImageBlockAddr make(u8 type, u8 len, u32 addr) {
      ImageBlockAddr iba;
      iba.init(type,len,addr);
      return iba;
    }
    void reset() { memset_s(this,'\0',sizeof(*this)); }
    
    void init(u8 blockcode, u8 len, u32 addr) {
      mIBAMagic = IBA_MAGIC;
      mBlockCode = blockcode;
      mArrayLength = len;
      mHostChunkOffsetOpt = U8_MAX; //< assume no host/ mapping
      mBlockAddr = addr;
    }

    bool goodMagic() const { return mIBAMagic == IBA_MAGIC; }

    u8 getBlockCode() const { return mBlockCode; }

    u32 getBlockAddr() const { return mBlockAddr; }

    u32 getHostChunkOffsetOpt() const { return mHostChunkOffsetOpt; }

    u32 getArrayLength() const { return mArrayLength; }

    const char *to_repr() const {
      static constexpr u32 BUF_SIZ = 100;
      static char buf[BUF_SIZ];
      const ImageBlockAddr & iba = *this;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
      if (iba.goodMagic()) 
        snprintf(buf,BUF_SIZ,
                 "<ImageBlockAddr:bc=%u,al=%u,hc=%u/0x%x,ad=0x%x>",
                 iba.getBlockCode(),
                 iba.getArrayLength(),
                 iba.getHostChunkOffsetOpt(),
                 iba.getHostChunkOffsetOpt() == U8_MAX ? 0u :
                 HOST_COMMS_MAP_CHUNK_SIZE*iba.getHostChunkOffsetOpt(),
                 iba.getBlockAddr());
      else
        snprintf(buf,BUF_SIZ,
                 "<ImageBlockAddr:invalid(0x%x),%u,%u,0x%x>",
                 iba.mIBAMagic,
                 iba.getBlockCode(),
                 iba.getArrayLength(),
                 iba.getBlockAddr());
#pragma GCC diagnostic pop
      return buf;
    }
  };

  struct ImageBlockHeader {
    static const u8 IBMAGIC = 0x1b; // 
    //    static const u8 IBCIGAM = 0xb2; //

    void init(const u32 * words) {
      init((const char *) words);
    }

    void init(const char * littleendian) {
      mIBMagic = littleendian[0];
      mImageCode = littleendian[1];
      mEntries = littleendian[2];
      mIBCheck = littleendian[3];
    }
    
    //// DATA MEMBERS
    u8 mIBMagic; 
    u8 mImageCode;              // 0x01..0xff
    u8 mEntries;                // # of following ImageBlockAddrs
    u8 mIBCheck;                // mImageCode^(mEntries<<2u)
    
    //// METHODS
    u8 checkbyte() const { return (mImageCode^(mEntries<<2u)); }

    bool isValid() const {
      return
        mIBMagic == IBMAGIC &&
        checkbyte() == mIBCheck;
    }

    void init(u8 imageCode, u8 entries) {
      mIBMagic = IBMAGIC;
      mImageCode = imageCode;
      mEntries = entries;
      mIBCheck = checkbyte();
    }

    u8 getImageCode() const {
      if (!isValid()) return 0u;
      return mImageCode;
    }

    u8 getEntriesCount() const {
      if (!isValid()) return 0u;
      return mEntries;
    }

    ImageBlockAddr findIBAIfAny(u8 blockcode) const {
      ImageBlockAddr ret;
      ret.reset();
      u8 ec = getEntriesCount();
      for (u8 i = 0u; i < ec; ++i) {
        const ImageBlockAddr *piba = getImageBlockAddressIfAny(i);
        if (piba) {
          ImageBlockAddr iba = *piba;
          if (iba.mBlockCode == blockcode) {
            ret = iba;
            break;
          }
        }
      }
      return ret;
    }

    const ImageBlockAddr * getImageBlockAddressIfAny(u8 entrynum) const {
      if (!isValid() || entrynum >= getEntriesCount()) return 0;
      const ImageBlockAddr * firstiba = (const ImageBlockAddr*) (((const u8*) this)+sizeof(*this));
      return firstiba + entrynum;
    }

  };

  template<unsigned COUNT> struct ImageBlockT : public ImageBlockHeader {
    ImageBlockAddr mBlocks[COUNT];
  };

}

#endif /*IMAGEBLOCK_H*/
