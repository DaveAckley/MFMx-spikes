#include "T6Grid.h"
#include "InterHub.h"
#include "HartTasks.h" // for HTFuncPtr
#include "T6CellO.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll
#include "BlockCode.h" // for STORAGE_COUNT_BC_CORNERHUB

namespace MFM {
  FAST_LOCAL_ARRAY(InterHubEP,8,myInterHubEP,n);

  static RCFlag manageInterHubNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {

      //// ONE-TIME INITS
      theInterHubL1Data.reset(); // zero all

      T6CellO cello;
      if (!cello.init()) FAIL(NO_MATCH);
      U8C ourct6 = cello.getUsCT6();
      U8C ournoc0 = U8C::makeNoC0CoordFromCT6Coord(ourct6);
      u32 ourtlbi = U8C::makeTLBIFromNoCCoord(ournoc0);

      // Enumerate the HUBs surrounding my (Platonic) cell
      const CellBlock & cb = cello.getOurCB();
      u32 ncount = 0u;
      S8C usREGC = cello.mCellNum; // cast to signed
      S8C ourstride = cb.mCellStride;

      const U8C uct6_per_ureg(2,2);        // UCT6 conversion to UREG
      const U8C ureg_per_uscl(2,2);        // UREG conversion to USCL
      HBASSERT_EQ(ourstride,S8C(uct6_per_ureg)); // confirm naming

        /* OK. For InterHub, we're going to divert the diagonal Dir8s
           so that their destinations are the CHUB blocks in the
           orthogonal hub CCW of the Dir8 corner, rather than the
           INTERHUB blocks of the Dir8 neighbor.

           We are also going to try saying that all CHUB blocks are
           'in', and the four corner IHUB blocks are 'out' (regardless
           of their tlbis), and all cars start 'out'.
         */
        /* But despite this diversion, we first need to ensure that
           the actual diagonal destination hub actually exists -
           because the edge diversion could exist even thought the
           diago doesn't. So we'll start by going all the way to ct6
           with the real destination, and only then decide whether to
           divert. */

      for (u8 d = D8_NT; d <= D8_NE; ++d) {
        Dir8 finalDestDir8 = (Dir8) d; // whether diversion or not
        Dir8 firstDestDir8 = finalDestDir8; // DEFAULT: overwritten if diversion

        // ESTABLISH THAT THE FINAL DEST EXISTS OR SKIP
        S8C finalDestREGV = S8C::makeS8CFromDir8(finalDestDir8); // region vector to final dest
        S8C finalDestRegionCT6C = (usREGC + finalDestREGV) * uct6_per_ureg + cb.mLayoutOffset;
        S8C finalDestHubCT6C = finalDestRegionCT6C + cello.mUsCellPos;

        HBPX(d);
        HBPTAG(finalDestDir8,dir8ToByteString(finalDestDir8));
        HBPX(finalDestREGV);
        HBPX(finalDestRegionCT6C);
        HBPX(finalDestHubCT6C);
        if (!S8C::onBoardCT6Coord(finalDestHubCT6C)) continue; // no final dest

        bool goForCHUB = (d&1); // Dir8 corners are all odd
        HBPX(goForCHUB);
        u8 finalDestBlockIndex = oppositeDir8(finalDestDir8);
        u8 firstDestBlockIndex = finalDestBlockIndex;            // DEFAULT
        BlockCode blockCode = BC_INTERHUB;                       // DEFAULT
        S8C firstDestREGV = S8C::makeS8CFromDir8(firstDestDir8); // DEFAULT
        S8C firstDestRegionCT6C =
          (usREGC + firstDestREGV) * uct6_per_ureg + cb.mLayoutOffset; // DEFAULT
        S8C firstDestHubCT6C = firstDestRegionCT6C + cello.mUsCellPos; // DEFAULT

        if (goForCHUB) {
          blockCode = BC_CORNERHUB;
          firstDestDir8 = dir8CornerToCCWDir8Edge(finalDestDir8);
          firstDestBlockIndex = dir8ToDir4CW(firstDestDir8);

          // ESTABLISH THAT THE FIRST DEST EXISTS OR SKIP
          firstDestREGV = S8C::makeS8CFromDir8(firstDestDir8);
          firstDestRegionCT6C = (usREGC + firstDestREGV) * uct6_per_ureg + cb.mLayoutOffset;
          firstDestHubCT6C = firstDestRegionCT6C + cello.mUsCellPos;
          if (!S8C::onBoardCT6Coord(firstDestHubCT6C)) continue; // no first dest
        }

        HBPX(blockCode);
        HBPTAG(bc,getNameFromBlockCode(blockCode));

        HBPTAG(finalDestDir8,dir8ToByteString(finalDestDir8));
        HBPX(finalDestBlockIndex);

        HBPTAG(firstDestDir8,dir8ToByteString(firstDestDir8));
        HBPX(firstDestBlockIndex);

        U8C firstDestHubUCT6;
        bool sane = firstDestHubCT6C.toU8C(firstDestHubUCT6);
        MFM_API_ASSERT(sane,ILLEGAL_STATE);

        HBPX(usREGC);
        HBPX(finalDestREGV);
        HBPX(finalDestHubCT6C);
        HBPX(firstDestHubUCT6);

        // cvt to noc0,
        // search ibas for our iba
        U8C firstDestHubNoC0C = U8C::makeNoC0CoordFromCT6Coord(firstDestHubUCT6);
        HBPX(firstDestHubNoC0C);
        {
          S8C ngbct6off = finalDestHubCT6C-ourct6;

          T6Neighbor ngb;
          if (ngb.init(ournoc0,ngbct6off)) {
            ImageBlockAddr usiba = ngb.findIBAIfAny(blockCode, /*debug=*/false);
            MFM_API_ASSERT(usiba.isValid(),ILLEGAL_STATE);
            u32 theirtlbi = U8C::makeTLBIFromNoCCoord(firstDestHubNoC0C);
            bool weAreIn = goForCHUB ? false : ourtlbi < theirtlbi;

            InterHubEP & ihep = myInterHubEP[finalDestDir8];
            HBPTAG(d8Now,dir8ToByteString(firstDestDir8));
            HBPTAG(8IHEP-WEIN,weAreIn);
            /* srcEPA is IHUB and finalDestDir8 no matter what
               vvvvvvvvvvv  vvvvvv */
            ihep.initInterHubEP({ BC_INTERHUB, finalDestDir8 }, weAreIn, theInterHubL1Data);
            InterHubStorage & ihstg = theInterHubL1Data.mTheTCStorages[finalDestDir8];
            HBPTAG(8IHEP-srcD8,dir8ToByteString(finalDestDir8));
            for (u32 c = 0; c < ihstg.getCarCount(); ++c) {
              InterHubBlock & ihb = ihstg.getTC(c);
              ihb.init();
              HBPTAG(8IHEP-car,c);
            }

            ihep.configureDest(ournoc0, firstDestHubNoC0C, { blockCode, firstDestBlockIndex });
            HBPX(firstDestBlockIndex);
          } else HBPTAG(!NGB@,ngbct6off);
        }
      }

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      //LOGPTAG(IHUBARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS);

      HBNOTE(GOFIH);
      for (u8 d8 = D8_NT; d8 <= D8_NE; ++d8) {
        HBPTAG(d8OPEN,dir8ToByteString((Dir8) d8));
        HBPTAG(ihsta,getNameFromEPState(theInterHubL1Data.getPublicEPState(d8)));
        if (theInterHubL1Data.getPublicEPState(d8) != EPState::CONFIGURED) continue;
        myInterHubEP[d8].activate(); // release the hounds
        HBPTAG(ihsta,getNameFromEPState(theInterHubL1Data.getPublicEPState(d8)));
      }
      HBMARK;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {
      //SNAP(2,LOGPTAG(#IHUBARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS));
      //      HBXTAG(IHLIV,0);
      //// LIVING
      for (u32 d = D8_NT; d <= D8_NE; ++d) {
        Dir8 d8 = (Dir8) d;
        InterHubEP & myIHEPNC = myInterHubEP[d8];
        EACH(10'000'000,{
            //LOGPTAG(IHUBAROHAROHARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS);
            LOGPTAG(d8,dir8ToByteString(d8));
            LOGPX(T6Grid::FULL_HEIGHT);
            LOGPX(T6Grid::FULL_WIDTH);
            LOGPX(T6Grid::SELF_ORIGIN);
            LOGPX(T6Grid::SELF_MAX);
            LOGPX(T6Grid::getCacheRange(d8));
            LOGPX(T6Grid::getCacheRange(d8).dims());
            LOGPX(T6Grid::getCacheRange(d8).area());
            //LOGPTAG(cache/IHPax,(T6Grid::getCacheRange(d8).area()+InterHubPayload::MAX_ATOMS-1)/InterHubPayload::MAX_ATOMS);
          });
        //        HBPTAG(IHUO*,&myIHEPNC);
        if (!myIHEPNC.isInitted()) continue;

        //        HBPTAG(IHUO,n);
        myIHEPNC.updateOps();
        //          HBMARK;
      }
    } else LOGPTAG(unknown htoc,htoc);

    return ret;
  }

  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr interhubEPPtr = &manageInterHubNC;

