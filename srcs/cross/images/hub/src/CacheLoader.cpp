#include "CacheLoader.h"
#include "Debug.h"
#include "InterHub.h"
#include "HubLiveB.h"  // for fB
#include "EthSeam.h"
#include "DemoGlobal.h"

namespace MFM {
  CacheLoaders theL1CacheLoaders;

  void CacheLoaders::configureCacheLoaders(u8 arg) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    L1GridManagerControl & gml1 = theL1GridManagerControl;
    gml1.setSuperCellLeader((u8) arg); // NOLOK??
    U8C ldrREGV_of_SCLC = U8C::makeSuperCellCoordFromLeaderCode((u32) arg);

    /// FIND LEADER DIR8(S) AND CONFIGURE CACHELOADERS
    theL1CacheLoaders.init();

    mConfiguredSCL = arg;

    HostBlock & hb = theHostBlock;
    LOGPTAG(TBSL#,100*hb.mTLBI + mConfiguredSCL);

    GridManager & gm = fB.mGridManager;
    bool iAmLeader = (mConfiguredSCL==gm.getOurSuperCellPosition());
    LOGPTAG(IAM..,iAmLeader ? "LEADER" : "FOLLOWER");

    U8C uct6_per_ureg(2,2);        // UCT6 conversion to UREG
    U8C ureg_per_uscl(2,2);        // UREG conversion to USCL
    // uscl_per_ublk is not uniform due to chip limits atm

    U8C ourCT6C = U8C::makeCT6CoordFromTLBI(hb.mTLBI);
    U8C ourREGC = ourCT6C / uct6_per_ureg; //< our position in region space

    //U8C ourCT6V_of_REGC = ourCT6C % uct6_per_ureg;           //< our UCT6 offset in region ourREGC
    //U8C ourREGC_in_UCT6 = ourREGC * uct6_per_ureg;           //< UL corner of our region in CT6
    //U8C ourSCLC_in_REGC = ourREGC / ureg_per_uscl;           //< UL corner of our supercell in region coords
    //U8C ourREGV_of_SCLC = ourSCLC_in_REGC % ureg_per_uscl;   //< our UREG offset in supercell ourSCLC

    if (false) {
      LOGPTAG(oCT,ourCT6C);
      LOGPTAG(oRC,ourREGC);
    }

    EthSeam::SeamTable seams;
    EthSeam::decodeSeamMask(hb.getEthSeamMask(), seams);

    for (u32 i = D8_NT; i <= D8_NE; ++i) {
      Dir8 d8 = (Dir8) i;

      S8C d8nREGV = S8C::makeS8CFromDir8(d8);

      /*  /-CT6V-\= /-REGV-\ * /--UCT6/UREG---\  */
      S8C d8nCT6V = d8nREGV * S8C(uct6_per_ureg);

      /*  /-CT6C-\=  /-CT6C---\  + /-CT6V-\  */
      S8C d8nCT6C = S8C(ourCT6C) + d8nCT6V;

      // Does that neighbor hub exist?  On this chip, yes.  Off this chip
      // it exists only across a live eth-seam: in a Dir4 direction, or
      // diagonally through the two-hop corner crossing (InterHub.cpp,
      // EthSeam.h PART 5).  Anywhere else its loader stays unused.
      U8C ud8nCT6C;
      bool onChip = d8nCT6C.toU8C(ud8nCT6C) &&
        U8C::makeNoC0CoordFromCT6Coord(ud8nCT6C) != U8C::UxC_MAX_VAL;
      if (!onChip) {
        bool seamHere;
        if ((d8 & 1u) != 0u) {
          EthSeam::DiagSend ds;
          seamHere = EthSeam::diagSend(seams,
                                       d8,
                                       ourCT6C.x,
                                       ourCT6C.y,
                                       EthSeam::CELL_C2X2,
                                       ds);
        } else {
          EthSeam::DestDir4 dest;
          seamHere = EthSeam::destDir4(seams,
                                       dir8ToDir4CW(d8),
                                       ourCT6C.x,
                                       ourCT6C.y,
                                       EthSeam::CELL_C2X2,
                                       dest);
        }
        if (!seamHere) continue;                // off chip, no seam
        LOGPTAG(SEAMLDR,dir8ToByteString(d8));
      }

      // Their place in its 2x2 supercell, counting hubs GLOBALLY across
      // chips (DemoGlobal.h): a neighbor on another chip is just the next
      // hub over.
      U8C d8nREGV_of_SCLC =
        DG::getHubSuperCellCoord(hb.mChipNum,
                                 (s32) ourREGC.x + d8nREGV.x,
                                 (s32) ourREGC.y + d8nREGV.y);
      bool d8IsLeader = (d8nREGV_of_SCLC == ldrREGV_of_SCLC);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

      if (true) {
        //LOGPTAG(REPHd8nNoC0C,d8nNoC0C);

        //LOGPX(d8nREGV_of_SCLC);
        //LOGPX(ldrREGV_of_SCLC);
        //LOGPX(d8IsLeader);

        Dir8 d8FromUs = d8; // XXX RIGHT?
      
        CacheLoader & cl = theL1CacheLoaders.get(d8FromUs);
//        LOGPTAG(REPHd8,dir8ToByteString(d8));
//        LOGPTAG(REPHcD8,dir8ToByteString(cl.mDir8));

        U8C uroa_per_ureg(NgbCacheMgr::BW,NgbCacheMgr::BH);
        S8C o2oREGV = d8nREGV; // origin to origin region vector
        S16C o2oROAV = S16C(o2oREGV) * S16C(uroa_per_ureg);

        //LOGPX(o2oROAV);

        U8CRange mySitesForYou = NgbCacheMgr::SITE_RANGES[d8];  // my follower sites (for your cache)
        U8CRange myCacheForYou = NgbCacheMgr::CACHE_RANGES[d8]; // my leader cache (for your sites)

#pragma GCC diagnostic pop

        if (d8IsLeader) {
          MFM_API_ASSERT(!iAmLeader,ILLEGAL_STATE);
          LOGPTAG(ULDR,dir8ToByteString(d8));
          //LOGPTAG(REPH_TLBI,theirTLBI);
          //LOGPTAG(REPHmyS4UC,mySitesForYou);
          cl.mLocalItr = U8CRange::iterator(mySitesForYou);
          cl.mDestType = 1;
        } else if (iAmLeader) {
          LOGPTAG(UFLW,dir8ToByteString(d8));
          //LOGPTAG(REPH_TLBI,theirTLBI);
          //LOGPTAG(REPHUC4MyS,myCacheForYou);
          cl.mLocalItr = U8CRange::iterator(myCacheForYou);
          cl.mDestType = -1;
        } else { // we both followers here
          //LOGPTAG(FOFO,dir8ToByteString(d8));
          cl.mLocalItr = U8CRange::iterator(); //< empty iterator
          //LOGPTAG(REPH04U,U8CRange());
          cl.mDestType = -1;
        }
        cl.mDir8 = d8;
      }
    }
  }

