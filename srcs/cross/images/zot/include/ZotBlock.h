#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "EP.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"

namespace MFM {
  struct ZotPayload {
    u32 mZBMAG;
    u32 mData;
    bool mBongo;
    u32 mGAMZB;

    void init(u32 data, bool bongo) {
      mZBMAG = 0x5b10cdef;
      mData = data;
      mBongo = bongo;
      mGAMZB = 0xfedc01b5;
    }
  };

  struct ZotBlock : public TC<sizeof(ZotPayload)> {
    //// TC API
    void reset() { payload().init(0,false); }
    bool readyToClose(TCOpsData & tms, u32 msnow) const { FAIL(INCOMPLETE_CODE); }
    
    ZotPayload & payload() { return *(ZotPayload*) getDataStart(); }
    void init(u32 data, bool bongo) {
      payload().init(data,bongo);
    }
    ZotBlock() {
      memset_s(this, '\0', sizeof(*this));
    }
  };

  struct ZotBlockStg; // FORWARD

  struct ZotEP : public EP {
    static u8 constexpr CAR_COUNT = 2u;

    //// EP API
    const char * getName() const override { return isIn() ? "ZEPI" : "ZEPO"; }
    u32 getCarSize() const { return sizeof(ZotBlock); }
    TCBase * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) override {
      MFM_API_ASSERT_NONNULL(carindex < CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(TCBase & car, u8 carindex) { FAIL(INCOMPLETE_CODE); }
    bool shipTC(TCBase & car, u8 carindex) { FAIL(INCOMPLETE_CODE); }

    void init(bool isin, ZotBlockStg & cars, AtomicLock & al) {
      mCarStgPtr = &cars;       // set up BEFORE calling EP::init doh
      {
        static u32 once;
        if (once < 5) {
          extern HostBlock theHostBlock;
          theHostBlock.addBytes(isin?'I':'O',hartChar(fAll.mHartNum));
          ++once;
        }
      }
      EP::init(al, isin, CAR_COUNT, false);
      {
        static u32 once;
        if (once < 5) {
          extern HostBlock theHostBlock;
          theHostBlock.addBytes(isin?'i':'o',hartChar(fAll.mHartNum));
          ++once;
        }
      }
    }
    ZotBlockStg * mCarStgPtr;
    TCOpsData mOpsDataStg[CAR_COUNT];
  };

  struct ZotBlockStg : EPStg<ZotBlockStg> {
    u32 getCarSize() const { return sizeof(ZotBlock); }
    u32 getCarCount() const { return ZotEP::CAR_COUNT; }
    TCBase * getCarPtr(u32 index) {
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('s',hartChar(fAll.mHartNum));
        theHostBlock.addBytes('0'+index,hartChar(fAll.mHartNum));
        once++;
      }
    }
      MFM_API_ASSERT(index<ZotEP::CAR_COUNT,ILLEGAL_ARGUMENT);
      return &mZotBlocks[index];
    }
    ZotBlock mZotBlocks[ZotEP::CAR_COUNT];
  };

  extern ZotBlockStg theZotBlockCarsIO[2];

}

