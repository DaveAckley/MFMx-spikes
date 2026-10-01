#include "QuietBox.h"
#include "Blackhole.h"
#include "HostUtils.h"
#include "ImageCode.h"
#include "HostRandom.h"

namespace MFM {
  BGRImageHD QuietBox::t6gridRenderBlock;

  QuietBox theQuietBox;

  QuietBox::QuietBox() = default;

  QuietBox & QuietBox::get() {
    return theQuietBox;
  }

  void QuietBox::addBlackhole(Blackhole & bh) {
    u32 chipnum = bh.getChipNumber();
    MFM_API_ASSERT(chipnum < MAX_BLACKHOLES,OUT_OF_ROOM);
    MFM_API_ASSERT(mBHPtrs[chipnum] == 0,DUPLICATE_ENTRY);
    mBHPtrs[chipnum] = &bh;
  }

  Blackhole * QuietBox::getBlackholeIfPresent(u32 chipnum) const {
    MFM_API_ASSERT(chipnum < MAX_BLACKHOLES,OUT_OF_ROOM);
    return mBHPtrs[chipnum];
  }
    
  std::string QuietBox::to_repr() const {
    std::string ret = "<QuietBox";
    for (u32 i = 0; i < MAX_BLACKHOLES; ++i) {
      if (getBlackholeIfPresent(i))
        ret += " BH" + std::to_string(i);
      else
        ret += " _";
    }
    ret += ">";
    return ret;
  }

  std::string DG::to_string(const DG::Coord c) {
    std::string ret = "@(";
    ret += std::to_string(c.x);
    ret += "," + std::to_string(c.y);
    ret += ")";
    return ret;
  }

  std::string DG::to_string(const DG::Address a) {
    std::string ret = "@@(";
    if (!a.isValid()) {
      ret += "invalid)";
      return ret;
    }
    ret += "bh#" + std::to_string(a.mChipNum);
    ret += " c#" + std::to_string(a.mCellNum.x);
    ret += "," + std::to_string(a.mCellNum.y);
    ret += " s#" + std::to_string(a.mT6GridC.x);
    ret += "," + std::to_string(a.mT6GridC.y);
    ret += ")";
    return ret;
  }

