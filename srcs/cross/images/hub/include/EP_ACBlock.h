#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"
#include "PT_ACBlock.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 ACB_BLOCK_COUNT = 1u;
  struct ACBlockEP : public T6EP<ACBlockEP,ACBlockStg,ACB_BLOCK_COUNT,HARTNUM_NC> {
    using Super = T6EP<ACBlockEP,ACBlockStg,ACB_BLOCK_COUNT,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "ACBi" : "ACBo"; }
    u32 getCarSize() const { return sizeof(ACBlock); }
    ACBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LT(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(ACBlock & car, u8 carindex) ;

    void initACBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

  struct ACBlockL1Control {
    using TheL1Data = ACBlockEP::Super::L1Data;

    AtomicLock mLock;
    ACBlock * mCurrentACBlock;
    u8 mCurrentCarIndex;        // valid whenever mCurrentACBlock != 0
    u32 mBaseTicks;
    u32 mAReportsMissed;
    static constexpr u8 ACBL1_NC_INITTED = 0x01;
    static constexpr u8 ACBL1_T0_INITTED = 0x02;
    static constexpr u8 ACBL1_T1_INITTED = 0x04;
    u8 mFlags;

    void init() ;
    void setFlags(u8 flags) ;
    bool testFlags(u8 flags) const ;
    u8 getFlags() const { return mFlags; }

    // int step(HostBlock & hb) ;
    bool readyToClose() ;
    bool writeByteToCurrentACB(u8 byte) ;
    void setupNewCar(TheL1Data::CarIdxRB & crbi) ; // COMP-SIDE CALLER HOLDS LOCK & crbi IS NON-EMPTY
 private:
  };

  extern T6EPL1Data<ACBlockStg,1u> theACBlockL1Data;
  extern ACBlockL1Control theACBlockL1Control;
  extern ACBlockEP myACBlockEPNC;
}
