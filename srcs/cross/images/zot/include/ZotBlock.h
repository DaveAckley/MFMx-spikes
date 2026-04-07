#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "HostBlock.h"
#include "TCBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"

namespace MFM {
  struct ZotPayload {
    u32 mZBMAG;
    u32 mData;
    u32 mCounts[2];
    bool mBongo;
    u8 mTakeMoreSpace[73];
    u32 mGAMZB;

    void init(u32 data, bool bongo) {
      memset_s(this,'\0',sizeof(*this));
      mZBMAG = 0x5b10cdef;
      mData = data;
      mBongo = bongo;
      mGAMZB = 0xfedc01b5;
    }
    void update(bool isin) {
      ++mCounts[ isin ? 0 : 1 ];
    }
  };

  struct ZotBlock : public TC<ZotBlock,sizeof(ZotPayload)> {
    //// TC API
    const char * getName() const { return "ZotBlock"; }

    bool readyToClose(TCOpsData & tms, u32 msnow) const { FAIL(INCOMPLETE_CODE); }
    
    ZotPayload & payload() { return *(ZotPayload*) getDataStart(); }
    void init(u32 data, bool bongo) ;
  };

  struct ZotBlockStg : TCBlock<ZotBlock,2> { // umm this struct could have been a typedef
    typedef TCBlock<ZotBlock,2> Super;
  };


  struct ZotEP : public T6EP<ZotEP,ZotBlockStg,2,HARTNUM_NC> { // two zotblockstgs
    using Super = T6EP<ZotEP,ZotBlockStg,2,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "ZotEP-i" : "ZotEP-o"; }
    u32 getCarSize() const { return sizeof(ZotBlock); }
    ZotBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LS(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(ZotBlock & car, u8 carindex) ;
    //    bool shipTC(ZotBlock & car, u8 carindex) ; ..handled by T6EP

    void initZotEP(BlockCode destbc, bool isin, Super::L1Data & l1data) ;
  };
  
  static constexpr u32 ZOTBLOCKS_IN_IDX = 0u;
  static constexpr u32 ZOTBLOCKS_OUT_IDX = 1u;
  static constexpr u32 ZOTBLOCKS_DEMO_COUNT = 2u;
  // extern ZotBlockStg theZotBlockCarsIO[ZOTBLOCKS_DEMO_COUNT];

  extern T6EPL1Data<ZotBlockStg,ZOTBLOCKS_DEMO_COUNT> theZotBlockL1Data;

}