  ImageBlockAddr * QuietBox::findBCInCell(ImageManager & im, const BlockCode bc, const CellBlock cb, U8C & cellp) {
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C atcp(x,y);
        u8 ic = cb.getImageAtCellPos(atcp);
        std::string ikey = im.getIKeyIfAny(ic);
        T6Image & img = im.getT6Image(ikey); // or bang
        ImageBlockAddr * ibap = img.getImageBlockAddrForBlockCodeIfAny(BC_T6GRID);
        if (ibap) {
          cellp = atcp;
          return ibap;
        }
        /*
        Eprintf("\n %s (%u,%u) ic %u %s = %p\n",
                __FUNCTION__,
                x,y,ic,ikey.c_str(),ibap);
        */
      }
    }

    return 0;               // blockcode not found
  }

  void QuietBox::loadMaps(ImageManager & im, u32 bhc) {
    Blackhole * bhp = getBlackholeIfPresent(bhc);
    MFM_API_ASSERT_NONNULL(bhp);
    Blackhole & bh = *bhp;
    OurTLBs & tlbs = bh.getOurTLBs();

    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         ++tlbi) {
      OurTLBs::TLBInfo & info = tlbs.getTLBInfo(tlbi);
      const T6Image & image = info.getDeployedImageOrDie();
      u32 ic = image.getImageCode();
      if (ic != IC_HUB) continue;

      CellBlock cb = image.copyCellBlockOrDie();
      U8C stride = cb.mCellStride;
      U16C c = DG::getChipOrigin(bhc); 
      U16C o = c + DG::getTLBIOrigin(tlbi,stride); 
      
      const T6GridInfo & t6gi = getT6GridInfoFor(DG::Coord(o.x+13,o.y+11)); // not origin for test
      Eprintf("loadMaps(BH#%u,%u,%s) co(%u,%u) -> %u@0x%08x\n",
              bhc,
              tlbi,
              getNameFromImageCode((ImageCode) ic),
              t6gi.mT6GridOrigin.x,t6gi.mT6GridOrigin.y,
              t6gi.mTLBI, t6gi.mT6GridL1Base
              );
    }
      /*      
    XXX WRITE ME
      call getT6GridInfoFor
      for t6grid [0,0] of each hub
    have it populate both maps
      */
  }

  const T6GridInfo & QuietBox::getT6GridInfoFor(const DG::Coord to) {
    const U16C t6siz = DG::getSingleT6GridBaseSize();
    const U16C gridoff = DG::getGlobalGridOffset(); //< b/c no cache at N and W
    const T6GridIndex t6grididx = T6GridIndex((to.x/* + t6siz.x - 1*/) / t6siz.x, (to.y/* + t6siz.y - 1*/) / t6siz.y);
    //    const DG::Coord t6origin = DG::Coord(t6grididx.x * t6siz.x - 4, t6grididx.y * t6siz.y - 4); // HACK4XXX
    const DG::Coord t6origin = DG::Coord(t6grididx.x * t6siz.x - 0, t6grididx.y * t6siz.y - 0); 
    auto item = mT6GridInfoByCoordMap.find(t6grididx);
    if (item != mT6GridInfoByCoordMap.end()) return item->second; // Hit!

    // Need to do the lookup
    T6GridInfo value;
    
    do { // so we can break

      value.mT6GridOrigin = t6origin; // begin populating
      value.mDGAddress = DG::mapCoordToAddress(t6origin); // store t6grid origin, not 'to' directly

      if (false) {
        BHTag tag(TagType::HOSTCT,5);
        EACH(100'000,{
            KTprintf(tag,"GIDPIX t6siz = (%u,%u), to = (%u,%u), t6gidx = (%u,%u), t6org = (%u,%u), dg = (%u,%u), cn = (%u,%u), #%u\n",
                     t6siz.x, t6siz.y,
                     to.x, to.y,
                     t6grididx.x, t6grididx.y,
                     t6origin.x, t6origin.y,
                     value.mDGAddress.mT6GridC.x, value.mDGAddress.mT6GridC.y,
                     value.mDGAddress.mCellNum.x, value.mDGAddress.mCellNum.y,
                     value.mDGAddress.mChipNum
                     );});
      }

      
      if (!value.mDGAddress.isValid()) break;

      Blackhole * bhp = getBlackholeIfPresent(value.mDGAddress.mChipNum);
      if (!bhp) { value.mDGAddress.mValidAddr = false; break; }
      
      NSIM nsim;
      Blackhole & bh = *bhp;
      ImageManager & im = nsim.g();
      Layout & layout = nsim.getActiveLayout();
      std::string cellname = layout.getCellnameForChip(value.mDGAddress.mChipNum);
      Cell * cellp = im.getCells().getItemIfAny(cellname);
      MFM_API_ASSERT_NONNULL(cellp);
      CellBlock cb;
      if (!cellp->configureCellBlock(im,cb)) FAIL(INCOMPLETE_CODE);
      // find first cellp that supports BC_T6GRID
      // die if none
      U8C cellc;
      ImageBlockAddr * ibap = findBCInCell(im,BC_T6GRID,cb,cellc);
      MFM_API_ASSERT_NONNULL(ibap);
      
      // map cellp to (ct6 and then) tlbi
      U8C celloriginct6 = value.mDGAddress.mCellNum * cb.mCellStride;
      U8C gridct6 = celloriginct6 + cellc;
      u32 tlbi = U8C::makeTLBIFromCT6Coord(gridct6);

      // find actual l1 address
      u32 t6gridl1base = ibap->mBlockAddr;

      // load cache
      value.mTLBI = tlbi;
      value.mT6GridL1Base = t6gridl1base;
    } while (0);
    mT6GridInfoByCoordMap[t6origin] = value;
    ChipAndTLBI key(value.mDGAddress.mChipNum,value.mTLBI);
    mT6GridInfoByChipAndTLBIMap[key] = &mT6GridInfoByCoordMap[t6origin];
    return mT6GridInfoByCoordMap[t6origin];
  }

  bool QuietBox::storeP4Atom(const DG::Coord to, const P4Atom atom) {
    DG::Address a = DG::mapCoordToAddress(to);
    if (!a.isValid()) return false;
    Blackhole * bhp = getBlackholeIfPresent(a.mChipNum);
    if (!bhp) return false;
    NSIM nsim;
    Blackhole & bh = *bhp;
    ImageManager & im = nsim.g();
    Layout & layout = nsim.getActiveLayout();
    std::string cellname = layout.getCellnameForChip(a.mChipNum);
    Cell * cellp = im.getCells().getItemIfAny(cellname);
    MFM_API_ASSERT_NONNULL(cellp);
    CellBlock cb;
    if (!cellp->configureCellBlock(im,cb)) FAIL(INCOMPLETE_CODE);
    // find first cellp that supports BC_T6GRID
    // die if none
    U8C cellc;
    ImageBlockAddr * ibap = findBCInCell(im,BC_T6GRID,cb,cellc);
    MFM_API_ASSERT_NONNULL(ibap);

    // map cellp to (ct6 and then) tlbi
    U8C celloriginct6 = a.mCellNum * cb.mCellStride;
    U8C gridct6 = celloriginct6 + cellc;
    u32 tlbi = U8C::makeTLBIFromCT6Coord(gridct6);

    // find actual l1 address
    u32 t6gridl1base = ibap->mBlockAddr;
    u32 atomoffset = T6Grid::getByteOffsetToT6GridAtom(a.mT6GridC);
    u32 destbyteaddr = t6gridl1base + atomoffset;

    // INITIATE THE FUCKING WRITE!
    OurTLBs & ourtlbs = bh.getOurTLBs();
    ourtlbs.writeToWords(tlbi,destbyteaddr, (u32*) &atom, sizeof(P4Atom)/sizeof(u32));
    
    Eprintf("\nstoreP4Atom(%s aka %s, ..) cn:%s -> tlbi(%u) [0x%08x+%u]\n",
            DG::to_string(to).c_str(),
            DG::to_string(a).c_str(),
            cellname.c_str(),
            tlbi,
            t6gridl1base,
            atomoffset
            );
    return ibap!=0;
  }

  std::string QuietBox::phaseIndices() {
    std::string ret = "";
    for (u32 chip = 0; chip < MAX_BLACKHOLES; ++chip) {
      if (chip != 0) ret += "/";
      Blackhole *bhp = getBlackholeIfPresent(chip);
      if (!bhp)
        ret += "x";
      else 
        ret += std::to_string(bhp->getOurTLBs().phaseIndex());
    }
    return ret;
  }

  std::string QuietBox::onPhases() {
    std::string ret = "";
    for (u32 chip = 0; chip < MAX_BLACKHOLES; ++chip) {
      if (chip != 0) ret += "/";
      Blackhole *bhp = getBlackholeIfPresent(chip);
      if (!bhp)
        ret += "x";
      else 
        ret += std::to_string(bhp->getOurTLBs().getOnPhase());
    }
    return ret;
  }

  std::string QuietBox::loggingSummary() {
    OutputCount & oc = OutputCount::get();
    std::size_t count = oc.getTotalOutput();
    return size4(count);
  }

  void QuietBox::shootPHASER(PhaserBolt::Cmd cmd, std::vector<s32> args) {
    HTprintf("%u QuBo::shootPHASER %s \n",gettid(),PhaserBolt::phaserCmdName(cmd));
    std::shuffle(mBHNumbers.begin(),mBHNumbers.end(),hostPRNG);
    for (u32 i = 0; i < mBHNumbers.size(); ++i) {
      u32 chip = mBHNumbers[i];
      Blackhole *bhp = getBlackholeIfPresent(chip);
      if (!bhp) continue;
      Blackhole & bh = *bhp;
      bh.getOurTLBs().shootPHASER(cmd,args);
    }
  }

  void QuietBox::doASuperCycle(u32 nextLeader) { // runs on IHHThread
    /* A single SuperCycle, we declare, consists of

      (0) NEW LEADER: X
      (1) LOAD CACHE
      (2) CARRY_ON
      (3) SUSPEND EVENTS
      (4) SAVE CACHE

     */

    static u32 scyCount = 0;
    HTprintf("%u QuBo::SCY SUPERCYCLE #%u BEGINS WITH LEADER %u\n",gettid(),++scyCount,nextLeader);

    static constexpr u32 MIN_MS_SMALL = 500;
    static constexpr u32 MAX_MS_SMALL = 2'000;

    static constexpr u32 MIN_MS_MEDIUM = 2'000;
    static constexpr u32 MAX_MS_MEDIUM = 3'000;

    static constexpr u32 MIN_MS_PHASE = 7'000;
    static constexpr u32 MAX_MS_PHASE = 8'000;

    static constexpr u32 EWP_DRAIN_SEC = 7;

    //// ANNOUNCE NEW LEADER AND "LET THAT SINK IN"
    HTprintf("%u QuBo::SCY ANNOUNCING NEW LEADER cL%u\n", gettid(), nextLeader);    
    shootPHASER(PhaserBolt::CMD_NEW_LEADER,{(s32) nextLeader});
    sleepMsec(hostPRNG.Between(MIN_MS_SMALL,MAX_MS_SMALL)); 

    /// LOAD LEADER CACHES
    HTprintf("%u QuBo::SCY cL%u follower sites loading to leader caches\n", gettid(), nextLeader);
    shootPHASER(PhaserBolt::CMD_LOAD_CACHE,{});
    sleepMsec(hostPRNG.Between(MIN_MS_MEDIUM,MAX_MS_MEDIUM)); 

    /// RUN EVENTS
    HTprintf("%u QuBo::SCY cL%u RUNNING EVENTS\n", gettid(), nextLeader);
    shootPHASER(PhaserBolt::CMD_CARRY_ON,{});
    sleepMsec(hostPRNG.Between(MIN_MS_PHASE,MAX_MS_PHASE)); 

    /// SUSPEND EVENTS
    HTprintf("%u QuBo::SCY cL%u SUSPENDING EVENTS\n", gettid(), nextLeader);
    shootPHASER(PhaserBolt::CMD_SUSPEND_EWPS,{1});

    /// FLUSH LEADER CACHES
    HTprintf("%u QuBo::SCY cL%u leader saving caches to follower sites\n", gettid(), nextLeader);
    shootPHASER(PhaserBolt::CMD_SAVE_CACHE,{});
    sleepMsec(hostPRNG.Between(MIN_MS_MEDIUM,MAX_MS_MEDIUM)); 

    HTprintf("%u QuBo::SCY SUPERCYCLE #%u ENDS WITH LEADER cL%u\n", gettid(), scyCount, nextLeader);
    return;
  }

