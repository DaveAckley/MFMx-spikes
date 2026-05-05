#include "ZotBlock.h"
#include "FastNC.h" // for EPFuncPtr

namespace MFM {
  T6EPL1Data<ZotBlockStg,2> theZotBlockL1Data;

  // ZotBlockStg theZotBlockCarsIO[2];
  // AtomicLock theZotBlockIOLock[2];
  // ZotEP::CarIdxs theZotBlockIdxs[2];

  FAST_LOCAL(ZotEP,myZotEPIN,n);
  FAST_LOCAL(ZotEP,myZotEPOUT,n);

  static void zotBlockHook(u16 f, u16 l) {
    extern T6EPL1Data<ZotBlockStg,2> theZotBlockL1Data;                 \
    if (theZotBlockL1Data.getPublicEPState(0)==0 ||                     \
        theZotBlockL1Data.getPublicEPState(1)==0) {                     \
      FIDLPTAG(f,l,zZBANG,&theZotBlockL1Data);                          \
      FIDLPTAG(f,l,&ep0,&theZotBlockL1Data.getPublicEPState(0));        \
      FIDLPTAG(f,l,ep0,theZotBlockL1Data.getPublicEPState(0));          \
      FIDLPTAG(f,l,&ep1,&theZotBlockL1Data.getPublicEPState(1));        \
      FIDLPTAG(f,l,ep1,theZotBlockL1Data.getPublicEPState(1));          \
      HBASSERT_EQ(1,0);                                                 \
    }                                                                   \
  }
 

#define assertZotBlocksInitted()                                        \
  do {                                                                  \
    extern T6EPL1Data<ZotBlockStg,2> theZotBlockL1Data;                 \
    if (theZotBlockL1Data.getPublicEPState(0)==0 ||                     \
        theZotBlockL1Data.getPublicEPState(1)==0) {                     \
      HBPTAG(ZBANG,&theZotBlockL1Data);                                 \
      HBPTAG(&ep0,&theZotBlockL1Data.getPublicEPState(0));              \
      HBPTAG(ep0,theZotBlockL1Data.getPublicEPState(0));                \
      HBPTAG(&ep1,&theZotBlockL1Data.getPublicEPState(1));              \
      HBPTAG(ep1,theZotBlockL1Data.getPublicEPState(1));                \
      /*HBASSERT_EQ(1,0);*/                                             \
    }                                                                   \
  } while (0);

  static bool manageZotzNC(bool doInit) {
    bool ret = false;
    if (unlikely(doInit)) {
      //      setGlobalDebugHook(zotBlockHook);

      theZotBlockL1Data.reset(); // zero all
      
      ZotBlockStg (&theZotBlockCarsIO)[2] = theZotBlockL1Data.mTheTCStorages;
      for (u32 i = 0; i < sizeof(theZotBlockCarsIO)/sizeof(theZotBlockCarsIO[0]); ++i) {
        ZotBlockStg & zbs = theZotBlockCarsIO[i];
        for (u32 c = 0; c < zbs.getCarCount(); ++c) {
          ZotBlock & zb = zbs.getTC(c);
          zb.init((i+1)*(c+1),true);
        }
      }

      myZotEPIN.initZotEP( { BC_ZOTBLOCK, ZOTBLOCKS_IN_IDX }, true, theZotBlockL1Data); 
      myZotEPOUT.initZotEP( { BC_ZOTBLOCK, ZOTBLOCKS_OUT_IDX }, false, theZotBlockL1Data); 

      myZotEPIN.configureDest(fAll.mNoC0,fAll.mNoC0, { BC_ZOTBLOCK, ZOTBLOCKS_OUT_IDX }); // in -> out
      myZotEPOUT.configureDest(fAll.mNoC0,fAll.mNoC0, { BC_ZOTBLOCK, ZOTBLOCKS_IN_IDX }); // out -> in
      assertZotBlocksInitted() ;

      HBNOTE("ZOTINS");
      myZotEPIN.activate();
      myZotEPOUT.activate();
      assertZotBlocksInitted() ;
      ret = true;
    } else {
      assertZotBlocksInitted() ;
      if (myZotEPIN.updateOps()) ret = true;
      if (myZotEPOUT.updateOps()) ret = true;
    }    
    return ret;
  }

  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr zotEPPtr = &manageZotzNC;

  void ZotEP::initZotEP(EndPointAddress srcEPA, bool isin, Super::L1Data & l1data) {
    HBPTAG(initZ,this->getName());
    Super::initT6EP(srcEPA, isin, l1data);
  }
     
  bool ZotEP::recvTC(ZotBlock & car, u8 carindex) {
    typename Super::L1Data::CarIdxRB & crb = this->getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crb.isFull()) return false; // bail if can't notify COMP??

    HBPTAG(zot/ZRCV,carindex);

    car.openTC();     // Open it up.
    crb.add(carindex); //notify hB
    HBPTAG(2comp,this->getName());
    
    return true;
  }

  ZotBlock * ZotEP::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    ZotBlock * ret = &this->getCarStg().getTC(carindex);
    //    HBPTAG(zGCPia,ret);
    return ret;
  }

  void ZotBlock::init(u32 data, bool bongo) {
    HBPTAG(zbinit,(void*)this);
    TC::reset(); // 0's all, sets header only with maxpayloadsize and state 0==UNUSED
    HBPTAG(zbrst,this->getTCState());
    openTC();                   // set state open
    HBPTAG(zbopt,this->getTCState());

    closeTC(sizeof(payload())); // and close it, zot cars are all full
    HBPTAG(zbclt,this->getTCState());
    setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
  }
}