  void CacheLoaders::loadCaches() {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    GridManager & gm = fB.mGridManager;

    for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
      CacheLoader & cl = get(i);
      if (cl.isThatAFollower())
        cl.unloadPacketIfNeeded(gm);
      else if (cl.isThatALeader())
        cl.loadPacketIfNeeded(gm);
    }
  }

  /* SAVE is the mirror image of LOAD (loadCaches): the leader ships its
     caches back to the followers' sites, and the followers receive them.

                      RUN_EVENTS (loadCaches)   NEW_LEADER (saveCaches)
       leader tile     unload (recv sites)       load (send caches)
       follower tile   load (send sites)         unload (recv into sites)

     Cars need no explicit repositioning: a receiver-role loader that finds
     a car in its inbound ring unloads it and ships it back, and an EMPTY
     car (zero atoms) unloads as a no-op.  So the empties that RUN_EVENTS
     left parked at the followers are handed to the leader automatically
     as the followers' first SAVE 'unloads'.  Likewise, when roles change
     with a new leader, whichever end holds the cars hands them to the end
     that needs to send. */
  void CacheLoaders::saveCaches() {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    GridManager & gm = fB.mGridManager;

    for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
      CacheLoader & cl = get(i);
      SNAP(20,LOGPTAG(CLS$U,i));
      if (cl.isThatAFollower())       // we lead: send our caches to them
        cl.loadPacketIfNeeded(gm);
      else if (cl.isThatALeader())    // we follow: receive their caches into our sites
        cl.unloadPacketIfNeeded(gm);
    }
  }

  void CacheLoader::loadPacketIfNeeded(GridManager & gm) {
    if (!needsAnotherPacket()) return;

    using IHubData = InterHubEP::Super::L1Data;
    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[mDir8];
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[mDir8];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    
    if (crbi.isEmpty() || crbo.isFull()) return; // just wishin' and hopin'

    u8 carindex;
    bool ret = crbi.remove(carindex);
    MFM_API_ASSERT(ret, ILLEGAL_STATE);
    InterHubBlock & ihb = ihs.getTC(carindex);
    Buf32 b32;
    LOGPTAG(bLP,formatIHBlockBuf32(b32,ihb,mDir8,carindex));
    loadPacket(gm,ihb);
    LOGPTAG(aLP,formatIHBlockBuf32(b32,ihb,mDir8,carindex));
    LOGPTAG(cstat,ihb.packetStatus(strTag4(&ihb)));
    crbo.add(carindex);
    sleepCycles(10'000'000); // DEBUG give hn a chacne
  }

  void CacheLoader::unloadPacketIfNeeded(GridManager & gm) {
    if (!needsAnotherPacket()) return;

    using IHubData = InterHubEP::Super::L1Data;
    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[mDir8];
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[mDir8];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    
    if (crbi.isEmpty() || crbo.isFull()) return; 

    u8 carindex;
    bool ret = crbi.remove(carindex);
    MFM_API_ASSERT(ret,ILLEGAL_STATE);
    InterHubBlock & ihb = ihs.getTC(carindex);
    Buf32 b32;
    bool wasEmpty = ihb.payload().getType() == IHPT_EMPTY;
    if (!wasEmpty)
      LOGPTAG(UNLB,formatIHBlockBuf32(b32,ihb,mDir8,carindex));
    unloadPacket(gm,ihb);
    if (!wasEmpty)
      LOGPTAG(UNLA,formatIHBlockBuf32(b32,ihb,mDir8,carindex));
    crbo.add(carindex);
    sleepCycles(10'000'000);
  }

  /** pack local atoms into ihb */
  void CacheLoader::loadPacket(GridManager & gm, InterHubBlock & ihb) {
    MFM_API_ASSERT(needsAnotherPacket(),ILLEGAL_STATE);
    Buf32 b32;

    //LOGPTAG(LP,dir8ToByteString(mDir8)); 

    U8CRange::iterator & itr = mLocalItr;

//    LOGPTAG(lodPR,itr.range); 
//    LOGPTAG(lodP@,itr.at); 

    if (ihb.getType() != IHPT_ATOMS) {
      LOGPTAG(lodPT,ihb.getType()); 
      ihb.initType(IHPT_ATOMS);
    }
    // Now it's an atoms carrier

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();

    if (patoms.getAtomsInPacket() != 0) {
      LOGPTAG(lp!EMP?,patoms.getAtomsInPacket());
      patoms.setToZeroAtoms();
    }
    // Now it's carrying no atoms

    patoms.setStartsAt(*itr);
    LOGPTAG(STitr,patoms.getStartsAt());
    // Now we've marked where we're starting

    while (!itr.atEnd()) {
      if (patoms.isFull()) break; // Room For No More
      U8C ac = *itr++;
      const P4Atom a = t6grid.getAtom(ac);
      patoms.pushAtomAtEndOfPacketOrDie(a);
    }
    MFM_API_ASSERT(patoms.isFull() || itr.atEnd(),ILLEGAL_STATE);    // can't stop unless car full or itr done..

    if (itr.atEnd()) LOGPTAG(lodDONE,dir8ToByteString(mDir8));
    else LOGPTAG(lod@,itr.at);

    LOGPTAG(SHI#,patoms.getAtomsInPacket());
    // MUST include the IHPHeader: patoms.getCurrentPayloadSize() alone
    // omits the 8 header bytes, so a zero-atom car got size code 0 (8B
    // payload) and its footer landed on mAtomCount/mStartsAt.
    u32 pktsize = pay.getCurrentPayloadSize();
    //LOGPTAG(SHIZ,pktsize);
    ihb.closeTC(pktsize);              // packet loaded
  }

  /** unpack ihb into local atoms */
  void CacheLoader::unloadPacket(GridManager & gm, InterHubBlock & ihb) {
    MFM_API_ASSERT(!isThatUnused(),ILLEGAL_STATE); // whaaat?
    MFM_API_ASSERT(needsAnotherPacket(),ILLEGAL_STATE); // don't you be calling me either

    /* itr is over CACHES if we're leader; SITES if we're a follower */
    U8CRange::iterator & itr = mLocalItr;

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    if (pay.getType() == IHPT_EMPTY) {
      //EACH(1,LOGPTAG(Em,__EACHNUM__));
      pay.initIHP(IHPT_ATOMS,{0,0},{0,0});
    }
    IHPAtoms & patoms = pay.asAtomsOrDie(); // really die if not atoms at this point?
    u32 nextIdx = 0;
    u32 atomsInPacket = patoms.getAtomsInPacket();

    if (atomsInPacket > 0) {
      LOGPTAG(uPKTRge,itr.range); 
      U8C theyAreAt = patoms.getStartsAt();
      U8C weShouldBeAt = NgbCacheMgr::mapDestCToUs(mDir8, isThatAFollower(), theyAreAt);
      //LOGPTAG(THEY@,theyAreAt);
      //LOGPTAG(WE@,itr.at); 
      //LOGPTAG(STATU,weShouldBeAt);
      if (itr.at != weShouldBeAt) {
        Buf32 b32;
        LOGPTAG(BADC,formatIHBlockBuf32(b32,ihb,(Dir8) 99,99));
        sleepCycles(1'000'000);
      }
      HBASSERT_EQ(itr.at,weShouldBeAt); // ILLEGAL_STATE
    }

    while (!itr.atEnd()) {
      if (nextIdx >= atomsInPacket) break; // Ain't Got No More
      U8C ac = *itr++;
      P4Atom a = patoms.getAtomFromPacketOrDie(nextIdx++);
      gm.setAtom(U16C(ac.x,ac.y),a);
    }
    MFM_API_ASSERT(itr.atEnd() || nextIdx >= atomsInPacket,ILLEGAL_STATE);    // can't stop unless car empty or itr done..

    if (itr.atEnd()) LOGPTAG(uDONE,dir8ToByteString(mDir8));
    else if (atomsInPacket > 0) LOGPTAG(uGOON,itr.at);
    patoms.setToZeroAtoms();
    u32 pktsize = pay.getCurrentPayloadSize(); // incl. IHPHeader, see loadPacket
    //LOGPTAG(unPSZ,pktsize);
    ihb.closeTC(pktsize);                // packet unloaded
  }

}
