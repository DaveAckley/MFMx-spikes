#include "CacheLoader.h"
#include "Debug.h"
#include "InterHub.h"
#include "HubLiveB.h"  // for fB

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
    LOGPTAG(BOLTR_oNEWLEADER,100*hb.mTLBI + mConfiguredSCL);

    GridManager & gm = fB.mGridManager;
    LOGPTAG(WE..ARE..,(mConfiguredSCL==gm.getOurSuperCellPosition()) ? "LEADER" : "FOLLOWER");

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
    for (u32 i = D8_NT; i <= D8_NE; ++i) {
      Dir8 d8 = (Dir8) i;

      S8C d8nREGV = S8C::makeS8CFromDir8(d8);

      /*  /-CT6V-\= /-REGV-\ * /--UCT6/UREG---\  */
      S8C d8nCT6V = d8nREGV * S8C(uct6_per_ureg);

      /*  /-CT6C-\=  /-CT6C---\  + /-CT6V-\  */
      S8C d8nCT6C = S8C(ourCT6C) + d8nCT6V;

      U8C ud8nCT6C;
      if (!d8nCT6C.toU8C(ud8nCT6C)) continue; // off chip N or W
      //LOGPTAG(REPHud8nCT6C,ud8nCT6C);

      U8C d8nNoC0C = U8C::makeNoC0CoordFromCT6Coord(ud8nCT6C);
      if (d8nNoC0C == U8C::UxC_MAX_VAL) continue; // off chip E or S
      //LOGPTAG(REPHd8nNoC0C,d8nNoC0C);

      U8C d8nREGC = ud8nCT6C / uct6_per_ureg; //< their position in region space

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
      U8C d8nSCLC = d8nREGC / ureg_per_uscl; //< their position in supercell space
      U8C d8nSCLC_in_REGC = d8nSCLC * ureg_per_uscl; //< UL of their supercell in region space
      U8C d8nSCLC_in_CT6 = d8nSCLC_in_REGC * uct6_per_ureg; //< " in CT6 coords
      U8C d8nSCLC_in_NoC0C = U8C::makeNoC0CoordFromCT6Coord(d8nSCLC_in_CT6); //< " in NoC0 coords

      
      U8C d8nREGV_of_SCLC = d8nREGC % ureg_per_uscl;   //< d8n UREG offset in supercell theirSCLC

      //LOGPTAG(REPHd8nREGV_of_SCLC,d8nREGV_of_SCLC);
      bool leadNotFollow = (d8nREGV_of_SCLC == ldrREGV_of_SCLC);
      
      if (true) {
        //LOGPTAG(REPHd8nNoC0C,d8nNoC0C);

        //LOGPX(d8nREGV_of_SCLC);
        //LOGPX(ldrREGV_of_SCLC);

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

        u32 theirTLBI = U16C::makeTLBIFromNoCCoord(U16C(d8nNoC0C.x,d8nNoC0C.y)); 
        if (leadNotFollow) {
          LOGPTAG(REPH_LDR!,dir8ToByteString(d8));
          LOGPTAG(REPH_TLBI,theirTLBI);
          LOGPTAG(REPHUC4MyS,myCacheForYou);
          cl.mLocalItr = U8CRange::iterator(myCacheForYou);
          cl.mDestType = 1;
        } else {
          LOGPTAG(REPH_FWR!,dir8ToByteString(d8));
          LOGPTAG(REPH_TLBI,theirTLBI);
          LOGPTAG(REPHmyS4UC,mySitesForYou);
          cl.mLocalItr = U8CRange::iterator(mySitesForYou);
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
      cl.loadPacketIfNeeded(gm);
    }
  }

  void CacheLoaders::saveCaches() {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    GridManager & gm = fB.mGridManager;

    for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
      CacheLoader & cl = get(i);
      SNAP(100,LOGPTAG(CLS$U,i));
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
    LOGPTAG(IHCARLB,formatIHBlock(ihb,carindex));
    loadPacket(gm,ihb);
    LOGPTAG(IHCARLA,formatIHBlock(ihb,carindex));
    crbo.add(carindex);
  }

  void CacheLoader::unloadPacketIfNeeded(GridManager & gm) {
    if (!needsAnotherPacket()) return;

    using IHubData = InterHubEP::Super::L1Data;
    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[mDir8];
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[mDir8];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    
    if (crbi.isEmpty() || crbo.isFull()) return; // just wishin' and hopin'

    u8 carindex;
    bool ret = crbi.remove(carindex);
    MFM_API_ASSERT(ret,ILLEGAL_STATE);
    InterHubBlock & ihb = ihs.getTC(carindex);
    LOGPTAG(IHCARUB,formatIHBlock(ihb,carindex));
    unloadPacket(gm,ihb);
    LOGPTAG(IHCARUA,formatIHBlock(ihb,carindex));
    crbo.add(carindex);
  }

  /** pack local atoms into ihb */
  void CacheLoader::loadPacket(GridManager & gm, InterHubBlock & ihb) {
    MFM_API_ASSERT(needsAnotherPacket(),ILLEGAL_STATE);

    LOGPTAG(_ducFL@,dir8ToByteString(mDir8)); 

    U8CRange::iterator & itr = mLocalItr;

    LOGPTAG(lodPKTRange,itr.range); 
    LOGPTAG(lodPKT@,itr.at); 

    if (ihb.getType() != IHPT_ATOMS) {
      LOGPTAG(lodTYP@,ihb.getType()); 
      ihb.initType(IHPT_ATOMS);
    }
    // Now it's an atoms carrier

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();

    if (patoms.getAtomsInPacket() != 0) {
      LOGPTAG(lodPKT_NOTEMPTY,formatIHBlock(ihb,99));
      patoms.setToZeroAtoms();
    }
    // Now it's carrying no atoms

    while (!itr.atEnd()) {
      if (patoms.isFull()) break; // Room For No More
      U8C ac = *itr++;
      const P4Atom a = t6grid.getAtom(ac);
      patoms.pushAtomAtEndOfPacketOrDie(a);
    }
    MFM_API_ASSERT(patoms.isFull() || itr.atEnd(),ILLEGAL_STATE);    // can't stop unless car full or itr done..

    LOGPTAG(SHI@,itr.at);
    LOGPTAG(SHI#,patoms.getAtomsInPacket());
    u32 pktsize = patoms.getCurrentPayloadSize();
    ihb.closeTC(pktsize);              // packet loaded
  }

  /** unpack ihb into local atoms */
  void CacheLoader::unloadPacket(GridManager & gm, InterHubBlock & ihb) {
    MFM_API_ASSERT(needsAnotherPacket(),ILLEGAL_STATE); // don't you be calling me either

    U8CRange::iterator & itr = mLocalItr;

    LOGPTAG(unlPKTRange,itr.range); 
    LOGPTAG(unlPKT@,itr.at); 

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    if (pay.getType() == IHPT_EMPTY) {
      EACH(1,LOGPTAG(ECOMCON,__EACHNUM__));
      pay.initIHP(IHPT_ATOMS,{0,0},{0,0});
    }
    IHPAtoms & patoms = pay.asAtomsOrDie(); // really die if not atoms at this point?
    u32 nextIdx = 0;
    u32 atomsInPacket = patoms.getAtomsInPacket();
    while (!itr.atEnd()) {
      if (nextIdx >= atomsInPacket) break; // Ain't Got No More
      U8C ac = *itr++;
      P4Atom a = patoms.getAtomFromPacketOrDie(nextIdx++);
      t6grid.setAtom(U16C(ac.x,ac.y),a);
    }
    MFM_API_ASSERT(itr.atEnd() || nextIdx >= atomsInPacket,ILLEGAL_STATE);    // can't stop unless car empty or itr done..

    LOGPTAG(unlPKT@,itr.at);
    patoms.setToZeroAtoms();
    u32 pktsize = patoms.getCurrentPayloadSize();
    LOGPTAG(unlPKTSZ,pktsize);
    ihb.closeTC(pktsize);                // packet unloaded
  }

}
