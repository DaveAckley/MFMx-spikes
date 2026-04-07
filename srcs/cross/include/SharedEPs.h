#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

#include "SharedTCs.h"
#include "TCBlock.h"

namespace MFM {

#if 1
  struct EwpBlockStg : TCBlock<EwpBlock,2> { // umm this struct could have been a typedef
    typedef TCBlock<EwpBlock,2> Super;
  };
#else
  typedef TCBlock<EwpBlock,2> EwpBlockStg;
#endif

  template <u8 BLOCK_COUNT>
  struct EwpEP : public T6EP<EwpEP<BLOCK_COUNT>,EwpBlockStg,BLOCK_COUNT,HARTNUM_NC> {
    using Super = T6EP<EwpEP<BLOCK_COUNT>,EwpBlockStg,BLOCK_COUNT,HARTNUM_NC>;
    static constexpr u32 CAR_COUNT = Super::CAR_COUNT;
    //// EP API
    const char * getName() const { return this->isIn() ? "EwpEP-i" : "EwpEP-o"; }
    u32 getCarSize() const { return sizeof(EwpBlock); }
    EwpBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LS(carindex, CAR_COUNT);
      return Super::mOpsDataStg[carindex];
    }
    bool recvTC(EwpBlock & car, u8 carindex) ;
    //    bool shipTC(EwpBlock & car, u8 carindex) ; ..handled by T6EP

    void initEwpEP(BlockCode destbc, bool isin, typename Super::L1Data & l1data) ;
  };
  

  static constexpr u32 IHUB_BLOCKS = 4u;
  struct InterHubEP : public T6EP<InterHubEP,InterHubStorage,IHUB_BLOCKS,HARTNUM_NC> {
    using Super = T6EP<InterHubEP,InterHubStorage,IHUB_BLOCKS,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "IHEP-i" : "IHEP-o"; }
    u32 getCarSize() const { return sizeof(InterHubBlock); }
    InterHubBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LS(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(InterHubBlock & car, u8 carindex) ;

    void initInterHubEP(BlockCode destbc, bool isin, typename Super::L1Data & l1data) ;
  };

 
}
#include "SharedEPs.tcc"
