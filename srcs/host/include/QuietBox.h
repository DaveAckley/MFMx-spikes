#pragma once   /* -*- C++ -*- */

#include "itype.h"
#include "DemoGlobal.h"
#include "UxC.h" // for U16C
#include "P4Atom.h"
#include "BlockCode.h"
#include "CellBlock.h"
#include "ImageManager.h"
#include "BGRImage.h"
#include "NgbCacheMgr.h"
#include <atomic>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

namespace std {
  template<>
  struct hash<MFM::U16C> {
    size_t operator()(const MFM::U16C & c) const {
      return (c.x<<16)^c.y^(c.x>>16);
    }
  };
}

namespace MFM {

  namespace DG {
    // (here instead of DemoGlobal to hide from cross )
    static std::string to_string(const Coord a) ;
    static std::string to_string(const Address a) ;
  }

  struct Blackhole; // FORWARD
  
  struct T6GridInfo {
    DG::Coord mT6GridOrigin;    //< where t6grid starts in HD
    DG::Address mDGAddress;     //< physical address of P4Atom [0][0] (may be invalid)
    u32 mTLBI;
    u32 mT6GridL1Base;          //< T6Grid L1 block addr
  };

  struct QuietBox {
    static constexpr u32 MAX_BLACKHOLES = 4u;
    static QuietBox & get() ;   // MAX_QUIETBOX = 1

    QuietBox() ;
    // We do not own the BHs, but we do own the InterHub hack thread, and
    // destroying a joinable std::thread calls std::terminate.
    ~QuietBox() { _stopInterHubHackThread(); }

    /** Stop and join the InterHub hack thread.  Safe to call more than
        once; init() can start a new one afterwards. */
    void shutdown() { _stopInterHubHackThread(); }

    void init() {
      initSimGrid();
      setupInterHubHackThread();
    }

    static ImageBlockAddr * findBCInCell(ImageManager & im,
                                         const BlockCode bc,
                                         const CellBlock cb,
                                         U8C & cellp) ;

    void loadMaps(ImageManager& im, u32 bhc) ;

    void writeCacheSites(u32 currentLeader, bool tofollowers) ;

    void _stopInterHubHackThread();
    std::unique_ptr<std::thread> mInterHubHackThreadPtr;
    std::atomic<bool> mQuitInterHubHackThread;
    std::atomic<bool> mSuspendInterHubHacks;
    AtomicLock mInterHubHackThreadMutex;
    void suspendInterHubHacks(bool suspend) {
      mSuspendInterHubHacks.store(suspend);
    }

    void setupInterHubHackThread();
    //    void doAnIHHHack() ; // runs on IHHThread
    void doASuperCycle(u32 nextLeader) ; // runs on IHHThread

    u32 mSuperCycleCount;

    static BGRImageHD t6gridRenderBlock;

    static BGRImageHD & getT6GridImage() {
      return t6gridRenderBlock;
    }

    static BGRImageHD & clearT6GridImage() {
      BGRImageHD & bgr = getT6GridImage();
      bgr.reset();
      resetPixelEpochs();
      return bgr;
    }

    /* Newest report CHANGE KEY (AtomReport.h change stamp: changeEpoch << 1
       | phase, 16 bits) accepted per pixel.  (Was the emission epoch.)
       Hubs overlap (each paints its S/E cache ring on a neighbor's pixels),
       and the streams arrive in no fixed order, so a late older report must
       not overwrite a newer one.  Epochs are the leader cycle count mod
       65536, compared as a wraparound difference; the idle refresh touches
       every site within about a minute, far inside half the range. */
    static constexpr u32 PIXEL_EPOCHS = 1920u*1080u;
    static std::atomic<u16> pixelEpochs[PIXEL_EPOCHS];
    static void resetPixelEpochs() {
      for (u32 i = 0u; i < PIXEL_EPOCHS; ++i) {
        pixelEpochs[i].store(0u,std::memory_order_relaxed);
        painters[i].store(0u,std::memory_order_relaxed);
        for (u32 k = 0u; k < PIXEL_HISTORY; ++k)
          pixelHistory[i*PIXEL_HISTORY+k].store(0u,std::memory_order_relaxed);
      }
      newestEpoch.store(0u,std::memory_order_relaxed);
    }

    /* WHO PAINTED A PIXEL, last: for hunting ghost pixels ("turds").  One
       word per pixel: valid, epoch known, ring (the report came from the
       painting hub's S/E cache ring, not its own SELF sites), chip, tlbi,
       atom type (9 bits), epoch, and the site's grid x,y in that hub (for l1grid --site).  Written by the decoder threads (relaxed; a torn
       read of a pixel being painted just shows one painter or the other). */
    struct Painter {
      bool mValid = false;
      bool mEpochKnown = false;
      bool mRing = false;
      u32 mChip = 0u, mTLBI = 0u, mType = 0u, mEpoch = 0u;
      u32 mGX = 0u, mGY = 0u;     // the painting hub's own grid coords of the site
    };
    static std::atomic<u64> painters[PIXEL_EPOCHS];

