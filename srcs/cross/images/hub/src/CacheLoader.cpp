#include "CacheLoader.h"
#include "Debug.h"
#include "InterHub.h"

namespace MFM {
  CacheLoaders theL1CacheLoaders;

  void CacheLoaders::configureCacheLoaders(u8 arg) {
    LOGPTAG(BOLTR_oNEWLEADER,arg);

    L1GridManagerControl & gml1 = theL1GridManagerControl;
    gml1.setSuperCellLeader((u8) arg); // NOLOK??
    U8C ldrREGV_of_SCLC = U8C::makeSuperCellCoordFromLeaderCode((u32) arg);

    /// FIND LEADER DIR8(S) AND CONFIGURE CACHELOADERS
    theL1CacheLoaders.init();

    HostBlock & hb = theHostBlock;
    U8C uct6_per_ureg(2,2);        // UCT6 conversion to UREG
    U8C ureg_per_uscl(2,2);        // UREG conversion to USCL
    // uscl_per_ublk is not uniform due to chip limits atm

    U8C ourCT6C = U8C::makeCT6CoordFromTLBI(hb.mTLBI);
    U8C ourREGC = ourCT6C / uct6_per_ureg; //< our position in region space

    //U8C ourCT6V_of_REGC = ourCT6C % uct6_per_ureg;           //< our UCT6 offset in region ourREGC
    //U8C ourREGC_in_UCT6 = ourREGC * uct6_per_ureg;           //< UL corner of our region in CT6
    //U8C ourSCLC_in_REGC = ourREGC / ureg_per_uscl;           //< UL corner of our supercell in region coords
    //U8C ourREGV_of_SCLC = ourSCLC_in_REGC % ureg_per_uscl;   //< our UREG offset in supercell ourSCLC

    LOGPX(ourCT6C);
    LOGPX(ourREGC);

    for (u32 i = D8_NT; i <= D8_NE; ++i) {
      Dir8 d8 = (Dir8) i;
      LOGPTAG(REPHd8,dir8ToByteString(d8));

      S8C d8nREGV = S8C::makeS8CFromDir8(d8);

      /*  /-CT6V-\= /-REGV-\ * /--UCT6/UREG---\  */
      S8C d8nCT6V = d8nREGV * S8C(uct6_per_ureg);

      /*  /-CT6C-\=  /-CT6C---\  + /-CT6V-\  */
      S8C d8nCT6C = S8C(ourCT6C) + d8nCT6V;

      U8C ud8nCT6C;
      if (!d8nCT6C.toU8C(ud8nCT6C)) continue; // off chip N or W
      LOGPTAG(REPHud8nCT6C,ud8nCT6C);

      U8C d8nNoC0C = U8C::makeNoC0CoordFromCT6Coord(ud8nCT6C);
      if (d8nNoC0C == U8C::UxC_MAX_VAL) continue; // off chip E or S
      LOGPTAG(REPHd8nNoC0C,d8nNoC0C);

      U8C d8nREGC = ud8nCT6C / uct6_per_ureg; //< their position in region space

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
      U8C d8nSCLC = d8nREGC / ureg_per_uscl; //< their position in supercell space
      U8C d8nSCLC_in_REGC = d8nSCLC * ureg_per_uscl; //< UL of their supercell in region space
      U8C d8nSCLC_in_CT6 = d8nSCLC_in_REGC * uct6_per_ureg; //< " in CT6 coords
      U8C d8nSCLC_in_NoC0C = U8C::makeNoC0CoordFromCT6Coord(d8nSCLC_in_CT6); //< " in NoC0 coords

      
      U8C d8nREGV_of_SCLC = d8nREGC % ureg_per_uscl;   //< d8n UREG offset in supercell theirSCLC

      LOGPTAG(REPHd8nREGV_of_SCLC,d8nREGV_of_SCLC);
      bool leadNotFollow = (d8nREGV_of_SCLC == ldrREGV_of_SCLC);
      
      if (true) {
        LOGPTAG(REPHd8nNoC0C,d8nNoC0C);

        //LOGPX(d8nREGV_of_SCLC);
        //LOGPX(ldrREGV_of_SCLC);

        Dir8 d8FromUs = d8; // XXX RIGHT?
      
        CacheLoader & cl = theL1CacheLoaders.get(d8FromUs);
        LOGPTAG(REPHthisCLI,cl.mDir8);

        U8C uroa_per_ureg(NgbCacheMgr::BW,NgbCacheMgr::BH);
        S8C o2oREGV = d8nREGV; // origin to origin region vector
        S16C o2oROAV = S16C(o2oREGV) * S16C(uroa_per_ureg);

        //LOGPX(o2oROAV);

        U8CRange mySitesForYourCache = NgbCacheMgr::SITE_RANGES[d8];      // my ST sites go
        U8CRange yourCacheForMySites = NgbCacheMgr::CACHE_RANGES[oppositeDir8(d8)]; // in your NT cache

        LOGPTAG(REPHmySitesForYourCache,mySitesForYourCache);
        LOGPTAG(REPHyourCacheForMySites,yourCacheForMySites);
#pragma GCC diagnostic pop

        if (leadNotFollow) {
          LOGPTAG(REPH_LEADER!,dir8ToByteString(d8));
          cl.mDestType = 1;
        } else {
          LOGPTAG(REPH_FOLLOWER!,dir8ToByteString(d8));
          cl.mDestType = -1;
        }
        cl.mDir8 = d8;
        cl.mLocalItr = U8CRange::iterator(mySitesForYourCache);
        cl.mRemoteItr = U8CRange::iterator(yourCacheForMySites);
      }
    }
  }

