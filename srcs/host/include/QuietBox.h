#pragma once   /* -*- C++ -*- */

#include "itype.h"
#include "DemoGlobal.h"
#include "UxC.h" // for U16C
#include "P4Atom.h"
#include "BlockCode.h"
#include "CellBlock.h"
#include "ImageManager.h"
#include "BGRImage.h"

#include <pybind11/pybind11.h>
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
    DG::Coord mT6GridOrigin;  //< where t6grid starts in HD
    DG::Address mDGAddress;   //< physical address of P4Atom [0][0] (may be invalid)
    u32 mTLBI;
    u32 mT6GridL1Base;
  };

  struct QuietBox {
    static constexpr u32 MAX_BLACKHOLES = 4u;
    static QuietBox & get() ;   // MAX_QUIETBOX = 1

    QuietBox() ;
    ~QuietBox() { /* we do not own the BHs */ }

    static ImageBlockAddr * findBCInCell(ImageManager & im,
                                         const BlockCode bc,
                                         const CellBlock cb,
                                         U8C & cellp) ;

    void loadMaps(ImageManager& im, u32 bhc) ;

    static BGRImageHD t6gridRenderBlock;

    static BGRImageHD & getT6GridImage() {
      return t6gridRenderBlock;
    }

    static BGRImageHD & clearT6GridImage() {
      BGRImageHD & bgr = getT6GridImage();
      bgr.reset();
      return bgr;
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

    bool storeP4Atom(const DG::Coord to, const P4Atom atom) ;
    bool getT6GridOrigin(u32 chipNum, u32 tlbi, U32C & origin) ;

    void addBlackhole(Blackhole & bh) ;

    Blackhole * getBlackholeIfPresent(u32 chipnum) const ;
    
    std::string to_string() const { return to_repr(); }

    std::string to_repr() const ;

    Blackhole * mBHPtrs[MAX_BLACKHOLES];

    static void pybindings(py::module & m) {
      py::class_<QuietBox> ewc(m,"QuietBox");
      ewc.def_static("get",&QuietBox::get, py::return_value_policy::reference);
      ewc.def("__str__",&QuietBox::to_string);
      ewc.def("__repr__",&QuietBox::to_repr);
    }    
  };
}

