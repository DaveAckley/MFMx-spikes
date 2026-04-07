#include "ZotBlock.h"
#include "FastNC.h" // for EPFuncPtr

namespace MFM {
  T6EPL1Data<ZotBlockStg,2> theZotBlockL1Data;

  // ZotBlockStg theZotBlockCarsIO[2];
  // AtomicLock theZotBlockIOLock[2];
  // ZotEP::CarIdxs theZotBlockIdxs[2];

  FAST_LOCAL(ZotEP,myZotEPIN,n);
  FAST_LOCAL(ZotEP,myZotEPOUT,n);

  static bool manageZotzNC(bool doInit) {
    bool ret = false;
#ifndef BUILD_HOST      
#if 0
    if (true) {
      static u32 once = 0;
      if (++once < 4) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," #%lu c%u s%u t%u zung 0x%p 0x%p 0x%p / 0x%p 0x%p 0x%p.",
                   once,
                   sizeof(theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(0)),
                   sizeof(ZotBlockStg),
                   sizeof(ZotBlock),
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX],
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(0),
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(1),
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX],
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX].getTC(0),
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX].getTC(1));
      theHostBlock.packString(buf);
    }
    }
#endif
#endif
    if (unlikely(doInit)) {
      theZotBlockL1Data.reset(); // zero all
      
      ZotBlockStg (&theZotBlockCarsIO)[2] = theZotBlockL1Data.mTheTCBlocks;
      for (u32 i = 0; i < sizeof(theZotBlockCarsIO)/sizeof(theZotBlockCarsIO[0]); ++i) {
        ZotBlockStg & zbs = theZotBlockCarsIO[i];
        for (u32 c = 0; c < zbs.getCarCount(); ++c) {
          ZotBlock & zb = zbs.getTC(c);
          zb.init((i+1)*(c+1),true);
        }
      }

      myZotEPIN.initZotEP(BC_ZOTBLOCK, true, theZotBlockL1Data); 
      myZotEPIN.configureDest(fAll.mNoC0,fAll.mNoC0,ZOTBLOCKS_OUT_IDX); // in -> out

      myZotEPOUT.initZotEP(BC_ZOTBLOCK, false, theZotBlockL1Data); 
      myZotEPOUT.configureDest(fAll.mNoC0,fAll.mNoC0,ZOTBLOCKS_IN_IDX); // out -> in

      myZotEPIN.activate();
      myZotEPOUT.activate();

      ret = true;
    } else {
      extern HostBlock theHostBlock;
      HostBlock & hb = theHostBlock;
      //      hb.addBytes('A','A');

      if (myZotEPIN.updateOps()) ret = true;
      //      hb.addBytes('B','B');
      if (myZotEPOUT.updateOps()) ret = true;
      //      hb.addBytes('C','C');
    }    
    return ret;
  }

  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr zotEPPtr = &manageZotzNC;

  void ZotEP::initZotEP(BlockCode destbc, bool isin, Super::L1Data & l1data) {
    {
      static u32 once;
      if (once < 5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(isin?'I':'O',hartChar(fAll.mHartNum));
        ++once;
      }
    }

    Super::initT6EP(destbc, isin, l1data);
    {
      static u32 once;
      if (once < 5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(isin?'i':'o',hartChar(fAll.mHartNum));
        ++once;
      }
    }
  }
     
  bool ZotEP::recvTC(ZotBlock & car, u8 carindex) {
    typename Super::L1Data::CarIdxRB & crb = this->getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crb.isFull()) return false; // bail if can't notify COMP??

    HBNOTE("zot/ZRCV");

    car.openTC();     // Open it up.
    crb.add(carindex); //notify hB
    HBNOTE(getName());
    
    return true;
  }

  ZotBlock * ZotEP::getCarPtrIfAny(u8 carindex) const {
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(':',hartChar(fAll.mHartNum));
        theHostBlock.addBytes('0'+carindex,hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
    if (carindex >= CAR_COUNT) return 0;
    return &this->getCarStg().getTC(carindex);
  }

  void ZotBlock::init(u32 data, bool bongo) {
      TC::reset(); // 0's all, sets header only with maxpayloadsize and state 0==UNUSED
      openTC();                   // set state open

#ifndef BUILD_HOST      
    if (true) {
      static u32 once = 0;
      if (++once < 4) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," ZIN- 0x%p pay %luB 0c%lu 1c%lu\n",
                   this,
                   getHeader().getPayloadCapacity(),
                   payload().mCounts[0],
                   payload().mCounts[1]);
      theHostBlock.packString(buf);
    }
    }
#endif    

    closeTC(sizeof(payload())); // and close it, zot cars are all full
    setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'

#ifndef BUILD_HOST      
    if (true) {
      static u32 once = 0;
      if (++once < 4) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," ZIN+ 0x%p pay %luB 0c%lu 1c%lu\n",
                   this,
                   getHeader().getPayloadCapacity(),
                   payload().mCounts[0],
                   payload().mCounts[1]);
      theHostBlock.packString(buf);
    }
    }
#endif    

  }
}
