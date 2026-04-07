/* -*- C++ -*- */

#include "NRIUtils.h"
#include "Fail.h"
#include "HostBlock.h"
#include "ImageBlock.h"
#include "nanoprintf.h"

namespace MFM {
  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  void T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::setPublicEPState(EPState newstate) { 
    ASSERT_RIGHT_HART();
    EPState & pubstate = getPublicEPState();
    pubstate = newstate;
  }

  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  void T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::configureDest(U8C ournoc0, U8C destnoc0, u8 blockindex) {
    ASSERT_RIGHT_HART();
    mDestNoC0 = destnoc0;
    this->setDestBlockCodeIndex(blockindex);

    // Set up block address
    ImageBlockAddr iba;
    BlockCode bc = this->getDestBlockCode();
    HBNOTE("CFDS");
    HBPVAL(this->getName());
    HBPVAL(bc);
    bool ret = NRI3::findBlockCodeInNoC0(ournoc0, destnoc0, bc, iba);
    HBPVAL(destnoc0);
    HBPVAL(blockindex);
    MFM_API_ASSERT(ret,NOT_FOUND);

    HBPVAL(bc);
    HBPVAL(iba.mArrayLength);
    HBASSERT_LS(blockindex, iba.mArrayLength);
    mDestBlockAddr = iba.mBlockAddr + blockindex * getSizeFromBlockCode(bc);
  }

  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  typename T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::SUBTC
  * T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::getClosedTCPtrIfAny() const { 
    ASSERT_RIGHT_HART();
    using CarIdxs = typename L1Data::CarIdxs;
    CarIdxs & idxs = this->getCarIdxs();
    SNAP(2,HBPVAL(&idxs));
    u8 carindex;
    if (idxs.mTheIdxs[CarIdxs::COMP2COMM].remove(carindex)) {
      HBNOTE("GCDEP");
      //HBPVAL(&mCarIdxsPtr->mIdxs[COMP2COMM]);
      HBPVAL(carindex);
      SUBTC* carp = this->getCarPtrIfAny(carindex);
      if (!carp) HBNOTE("NULLGO?");
      else HBASSERT_EQ(carp->getTCState(), TCState::CLOSED); 
      return carp;
    }
    return 0;
  }


  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  bool T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::shipTC(SUBTC & car, u8 carindex) {
    ASSERT_RIGHT_HART();
    
    /* OK. Now our goals here are to:

     [Now done by configureDest, above:
       - Use ImageBlock stuff to find the IBA for mDestBlockCode
       - Ensure mDestBlockCodeIndex < iba.getArrayLength()
       - compute u32 destblockbaseaddr = iba.getBlockAddr() + TC_BLOCK_SIZE*mDestBlockCodeIndex
     ]

     - compute u32 destcaraddr = destblockbaseaddr + CAR_SIZE*carindex

     - use NRIUtils to SHIP DAT MOFO

     */

    bool isremotehost = false;// this->isRemoteHost();
    BlockCode destbc = this->getDestBlockCode();
    u8 destbcindex = this->getDestBlockCodeIndex();

    HBPVAL(destbcindex);

    if (isremotehost) {
      //      u32 hostchunkoffset = iba.getHostChunkOffsetOpt();
      //      MFM_API_ASSERT(hostchunkoffset != U8_MAX,ILLEGAL_STATE);
      FAIL(INCOMPLETE_CODE);
    }
    u32 destcaraddr = mDestBlockAddr + CAR_SIZE*carindex;

    U8C ournoc0 = fAll.mNoC0;
    U8C destnoc0 = mDestNoC0;

    if (!U8C::isNoC0CoordAT6(destnoc0)) {
      HBNOTE("NODEST");
      /// DEBUG PRETEND WE SHIPT TO LOCK UP THIS CAR
      return true;
    }
    u32 wordCount = car.getHeader().getPacketWords();

    HBMARK;
    HBNOTE("SHPTC");
    HBPVAL(ournoc0);
    HBPVAL((void*) &car);
    HBPVAL(destnoc0);
    HBPVAL((void*) destcaraddr);
    s32 status = NRI3::initiateWriteToT6(ournoc0,(u32*) &car, wordCount, destnoc0, destcaraddr);
    HBPVAL(status);
    return status > 0;
  }
  

  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  void T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>:: initT6EP(BlockCode bc,
                                                                bool isin,
                                                                L1Data & l1data) {
    ASSERT_RIGHT_HART();
    MFM_API_ASSERT_L1_ADDRESS(&l1data);
    mL1Data = &l1data;

    mDestNoC0 = { U8_MAX, U8_MAX }; // init to illegal addr

    //    HBMARK;

    this->initEP(bc, this->getAtomicLock(), isin, CAR_COUNT, false);

    HBMARK;
  }
}
