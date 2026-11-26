#include "ArcTelemetry.h"
#include "CacheLoader.h"
#include "Debug.h"
#include "InterHub.h"
#include "HubLiveB.h"  // for fB
#include "EthSeam.h"
#include "DemoGlobal.h"
#include "Printf.h"      //< for snprintf
#include "CachingPolicy.h" //< supercell vs four corners
#include "HubEdges.h"      //< neighbor hub existence

namespace MFM {

  /* Policy ewsnap/ewsnap10: point cl's walk at its band of whole tiles
     (EWLattice.h): the neighbor's tiles in our event area if we borrow,
     ours in its area if we lend; both ends compute the same rectangle
     and walk it the same way. */
  template <class L>
  static void initEWBand(CacheLoader & cl, const GridManager & gm,
                         DirPlan plan, Corner4 c4, S8C dv) {
    const EWGeom::Rect me = L::hubRect(gm.mHubX, gm.mHubY, gm.mPermEdges);
    const EWGeom::Rect you =
      L::hubRect(gm.mHubX + dv.x, gm.mHubY + dv.y,
                 L::neighborPermEdges(gm.mPermEdges, dv.x, dv.y));
    EWGeom::Rect band{0, 0, 0, 0};               // NONE: empty walk
    if (plan == DirPlan::SEND_SITES) band = L::band(you, me, c4);
    else if (plan == DirPlan::RECV_CACHES) band = L::band(me, you, c4);
    cl.mBand.template init<L>(band,
                              L::accessibleRect(gm.mHubX, gm.mHubY,
                                                gm.mPermEdges),
                              gm.localOrigin());
  }
  CacheLoaders theL1CacheLoaders;

  void CacheLoaders::configureCacheLoaders(u8 arg) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    L1GridManagerControl & gml1 = theL1GridManagerControl;
    gml1.setSuperCellLeader((u8) arg); // NOLOK??
    U8C ldrREGV_of_SCLC = U8C::makeSuperCellCoordFromLeaderCode((u32) arg);

    /// FIND LEADER DIR8(S) AND CONFIGURE CACHELOADERS
    // init() zeroes the whole struct; the elision state is decided at SAVE
    // (setKeepMaskFor) and must survive into the following LOAD.
    const u8 keepMask = mKeepMask;
    const u8 haveLoaded = mHaveLoaded;
    theL1CacheLoaders.init();
    mKeepMask = keepMask;
    mHaveLoaded = haveLoaded;

    mConfiguredSCL = arg;

