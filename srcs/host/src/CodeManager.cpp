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
#include "FileIDs.h" // for GET_PATH_FROM_FILE_ID
#include "UxC.h"
#include "HostRandom.h" // for hostPRNG

namespace MFM {
  void CodeManager::dumpT6Image(const T6Image & image, u8 fromTLBI) {
    u32 rvsize = image.getBinFileSize();
    u8 * bytes = extractRISCVCodeFromTLBI(0, rvsize, fromTLBI);
    BHLog & bhl = BHLog::getTheBHLog();
    std::string path = "/tmp/dumpity.dump";
    std::ofstream file(path,std::ios::binary);
    if (file) {
      file.write((const char*) bytes, rvsize);
      file.close();
      LOGprintf(mChipNum," Wrote %uB of '%s' to '%s'\n",
                rvsize, image.getName().c_str(),
                path.c_str());
    } else {
      LOGprintf(mChipNum," Couldn't write '%s'\n",path.c_str());
    }

    delete [] bytes;
    bytes = 0;
  }

  u8 * CodeManager::extractRISCVCodeFromTLBI(u32 baseaddress, u32 rvsize, u8 fromTLBI) {
    U8C fromnoc = U8C::makeUxCNoCCoordFromTLBI(fromTLBI);
    if (!U8C::isNoC0CoordAT6(fromnoc)) return 0;
    
    BHLog & bhl = BHLog::getTheBHLog();
    BHTag tag(TagType::T6TADR, mChipNum, fromTLBI);
    LOGprintf(mChipNum," Extracting %uB of L1 starting at address 0x%x of TLBI%u noc(%u,%u)\n",
              rvsize, baseaddress, fromTLBI, fromnoc.x,fromnoc.y);

    u8 * rvcode = new u8[rvsize];
    u32 * codewords = (u32*) rvcode;

    mOurTLBs.readFromBytes(fromTLBI, baseaddress, rvcode, rvsize);
    return rvcode;              // CALLER TAKES OWNERSHIP
  }

