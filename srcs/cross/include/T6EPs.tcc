/* -*- C++ -*- */

#include "NRIUtils.h"
#include "Fail.h"
#include "HostBlock.h"
#include "ImageBlock.h"
#include "nanoprintf.h"

namespace MFM {
  extern HostBlock theHostBlock;

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

    // Set up dest block address
    LOGPTAG(CFDS,this->getName());
    LOGPVAL(getNameFromBlockCode(destbc));
    LOGPTAG(sBlkIdx,this->getSrcBlockCodeIndex());
    ImageBlockAddr iba;
    if (destnoc0 == PCIeTILE_NOC0) { // targetting our PCIe tile means dest is host

      // (0) find our own ImageBlockAddr for getSrcEPA().mBlockCode else bang
      // (1) find owniba.mHostChunkOffsetOpt != 255 or bang
      // (2) find u64 hostbaseaddr from hostblock lo,hi
      // (3) mDestBlockAddr = hostbaseaddr + 64*owniba.mHostChunkOffsetOpt
      ImageBlockHeader & ib = T6ImageBlock::getOurImageBlock();
      ImageBlockAddr iba = ib.findIBAIfAny(this->getSrcEPA().mBlockCode);
      MFM_API_ASSERT(iba.isValid(),ILLEGAL_STATE); // (0)
      u8 hchunk = iba.getHostChunkOffsetOpt();
      MFM_API_ASSERT(hchunk!=255u,NO_MATCH); // (1)
      const HostBlock & hb = theHostBlock;
      u64 hostbaseaddr = hb.getOurHostNoCBaseAddress(); // (2)
      mDestBlockAddr = hostbaseaddr + 64u * hchunk; // (3)
      u32 ourtlbi = U8C::makeTLBIFromNoCCoord(ournoc0);
      //HBXTAG64(hbAddr,hostbaseaddr);
      HBXTAG64(mDBAdr,mDestBlockAddr);
      //HBPTAG(oNoC0,ournoc0);
      HBXTAG(tlbi,ourtlbi);
    } else {
      // dest is T6
      bool ret = NRI3::findBlockCodeInNoC0(ournoc0, destnoc0, destbc, iba);
      LOGPTAG(t6dest0,destnoc0);
      LOGPTAG(dBlkIdx,destblockindex);
      HBASSERT_EQ(ret,true);
      HBASSERT_LS(destblockindex, iba.mStorageCount);
      mDestBlockAddr = iba.mBlockAddr + destblockindex * getStorageSizeFromBlockCode(destbc);
    }

    LOGPTAG64(dBA,mDestBlockAddr);
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
      HBPTAG(GCDEP,carindex);
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
    u64 destcaraddr = mDestBlockAddr + CAR_SIZE*carindex;

    U8C ournoc0 = fAll.mNoC0;
    U8C destnoc0 = mDestNoC0;

    u32 wordCount = car.getHeader().getPacketWords();

    if (U8C::isNoC0CoordAT6(destnoc0)) {
      s32 status = NRI3::initiateWriteToT6(ournoc0,(u32*) &car, wordCount, destnoc0, (u32) destcaraddr);
      return status > 0;
    }

    if (destnoc0 == PCIeTILE_NOC0) { // host target
      s32 status = NRI3::initiateWriteToHost(ournoc0,(u32*) &car, wordCount, destcaraddr);
      if (status == 0) HBPTAG(FAILSHIPHOST,wordCount);
      else {
        HBPTAG(FROMT6,this->getName());
        HBPTAG(TOHOST,wordCount);
      }
      return status > 0;
    }

    HBNOTE("NODEST");
    /// DEBUG PRETEND WE SHIPT TO LOCK UP THIS CAR
    return true;
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
