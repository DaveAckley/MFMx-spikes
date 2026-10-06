#include "T6BoltResponder.h"
#include "EP_InterHub.h"  //< for InterHubL1Control
#include "HubLiveB.h"     //< for fB
#include "NgbCacheMgr.h"  //< for sites-and-caches constants

namespace MFM {
  /// HUB BOLT RESPONDERS

  struct CacheLoader {

    U8CRange::iterator mLocalItr; //< in local coords
    U8CRange::iterator mRemoteItr; //< in their local coords
    Dir8 mLeaderDir8; //< where leader is from us (for routing?)
    u8 mCacheLoaderIndex;

    void init() {
      memset_s(this, '\0', sizeof(*this));
    }

    bool tryLoad(GridManager & gm) { //< return true iff done
      U8CRange::iterator & itr = mLocalItr;
      if (itr.atEnd()) return true;
      Dir8 d8 = mLeaderDir8;
      LOGPTAG(ldrD8,dir8ToByteString(d8));

      MFM_API_ASSERT_ON_HART(HARTNUM_B);

      using IHubData = InterHubEP::Super::L1Data;
      IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[d8];
      IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
      IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    
      LOGPX(crbi.isEmpty());
      LOGPX(crbo.isEmpty());
      LOGPX(crbi.isFull());
      LOGPX(crbo.isFull());

      InterHubPrivateControl & ihpc = fB.mPIHControl;
      MFM_API_ASSERT_NONNULL(ihpc.mIHL1Control);
      //LOGPX(ihpc.mIHL1Control);  // same
      //LOGPX(&theInterHubL1Control); // same?
      //LOGPX(&theInterHubL1Data); 

      InterHubL1Control::L1Hub1 & ihl1 = ihpc.mIHL1Control->getL1Hub1(mLeaderDir8);
      if (!ihl1.mCurrentInterHub) {
        LOGNOTE(LDC$WAIT);
        return false;
      }
      InterHubBlock & ihb = *ihl1.mCurrentInterHub;
      u8 carindex = ihl1.mCurrentCarIndex;

      LOGPTAG(SHI2,itr.range); 

      T6Grid & t6grid = gm.getT6GridOrDie();
      InterHubPayload & pay = ihb.payload();
      IHPAtoms & patoms = pay.asAtomsOrDie();
      patoms.init();
      while (!itr.atEnd()) {
        if (patoms.isFull()) break;
        U8C ac = *itr++;
        P4Atom a = t6grid.getAtom(ac);
        patoms.storeAtomOrDie(a);
      }
      if (patoms.isFull() || itr.atEnd()) {
        LOGPTAG(SHI@,itr.at);
        u32 pktsize = patoms.getCurrentPayloadSize();
        ihb.closeTC(pktsize);
        crbo.add(carindex);         // hand control back to comm
        ihl1.dropCar();             // flush the current car
        LOGPTAG(SHIPT,pktsize);
      }
      if (itr.atEnd()) LOGPTAG(SHIPT!,itr.at);

      return false;
    }

  };

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 4;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    u8 mCacheLoadersInUse;
    void init() {
      memset_s(this, '\0', sizeof(*this));
    }
    CacheLoader & allocate() {
      CacheLoader & ret = get(mCacheLoadersInUse);
      ret.mCacheLoaderIndex = mCacheLoadersInUse++;
      return ret;
    }
    CacheLoader & get(u32 idx) {
      MFM_API_ASSERT(idx < CacheLoaders::CACHELOADER_COUNT, ILLEGAL_ARGUMENT);
      return mCacheLoaders[idx];
    }
  };

  CacheLoaders theL1CacheLoaders;

  static bool bRespondToCMD_NEW_LEADER(s32 arg) {
    LOGPTAG(BOLTR_NEWLEADER,arg);
    InterHubL1Control & ihlc = theInterHubL1Control;
    L1GridManagerControl & gml1 = ihlc.getL1GridManagerControl();
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
    /*
    LOGPX(ourCT6V_of_REGC);
    LOGPX(ourREGC_in_UCT6);
    LOGPX(ourSCLC_in_REGC);
    LOGPX(ourREGV_of_SCLC);
    */

    for (u32 i = D8_NT; i <= D8_NE; ++i) {
      Dir8 d8 = (Dir8) i;

      S8C d8nREGV = S8C::makeS8CFromDir8(d8);

      /*  /-CT6V-\= /-REGV-\ * /--UCT6/UREG---\  */
      S8C d8nCT6V = d8nREGV * S8C(uct6_per_ureg);

      /*  /-CT6C-\=  /-CT6C---\  + /-CT6V-\  */
      S8C d8nCT6C = S8C(ourCT6C) + d8nCT6V;

      U8C ud8nCT6C;
      if (!d8nCT6C.toU8C(ud8nCT6C)) continue; // off chip N or W

      U8C d8nNoC0C = U8C::makeNoC0CoordFromCT6Coord(ud8nCT6C);
      if (d8nNoC0C == U8C::UxC_MAX_VAL) continue; // off chip E or S

      U8C d8nREGC = ud8nCT6C / uct6_per_ureg; //< their position in region space

      U8C d8nSCLC = d8nREGC / ureg_per_uscl; //< their position in supercell space
      U8C d8nSCLC_in_REGC = d8nSCLC * ureg_per_uscl; //< UL of their supercell in region space
      U8C d8nSCLC_in_CT6 = d8nSCLC_in_REGC * uct6_per_ureg; //< " in CT6 coords
      U8C d8nSCLC_in_NoC0C = U8C::makeNoC0CoordFromCT6Coord(d8nSCLC_in_CT6); //< " in NoC0 coords
      
      U8C d8nREGV_of_SCLC = d8nREGC % ureg_per_uscl;   //< d8n UREG offset in supercell theirSCLC

      if (d8nREGV_of_SCLC != ldrREGV_of_SCLC) continue; // not our leader monkey circus

      LOGPTAG(d8LDR,dir8ToByteString(d8));
      LOGPX(d8nNoC0C);
      if (false) {
        LOGPX(d8nCT6C);
        LOGPX(d8nREGC);
        LOGPX(d8nREGV_of_SCLC);
        LOGPX(d8nSCLC_in_NoC0C);
      }

      CacheLoader & cl = theL1CacheLoaders.allocate();
      LOGPTAG(thisCLI,cl.mCacheLoaderIndex);

      U8C uroa_per_ureg(NgbCacheMgr::BW,NgbCacheMgr::BH);
      S8C o2oREGV = d8nREGV; // origin to origin region vector
      S16C o2oROAV = S16C(o2oREGV) * S16C(uroa_per_ureg);

      LOGPX(o2oROAV);

      U8CRange mySitesForYourCache = NgbCacheMgr::SITE_RANGES[d8];      // my ST sites go
      U8CRange yourCacheForMySites = NgbCacheMgr::CACHE_RANGES[oppositeDir8(d8)]; // in your NT cache

      LOGPX(mySitesForYourCache);
      LOGPX(yourCacheForMySites);

      cl.mLeaderDir8 = d8;
      cl.mLocalItr = U8CRange::iterator(mySitesForYourCache);
      cl.mRemoteItr = U8CRange::iterator(yourCacheForMySites);
    }
    
    for (u32 i = 0; i < theL1CacheLoaders.mCacheLoadersInUse; ++i) {
      CacheLoader & cl = theL1CacheLoaders.get(i);
      LOGPTAG(CLidx,i);
      LOGPTAG(CLdir8,dir8ToByteString(cl.mLeaderDir8));
    }
    LOGPTAG(#CLs,theL1CacheLoaders.mCacheLoadersInUse);
    return true;                // response complete
  }

  static bool bRespondToCMD_LOAD_CACHE() {
    GridManager & gm = fB.mGridManager;
    SCStatus stat = gm.leadFollowOrGetOutOfWay();
    LOGPTAG(BOLTR_LOAD_CACHE,getSCStatusName(stat));
    if (stat == SCStatus::WE_LEAD || stat == SCStatus::NO_LEADER)
      return true;              // followers deal with loading cache

    bool allDone = true;
    for (u32 i = 0; i < theL1CacheLoaders.mCacheLoadersInUse; ++i) {
      CacheLoader & cl = theL1CacheLoaders.get(i);
      if (!cl.tryLoad(gm)) allDone = false;
    }
    LOGPTAG(SHID,allDone);
    return allDone;             // response complete when shipping done
  }


  bool imageDoneRespondingToBoltB(PhaserBolt & pb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    if (pb.getCmd() == PhaserBolt::CMD_NEW_LEADER) {
      s32 arg;
      if (pb.getBoltDataWordIfAny(0,arg) && arg >= 0 && arg < 256)
        return bRespondToCMD_NEW_LEADER(arg);
      FAIL(OUT_OF_BOUNDS);
    }

    if (pb.getCmd() == PhaserBolt::CMD_LOAD_CACHE) {
      return bRespondToCMD_LOAD_CACHE();
    }

    return true; // done either way.
  }
  bool imageDoneRespondingToBoltT0(PhaserBolt & pb) { return true; }
  bool imageDoneRespondingToBoltT1(PhaserBolt & pb) { return true; }
  bool imageDoneRespondingToBoltT2(PhaserBolt & pb) { return true; }
  bool imageDoneRespondingToBoltNC(PhaserBolt & pb) { return true; }
}