  s32 CodeManager::deployRISCVCodeFromImage(const T6Image & image, u8 toTLBI) {
    BHLog & bhl = BHLog::getTheBHLog();
    BHTag tag(TagType::T6TADR, mChipNum, toTLBI);
    u32 rvsize = image.getBinFileSize();
    const char * rvcode = image.getTheBinFile();
    u32 * codewords = (u32*) rvcode;
    u32 wordcount = rvsize >> 2u;
    LOGprintf(mChipNum," LENGTH=%d (0x%08x, 0x%08x, 0x%08x, 0x%08x)\n",
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

    LOGprintf(mChipNum," HB0 %u/0x%x %lu T6HBA:0x%08x\n   hb %p hr %p hn 0x%lx MC 0x%x CM 0x%x\n",
           rvsize, rvsize, sizeof(HostBlock), hostblockt6addr,
           hb, mOurTLBs.hostRAMPtr(),
           mOurTLBs.hostRAMNoCAddr(),
           hb->mHBMagic, hb->mHBCigam);

    memset_s(hb,'\0',sizeof(*hb)); // Clear all
    u64 hostBufferBase = mOurTLBs.hostRAMNoCAddr();
    hb->mHostBaseAddrLo = (u32) (hostBufferBase & 0xffffffff);
    hb->mHostBaseAddrHi = (u32) ((hostBufferBase>>32) & 0xffffffff);
    hb->mAIClockFrequency = (u32) 800'000'000u; // XXX ASSUME 'IDLE' CLOCK SPEED FOR NOW
    hb->mChipNum = mChipNum < U8_MAX ? mChipNum : U8_MAX;
    hb->mCommonArgs[0] = time(0); // per-run nonce
    hb->mCommonArgs[1] = mStartDecayType; // optional start symbol behavior selection
    hb->mHBMagic = HostBlock::HBMAGIC;
    hb->mHBCigam = HostBlock::HBCIGAM;
    LOGprintf(mChipNum," HB1 0x%08x 0x%08x\n",
           hb->mHostBaseAddrHi,
           hb->mHostBaseAddrLo);

    if (toTLBI == U8_MAX) {
      // "Multicast all the code to the entire fleet"
      LOGprintf(mChipNum," Multicasting image '%s' to the fleet\n",
                image.getName().c_str());
      mOurTLBs.writeToWords(OurTLBs::AHAX_TLBI_L1_MULTI, 0u, codewords, wordcount);
      for (u32 i = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
           i <= OurTLBs::AHAX_TLBI_L1_LAST_UNI; ++i) 
        mOurTLBs.getTLBInfo(i).setDeployedImage(image);
    } else {
      U8C c = U8C::makeCT6CoordFromTLBI(toTLBI);
      U8C nocc = U8C::makeUxCNoCCoordFromTLBI(toTLBI);
      LOGprintf(mChipNum," Deploying '%s' to TLBI%u cell(%u,%u) noc(%u,%u)\n",
                image.getName().c_str(),toTLBI,c.x,c.y,nocc.x,nocc.y);
      mOurTLBs.writeToWords(toTLBI, 0u, codewords, wordcount);
      mOurTLBs.getTLBInfo(toTLBI).setDeployedImage(image);

      //// >>FOR UNICAST ONLY<< REMEMBER TLBI IF IMAGE PROVIDES BC_T6GRID
      {
        u32 t6gridaddr = mOurTLBs.getTLBInfo(toTLBI).mT6GridStart;
        if (t6gridaddr!=0) {
          char * l1base = mOurTLBs.getL1HostAddressForTLBI(toTLBI);
          T6Grid * t6gp = (T6Grid*) (l1base + t6gridaddr);
          U8C noc0 = U8C::makeUxCNoCCoordFromTLBI(toTLBI);
          addHub(toTLBI,*t6gp);

          // XXXXX DEBUG
          if (!image.hasCellBlock()) Eprintf("\nNO CELL BLOCK FOR %u??\n", toTLBI);
          else {
            CellBlock cb = image.copyCellBlockOrDie();
            U8C stride = cb.mCellStride;
            U16C c = DG::getChipOrigin(mChipNum); 
            U16C o = c + DG::getTLBIOrigin(toTLBI,stride); 
            U16C s = DG::getSingleT6GridSize();
            U16C e = o+s;
            std::string rep = s.to_string()+"@"+o.to_string()+"-"+e.to_string();
            Eprintf("\n %s,HUBADDED at %p from 0x%08x noc[%u,%u] %s\n",
                    BHTag::t6adt(mChipNum,toTLBI).c_str(),t6gp,t6gridaddr,noc0.x,noc0.y,rep.c_str());
            if (false) {
              // MORE DEBUG
              U16C globalsize = DG::getGlobalGridSize();
              DG::Coord center(hostPRNG.Between(0,2),//globalsize.x-1),
                               hostPRNG.Between(0,2));//globalsize.y-1));
              DG::Address seedaddr = DG::mapCoordToAddress(center);
              if (seedaddr.isValid()) {
                Eprintf("HEWO QUIETBOX! %s\n",QuietBox::get().to_string().c_str());
                bool q = QuietBox::get().storeP4Atom(center,P4Atom::makeStartAtom());
                Eprintf("WANTED TO SEED (%u,%u) -> BH#%u cn<%u,%u> (%u,%u), AND %u\n",
                        center.x,center.y,
                        seedaddr.mChipNum,
                        seedaddr.mCellNum.x, seedaddr.mCellNum.y,
                        seedaddr.mT6GridC.x, seedaddr.mT6GridC.y,
                        q);
              } else {
                Eprintf("WANT TO SEED (%u,%u) -> fail?\n",
                        center.x,center.y);
              }
            }
          }
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
          LOGprintf(mChipNum,"IMGBLOCKREREAD 0x%x:0x%08x\n",word<<2u,data);
        if (codewords[word] != data) {
          ++misses;
          LOGprintf(mChipNum,"{%d},%3d.   MISS %d @ 0x%x: got 0x%08x need 0x%08x\n",
                    mChipNum, tlbi, misses, word<<2, data, codewords[word]);
        } else {
          ++hits;
          if (false && word >= MINWORD && word <= MAXWORD)
            LOGprintf(mChipNum,"%3d. Hit %2d on 0x%x:0x%08x\n",tlbi, hits, word<<2, data);
        }
      }
        
      // confirm certain addresses are in 'prerun' state
      u32 hostblockaddr = rvsize - sizeof(HostBlock); // ASSUMES HOSTBLOCK IS LAST IN IMAGE!
      //LOGprintf(mChipNum,"SPOTCHECKING HB AT %u/%x\n",hostblockaddr,hostblockaddr);
      {
        HostBlock rbhb;
        memset_s(&rbhb,0,sizeof(rbhb));
        mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) & rbhb, sizeof(HostBlock)>>2u);
        u32 hbmagicpre = rbhb.mHBMagic;
        //printf("HostBlock magic %x\n", hbmagicpre);
        //printf("HostBlock x %u y %u\n", rbhb.mNoC0.x, rbhb.mNoC0.y);
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
    LOGprintf(mChipNum," REIMLENGTH=%d (0x%08x, 0x%08x, 0x%08x, 0x%08x)\n",
              rvsize,
              codewords[5],
              codewords[6],
              codewords[7],
              codewords[8]
              );
        
    LOGprintf(mChipNum,"SPOT CHECK READBACK: hits=%d misses=%d\n", hits, misses);
    return 0;
  }

  void CodeManager::releaseTheHounds() {
    LOGprintf(mChipNum,"PHASE-------RELEASE THE HOUNDS\n");
    mOurTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                     RISCV_DEBUG_REG_SOFT_RESET_0,
                     SOFT_RESET_ALL_RISCV_EXCEPT_B);
    sleepUsec(1'000'000);
    LOGprintf(mChipNum,"PHASE-------Check magic\n");
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
        U16C noc = U16C::makeNoCCoordFromTLBI(tlbi);
        BHTag tag(TagType::T6TADR, mChipNum, noc.x, noc.y);
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
        LOGprintf(mChipNum,"No image deployed to tlbi %u, skipping\n",tlbi);
        continue;
      }
      const T6Image & t6i = *t6ip;

      u32 hostblockaddr = t6i.getBinFileSize() - sizeof(HostBlock);
      LOGprintf(mChipNum,"GOODMAGICKING HB AT %u/%x\n",hostblockaddr,hostblockaddr);
      HostBlock hb;
      memset_s(&hb,0u,sizeof(hb)); //<< memset_s(,0,) uses explicit_bzero host side

      mOurTLBs.readFromWords(tlbi, hostblockaddr, (u32*) &hb, sizeof(hb)>>2u);
      if (false) {
        LOGprintf(mChipNum,"HBMAGIC 0x%08x @ %u vs %u (%u,%u)[%d,%d,%d,%d,%d]\n",
               hb.mHBMagic,tlbi,hb.mTLBI,
               hb.mNoC0.x,hb.mNoC0.y,
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
      U16C nocc = U16C::makeNoCCoordFromTLBI(tlbi);
      if (true && (hb.mNoC0.x != nocc.x || hb.mNoC0.y != nocc.y || hb.mTLBI != tlbi))
        HOST_FATAL(BAD_VALUE,"Bad NOC0 COORD (%u,%u) wanted (%u,%u) @ %u vs %u\n",
                   hb.mNoC0.x,hb.mNoC0.y,
                   nocc.x, nocc.y,
                   tlbi, hb.mTLBI
                   );
      if (true) {
        HostBlock::LogBuffer & lb = hb.mLogBuffer;
        const u32 BUF_SIZE = 1000u;
        u16 buf[BUF_SIZE+1u];
        u32 idx = 0u;
        while (!lb.isEmpty()) {
          lb.remove(buf[idx]);
          if (++idx >= BUF_SIZE)
            break;
        }
        if (idx != 0u) {
          buf[idx] = 0;
          LOGprintf(mChipNum,"(%u,%u)HB<%s>\n",hb.mNoC0.x,hb.mNoC0.y,buf);
        }
      }
    }
    LOGprintf(mChipNum,"  ALL HBMAGIC+ IS GOOD\n");
  }

  s32 CodeManager::awaitResults() {
    FAIL(INCOMPLETE_CODE);
#if 0
XXX    u32 hostblockaddr = mRVCodeSize - sizeof(HostBlock);
    for (u32 tries = 0u; tries < 100u; ++tries) {
      u32 stats[140] = { 0u };
      u32 living[140] = { 0u };
      u32 allDone = 0u;
      LOGprintf(mChipNum,"PASS %d ",tries);
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
            bool first = true;
            s32 ch;
            while ((ch = hb.removeByte()) >= 0) {
              if (first) Eprintf("%d HOSTBUFvvvvv\n",tlbi);
              first = false;
              Eprintf("%c",(u8) ch);
            }
            if (!first) Eprintf("%d HOSTBUF^^^^^\n",tlbi);
        }

        U16C nocc = U16C::makeNoCCoordFromTLBI(tlbi);
        if (hb.mNoC0.x != nocc.x || hb.mNoC0.y != nocc.y)
          LOGprintf(mChipNum,"CROOD MIMSATCH %u (%u,%u) vs (%u,%u)\n",
                 tlbi, nocc.x, nocc.y,
                 hb.mNoC0.x, hb.mNoC0.y);
        if (true && tries<3 && tlbi<10u)
          LOGprintf(mChipNum,"\n[%u] %d,%d,%d,%d,%d\n",
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
            LOGprintf(mCnipNum,"TLBI %u %s FAIL%d: %s\n",
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

  u32 CodeManager::readT6GridTotalChanges(T6Grid& t6g, u32 tlbi) {
    OurTLBs::TLBInfo & info = mOurTLBs.getTLBInfo(tlbi);
    u32 t6gridaddr = info.mT6GridStart;

    if (t6gridaddr == 0u) return 0; // no t6grid in this one??

    u32 readaddr = t6gridaddr + offsetof(T6Grid,mTotalChanges);
    u32 count;
    mOurTLBs.readFromWords(tlbi, readaddr, &count, sizeof(u32)>>2);
    return count;
  }

  BGRImageHD & CodeManager::renderT6GridToImage(const T6Grid& t6g, const T6GridInfo & t6i) {
    BGRImageHD & bgr = QuietBox::getT6GridImage();
    U16C t6origin(t6i.mT6GridOrigin.x,t6i.mT6GridOrigin.y);
    if (false)
      Eprintf("RND6OR(%u,%u) aval %u BH#%u GC(%u,%u)\n",
              t6origin.x,t6origin.y,
              t6i.mDGAddress.isValid(),
              t6i.mDGAddress.mChipNum,
              t6i.mDGAddress.mT6GridC.x,t6i.mDGAddress.mT6GridC.y
              );
    RGBPix c;
    U16C size = T6Grid::getGridSize();
    for (u32 x = 0u; x < size.x; ++x) {
      for (u32 y = 0u; y < size.y; ++y) {
        U16C atomc(x,y);
        c.set(15u,10u,5u); // assume blackish (empty)
        //        c.set(0xee,0xaa,0x66); // DEBUG: something VISIBLE

        P4Atom a = t6g.getAtom(atomc);
        u16 t = a.getType();
        switch (t) {
        case P4Atom::EMPTY_TYPE:
          break; // render all sites including empties, since we're not resetting the img..

        case 2u: // (DReg)
          c.set(250u,20u,30u);
          break;

        case 3u: // (Res)
          c.set(220u,220u,20u);
          break;

        case 5u: // MAXFB
          {
            constexpr u32 slowBits = 1u;
            u32 val = a.mStg[1]; // get hidden counter
            u8 rd = (val>>0+slowBits)&0xf; rd = (rd-8)*(rd-8);
            u8 gd = (val>>4+slowBits)&0xf; gd = (gd-8)*(gd-8);
            u8 bd = (val>>8+slowBits)&0xf; bd = (bd-8)*(bd-8);
            c.set(50u+3u*rd,50u+3u*gd,50+3u*bd);
            if (false)
              Eprintf("(%u,%u) fbrgb(%u,%u,%u)\n",
                      atomc.x,atomc.y,
                      c.mRGB[0],c.mRGB[1],c.mRGB[2]);
            break;
          }
        case P4Atom::INACCESSIBLE_TYPE: c.set(0x30,0x40,0x50);
          break;
        default:
          c.set((u8) (t*50), 20u, (u8) (255-(t*50)));
        }
        {
          U16C pixc = t6origin+atomc;
          bgr.setPixel(pixc, c);
          RGBPix reread = bgr.getPixel(pixc);
          if (false)
            Eprintf("RITE2(%u,%u) = 0x%02x%02x%02x\n",
                    pixc.x,pixc.y,
                    c.mRGB[0],c.mRGB[1],c.mRGB[2]);
        }
      }
    }
    return bgr;
  }

  void CodeManager::readAndDisplayT6Grid(T6Grid &t6g, u32 tlbi) {
    OurTLBs::TLBInfo & info = mOurTLBs.getTLBInfo(tlbi);
    u32 t6gridaddr = info.mT6GridStart;

    MFM_API_ASSERT(t6gridaddr != 0, ILLEGAL_STATE);
    // Reread whole t6grid
    T6Grid tmp;
    mOurTLBs.readFromWords(tlbi, t6gridaddr, (u32*)&tmp, sizeof(t6g)>>2);
    //    mOurTLBs.readFromWords(tlbi, t6gridaddr, (u32*)&t6g, sizeof(t6g)>>2);
    QuietBox & qb = QuietBox::get();
    const T6GridInfo & t6i = qb.getT6GridInfoByChipAndTLBI(mChipNum,tlbi);
    Eprintf("BH#%u totalchanges %u %u HD(%u,%u) dga(%u,%u)\n",
            mChipNum, tmp.mTotalChanges, t6i.mTLBI,
            t6i.mT6GridOrigin.x, t6i.mT6GridOrigin.y,
            t6i.mDGAddress.mT6GridC.x, t6i.mDGAddress.mT6GridC.y);
    renderT6GridToImage(tmp,t6i);
  }

  s32 CodeManager::scanHubGrid() {
    s32 ret = 0;
    for (auto & item : mHubTLBIToT6Grid) {
      HubTLBI tlbi = item.first;
      ChangeCount ccnt = item.second.first;
      T6Grid * tgp = item.second.second;

      u32 tcnt = readT6GridTotalChanges(*tgp,tlbi);
      if (ccnt != tcnt) {
        item.second.first = tcnt;
        readAndDisplayT6Grid(*tgp,tlbi);
        ret++;
      }
    }
    return ret;
  }

#if 0 // OLD  
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
    return ret;
  }
#endif

  s32 CodeManager::slowScanHostBlocks() {
    if (mLastTLBISlowScanned >= OurTLBs::AHAX_TLBI_L1_LAST_UNI)
      mLastTLBISlowScanned = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
    else
      ++mLastTLBISlowScanned;

    u32 tlbi = mLastTLBISlowScanned;
    U8C nocc = U8C::makeUxCNoCCoordFromTLBI(tlbi);
    OurTLBs::TLBInfo & info = mOurTLBs.getTLBInfo(tlbi);
    const T6Image * t6ip = info.getDeployedImageIfAny();
    if (!t6ip) {
      LOGprintf(mChipNum,"No image deployed to tlbi %u, skipping\n",tlbi);
      return 0;
    }
    const T6Image & t6i = *t6ip;
    u32 hostblockaddr = t6i.getHostBlockAddr();

    BHLog & bhl = BHLog::getTheBHLog();
    BHTag tag(TagType::T6TADR, mChipNum, tlbi);
    //bhl.printf(tag,"IMCO %s sz%d hb0x%08x\n",
    if (false)
      Eprintf("%s[%s] IMCO sz%d hb0x%08x\n",
              BHTag::t6adc(mChipNum,nocc.x,nocc.y).c_str(),
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
              mChipNum,nocc.x,nocc.y,
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

    bool anystuck = false;
    for (u32 hart = 0u; hart < 5u; ++hart) {
      if (info.mLastWatchdog[hart] == hb.mPerHartWatchdog[hart]) {
        if (info.mStuckDog[hart]) {
          anystuck = true;
          if (hb.mPerHartFailFileID[hart] != 0) {
            const char * path = GET_PATH_FROM_FILE_ID(hb.mPerHartFailFileID[hart]);
            while (*path) if (*path++ == '/') break; // hack: eat mfmx/ prefix
            Eprintf("%.03f %s[%s] %s STUCK?\n%s:%u: %s\n",
                    runTimeSeconds(),
                    BHTag::t6adc(mChipNum,hb.mNoC0.x,hb.mNoC0.y).c_str(),
                    t6i.getName().c_str(),
                    hartName(hart),
                    path,
                    hb.mPerHartFailFileLine[hart],
                    getFailCodeString((FAILCode) hb.mPerHartStatus[hart])
                    );
          } else {
            Eprintf("%.03f %s[%s] %s NOFID? 0x%08x = FAIL%d:%s\n",
                    runTimeSeconds(),
                    BHTag::t6adc(mChipNum,hb.mNoC0.x,hb.mNoC0.y).c_str(),
                    t6i.getName().c_str(),
                    hartName(hart),
                    hb.mPerHartWatchdog[hart],
                    hb.mPerHartStatus[hart],
                    getFailCodeString((FAILCode) hb.mPerHartStatus[hart])
                    );
          }
        } else info.mStuckDog[hart] = true;
      } else {
        info.mLastWatchdog[hart] = hb.mPerHartWatchdog[hart];
        info.mStuckDog[hart] = false;
      }
    }

    if (anystuck && !info.mHasBeenDumped) {
      dumpT6Image(t6i,tlbi);
      info.mHasBeenDumped = true;
    }

    {
      HostBlock::LogBuffer & lb = hb.mLogBuffer;
      const u32 BUF_SIZE = sizeof(HostBlock::LogBuffer);
      u16 buf[BUF_SIZE+1u];
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
        Eprintf("%.03f %s[%s]<<<%s>>>%s[%s]\n",
                runTimeSeconds(),
                BHTag::t6adc(mChipNum,hb.mNoC0.x,hb.mNoC0.y).c_str(),
                t6i.getName().c_str(),
                buf,
                BHTag::t6adc(mChipNum,hb.mNoC0.x,hb.mNoC0.y).c_str(),
                t6i.getName().c_str());
      }
    }
    return 0;
  }  

}

