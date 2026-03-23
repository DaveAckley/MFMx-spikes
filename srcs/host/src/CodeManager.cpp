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
#include "T6Grid.h"

namespace MFM {
  s32 CodeManager::deployRISCVCodeFromImage(const T6Image & image, u8 toTLBI) {
    BHLog & bhl = BHLog::getTheBHLog();
    BHTag tag(TagType::T6TADR, mCardNum, toTLBI);
    u32 rvsize = image.getBinFileSize();
    const char * rvcode = image.getTheBinFile();
    u32 * codewords = (u32*) rvcode;
    u32 wordcount = rvsize >> 2u;
    LOGprintf(mCardNum," LENGTH=%d (0x%08x, 0x%08x, 0x%08x, 0x%08x)\n",
           rvsize,
           codewords[5],
           codewords[6],
           codewords[7],
           codewords[8]
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
    hb->mPos.y = 99;             // but pre-blow ypos
    LOGprintf(mCardNum," HB1 0x%08x 0x%08x\n",
           hb->mHostBaseAddrHi,
           hb->mHostBaseAddrLo);

    if (toTLBI == U8_MAX) {
      // "Multicast all the code to the entire fleet"
      LOGprintf(mCardNum," Multicasting image '%s' to the fleet\n",
                image.getName().c_str());
      mOurTLBs.writeToWords(OurTLBs::AHAX_TLBI_L1_MULTI, 0u, codewords, wordcount);
      for (u32 i = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
           i <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++i) 
        mOurTLBs.getTLBInfo(i).setDeployedImage(image);
    } else {
      U8C c = U8C::makeCT6CoordFromTLBI(toTLBI);
      U8C nocc = U8C::makeU8CNoCCoordFromTLBI(toTLBI);
      LOGprintf(mCardNum," Deploying '%s' to TLBI%u cell(%u,%u) noc(%u,%u)\n",
                image.getName().c_str(),toTLBI,c.x,c.y,nocc.x,nocc.y);
      mOurTLBs.writeToWords(toTLBI, 0u, codewords, wordcount);
      mOurTLBs.getTLBInfo(toTLBI).setDeployedImage(image);

      //// >>FOR UNICAST ONLY<< REMEMBER TLBI IF IMAGE PROVIDES BC_T6GRID
      {
        u32 t6gridaddr = mOurTLBs.getTLBInfo(toTLBI).mT6GridStart;
        if (t6gridaddr!=0) {
          char * l1base = mOurTLBs.getL1HostAddressForTLBI(toTLBI);
          T6Grid * t6gp = (T6Grid*) (l1base + t6gridaddr);
          addHub(toTLBI,*t6gp);
          Eprintf("HUBADDED %u at %p\n",toTLBI,t6gp); 
        }
      }

    }

    // Waste Some Time OK
    // sleepUsec(1'000'000);

    {
      bhl.printf(tag,"IBLI %s 24:0x%08x 32:0x%08x",
                 image.getName().c_str(),
                 codewords[6],codewords[8]);
    }    

    // Let's try some sanity read-backs..
    u32 hits = 0u, misses = 0u;
    u32 first, last, stride = 1u;
    if (toTLBI == U8_MAX) {
      first = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
      last = OurTLBs::AHAX_TLBI_L1_LAST_UNI;
    } else {
      first = last = toTLBI;
    }
    for (u32 tlbi = first; tlbi <= last; tlbi += stride) {
      for (u32 word = 0u; word < wordcount; word += 1) {
        u32 byteaddr = word<<2u; // 4 bytes/word
        u32 data = mOurTLBs.read32(tlbi, byteaddr);
        const u32 MINWORD = 4u;
        const u32 MAXWORD = 9u;
        if (false && word >= MINWORD && word <= MAXWORD)
          LOGprintf(mCardNum,"IMGBLOCKREREAD 0x%x:0x%08x\n",word<<2u,data);
        if (codewords[word] != data) {
          ++misses;
          LOGprintf(mCardNum,"{%d},%3d.   MISS %d @ 0x%x: got 0x%08x need 0x%08x\n",
                    mCardNum, tlbi, misses, word<<2, data, codewords[word]);
        } else {
          ++hits;
          if (false && word >= MINWORD && word <= MAXWORD)
            LOGprintf(mCardNum,"%3d. Hit %2d on 0x%x:0x%08x\n",tlbi, hits, word<<2, data);
        }
      }
        
      // confirm certain addresses are in 'prerun' state
      u32 hostblockaddr = rvsize - sizeof(HostBlock); // ASSUMES HOSTBLOCK IS LAST IN IMAGE!
      //LOGprintf(mCardNum,"SPOTCHECKING HB AT %u/%x\n",hostblockaddr,hostblockaddr);
      {
        HostBlock rbhb;
        memset_s(&rbhb,0,sizeof(rbhb));
        mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) & rbhb, sizeof(HostBlock)>>2u);
        u32 hbmagicpre = rbhb.mHBMagic;
        //printf("HostBlock magic %x\n", hbmagicpre);
        //printf("HostBlock x %u y %u\n", rbhb.mPos.x, rbhb.mPos.y);
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
    LOGprintf(mCardNum," REIMLENGTH=%d (0x%08x, 0x%08x, 0x%08x, 0x%08x)\n",
              rvsize,
              codewords[5],
              codewords[6],
              codewords[7],
              codewords[8]
              );
        
    LOGprintf(mCardNum,"SPOT CHECK READBACK: hits=%d misses=%d\n", hits, misses);
    return 0;
  }

  void CodeManager::releaseTheHounds() {
    LOGprintf(mCardNum,"PHASE-------RELEASE THE HOUNDS\n");
    mOurTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                     RISCV_DEBUG_REG_SOFT_RESET_0,
                     SOFT_RESET_ALL_RISCV_EXCEPT_B);
    sleepUsec(1'000'000);
    LOGprintf(mCardNum,"PHASE-------Check magic\n");
    assertGoodMagic();
  }