#if 0
  void QuietBox::doAnIHHHack() { // runs on IHHThread
    HTprintf("%u QuBo::IHH HACK BEGIN \n",gettid());

    static u32 spin = 0;
    if (spin++ % 1'000'000 == 0)
      Eprintf("QB::IHHThread doAnIHHHack %u\n",spin);
    /*
      (0) Suspend event processing

      (1) Wait a "long time" for events to drain, because we haven't
      implemented the delay-til-drained phaser reply that we wanted
      to.

      (2) Choose at random in (0..1,0..1) to pick the leader of each
      2x2 supercell

      (3) Shoot that out to the fleet. Each supercell hub determines
      whether it is leader, which tells it whether it will shrink or
      expand its event region.

      (4) Using the existing contents of the full grid (which are
      supposedly now up-to-date globally), write to the caches of each
      supercell leader. We'd like it if we could do that in parallel
      across the BHs, which means we want to tell a per-BH thread to
      do the writing, I guess.

      (5) After the writing is done to all supercell leader caches,
      resume event processing. Leaders place event centers no less
      than 4 (EW radius) away from their cache boundaries; followers
      place event centers at least 4 (EW radius) + 10 (cache width)
      away from their local grid boundaries.

      (6) Wait a loooong time for some stuff to happen.

      (7) Suspend event processing and wait AGAIN.

      (8) Using the existing contents of the full grid (which are now
      again supposedly up to date, including the cache regions the
      supercell leaders were doing events on), write the relevant
      caches back to all the follower hubs.

      (9) Go to (2) until the cows come home.

     */

    static u32 currentLeader = U32_MAX;
    u32 nextLeader;
    static constexpr u32 MIN_MS_PHASE = 7'000;
    static constexpr u32 MAX_MS_PHASE = 8'000;

    static constexpr u32 EWP_DRAIN_SEC = 7;

    //// PICK NEW LEADER
    nextLeader = hostPRNG.Between(0,3);
    HTprintf("QuBo::IHH NEXT LEADER nL%u (cL%u)\n", nextLeader, currentLeader);

    if (nextLeader == currentLeader) {
      /// Same As The Old Leader: no reconfig needed
      sleepMsec(hostPRNG.Between(MIN_MS_PHASE,MAX_MS_PHASE)); 
      return; // Fast phase!
    }

    //// RETIRE OLD LEADER IF ANY
    if (currentLeader <= 3) {      // do we even have a leader?
      /// Finish Current Leader's Term

      HTprintf("%u QuBo::IHH RETIRE CURRENT LEADER (cL%u)\n", gettid(), currentLeader);

      // suspend events
      shootPHASER(PhaserBolt::CMD_SUSPEND_EWPS,{1});

      // wait til global grid is fully updated
      sleepSec(EWP_DRAIN_SEC);  // haha we don't know how to atm

      HTprintf("%u QuBo::IHH GRID 'FULLY UPDATED' FOR (cL%u)\n", gettid(), currentLeader);

      // Copy leader caches from global grid to follower sites
      writeCacheSites(currentLeader,true);

      HTprintf("%u QuBo::IHH cL%u caches written back to followers\n", gettid(), currentLeader);
      
    }
    
    //// ANNOUNCE NEW LEADER
    currentLeader = nextLeader;
    shootPHASER(PhaserBolt::CMD_SUPERCELL_LEADER,{(s32) currentLeader});

    HTprintf("%u QuBo::IHH NEW LEADER cL%u ANNOUNCED\n", gettid(), currentLeader);    

    /// Copy follower sites from global grid to leader caches
    writeCacheSites(currentLeader,false);

    HTprintf("%u QuBo::IHH cL%u follower sites written to leader caches\n", gettid(), currentLeader);

    /// Resume Events
    shootPHASER(PhaserBolt::CMD_SUSPEND_EWPS,{0});

    HTprintf("%u QuBo::IHH cL%u EVENTS RESUMED\n", gettid(), currentLeader);

    /// For Some Time
    sleepMsec(hostPRNG.Between(MIN_MS_PHASE,MAX_MS_PHASE)); 

    HTprintf("%u QuBo::IHH HACK DONE\n", gettid());
    return;
  }
