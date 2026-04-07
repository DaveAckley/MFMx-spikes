#include "ZotBlock.h"
#include "FastNC.h" // for EPFuncPtr

namespace MFM {
  ZotBlockStg theZotBlockCarsIO[2];
  AtomicLock theZotBlockIOLock[2];
  ZotEP::CarIdxs theZotBlockIdxs[2];

  FAST_LOCAL(ZotEP,myZotEPIN,nc);
  FAST_LOCAL(ZotEP,myZotEPOUT,nc);

  static bool manageZotzNC(bool doInit) {
    bool ret = false;
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," zung 0x%p 0x%p 0x%p / 0x%p 0x%p 0x%p.",
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX],
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(0),
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(1),
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX],
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX].getTC(0),
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX].getTC(1));
      theHostBlock.packString(buf);
    }
#endif
    if (unlikely(doInit)) {
      memset_s(&theZotBlockCarsIO[0],'\0',sizeof(theZotBlockCarsIO));
      memset_s(&theZotBlockIOLock[0],'\0',sizeof(theZotBlockIOLock));
      memset_s(&theZotBlockIdxs[0],'\0',sizeof(theZotBlockIdxs));

      myZotEPIN.init(BC_ZOTBLOCK, ZOTBLOCKS_OUT_IDX, true, // note 2nd arg reversed! it's the dest!
                        theZotBlockCarsIO[ZOTBLOCKS_IN_IDX],
                        theZotBlockIOLock[ZOTBLOCKS_IN_IDX],
                        theZotBlockIdxs[ZOTBLOCKS_IN_IDX]);
      myZotEPOUT.init(BC_ZOTBLOCK, ZOTBLOCKS_IN_IDX, false, // note 2nd arg reversed! it's the dest!
                         theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX],
                         theZotBlockIOLock[ZOTBLOCKS_OUT_IDX],
                         theZotBlockIdxs[ZOTBLOCKS_OUT_IDX]);


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

  void ZotEP::init(BlockCode destbc, u32 destidx, bool isin, ZotBlockStg & cars, AtomicLock & al, CarIdxs & caridxs) {
    {
      static u32 once;
      if (once < 5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(isin?'I':'O',hartChar(fAll.mHartNum));
        ++once;
      }
    }

    Super::init(destbc, destidx, isin, cars, al, caridxs);
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

    CarIdxRB & crb = mCarIdxsPtr->mIdxs[TC2EP];
    if (crb.isFull()) return false; // bail if can't notify??

#ifndef BUILD_HOST      
    {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," %s ZRCV #%u 0x%p chgp%luB\n",
                   this->getName(),
                   carindex,&car,
                   car.getHeader().getPacketWords()*4);
      theHostBlock.packString(buf);
    }
#endif


    // Open it up. (Assuming all full-size payloads here..)
    car.setTCState(TCState::OPEN,ZotBlock::MAX_PAYLOAD_SIZE);
    crb.add(carindex); //notify hB
    
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('(','(');
      theHostBlock.addBytes('0'+carindex,hartChar(fAll.mHartNum));
      theHostBlock.addBytes('0'+crb.mFirstUsedIdx,'0'+crb.mFirstFreeIdx);
      theHostBlock.addBytes(')',')');
    }
#endif
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
    MFM_API_ASSERT_NONNULL(mCarStgPtr);
    return &mCarStgPtr->getTC(carindex);
  }
}
