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

    /* Newest report epoch (AtomReport.h, epoch marker) accepted per pixel.
       Hubs overlap (each paints its S/E cache ring on a neighbor's pixels),
       and the streams arrive in no fixed order, so a late older report must
       not overwrite a newer one.  Epochs are the leader cycle count mod
       65536, compared as a wraparound difference; the idle refresh touches
       every site within about a minute, far inside half the range. */
    static constexpr u32 PIXEL_EPOCHS = 1920u*1080u;
    static std::atomic<u16> pixelEpochs[PIXEL_EPOCHS];
    static void resetPixelEpochs() {
      for (u32 i = 0u; i < PIXEL_EPOCHS; ++i)
        pixelEpochs[i].store(0u,std::memory_order_relaxed);
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