#endif

  /* Write sections from the full grid to the caches sites around the
     currentLeader in (each) supercell.
   */
  void QuietBox::writeCacheSites(u32 currentLeader, bool tofollowers) {
    std::shuffle(mBHNumbersForIHH.begin(),mBHNumbersForIHH.end(),hostPRNG);
    for (u32 i = 0; i < mBHNumbersForIHH.size(); ++i) {
      u32 chip = mBHNumbersForIHH[i];
      Blackhole *bhp = getBlackholeIfPresent(chip);
      if (!bhp) continue;
      Blackhole & bh = *bhp;
      HTprintf("QuBo::writeCacheSites BH#%u cL%u tF%u\n", chip, currentLeader, tofollowers);
      bh.writeCacheSites(*this, currentLeader,tofollowers);
    }
  }

  void QuietBox::setupInterHubHackThread() {
    Eprintf("QB::IHHThread - setup\n");

    // hold thread lock before fucking with the IHH thread
    AtomicScopeLock guard(mInterHubHackThreadMutex);

    Eprintf("QB::IHHThread HAVE mutex\n");
    if (mInterHubHackThreadPtr) // already have a thread?
      HOST_FATAL(ILLEGAL_STATE,"QB::IHHThread already running"); // uwack

    mQuitInterHubHackThread.store(false); // set up for thread: don't quit
    mSuspendInterHubHacks.store(true);    // but do start suspended

    mInterHubHackThreadPtr = std::make_unique<std::thread>([this]() {
      
      BHLog & bhl = BHLog::getTheBHLog();

      Eprintf("QB::IHHThread (%u) THREAD STARTUP",bhl.getThrId());

      for (MFM::u64 i = 0u; ++i != 0u; ) {
        if (this->mQuitInterHubHackThread.load()) { // should we quit?
          Eprintf("QB::IHHThread (%u) QUIT REQ\n",bhl.getThrId());
          break;
        }
        if (this->mSuspendInterHubHacks.load()) { // should we not do an ihh loop?
          sleepSec(1);
        } else {
          Eprintf("QB::IHHThread (%u) START SUPERCYCLE vvvvv\n",bhl.getThrId());

          static u32 lastLeader = U32_MAX;
          u32 nextLeader;
          do { nextLeader = hostPRNG.Between(0,3); }
          while (nextLeader == lastLeader);
          lastLeader = nextLeader;
          this->doASuperCycle(lastLeader);
          Eprintf("QB::IHHThread (%u) SUPERCYCLE DONE  ^^^^^\n",bhl.getThrId());
        }
          
        const MFM::u64 aMILLION = 1'000'000ul;
        if (true && (i % aMILLION == 0)) {
          Eprintf("QB::IHHThread (%u) %u MILLION UPDATES\n",bhl.getThrId(),(MFM::u32) (i/aMILLION));
        }
        // XXXX DO IHHTHREAD WORK HERE
        // this->mOurTLBs.updateTransports(getPhase() >= Phase::HAS_T6_EVENT_WINDOWS);
      }
      Eprintf("QB::IHHThread (%u) OUT\n",bhl.getThrId());
    });
    Eprintf("QB::IHHThread 14 NEW((%p)) OUT releasing transmut\n",
            mInterHubHackThreadPtr.get());
  }

  void QuietBox::initSimGrid() {
    Eprintf("QB::initSimGrid HARO\n");
    const P4Atom a = P4Atom::makeEmptyAtom(); 
    for (u32 x = 0; x < DG::DEMO_GLOBAL_GRID_WIDTH; ++x) {
      for (u32 y = 0; y < DG::DEMO_GLOBAL_GRID_HEIGHT; ++y) {
        mFullSimGrid[x][y] = a;
      }
    }
  }
}