  /** pack local sites into ihb */
  bool CacheLoader::loadPacket(bool localNotRemote, GridManager & gm, InterHubBlock & ihb) {

    U8CRange::iterator & itr = localNotRemote ? mLocalItr : mRemoteItr;

    if (itr.atEnd()) return false; // packet not touched

    LOGPTAG(lodPKTRange,itr.range); 
    LOGPTAG(lodPKT@,itr.at); 

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();

    if (!patoms.isEmpty())      // somebody else's job to empty, we think..
      LOGPTAG(lodPKT_NOTEMPTY,patoms.mDataUsed);
    patoms.init();              // well it's empty now
    while (!itr.atEnd()) {
      if (patoms.isFull()) break; // Room For No More
      U8C ac = *itr++;
      P4Atom a = t6grid.getAtom(ac);
      patoms.loadAtomIntoPacketOrDie(a);
    }
    MFM_API_ASSERT(patoms.isFull() || itr.atEnd(),ILLEGAL_STATE);    // can't stop unless car full or itr done..

    LOGPTAG(SHI@,itr.at);
    u32 pktsize = patoms.getCurrentPayloadSize();
    ihb.closeTC(pktsize);
    return true;                // packet loaded
  }

  /** unpack ihb into local sites */
  bool CacheLoader::unloadPacket(bool localNotRemote, GridManager & gm, InterHubBlock & ihb) {

    U8CRange::iterator & itr = localNotRemote ? mLocalItr : mRemoteItr;

    if (itr.atEnd()) return false; // packet not touched

    LOGPTAG(unlPKTRange,itr.range); 
    LOGPTAG(unlPKT@,itr.at); 

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();

    while (!itr.atEnd()) {
      if (patoms.isEmpty()) break; // Ain't Got No More
      U8C ac = *itr++;
      P4Atom a;
      patoms.unloadAtomFromPacketOrDie(a);
      t6grid.setAtom(U16C(ac.x,ac.y),a);
    }
    MFM_API_ASSERT(patoms.isEmpty() || itr.atEnd(),ILLEGAL_STATE);    // can't stop unless car empty or itr done..

    LOGPTAG(unlPKT@,itr.at);
    u32 pktsize = patoms.getCurrentPayloadSize();
    ihb.closeTC(pktsize);
    return true;                // packet loaded
  }


#if 0
  bool CacheLoader::tryLoad(GridManager & gm) { //< return true iff done
    U8CRange::iterator & itr = mLocalItr;
    if (itr.atEnd()) return true;
    Dir8 d8 = mDir8;
    LOGPTAG(ldrD8,dir8ToByteString(d8));

    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    using IHubData = InterHubEP::Super::L1Data;
    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[d8];
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[d8];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    
    if (crbo.isFull()) return false; // No Room At The Outt
    u8 carindex;
    if (crbi.remove(carindex)) return false; // No Cars Waiting
    InterHubBlock & ihb = ihs.getTC(carindex);

    LOGPTAG(SHI2,itr.range); 

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();
    patoms.init();
    while (!itr.atEnd()) {
      if (patoms.isFull()) break; // Room For No More
      U8C ac = *itr++;
      P4Atom a = t6grid.getAtom(ac);
      patoms.storeAtomOrDie(a);
    }
    if (patoms.isFull() || itr.atEnd()) {
      LOGPTAG(SHI@,itr.at);
      u32 pktsize = patoms.getCurrentPayloadSize();
      ihb.closeTC(pktsize);
      crbo.add(carindex);         // hand control back to comm
      LOGPTAG(SHIPT,pktsize);
    } else
      FAIL(ILLEGAL_STATE);    // can't stop unless car full or itr done..

    if (itr.atEnd()) LOGPTAG(SHIPT!,itr.at);

    return false;
  }
#endif

}