    /* PIXEL HISTORY: the last PIXEL_HISTORY reports that reached each pixel,
       accepted or dropped as stale, newest first, so a turd's last few
       arrivals can be read back (who painted, in which epoch, and in what
       order).  One u32 each: bit 31 valid, 30 accepted, 29 ring, 28..20
       atom type, 16 change phase (1 = events), 14..0 change epoch
       (low 15 bits; 0 if the stream epoch is unknown).  Written by several
       decoder threads without a lock (shift, relaxed): a torn entry is
       possible under a race, which is fine for diagnosis.  33 MB. */
    static constexpr u32 PIXEL_HISTORY = 4u;
    static std::atomic<u32> pixelHistory[PIXEL_EPOCHS*PIXEL_HISTORY];
    static void noteHistory(u32 pixelIndex, bool accepted, bool ring,
                            u32 type, u16 changeKey) {
      std::atomic<u32> * h = &pixelHistory[pixelIndex*PIXEL_HISTORY];
      for (u32 k = PIXEL_HISTORY-1u; k > 0u; --k)
        h[k].store(h[k-1u].load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
      h[0].store((1u<<31) | ((u32) accepted << 30) | ((u32) ring << 29) |
                 ((type & 0x1ffu) << 20) | ((u32) (changeKey & 1u) << 16) |
                 (u32) (changeKey >> 1), std::memory_order_relaxed);
    }
    static u32 getHistory(u32 pixelIndex, u32 k) {
      if (pixelIndex >= PIXEL_EPOCHS || k >= PIXEL_HISTORY) return 0u;
      return pixelHistory[pixelIndex*PIXEL_HISTORY+k].load(std::memory_order_relaxed);
    }
    static void notePainter(u32 pixelIndex, bool epochKnown, u16 epoch,
                            bool ring, u32 chip, u32 tlbi, u32 type,
                            u32 gx, u32 gy) {
      u64 v = (1ull<<63) | ((u64) epochKnown << 62) | ((u64) ring << 61) |
        ((u64) (chip & 0xff) << 48) | ((u64) (tlbi & 0xff) << 40) |
        ((u64) (gx & 0xff) << 32) | ((u64) (gy & 0x7f) << 25) |
        ((u64) (type & 0x1ff) << 16) | epoch;
      painters[pixelIndex].store(v,std::memory_order_relaxed);
    }
    static Painter whoPainted(u32 pixelIndex) {
      Painter p;
      if (pixelIndex >= PIXEL_EPOCHS) return p;
      const u64 v = painters[pixelIndex].load(std::memory_order_relaxed);
      p.mValid = (v >> 63) & 1u;
      p.mEpochKnown = (v >> 62) & 1u;
      p.mRing = (v >> 61) & 1u;
      p.mChip = (u32) ((v >> 48) & 0xff);
      p.mTLBI = (u32) ((v >> 40) & 0xff);
      p.mType = (u32) ((v >> 16) & 0x1ff);
      p.mGX = (u32) ((v >> 32) & 0xff);
      p.mGY = (u32) ((v >> 25) & 0x7f);
      p.mEpoch = (u32) (v & 0xffff);
      return p;
    }
    /* newest epoch any stream has announced (bit 16 set once there is one) */
    static std::atomic<u32> newestEpoch;
    static void noteEpochSeen(u16 epoch) {
      u32 cur = newestEpoch.load(std::memory_order_relaxed);
      do {
        if ((cur & 0x10000u) && (s16) (u16) (epoch - (u16) cur) <= 0) return;
      } while (!newestEpoch.compare_exchange_weak(cur,0x10000u | epoch,
                                                  std::memory_order_relaxed));
    }
    /** true: draw this report (its epoch is not older than the pixel's);
        the pixel's epoch is advanced.  false: stale, ignore it. */
    static bool acceptPixelEpoch(u32 pixelIndex, u16 epoch) {
      std::atomic<u16> & pe = pixelEpochs[pixelIndex];
      u16 cur = pe.load(std::memory_order_relaxed);
      do {
        if ((s16) (u16) (epoch - cur) < 0) return false;   // older: drop
        if (epoch == cur) return true;                     // tie: arrival order
      } while (!pe.compare_exchange_weak(cur,epoch,std::memory_order_relaxed));
      return true;
    }

    using T6GridIndex = DG::Coord;
    using T6GridInfoByCoordMap = std::unordered_map<T6GridIndex,T6GridInfo>;
    T6GridInfoByCoordMap mT6GridInfoByCoordMap;

    using ChipAndTLBI = std::pair<u32,u32>;
    struct ChipAndTLBIHash {
      std::size_t operator()(const ChipAndTLBI& p) const {
        constexpr u32 BIFSHITS = 19u;
        return (p.first<<BIFSHITS) + p.second + (p.first>>(32-BIFSHITS));
      }
    };
    using T6GridInfoByChipAndTLBIMap = std::unordered_map<ChipAndTLBI,T6GridInfo*,ChipAndTLBIHash>;
    T6GridInfoByChipAndTLBIMap mT6GridInfoByChipAndTLBIMap;

    T6GridInfo & getT6GridInfoByAddress(DG::Address addr) ;
    T6GridInfo & getT6GridInfoByChipAndTLBI(u32 chipnum, u32 tlbi) {
      const ChipAndTLBI key(chipnum,tlbi);
      T6GridInfo * t6gip = mT6GridInfoByChipAndTLBIMap[key];
      MFM_API_ASSERT_NONNULL(t6gip);
      return *t6gip;
    }

    const T6GridInfo & getT6GridInfoFor(const DG::Coord to) ;

    bool storeP4Atom(const DG::Coord to, const P4Atom atom) ; //< INTO A T6 HUB SITE!
    bool getT6GridOrigin(u32 chipNum, u32 tlbi, U32C & origin) ;

    void addBlackhole(Blackhole & bh) ;

    Blackhole * getBlackholeIfPresent(u32 chipnum) const ;
    
    std::string to_string() const { return to_repr(); }

    std::string to_repr() const ;

    Blackhole * mBHPtrs[MAX_BLACKHOLES];

    std::string onPhases() ;
    std::string phaseIndices() ;
    std::string loggingSummary() ;

    std::string getConstants() { return DG::dumpConstants(); }
    std::string getRanges() { return NgbCacheMgr::dumpAllRanges(); }

    void shootPHASER(PhaserBolt::Cmd cmd, std::vector<s32> args) ;
    void firePHASER(PhaserBolt::Cmd cmd, std::vector<s32> args) ;  // non-blocking: write to L1 and return; poll onPhases()
    std::array<u8,4> mBHNumbers = {0,1,2,3};
    PhaserBlock mPhaserBlock;

    std::array<u8,4> mBHNumbersForIHH = {0,1,2,3};
    
    bool mDrawGrid;

    // HOST-SIDE FULL GRID
    P4Atom mFullSimGrid[DG::DEMO_GLOBAL_GRID_WIDTH][DG::DEMO_GLOBAL_GRID_HEIGHT];
    void initSimGrid() ;
    P4Atom & getSimAtomOrDie(U16C gridc) {
      MFM_API_ASSERT(gridc.x < DG::DEMO_GLOBAL_GRID_WIDTH &&
                     gridc.y < DG::DEMO_GLOBAL_GRID_HEIGHT,
                     ILLEGAL_ARGUMENT);
      return mFullSimGrid[gridc.x][gridc.y];
    }
    bool setSimAtom(U16C gridc, const P4Atom newa) {
      if (gridc.x < DG::DEMO_GLOBAL_GRID_WIDTH &&
          gridc.y < DG::DEMO_GLOBAL_GRID_HEIGHT /* && newa!= olda? */) {
        mFullSimGrid[gridc.x][gridc.y] = newa;
        return true;
      }
      return false;
    }

    static void pybindings(py::module & m) {
      py::class_<QuietBox> qb(m,"QuietBox");
      qb.def_static("get",&QuietBox::get, py::return_value_policy::reference);
      qb.def("__str__",&QuietBox::to_string);
      qb.def("__repr__",&QuietBox::to_repr);
      qb.def_readwrite("mDrawGrid",&QuietBox::mDrawGrid);
      qb.def("init",&QuietBox::init);
      qb.def("shutdown",&QuietBox::shutdown,py::call_guard<py::gil_scoped_release>());
      qb.def("suspendIHH",&QuietBox::suspendInterHubHacks);
      qb.def("shootPHASER",&QuietBox::shootPHASER);
      qb.def("firePHASER",&QuietBox::firePHASER);
      qb.def("getConstants",&QuietBox::getConstants);
      qb.def("getRanges",&QuietBox::getRanges);
      qb.def("onPhases",&QuietBox::onPhases);
      qb.def("phaseIndices",&QuietBox::phaseIndices);
      qb.def("loggingSummary",&QuietBox::loggingSummary);


      // Expose Cmd for 1st arg to shootPHASER
      py::class_<PhaserBolt> ph(m,"PhaserBolt");
      py::native_enum<PhaserBolt::Cmd>(ph,"Cmd","enum.Enum")
#define XX(name,argc) .value("" #name, PhaserBolt::Cmd::name)
      ALL_PHASER_COMMANDS()
#undef XX
        .export_values()
        .finalize();
    }    
  };
}

