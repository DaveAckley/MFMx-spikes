#include "CodeManager.h"
#include <unistd.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>
#include <cstring>
#include <memory>

#include "FATAL.h"
#include "HostBlock.h"

namespace MFM {
  s32 CodeManager::deployRISCVCodeFromFile(const char * path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); // Open in binary mode and at end

    if (!file.is_open()) 
      HOST_FATAL(NOT_FOUND,"Failed to open file: %s", path);

    std::streamsize rvCodeSize = file.tellg(); 
    file.seekg(0, std::ios::beg); // Seek back to beginning

    auto rvcode = std::make_unique<char[]>(rvCodeSize);

    if (!file.read(rvcode.get(), rvCodeSize))
      HOST_FATAL(READ_FAILURE,"Failed to read file: %s", path);

    printf(" RVCODE %s: ", path);

    file.close();

    return deployThisRISCVCode(rvcode.get(),rvCodeSize);
  }

  s32 CodeManager::deployThisRISCVCode(const char * rvcode, u32 rvsize) {
    u32 * codewords = (u32*) rvcode;
    u32 wordcount = rvsize >> 2u;
    printf(" LENGTH=%d (0x%08x, 0x%08x, ..., 0x%08x, 0x%08x)\n",
           rvsize,
           codewords[0],
           codewords[1],
           codewords[wordcount-2],
           codewords[wordcount-1]
           );

    if (rvsize%4 != 0u) HOST_FATAL(BAD_ALIGNMENT,"Bad code size %u",rvsize);
    ///// FIND/UPDATE HOSTBLOCK AT END OF CODE
    u32 hostblockt6addr = rvsize-sizeof(HostBlock);
    HostBlock *hb = (HostBlock*) (rvcode+hostblockt6addr);
    printf(" HB0 %u/0x%x %lu T6HBA:0x%08x\n   hb %p hr %p hn 0x%lx MC 0x%x CM 0x%x\n",
           rvsize, rvsize, sizeof(HostBlock), hostblockt6addr,
           hb, mOurTLBs.hostRAMPtr(),
           mOurTLBs.hostRAMNocAddr(),
           hb->mHBMagic, hb->mHBCigam);
    //Sat Sep 27 15:19:02 2025     u64 hostBufferBase = (u64) (uintptr_t) mOurTLBs.hostRAMPtr();
    u64 hostBufferBase = mOurTLBs.hostRAMNocAddr();
    hb->mHostBaseAddrLo = (u32) (hostBufferBase & 0xffffffff);
    hb->mHostBaseAddrHi = (u32) ((hostBufferBase>>32) & 0xffffffff);
    //    hb->mHBMagic = 0xacab8645;  // setup hb magic ourselves
    //    hb->mHBCigam = 0x5468baca;
    hb->mHBMagic--;    // correct test10's deliberately wrong hb magic
    hb->mHBCigam--;
    hb->mYPos = 99;             // but pre-blow ypos
    printf(" HB1 0x%08x 0x%08x\n",
           hb->mHostBaseAddrHi,
           hb->mHostBaseAddrLo);
    mRVCodeSize = rvsize;

    // "Multicast all the code to the entire fleet"
    mOurTLBs.writeToWords(OurTLBs::AHAX_TLBI_L1_MULTI, 0u, codewords, wordcount);

    // Let's try some sanity read-backs..
    u32 hits = 0u, misses = 0u;
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         tlbi += 37) {
      for (u32 word = 0u; word < wordcount; word += 1) {
        u32 byteaddr = word<<2u; // 4 bytes/word
        u32 data = mOurTLBs.read32(tlbi, byteaddr);
        if (codewords[word] != data) {
          ++misses;
          printf("%3d.      MISS %d on 0x%08x : got 0x%08x need 0x%08x\n",
                 tlbi, misses, word, data, codewords[word]);
        } else {
          ++hits;
          //printf("%3d. Hit %d on 0x%08x : 0x%08x\n",tlbi, hits, word, data);
        }
        
        // confirm certain addresses are in 'prerun' state
        u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
        {
          HostBlock rbhb;
          mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) & rbhb, sizeof(HostBlock)>>2u);
          u32 hbmagicpre = rbhb.mHBMagic;
          //printf("HostBlock magic %x\n", hbmagicpre);
          if (hbmagicpre != HostBlock::HBMAGIC || rbhb.mHBCigam != HostBlock::HBCIGAM)
            HOST_FATAL(BAD_VALUE,"Bad HBMAGIC 0x%0x\n",hbmagicpre);
          //printf("HostBlock HBA 0x%08x:%08x\n", rbhb.mHostBaseAddrHi, rbhb.mHostBaseAddrLo);
          if (rbhb.mHostBaseAddrHi != hb->mHostBaseAddrHi ||
              rbhb.mHostBaseAddrLo != hb->mHostBaseAddrLo)
            HOST_FATAL(BAD_VALUE,"Corrupt HBA Hi 0x%08x:0x%08x wanted 0x%08x:0x%08x\n",
                  rbhb.mHostBaseAddrHi,rbhb.mHostBaseAddrLo,
                  hb->mHostBaseAddrHi,hb->mHostBaseAddrLo);
          else
            ++hits;
        }
      }
    }
    printf("SPOT CHECK READBACK: hits=%d misses=%d\n", hits, misses);
    return 0;
  }

  void CodeManager::assertGoodMagic() {
    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++tlbi) {
      HostBlock hb;
      memset(&hb,0,sizeof(hb));
      mOurTLBs.readFromBytes(tlbi, hostblockaddr, (u8*) &hb, sizeof(hb));
      if (hb.mHBMagic != HostBlock::HBMAGIC)
        HOST_FATAL(BAD_VALUE,"Bad HBMAGIC 0x%08x @ %u\n",hb.mHBMagic,tlbi);
      if (hb.mHBCigam != HostBlock::HBCIGAM)
        HOST_FATAL(BAD_VALUE,"Bad HBCIGAM 0x%08x @ %u\n",hb.mHBCigam,tlbi);
      U16C nocc = U16C::makeNocCoordFromTLBI(tlbi);
      if (hb.mXPos != nocc.x || hb.mYPos != nocc.y || hb.mTLBI != tlbi)
        HOST_FATAL(BAD_VALUE,"Bad NOC0 COORD (%u,%u) wanted (%u,%u) @ %u vs %u\n",
                   hb.mXPos,hb.mYPos,
                   nocc.x, nocc.y,
                   tlbi, hb.mTLBI
                   );
    }
    printf("  ALL HBMAGIC+ IS GOOD\n");

  }

  s32 CodeManager::awaitResults() {
    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
    for (u32 tries = 0u; tries < 100u; ++tries) {
      u32 stats[140] = { 0u };
      u32 living[140] = { 0u };
      u32 allDone = 0u;
      printf("PASS %d ",tries);
      for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
           tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++tlbi) {
        HostBlock hb;
        memset(&hb,0,sizeof(hb));
        mOurTLBs.readFromBytes(tlbi, hostblockaddr, (u8*) &hb, sizeof(hb));
        if (hb.mHBMagic != HostBlock::HBMAGIC)
          HOST_FATAL(BAD_VALUE,"Bad HBMAGIC 0x%08x @ %u\n",hb.mHBMagic,tlbi);
        if (hb.mHBCigam != HostBlock::HBCIGAM)
          HOST_FATAL(BAD_VALUE,"Bad HBCIGAM 0x%08x @ %u\n",hb.mHBCigam,tlbi);

        {
          /*          printf("HBLOG %u %u\n",
                 hb.mLogBuffer.mFirstFreeIdx,
                 hb.mLogBuffer.mFirstUsedIdx);
          */
            bool first = true;
            s32 ch;
            while ((ch = hb.removeByte()) >= 0) {
              if (first) printf("%d HOSTBUFvvvvv\n",tlbi);
              first = false;
              printf("%c",(u8) ch);
            }
            if (!first) printf("%d HOSTBUF^^^^^\n",tlbi);
        }

        // printf("0: 0x%08x   1: 0x%08x   2: 0x%08x\n",
        //        hb.mCommonArgs[0],hb.mCommonArgs[1],hb.mCommonArgs[2]);
        U16C nocc = U16C::makeNocCoordFromTLBI(tlbi);
        if (hb.mXPos != nocc.x || hb.mYPos != nocc.y)
          printf("CROOD MIMSATCH %u (%u,%u) vs (%u,%u)\n",
                 tlbi, nocc.x, nocc.y,
                 hb.mXPos, hb.mYPos);
        if (true && tries<3 && tlbi<10u)
          printf("\n[%u] %d,%d,%d,%d,%d\n",
                 tlbi,
                 hb.mPerHartStatus[0],
                 hb.mPerHartStatus[1],
                 hb.mPerHartStatus[2],
                 hb.mPerHartStatus[3],
                 hb.mPerHartStatus[4]);
        for (u32 i = 0u; i < 5u; ++i) {
          if (hb.mPerHartStatus[i] == i+1u ||
              hb.mPerHartStatus[i] == FAILCode::LIVING) {
            ++stats[tlbi];
            if (hb.mPerHartStatus[i] == FAILCode::LIVING)
              ++living[tlbi];
          } else if (hb.mPerHartStatus[i] != 0)
            printf("TLBI %u HART#%u FAILCODE %d\n",tlbi,i,hb.mPerHartStatus[i]);
        }
        if (stats[tlbi] == 5u) {
          ++allDone;
          if (true) {
            printf("%d HOSTBUFvvvvv\n",tlbi);
            s32 ch;
            while ((ch = hb.removeByte()) >= 0) {
              printf("%c",(u8) ch);
            }
            printf("%d HOSTBUF^^^^^\n",tlbi);
          }
        }
        if (tlbi & 1) {
          printf("%x",living[tlbi-1]+living[tlbi]);
        }
      }
      printf(" %d\n",allDone);
      if (allDone == 140u) return 0;
    }
    return -1;
  }

}

