/* -*- C++ -*- */

#include "NRIUtils.h"
#include "Fail.h"
#include "HostBlock.h"
#include "ImageBlock.h"
#include "nanoprintf.h"

namespace MFM {
  template <class SUBEP, class SUBTCBLOCK>
  bool T6ToT6EP<SUBEP,SUBTCBLOCK>::shipTC(SUBTC & car, u8 carindex) {
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100,"test %u 0x%p %u %u.",carindex,&car,sizeof(car),sizeof(SUBTCBLOCK));
      theHostBlock.packString(buf);
    }
#endif
    /* OK. Now our goals here are to:

     - Use ImageBlock stuff to find the IBA for mDestBlockCode

     - Ensure mDestBlockCodeIndex < iba.getArrayLength()

     - compute u32 destblockbaseaddr = iba.getBlockAddr() + TC_BLOCK_SIZE*mDestBlockCodeIndex

     - compute u32 destcaraddr = destblockbaseaddr + CAR_SIZE*carindex

     - use NRIUtils to SHIP DAT MOFO

     */

    BlockCode destbc = this->getDestBlockCode();
    u8 destbcindex = this->getDestBlockCodeIndex();
    ImageBlockHeader & ibh = *(ImageBlockHeader*) 0x14; //"WELL-KNOWN ADDRESS"
    ImageBlockAddr iba = ibh.findIBAIfAny(destbc);
    MFM_API_ASSERT(iba.isValid(),NO_MATCH);
    MFM_API_ASSERT(destbcindex < iba.getArrayLength(),ARRAY_INDEX_OUT_OF_BOUNDS);
    u32 destblockbaseaddr = iba.getBlockAddr() + TC_BLOCK_SIZE*destbcindex;
    u32 destcaraddr = destblockbaseaddr + CAR_SIZE*carindex;
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," bung 0x%p %u 0x%lx 0x%lx.",&car,sizeof(car),destcaraddr,destblockbaseaddr);
      theHostBlock.packString(buf);
    }
#endif

    U8C ournoc0 = fAll.mPos;
    u32 wordCount = car.getPacketWords();
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.packString("ALGN");
      const char * hex = "0123456789ABCDEF";
      theHostBlock.addBytes(hex[((u32)((u32*)&car))%16],hex[destcaraddr%16]);
      theHostBlock.addBytes(hex[iba.getBlockAddr()%16],hex[destblockbaseaddr%16]);
    }
#endif

    s32 status = NRI3::initiateWriteToT6(ournoc0,(u32*) &car, wordCount, ournoc0, destcaraddr);
#ifndef BUILD_HOST      
    {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," %s SHP #%u(%u) s%u 0x%p %luB -> 0x%lx = %ld\n",
                   this->getName(),carindex,this->getGoneCount(),car.getTCState(),&car,wordCount*4,destcaraddr,status);
      theHostBlock.packString(buf);
    }
#endif

    return status > 0;
  }

  template <class SUBEP, class SUBTCBLOCK>
  void T6ToT6EP<SUBEP,SUBTCBLOCK>::init(BlockCode bc, u8 blkIdx, bool isin, SUBTCBLOCK & stgblk, AtomicLock & al, CarIdxs & caridxs) {
    MFM_API_ASSERT_L1_ADDRESS(&stgblk);
    mCarStgPtr = &stgblk;

    MFM_API_ASSERT_L1_ADDRESS(&caridxs);
    mCarIdxsPtr = &caridxs;
    
    Super::init(bc, blkIdx, al, isin, CAR_COUNT, false);
  }
}