  /////////////// CORNERHUB FORWARDER ////////////////

  static constexpr u32 CHUB_BLOCK_COUNT = STORAGE_COUNT_BC_CORNERHUB;
  static constexpr u32 IHUB_CARS_PER_CHUB = InterHubStorage::CAR_COUNT;

  struct CornerHubStateNC {
    u32 mCWDir4IHUBBaseAddress;
    u8 mLastCHUBNonceSeen[IHUB_CARS_PER_CHUB];
    u8 mLastNonceInitted[IHUB_CARS_PER_CHUB];
    void init() {
      memset_s(this,'\0',sizeof(*this));
    }
    bool isValid() const {
      return mCWDir4IHUBBaseAddress!=0;
    }
    bool open(S8C ngbct6offset) { //< true if baseaddress set up
      T6Neighbor ngb;
      if (!ngb.init(theHostBlock.mNoC0,ngbct6offset)) return false;
      ImageBlockAddr iba = ngb.findIBAIfAny(BC_INTERHUB, /*debug=*/false);
      if (!iba.isValid()) return false;
      mCWDir4IHUBBaseAddress = iba.mBlockAddr;
      HBPTAG(CHUBOPEN,ngbct6offset);
      HBXTAG(CHUBBASE,mCWDir4IHUBBaseAddress);
      return true;
    }

