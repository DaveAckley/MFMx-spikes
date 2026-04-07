#include "EwpBlock.h"
#include "FastNC.h" // for EPFuncPtr

namespace MFM {
  EwpBlockStg theEwpBlockCars[1];
  AtomicLock theEwpBlockLock[1];
  EwpEP::CarIdxs theEwpBlockIdxs[1];

  FAST_LOCAL(EwpEP,myEwpEP,nc);
  static bool manageEwpNC(bool doInit) {
    { static bool once;
      extern HostBlock theHostBlock;
      if (!once) theHostBlock.packString(" mngEwpNC\n");
      once = true;
    }
    bool ret = false;
    if (unlikely(doInit)) {
      memset_s(&theEwpBlockCars[0],'\0',sizeof(theEwpBlockCars));
      memset_s(&theEwpBlockLock[0],'\0',sizeof(theEwpBlockLock));
      myEwpEP.init(BC_EWPCARS, 0u, false, theEwpBlockCars[0], theEwpBlockLock[0], theEwpBlockIdxs[0]);
    { static bool once;
      extern HostBlock theHostBlock;
      if (!once) theHostBlock.packString(" mngEwpINNC\n");
      once = true;
    }
      ret = true;
    } else {
    { static bool once;
      extern HostBlock theHostBlock;
      if (!once) theHostBlock.packString(" mngEwpUPNC\n");
      once = true;
    }
      if (myEwpEP.updateOps()) ret = true;
    }
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr ewpEPPtr = &manageEwpNC;
      

  void EwpEP::init(BlockCode destbc, u32 destidx, bool isin, EwpBlockStg & cars, AtomicLock & al, CarIdxs & caridxs) {
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
     
  bool EwpEP::recvTC(EwpBlock & car, u8 carindex) {

    CarIdxRB & crb = mCarIdxsPtr->mIdxs[TC2EP];
    if (crb.isFull()) return false; // bail if can't notify??

    // Open it up. (Assuming all full-size payloads here..)
    car.setTCState(TCState::OPEN,EwpBlock::MAX_PAYLOAD_SIZE);

#ifndef BUILD_HOST      
    {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," %s ERCV #%u 0x%p sz%u %luW\n",
                   this->getName(),carindex,&car,
                   car.getHeader().mTCMSize,
                   car.getHeader().getPacketWords());
      theHostBlock.packString(buf);
    }
#endif


    
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

  EwpBlock * EwpEP::getCarPtrIfAny(u8 carindex) const {
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
