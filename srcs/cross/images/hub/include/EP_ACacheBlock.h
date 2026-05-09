#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"
#include "PT_ACacheBlock.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 HACB_BLOCKS = 1u;
  struct ACacheBlockEP : public T6EP<ACacheBlockEP,ACacheBlockStg,HACB_BLOCKS,HARTNUM_NC> {
    using Super = T6EP<ACacheBlockEP,ACacheBlockStg,HACB_BLOCKS,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "ACBi" : "ACBo"; }
    u32 getCarSize() const { return sizeof(ACacheBlock); }
    ACacheBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LS(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(ACacheBlock & car, u8 carindex) ;

    void initACacheBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

  struct ACacheBlockL1Control {
    using TheL1Data = ACacheBlockEP::Super::L1Data;

    AtomicLock mLock;
    ACacheBlock * mCurrentACacheBlock;
    u8 mCurrentCarIndex;        // valid whenever mCurrentACacheBlock != 0
    u32 mBaseTicks;
    u32 mAReportsMissed;

    void init() ;
    int step(HostBlock & hb) ;
    bool readyToClose() ;
    bool writeAtom(const P4Atom atom, U16C gridc) ;
  private:
    void setupNewCar(TheL1Data::CarIdxRB & crbi) ; // CALLER HOLDS LOCK & crbi IS NON-EMPTY
  };

  struct DLGridList;            // FORWARD
  struct ACacheBlockPrivateControl {
    static constexpr u32 RUN_ON_HARTNUM = HARTNUM_T1;
    static constexpr u32 BOGOMS_PER_FRAME = 100u;
    ACacheBlockL1Control * mACBL1Control;
    DLGridList * mDLGridList;
    u32 mFramesSent;
    s32 mSlackMS;
    u32 mNextFrameBogoMS;

    void init(ACacheBlockL1Control & acbl1, DLGridList & dll1) ;
    int step(HostBlock & hb) ;
    void tryToSendFrame(HostBlock & hb);
  };

  extern T6EPL1Data<ACacheBlockStg,1u> theACacheBlockL1Data;
  extern ACacheBlockL1Control theACacheBlockL1Control;
  extern ACacheBlockEP myACacheBlockEPNC;
}