    HostBlock & hb = theHostBlock;
    LOGPTAG(TBSL#,100*hb.mTLBI + mConfiguredSCL);

    GridManager & gm = fB.mGridManager;
    bool iAmLeader = (mConfiguredSCL==gm.getOurSuperCellPosition());

    // Which caching policy this run uses (host flags, mfmxctl 'policy');
    // the argument is a leader code for CP_SUPERCELL, a Corner4 for
    // CP_FOURCORNERS (CachingPolicy.h).
    const CachePolicyId policy = cachePolicyFromHostFlags(hb.mHostFlags);
    {
      static bool saidPolicy = false;
      if (!saidPolicy) {
        saidPolicy = true;
        LOGPTAG(.cp,(u32) policy);   // dotted: survives HBF_NO_UTAG
        HBPTAG(.cp,(u32) policy);
        // GridManager::init also logs .pe, but init-time marks never reach
        // the log (run 073346 had no .pe at all); repeat it here, where .cp
        // is known to get out.
        LOGPTAG(.pe,(u32) gm.mPermEdges);
        HBPTAG(.pe,(u32) gm.mPermEdges);
        LOGPTAG(.ee,(u32) everyEventShipped());   // 1 = no AER cheating
        HBPTAG(.ee,(u32) everyEventShipped());
        if (gm.mEWSnap) {
          // .ct:N0:N1:N2:N3:N4. live event centers per center table (hex;
          // no corner, SE, SW, NW, NE), policies ewsnap and ewsnap10
          char ctb[64];
          snprintf(ctb, sizeof(ctb), "%x:%x:%x:%x:%x.",
                   (unsigned) gm.mCenterLive[0], (unsigned) gm.mCenterLive[1],
                   (unsigned) gm.mCenterLive[2], (unsigned) gm.mCenterLive[3],
                   (unsigned) gm.mCenterLive[4]);
          LOGPTAG(.ct,ctb);
          HBPTAG(.ct,ctb);
        }
        // .ck:STATUS:AICLK:AXICLK:ARCCLK:LIMIT. (hex; clocks in MHz) as
        // read from the ARC tile by hart NC at init (InterHub.cpp);
        // STATUS 0 = ok (ArcTelemetry.h).  Logged here, with .cp, because
        // init-time marks do not reach the log.
        {
          char ckb[48];
          const char * ck = formatArcClocks(ckb,sizeof(ckb),theArcClocks);
          LOGPTAG(.ck,ck);
          HBPTAG(.ck,ck);
        }
      }
    }
    if (policy == CP_SUPERCELL)
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

      // Does that neighbor hub exist?  On this chip, yes.  Off this chip
      // it exists only across a live eth-seam: in a Dir4 direction, or
      // diagonally through the two-hop corner crossing (InterHub.cpp,
      // EthSeam.h PART 5).  Anywhere else its loader stays unused.
      if (!HubEdges::neighborExists(d8, seams, ourCT6C))
        continue;                               // off chip, no seam
      if (!HubEdges::neighborOnChip(d8, ourCT6C))
        LOGPTAG(SEAMLDR,dir8ToByteString(d8));

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
        Dir8 d8FromUs = d8; // XXX RIGHT?
        CacheLoader & cl = theL1CacheLoaders.get(d8FromUs);

        U8CRange mySitesForYou = NgbCacheMgr::SITE_RANGES[d8];  // my edge sites (for your cache)
        U8CRange myCacheForYou = NgbCacheMgr::CACHE_RANGES[d8]; // my cache (for your sites)

#pragma GCC diagnostic pop

        PolicyCtx pc;
        pc.weLead = iAmLeader;
        pc.peerLeads = d8IsLeader;
        pc.noLeader = false;
        if (policy == CP_SUPERCELL && d8IsLeader)
          MFM_API_ASSERT(!iAmLeader,ILLEGAL_STATE);
        const DirPlan plan = CachingPolicies::plan(policy, mConfiguredSCL, d8, pc);

        if (plan == DirPlan::SEND_SITES) {         // peer receives (leader)
          LOGPTAG(ULDR,dir8ToByteString(d8));
          cl.mLocalItr = U8CRange::iterator(mySitesForYou);
          cl.mDestType = 1;
        } else if (plan == DirPlan::RECV_CACHES) { // we receive (leader)
          LOGPTAG(UFLW,dir8ToByteString(d8));
          cl.mLocalItr = U8CRange::iterator(myCacheForYou);
          cl.mDestType = -1;
        } else {                                   // no exchange this cycle
          cl.mLocalItr = U8CRange::iterator(); //< empty iterator
          cl.mDestType = -1;
        }
        cl.mDir8 = d8;
        cl.mEWSnap = gm.mEWSnap ? 1u : 0u;
        if (gm.mEWSnap) {
          const Corner4 c4 = (Corner4) (mConfiguredSCL & 3u);
          if (gm.mEWTile == 10u)
            initEWBand<EWLattice10>(cl, gm, plan, c4, d8nREGV);
          else
            initEWBand<EWLattice>(cl, gm, plan, c4, d8nREGV);
        }
      }
    }
  }

  void CacheLoaders::setKeepMaskFor(s32 nextCorner) {
    mKeepMask = 0u;
    HostBlock & hb = theHostBlock;
    if ((hb.mHostFlags & HostFlags::HBF_ELIDE_CACHES) == 0u) return;
    if (!mHaveLoaded) return;                    // nothing held yet
    if (nextCorner < 0 || nextCorner > 3) return;
    const CachePolicyId policy = cachePolicyFromHostFlags(hb.mHostFlags);
    if (!isCornerPolicy(policy)) return;         // only corner policies share bands
    for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
      const CacheLoader & cl = get(i);
      if (cl.isThatUnused()) continue;           // no neighbor that way
      if (CachingPolicies::keepAcross(policy, mConfiguredSCL, (u8) nextCorner,
                                      (Dir8) i))
        mKeepMask |= (u8) (1u << i);
    }
  }

  void CacheLoaders::loadCaches() {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    GridManager & gm = fB.mGridManager;

    for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
      if (isKept(i)) continue;                   // elided: borrower still holds it
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
      if (isKept(i)) continue;                   // elided: stays with the borrower
      CacheLoader & cl = get(i);
      SNAP(20,LOGPTAG(CLS$U,i));
      if (cl.isThatAFollower())       // we lead: send our caches to them
        cl.loadPacketIfNeeded(gm);
      else if (cl.isThatALeader())    // we follow: receive their caches into our sites
        cl.unloadPacketIfNeeded(gm);
    }
  }

  void CacheLoaders::formatWaiting(char * buf, u32 len) {
    using IHubData = InterHubEP::Super::L1Data;
    u32 need = 0u;                //< Dir8 bits still wanting a packet
    u32 ldr = 0u;                 //< Dir8 bits where the peer is the leader
    char in[CACHELOADER_COUNT + 1u];
    char out[CACHELOADER_COUNT + 1u];
    for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
      const CacheLoader & cl = get(i);
      if (cl.needsAnotherPacket()) need |= 1u << i;
      if (cl.isThatALeader()) ldr |= 1u << i;
      IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[i];
      const IHubData::CarIdxRB & rli = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
      u32 li = (rli.mFirstFreeIdx - rli.mFirstUsedIdx) & IHubData::CarIdxRB::RING_BUFFER_MASK; //< racy peek, diagnostic only
      const IHubData::CarIdxRB & rlo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
      u32 lo = (rlo.mFirstFreeIdx - rlo.mFirstUsedIdx) & IHubData::CarIdxRB::RING_BUFFER_MASK; //< racy peek, diagnostic only
      in[i] = "0123456789abcdef"[li & 0xfu];
      out[i] = "0123456789abcdef"[lo & 0xfu];
    }
    in[CACHELOADER_COUNT] = '\0';
    out[CACHELOADER_COUNT] = '\0';
    snprintf(buf, len, "%x:%x:%s:%s", need, ldr, in, out);
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
    ++theL1CacheLoaders.mPacketsMoved;
    theL1CacheLoaders.countMoved(mDir8);
    sleepCycles(PACKET_SLEEP_CYCLES); // DEBUG give hn a chacne
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
    ++theL1CacheLoaders.mPacketsMoved;
    theL1CacheLoaders.countMoved(mDir8);
    sleepCycles(PACKET_SLEEP_CYCLES);
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

    if (mEWSnap) {
      // ewsnap: startsAt carries the walk ordinal (low, high byte), which
      // the receiver checks against its own
      const u16 ord = mBand.ordinal();
      patoms.setStartsAt(U8C((u8) (ord & 0xffu), (u8) (ord >> 8)));
      while (!mBand.atEnd()) {
        if (patoms.isFull()) break;
        const EWGeom::Pt l = mBand.localSite();
        patoms.pushAtomAtEndOfPacketOrDie(t6grid.getAtom(U8C((u8) l.x,
                                                             (u8) l.y)));
        mBand.next();
      }
      MFM_API_ASSERT(patoms.isFull() || mBand.atEnd(),ILLEGAL_STATE);
    } else {
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
    }

    if (walkDone()) LOGPTAG(lodDONE,dir8ToByteString(mDir8));
    else if (!mEWSnap) LOGPTAG(lod@,itr.at);

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

    if (mEWSnap && atomsInPacket > 0) {
      const U8C theyAreAt = patoms.getStartsAt();
      const u16 theirs = (u16) (theyAreAt.x | ((u16) theyAreAt.y << 8));
      if (theirs != mBand.ordinal()) {
        Buf32 b32;
        LOGPTAG(BADC,formatIHBlockBuf32(b32,ihb,(Dir8) 99,99));
        sleepCycles(1'000'000);
      }
      HBASSERT_EQ(theirs,mBand.ordinal()); // ILLEGAL_STATE
    } else if (atomsInPacket > 0) {
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

    while (!walkDone()) {
      if (nextIdx >= atomsInPacket) break; // Ain't Got No More
      U8C ac;
      if (mEWSnap) {
        const EWGeom::Pt l = mBand.localSite();
        ac = U8C((u8) l.x, (u8) l.y);
        mBand.next();
      } else {
        ac = *itr++;
      }
      P4Atom a = patoms.getAtomFromPacketOrDie(nextIdx++);
      /* A follower's SAVE unload lands in its own sites (a leader's LOAD
         unload lands in its cache ring).  A changed site of ours must be
         reported by us, so it goes through the DL; cache sites stay
         quiet (Grid.h).  'Ours' is the SELF rectangle, or for ewsnap the
         EWs whose centers are in our hub rectangle. */
      const U16C gc(ac.x,ac.y);
      if (gm.ownsSite(gc)) gm.setAtomOrDrop(gc,a);  // pushes only if it changed
      else gm.setAtomQuiet(gc,a);
    }
    MFM_API_ASSERT(walkDone() || nextIdx >= atomsInPacket,ILLEGAL_STATE);    // can't stop unless car empty or itr done..

    if (walkDone()) LOGPTAG(uDONE,dir8ToByteString(mDir8));
    else if (atomsInPacket > 0 && !mEWSnap) LOGPTAG(uGOON,itr.at);
    patoms.setToZeroAtoms();
    u32 pktsize = pay.getCurrentPayloadSize(); // incl. IHPHeader, see loadPacket
    //LOGPTAG(unPSZ,pktsize);
    ihb.closeTC(pktsize);                // packet unloaded
  }

}