  u32 CodeManager::newFails(T6FailCallback cb) {
    FAIL(INCOMPLETE_CODE);
    u32 ret = 0u;
#if 0
XXX    u32 hostblockaddr = mRVCodeSiez - sizeof(HostBlock);
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

#endif
    return ret;
  }

  void CodeManager::assertGoodMagic() {
    sleepUsec(1'000'000);
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++tlbi) {
      OurTLBs::TLBInfo & tinfo = mOurTLBs.getTLBInfo(tlbi);
      const T6Image * t6ip = tinfo.getDeployedImageIfAny();
      if (!t6ip) {
        LOGprintf(mCardNum,"No image deployed to tlbi %u, skipping\n",tlbi);
        continue;
      }
      const T6Image & t6i = *t6ip;

      u32 hostblockaddr = t6i.getBinFileSize() - sizeof(HostBlock);
      LOGprintf(mCardNum,"GOODMAGICKING HB AT %u/%x\n",hostblockaddr,hostblockaddr);
      HostBlock hb;
      memset_s(&hb,0u,sizeof(hb)); //<< memset_s(,0,) uses explicit_bzero host side

      mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);
      if (false) {
        LOGprintf(mCardNum,"HBMAGIC 0x%08x @ %u vs %u (%u,%u)[%d,%d,%d,%d,%d]\n",
               hb.mHBMagic,tlbi,hb.mTLBI,
               hb.mPos.x,hb.mPos.y,
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
      if (true && (hb.mPos.x != nocc.x || hb.mPos.y != nocc.y || hb.mTLBI != tlbi))
        HOST_FATAL(BAD_VALUE,"Bad NOC0 COORD (%u,%u) wanted (%u,%u) @ %u vs %u\n",
                   hb.mPos.x,hb.mPos.y,
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
          LOGprintf(mCardNum,"(%u,%u)HB<%s>\n",hb.mPos.x,hb.mPos.y,buf);
        }
      }
    }
    LOGprintf(mCardNum,"  ALL HBMAGIC+ IS GOOD\n");
  }

  s32 CodeManager::awaitResults() {
    FAIL(INCOMPLETE_CODE);
#if 0
XXX    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
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
        if (hb.mPos.x != nocc.x || hb.mPos.y != nocc.y)
          LOGprintf(mCardNum,"CROOD MIMSATCH %u (%u,%u) vs (%u,%u)\n",
                 tlbi, nocc.x, nocc.y,
                 hb.mPos.x, hb.mPos.y);
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
#endif
    return -1;
  }

  s32 CodeManager::scanHubGrids() {
    s32 ret = 0;
    for (auto & item : mHubTLBIToT6Grid) {
      HubTLBI tlbi = item.first;
      ChangeCount ccnt = item.second.first;
      T6Grid * tgp = item.second.second;
      u32 tcnt = tgp->mTotalChanges;
      if (ccnt != tcnt) {
        item.second.first = tcnt;
        HNprintf(1000,"SHGD %u %u->%u %p\n",tlbi, ccnt, tcnt, tgp);
      }
    }
#if 0
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         ++tlbi) {

      OurTLBs::TLBInfo & info = mOurTLBs.getTLBInfo(tlbi);
      u32 t6gridaddr = info.mT6GridStart;

      if (t6gridaddr == 0u) continue; // no t6grid in this one

      T6Grid t6g;
      mOurTLBs.readFromWords(tlbi, t6gridaddr, (u32*) &t6g, sizeof(t6g)>>2);
      HNprintf(200,"SCANHUB %u %p (0x%x)\n", tlbi, &t6g, t6gridaddr);
      ++ret;
    }
