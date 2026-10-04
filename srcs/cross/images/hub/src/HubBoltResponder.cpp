#include "T6BoltResponder.h"
#include "EP_InterHub.h"  //< for InterHubL1Control
#include "HubLiveB.h"     //< for fB

namespace MFM {
  /// HUB BOLT RESPONDERS

  struct CacheLoader {
    //CacheLoader() = default;

    S16C mOffset; //< in grid distance between our sites and leader's cache
    U8CRange::iterator mIndex; //< in local coords
    Dir8 mLeaderDir8; //< where leader is from us (for routing?)

    bool inUse() const { return mOffset != S16C(0,0); }

    void init() {
      // mOffset==0 => 'struct not currently in use'
      memset_s(this, '\0', sizeof(*this));
    }
  };

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 4;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    void init() {
      memset_s(this, '\0', sizeof(*this));
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

    u32 nextCacheLoaderIdx = 0;
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
      LOGPX(d8nCT6C);
      LOGPX(d8nREGC);
      LOGPX(d8nREGV_of_SCLC);
      LOGPX(d8nSCLC_in_NoC0C);
      LOGPTAG(thisCLI,nextCacheLoaderIdx);

      CacheLoader & cl = theL1CacheLoaders.get(nextCacheLoaderIdx++);
      cl.mLeaderDir8 = d8;
      cl.mOffset = S16C(100,100); // XXXX FISK ME
      // cl.mOffset TBD
      // cl.mIndex TBD
    }

    for (u32 i = 0; i < CacheLoaders::CACHELOADER_COUNT; ++i) {
      CacheLoader & cl = theL1CacheLoaders.get(i);
      if (!cl.inUse()) continue;
      LOGPTAG(CLidx,i);
      LOGPTAG(CLdir8,dir8ToByteString(cl.mLeaderDir8));
    }
    LOGPTAG(#CLs,nextCacheLoaderIdx);
    return true;                // response complete
  }

  static bool bRespondToCMD_LOAD_CACHE() {
    GridManager & gm = fB.mGridManager;
    SCStatus stat = gm.leadFollowOrGetOutOfWay();
    LOGPTAG(BOLTR_LOAD_CACHE,getSCStatusName(stat));
    if (stat == SCStatus::WE_LEAD || stat == SCStatus::NO_LEADER)
      return true;              // followers deal with loading cache

    /* - Follower needs to know which Dir8 points to the leader. (NOTE
      THESE Dir8s WILL OFTEN POINT TO A DIFFERENT SUPERCELL.) */
    //    U8C superc = U8C::makeSuperCellCoordFromLeaderCode(currentLeader);
    // RIGHTMEEE
    return true;                // response complete
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
