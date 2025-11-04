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
#include "FailStrings.h"
#include "Constants.h"
#include "HostUtils.h"
#include "BHLog.h"

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

    LOGprintf(mCardNum," RVCODE %s: ", path);

    file.close();

    return deployThisRISCVCode(rvcode.get(),rvCodeSize);
  }

  s32 CodeManager::deployThisRISCVCode(const char * rvcode, u32 rvsize) {
    u32 * codewords = (u32*) rvcode;
    u32 wordcount = rvsize >> 2u;
    LOGprintf(mCardNum," LENGTH=%d (0x%08x, 0x%08x, ..., 0x%08x, 0x%08x)\n",
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
    LOGprintf(mCardNum," HB0 %u/0x%x %lu T6HBA:0x%08x\n   hb %p hr %p hn 0x%lx MC 0x%x CM 0x%x\n",
           rvsize, rvsize, sizeof(HostBlock), hostblockt6addr,
           hb, mOurTLBs.hostRAMPtr(),
           mOurTLBs.hostRAMNocAddr(),
           hb->mHBMagic, hb->mHBCigam);
    //Sat Sep 27 15:19:02 2025     u64 hostBufferBase = (u64) (uintptr_t) mOurTLBs.hostRAMPtr();
    u64 hostBufferBase = mOurTLBs.hostRAMNocAddr();
    hb->mHostBaseAddrLo = (u32) (hostBufferBase & 0xffffffff);
    hb->mHostBaseAddrHi = (u32) ((hostBufferBase>>32) & 0xffffffff);
    hb->mAIClockFrequency = (u32) 800'000'000u; // XXX ASSUME 'IDLE' CLOCK SPEED FOR NOW
    //    hb->mHBMagic = 0xacab8645;  // setup hb magic ourselves
    //    hb->mHBCigam = 0x5468baca;
    hb->mCommonArgs[0] = time(0); // per-run nonce
    hb->mCommonArgs[1] = mStartDecayType; // optional start symbol behavior selection
    hb->mHBMagic;
    hb->mHBCigam;
    hb->mYPos = 99;             // but pre-blow ypos
    LOGprintf(mCardNum," HB1 0x%08x 0x%08x\n",
           hb->mHostBaseAddrHi,
           hb->mHostBaseAddrLo);
    mRVCodeSize = rvsize;

    // "Multicast all the code to the entire fleet"
    mOurTLBs.writeToWords(OurTLBs::AHAX_TLBI_L1_MULTI, 0u, codewords, wordcount);

    // Let's try some sanity read-backs..
    u32 hits = 0u, misses = 0u;
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         tlbi += /*69*/1) {
      for (u32 word = 0u; word < wordcount; word += 11) {
        u32 byteaddr = word<<2u; // 4 bytes/word
        u32 data = mOurTLBs.read32(tlbi, byteaddr);
        if (codewords[word] != data) {
          ++misses;
          LOGprintf(mCardNum,"{%d},%3d.   MISS %d @ 0x%08x : got 0x%08x need 0x%08x\n",
                    mCardNum, tlbi, misses, word<<2, data, codewords[word]);
        } else {
          ++hits;
          //LOGprintf(mCardNum,"%3d. Hit %d on 0x%08x : 0x%08x\n",tlbi, hits, word, data);
        }
      }
        
      // confirm certain addresses are in 'prerun' state
      u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
      //LOGprintf(mCardNum,"SPOTCHECKING HB AT %u/%x\n",hostblockaddr,hostblockaddr);
      {
        HostBlock rbhb;
        memset_s(&rbhb,0,sizeof(rbhb));
        mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) & rbhb, sizeof(HostBlock)>>2u);
        u32 hbmagicpre = rbhb.mHBMagic;
        //printf("HostBlock magic %x\n", hbmagicpre);
        //printf("HostBlock x %u y %u\n", rbhb.mXPos, rbhb.mYPos);
        if (hbmagicpre != HostBlock::HBMAGIC)
          HOST_FATAL(BAD_VALUE,"Bad HBMAGIC 0x%0x\n",hbmagicpre);
        if (rbhb.mHBCigam != HostBlock::HBCIGAM)
          HOST_FATAL(BAD_VALUE,"Bad HBCIGAM 0x%0x\n",rbhb.mHBCigam);
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
    LOGprintf(mCardNum,"SPOT CHECK READBACK: hits=%d misses=%d\n", hits, misses);
    return 0;
  }

  void CodeManager::releaseTheHounds() {
    mOurTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                     RISCV_DEBUG_REG_SOFT_RESET_0,
                     SOFT_RESET_ALL_RISCV_EXCEPT_B);
    LOGprintf(mCardNum,"PHASE-------Check magic\n");
    assertGoodMagic();
  }

  u32 CodeManager::newFails(T6FailCallback cb) {
    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
    u32 ret = 0u;
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         tlbi++) {
      u32 failsOffset = offsetof(HostBlock,mFails)/4u*4u; 
      HostBlock hb;
      mOurTLBs.readFromBytes(tlbi,
                             hostblockaddr+failsOffset,
                             ((u8*) &hb)+failsOffset,
                             4u);
      OurTLBs::TLBInfo & tlbinfo = mOurTLBs.getTLBInfo(tlbi);
      if (tlbinfo.mFailStatus != hb.mFails) {
        // if new fails, read whole thing
        mOurTLBs.readFromBytes(tlbi, hostblockaddr, (u8*) &hb, sizeof(hb)); 
        U16C noc = U16C::makeNocCoordFromTLBI(tlbi);
        BHTag tag(TagType::T6TADR, mCardNum, noc.x, noc.y);
        cb(tag, hb, tlbinfo.mFailStatus, hb.mFails);
        tlbinfo.mFailStatus = hb.mFails;
        ++ret;
      }
    }

    return ret;
  }

  void CodeManager::assertGoodMagic() {
    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
    LOGprintf(mCardNum,"GOODMAGICKING HB AT %u/%x\n",hostblockaddr,hostblockaddr);
    HostBlock hb;
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++tlbi) {
      memset_s(&hb,0u,sizeof(hb)); //<< memset_s(,0,) uses explicit_bzero host side
      // memset(&hb,0x0,sizeof(hb)); // FAILS???? WTF?? BUT E.G. memset(&hb,0x1,sizeof(hb)); WORKKKKKKS!?
      mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);
      if (false) {
        LOGprintf(mCardNum,"HBMAGIC 0x%08x @ %u vs %u (%u,%u)[%d,%d,%d,%d,%d]\n",
               hb.mHBMagic,tlbi,hb.mTLBI,
               hb.mXPos,hb.mYPos,
               hb.mPerHartStatus[0],
               hb.mPerHartStatus[1],
               hb.mPerHartStatus[2],
               hb.mPerHartStatus[3],
               hb.mPerHartStatus[4]);
      }
      if (hb.mHBMagic != HostBlock::HBMAGIC)
        HOST_FATAL(BAD_VALUE,"Bad HBMAGIC 0x%08x @ %u\n",hb.mHBMagic,tlbi);
      if (hb.mHBCigam != HostBlock::HBCIGAM)
        HOST_FATAL(BAD_VALUE,"Bad HBCIGAM 0x%08x @ %u\n",hb.mHBCigam,tlbi);
      U16C nocc = U16C::makeNocCoordFromTLBI(tlbi);
      if (true && (hb.mXPos != nocc.x || hb.mYPos != nocc.y || hb.mTLBI != tlbi))
        HOST_FATAL(BAD_VALUE,"Bad NOC0 COORD (%u,%u) wanted (%u,%u) @ %u vs %u\n",
                   hb.mXPos,hb.mYPos,
                   nocc.x, nocc.y,
                   tlbi, hb.mTLBI
                   );
      if (true) {
        HostBlock::LogBuffer & lb = hb.mLogBuffer;
        const u32 BUF_SIZE = 1000u;
        u8 buf[BUF_SIZE+1u];
        u32 idx = 0u;
        while (!lb.isEmpty()) {
          lb.remove(buf[idx]);
          if (++idx >= BUF_SIZE)
            break;
        }
        if (idx != 0u) {
          buf[idx] = 0;
          LOGprintf(mCardNum,"(%u,%u)HB<%s>\n",hb.mXPos,hb.mYPos,buf);
        }
      }
    }
    LOGprintf(mCardNum,"  ALL HBMAGIC+ IS GOOD\n");
  }

  s32 CodeManager::awaitResults() {
    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
    for (u32 tries = 0u; tries < 100u; ++tries) {
      u32 stats[140] = { 0u };
      u32 living[140] = { 0u };
      u32 allDone = 0u;
      LOGprintf(mCardNum,"PASS %d ",tries);
      for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
           tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++tlbi) {
        HostBlock hb;
        memset_s(&hb,0,sizeof(hb));
        mOurTLBs.readFromBytes(tlbi, hostblockaddr, (u8*) &hb, sizeof(hb));
        if (hb.mHBMagic != HostBlock::HBMAGIC)
          HOST_FATAL(BAD_VALUE,"Bad HBMAGIC 0x%08x @ %u\n",hb.mHBMagic,tlbi);
        if (hb.mHBCigam != HostBlock::HBCIGAM)
          HOST_FATAL(BAD_VALUE,"Bad HBCIGAM 0x%08x @ %u\n",hb.mHBCigam,tlbi);

        if (false) {
          /*          Eprintf("HBLOG %u %u\n",
                 hb.mLogBuffer.mFirstFreeIdx,
                 hb.mLogBuffer.mFirstUsedIdx);
          */
            bool first = true;
            s32 ch;
            while ((ch = hb.removeByte()) >= 0) {
              if (first) Eprintf("%d HOSTBUFvvvvv\n",tlbi);
              first = false;
              Eprintf("%c",(u8) ch);
            }
            if (!first) Eprintf("%d HOSTBUF^^^^^\n",tlbi);
        }

        U16C nocc = U16C::makeNocCoordFromTLBI(tlbi);
        if (hb.mXPos != nocc.x || hb.mYPos != nocc.y)
          LOGprintf(mCardNum,"CROOD MIMSATCH %u (%u,%u) vs (%u,%u)\n",
                 tlbi, nocc.x, nocc.y,
                 hb.mXPos, hb.mYPos);
        if (true && tries<3 && tlbi<10u)
          LOGprintf(mCardNum,"\n[%u] %d,%d,%d,%d,%d\n",
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
            LOGprintf(mCardNum,"TLBI %u %s FAIL%d: %s\n",
                      tlbi,hartName(i),hb.mPerHartStatus[i],
                      getFailCodeString((FAILCode) hb.mPerHartStatus[i]));
        }
        if (stats[tlbi] == 5u) {
          ++allDone;
          if (true) {
            bool stuff = !hb.mLogBuffer.isEmpty(); 
            if (stuff) {
              Eprintf("%d HOSTBUFvvvvv\n",tlbi);
              s32 ch;
              while ((ch = hb.removeByte()) >= 0) {
                Eprintf("%c",(u8) ch);
              }
              Eprintf("%d HOSTBUF^^^^^\n",tlbi);
            }
          }
        }
        if (tlbi & 1) {
          Eprintf("%x",living[tlbi-1]+living[tlbi]);
        }
      }
      Eprintf(" %d\n",allDone);
      if (allDone == 140u) return 0;
    }
    return -1;
  }

  s32 CodeManager::slowScanHostBlocks() {
    if (mLastTLBISlowScanned >= OurTLBs::AHAX_TLBI_L1_LAST_UNI)
      mLastTLBISlowScanned = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
    else
      ++mLastTLBISlowScanned;
    u32 tlbi = mLastTLBISlowScanned;
    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);

    BHLog & bhl = BHLog::getTheBHLog();
    BHTag tag(TagType::T6TADR, mCardNum, tlbi);
    HostBlock hb;
    memset_s(&hb,0,sizeof(hb));
    mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);
    //    mOurTLBs.readFromBytes(tlbi, hostblockaddr, (u8*) &hb, sizeof(hb));
    if (hb.mHBMagic != HostBlock::HBMAGIC) {
      bhl.printf(tag,"  BAD MAGIC 0x%08x @ 0x%08x\n",hb.mHBMagic, hostblockaddr);
      return -1;
    }
    if (hb.mHBCigam != HostBlock::HBCIGAM) {
      bhl.printf(tag,"  BAD CIGAM 0x%08x\n",hb.mHBCigam);
      return -2;
    }

    OurTLBs::TLBInfo & info = mOurTLBs.getTLBInfo(tlbi);
    for (u32 hart = 0u; hart < 5u; ++hart) {
      if (info.mLastWatchdog[hart] == hb.mPerHartWatchdog[hart]) {
        if (info.mStuckDog[hart]) {
          Eprintf("BH%d:(%2u,%2u)%s STUCK? 0x%08x = FAIL%d:%s\n",
                  mCardNum,hb.mXPos,hb.mYPos,hartName(hart),
                  hb.mPerHartWatchdog[hart],
                  hb.mPerHartStatus[hart],
                  getFailCodeString((FAILCode) hb.mPerHartStatus[hart])
                  );
        } else info.mStuckDog[hart] = true;
      } else {
        info.mLastWatchdog[hart] = hb.mPerHartWatchdog[hart];
        info.mStuckDog[hart] = false;
      }
    }

    {
      HostBlock::LogBuffer & lb = hb.mLogBuffer;
      const u32 BUF_SIZE = 1000u;
      u8 buf[BUF_SIZE+1u];
      u32 idx = 0u;
      while (!lb.isEmpty()) {
        lb.remove(buf[idx]);
        if (++idx >= BUF_SIZE)
          break;
      }
      if (idx != 0u) {
        buf[idx] = 0;
        Eprintf("BH%d:(%2u,%2u)HOBU<<%s>>UBOH\n",mCardNum,hb.mXPos,hb.mYPos,buf);
        // UPDATE FLUSHED HostBlock!
        //mOurTLBs.writeToWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);
      }
    }
    return 0;
  }  

}

