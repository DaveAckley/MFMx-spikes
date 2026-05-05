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
    //HBPVAL(getNameFromEPState(pubstate));
    //HBPVAL(this->getName());
    //HBPTAG(&ps,&pubstate);
  }

  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  void T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::configureDest(U8C ournoc0, U8C destnoc0, EndPointAddress destEPA) {
    ASSERT_RIGHT_HART();
    mDestNoC0 = destnoc0;
    this->setDestEPA(destEPA);
    BlockCode destbc = destEPA.mBlockCode;
    u8 destblockindex = destEPA.mBlockCodeIndex;

    // Set up block address
    ImageBlockAddr iba;
    HBPTAG(CFDS,this->getName());
    HBPVAL(getNameFromBlockCode(destbc));
    HBPTAG(sBlkIdx,this->getSrcBlockCodeIndex());
    bool ret = NRI3::findBlockCodeInNoC0(ournoc0, destnoc0, destbc, iba);
    HBPTAG(t6dest0,destnoc0);
    HBPTAG(dBlkIdx,destblockindex);
    HBASSERT_EQ(ret,true);

//    HBXTAG(dib0,*(((u32*) &iba)+0));
//    HBXTAG(dib1,*(((u32*) &iba)+1));
    HBASSERT_LS(destblockindex, iba.mStorageCount);
    mDestBlockAddr = iba.mBlockAddr + destblockindex * getStorageSizeFromBlockCode(destbc);

    HBXTAG(dBA,mDestBlockAddr);
  }

  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  typename T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::SUBTC
  * T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>::getClosedTCPtrIfAny() const { 
    ASSERT_RIGHT_HART();
    using CarIdxs = typename L1Data::CarIdxs;
    CarIdxs & idxs = this->getCarIdxs();
    //SNAP(2,HBPVAL(&idxs));
    u8 carindex;
    if (idxs.mTheIdxs[CarIdxs::COMP2COMM].remove(carindex)) {
      //      HBPTAG(GCDEP,carindex);
      //HBPVAL(&mCarIdxsPtr->mIdxs[COMP2COMM]);
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

    //    HBPTAG(dstbci,destbcindex);

    if (isremotehost) {
      //      u32 hostchunkoffset = iba.getHostChunkOffsetOpt();
      //      MFM_API_ASSERT(hostchunkoffset != U8_MAX,ILLEGAL_STATE);
      FAIL(INCOMPLETE_CODE);
    }
    //    HBXTAG(mDBA,mDestBlockAddr);
    u32 destcaraddr = mDestBlockAddr + CAR_SIZE*carindex;

    U8C ournoc0 = fAll.mNoC0;
    U8C destnoc0 = mDestNoC0;

    if (!U8C::isNoC0CoordAT6(destnoc0)) {
      HBNOTE("NODEST");
      /// DEBUG PRETEND WE SHIPT TO LOCK UP THIS CAR
      return true;
    }
    u32 wordCount = car.getHeader().getPacketWords();

    HOOKIT();
    //    HBMARK;
    //    HBPTAG(SHPTC/us,ournoc0);
    //HBPTAG(ucar+,(void*) &car);
    //HBPTAG(ucar-,(void*)(((char*) &car)+4*wordCount));
    //    HBPTAG(dst,destnoc0);
    //HBPTAG(dcar+,(void*) destcaraddr);
    //HBPTAG(dcar-,(void*)(((char*) destcaraddr)+4*wordCount));
    s32 status = NRI3::initiateWriteToT6(ournoc0,(u32*) &car, wordCount, destnoc0, destcaraddr);
    HOOKIT();
    //    HBPTAG(stat,status);
    return status > 0;
  }
  

  template <class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  void T6EP<SUBEP,SUBTCBLOCKSTG,BLOCK_COUNT,FORHART>:: initT6EP(EndPointAddress srcEPA,
                                                                bool isin,
                                                                L1Data & l1data) {
    ASSERT_RIGHT_HART();
    MFM_API_ASSERT_L1_ADDRESS(&l1data);
    mL1Data = &l1data;

    mDestNoC0 = { U8_MAX, U8_MAX }; // init to illegal addr
    this->initEP(srcEPA, this->getAtomicLock(), isin, CAR_COUNT, false);
  }
}
