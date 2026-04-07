#include "ZotBlock.h"

namespace MFM {
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
      npf_snprintf(buf,100," %s RCV #%u 0x%p %luB\n",this->getName(),carindex,&car,car.getPacketWords()*4);
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