#endif
    return ret;
  }

  s32 CodeManager::slowScanHostBlocks() {
    if (mLastTLBISlowScanned >= OurTLBs::AHAX_TLBI_L1_LAST_UNI)
      mLastTLBISlowScanned = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
    else
      ++mLastTLBISlowScanned;

    u32 tlbi = mLastTLBISlowScanned;
    U8C nocc = U8C::makeU8CNoCCoordFromTLBI(tlbi);
    OurTLBs::TLBInfo & info = mOurTLBs.getTLBInfo(tlbi);
    const T6Image * t6ip = info.getDeployedImageIfAny();
    if (!t6ip) {
      LOGprintf(mCardNum,"No image deployed to tlbi %u, skipping\n",tlbi);
      return 0;
    }
    const T6Image & t6i = *t6ip;
    u32 hostblockaddr = t6i.getHostBlockAddr();

    BHLog & bhl = BHLog::getTheBHLog();
    BHTag tag(TagType::T6TADR, mCardNum, tlbi);
    //bhl.printf(tag,"IMCO %s sz%d hb0x%08x\n",
    if (false) Eprintf("BH%d:(%u,%u) IMCO %s sz%d hb0x%08x\n",
            mCardNum,nocc.x,nocc.y,
            t6i.getName().c_str(),
            t6i.getBinFileSize(),
            hostblockaddr);
    {
      const char * rvcode = t6i.getTheBinFile();
      u32 * codewords = (u32*) rvcode;
      u32 word[4]; // peek at imageblock
      char flag[4];
      bool anyfail = false;
      mOurTLBs.readFromWords(tlbi, 0x14, word, sizeof(word)>>2);
      for (u32 i = 0u; i < 4u; ++i) {
        flag[i] = word[i] == codewords[(0x14>>2)+i] ? ' ' : '>';
        if (flag[i] == '>') anyfail = true;
      }
      if (false) Eprintf("BH%d:(%u,%u) %s %s"
              "%c0x%x:0x%08x"
              "%c0x%x:0x%08x"
              "%c0x%x:0x%08x"
              "%c0x%x:0x%08x"
              "\n",
              mCardNum,nocc.x,nocc.y,
              anyfail ? "IXFAIL" : "IXGOOD",
              t6i.getName().c_str(),
              flag[0], (0<<2)+0x14, word[0],
              flag[1], (1<<2)+0x14, word[1],
              flag[2], (2<<2)+0x14, word[2],
              flag[3], (3<<2)+0x14, word[3]
              );

    }

    HostBlock hb;
    memset_s(&hb,0,sizeof(hb));
    mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);

    if (hb.mHBMagic != HostBlock::HBMAGIC) {
      bhl.printf(tag,"  BAD MAGIC 0x%08x @ 0x%08x\n",hb.mHBMagic, hostblockaddr);
      return -1;
    }
    if (hb.mHBCigam != HostBlock::HBCIGAM) {
      bhl.printf(tag,"  BAD CIGAM 0x%08x\n",hb.mHBCigam);
      return -2;
    }

    for (u32 hart = 0u; hart < 5u; ++hart) {
      if (info.mLastWatchdog[hart] == hb.mPerHartWatchdog[hart]) {
        if (info.mStuckDog[hart]) {
          Eprintf("BH%d:(%2u,%2u)%s STUCK? 0x%08x = FAIL%d:%s\n",
                  mCardNum,hb.mPos.x,hb.mPos.y,hartName(hart),
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
      const u32 BUF_SIZE = sizeof(HostBlock::LogBuffer);
      u8 buf[BUF_SIZE+1u];
      u32 idx = 0u;
      while (!lb.isEmpty()) {
        lb.remove(buf[idx]);
        if (++idx >= BUF_SIZE)
          break;
      }
      if (idx != 0u) {
        // UPDATE FLUSHED HostBlock!
        mOurTLBs.writeToWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);

        buf[idx] = 0;
        Eprintf("%.03f BH%d:(%u,%u)HOBU<<%s>>UBOH\n",
                runTimeSeconds(),
                mCardNum,hb.mPos.x,hb.mPos.y,buf);
      }
    }
    return 0;
  }  

}

