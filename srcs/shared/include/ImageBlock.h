#ifndef IMAGEBLOCK_H  /* -*- C++ -*- */
#define IMAGEBLOCK_H

#include "itype.h"
#include <string>
#include "S8C.h"

namespace MFM {
  struct ImageBlockAddr {
    static constexpr u8 IBA_MAGIC = 0xba;
    u8 mIBAMagic;
    u8 mBlockType;
    u8 mArrayLength;
    u8 mFlagsOrSomeShit;
    u32 mBlockAddr;

    static ImageBlockAddr make(u8 type, u8 len, u32 addr) {
      ImageBlockAddr iba;
      iba.init(type,len,addr);
      return iba;
    }
    void init(u8 type, u8 len, u32 addr) {
      mIBAMagic = IBA_MAGIC;
      mBlockType = type;
      mArrayLength = len;
      mFlagsOrSomeShit = 0;
      mBlockAddr = addr;
    }

    bool goodMagic() const { return mIBAMagic == IBA_MAGIC; }

    u8 getBlockType() const { return mBlockType; }

    u32 getBlockAddr() const { return mBlockAddr; }

    u32 getArrayLength() const { return mArrayLength; }
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
      mImageEdoc = littleendian[2];
      mEntries = littleendian[3];
    }
    
    //// DATA MEMBERS
    u8 mIBMagic; 
    u8 mImageCode;              // 0x01..0xff
    u8 mImageEdoc;              // mImageCode^0xff
    u8 mEntries;                // # of following ImageBlockAddrs
    
    //// METHODS
    bool isValid() const {
      return
        mIBMagic == IBMAGIC &&
        mImageCode == (mImageEdoc^0xff);
    }

    void init(u8 imageCode, u8 entries) {
      mIBMagic = IBMAGIC;
      mImageCode = imageCode;
      mImageEdoc = mImageCode^0xff;
      mEntries = entries;
    }

    u8 getImageCode() const {
      if (!isValid()) return 0u;
      return mImageCode;
    }

    u8 getEntriesCount() const {
      if (!isValid()) return 0u;
      return mEntries;
    }
  };

  template<unsigned COUNT> struct ImageBlockT : public ImageBlockHeader {
    ImageBlockAddr mBlocks[COUNT];
  };

  //  extern ImageBlock theImageBlock;
}

#endif /*IMAGEBLOCK_H*/