    void validCarIndex(u8 carindex) const {
      MFM_API_ASSERT(carindex < IHUB_CARS_PER_CHUB,ILLEGAL_ARGUMENT);
    }

    u8 getLastNonce(u8 carindex) const {
      validCarIndex(carindex);
      return mLastCHUBNonceSeen[carindex];
    }

    bool updateSavedNonce(u8 carindex, u8 chubNonce) {
      validCarIndex(carindex);
      const u8 c = carindex;
      bool ret = (mLastCHUBNonceSeen[c] != chubNonce);
      if (unlikely(!mLastNonceInitted[c])) {
        mLastNonceInitted[c] = 1;
        ret = true;
      }
      if (unlikely(ret)) mLastCHUBNonceSeen[c] = chubNonce;
      return ret;
    }
  };

  FAST_LOCAL_ARRAY(CornerHubStateNC,CHUB_BLOCK_COUNT,fastCornerHubStateNC,n);

  static RCFlag manageCornerHubNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {

      //// ONE-TIME INITS
      for (u32 i = 0; i < CHUB_BLOCK_COUNT; ++i) {
        fastCornerHubStateNC[i].init();
      }
      theCornerHubL1Data.reset();
      HBPTAG(CHUBINIT,sizeof(fastCornerHubStateNC));

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {

      for (u32 i = 0; i < CHUB_BLOCK_COUNT; ++i) {
        Dir4 axisd4 = (Dir4) i;
        HBPTAG(CHUBUS4,dir4ToByteString(axisd4));
        Dir4 ngbd4 = nextCWDir4(axisd4);
        HBPTAG(CHUBNG4,dir4ToByteString(ngbd4));
        S8C offct6 = S8C::makeS8CFromDir4(ngbd4)*2;
        HBPTAG(CHUBCT6,offct6);
        CornerHubStateNC & chs = fastCornerHubStateNC[i];
        bool found = chs.open(offct6);
        if (found) {
          HBXTAG(CHUBADDR,chs.mCWDir4IHUBBaseAddress);
        } else {
          HBPTAG(NOCHUB,dir4ToByteString(ngbd4));
        }
      }


    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {

      //// LIVING
      for (u32 i = 0; i < CHUB_BLOCK_COUNT; ++i) {
        Dir4 ourDir4 = (Dir4) i;
        CornerHubStateNC & chs = fastCornerHubStateNC[ourDir4];

        if (!chs.isValid()) continue;
        /// WE HAVE A DEST ADDR FOR THIS CORNER
        
        InterHubStorage & chstg = theCornerHubL1Data.mTheTCStorages[ourDir4];
        for (u32 c = 0; c < InterHubStorage::CAR_COUNT; ++c) {
          EACH(10'000'000,LOGPTAG(CHUBLIVE,__EACHNUM__));
          InterHubBlock & chbcar = chstg.getTC(c);
          if (!chbcar.isComplete()) continue;
          /// WE HAVE A COMPLETE PACKET
          EACH(1'000,LOGNOTE(CHUBCOMPLETE));

          u8 carnonce = chbcar.getHeader().getPacketNonce();
          if (!chs.updateSavedNonce(c,carnonce)) continue;
          /// WITH A NONCE DIFFERENT FROM BEFORE
          EACH(1'000,LOGPTAG(CHUBNEWNONCE,carnonce));
          
          // Forward to dest
          /* Q1: What Midway Axis do we represent?

             A1: We are chub block# 'i', which we can interpret
             both as a Dir4 and as a 'Midway code':

             'iXoY' Midway code
             iEoN D4_W=1
             iNoW D4_S=2
             iWoS D4_E=3
             iSoE D4_N=0

             Q2: Which ngb hub are we forwarding to?

             A2: Everything is set up for us to forward to the
             Dir4 immediately clockwise of the Midway Axis we
             represent:

             Dir4 destHubD4 = nextCWDir4((Dir4) i);

             Q3: Which is at what NoC0?

             A3: It's at 
             S8C stheirct6 = ourct6+2*makeS8CFromDir4(destHubD4);
             U8C theirct6 = stheirct6.toU8COrDie();
             U8C theirnoc0 = U8C::makeNoC0CoordFromCT6Coord(theirct6);

             Q4: And what address are we forwarding to, there? 

             A4: Well, we're forwarding to the IHUB block that's
             the opposite corner of the source IHUB block, and
             then, within that block, we're forwarding to IH car
             slot 'c' - or, equivalently, we forward to the
             address that's the same distance from the beginning
             of the dest IHUB block that we are from the
             beginning of our CHUB block.
                      
             Q5: Sheesh. Well, is that ALL we need to know to
             fucking finally light this candle?

             A5: Probably not. But the only way to be sure is..
          */

          U8C ournoc0 = theHostBlock.mNoC0;
          U8C ourct6 = U8C::makeCT6CoordFromNoC0Coord(ournoc0);

          Dir4 destHubD4 = nextCWDir4(ourDir4);
          S8C ngbct6offset = S8C::makeS8CFromDir4(destHubD4)*2; //2 should be cellstride or something
          S8C stheirct6 = S8C(ourct6)+ngbct6offset;
          U8C theirct6 = stheirct6.toU8COrDie();
          U8C theirnoc0 = U8C::makeNoC0CoordFromCT6Coord(theirct6);

          Dir8 destIHUBIdx = dir8ToCWDir8(dir4ToDir8(oppositeDir4(ourDir4)));

          u32 allIHUBBlocksBaseAddr = chs.mCWDir4IHUBBaseAddress;
          u32 destIHUBBlockBaseAddr = allIHUBBlocksBaseAddr + destIHUBIdx * STORAGE_SIZE_BC_INTERHUB;

          EACH(1,{
              LOGPTAG(CHUBSEQN,ourDir4);
              LOGPX(destHubD4);
              LOGPX(theirnoc0);
              LOGPX(destIHUBIdx);
              LOGXX(destIHUBBlockBaseAddr);
            });
          
          // okay, dest addr is car# * CARSIZE + baseaddr?
          u32 caroff = c * chstg.getCarSize();
          HBASSERT_EQ(caroff,(u32) (((u8*)&chbcar) - ((u8*)&chstg)));
          u32 destaddr = destIHUBBlockBaseAddr + caroff;

          u32 wordCount = chbcar.getHeader().getPacketWords();
          s32 status = NRI3::initiateWriteToT6(ournoc0,(u32*) &chbcar, wordCount, theirnoc0, destaddr);
          MFM_API_ASSERT(status > 0,ILLEGAL_STATE);
          LOGXTAG(CHUBSTILLALIVE,c);
          LOGPX(ournoc0);
          LOGPX(theirnoc0);
          LOGPTAG(car#,c);
          LOGXX(destaddr);
          // Spoil the packet to speed up future rejections. This is
          // technically a race - we officially yielded control of
          // this memory when we shipped it - but the path it's racing
          // against is sooooooooooo much longer I can't even
          // imagine..
          chbcar.getHeader().spoilCompletePacket();
        }
      }
    } else LOGPTAG(unknown htoc,htoc);

    return ret;
  }
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr cornerHubMgrPtr = &manageCornerHubNC;

}
