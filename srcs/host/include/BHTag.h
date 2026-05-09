#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "UxC.h" // for U16C

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <charconv> // for to_chars

namespace MFM {
  enum TagType : u8 {
    UNINIT = 0u,            // unknown / illegal
    T6TADR = 1u,            // t6 tile address
    BHCADR = 2u,            // blackhole chip address
    APPDBG = 3u,            // app-level (global) context
    PYTHON = 4u,            // python (supraglobal) context
    HOSTCT = 5u,            // host context
  };

  struct BHTag {
    TagType mType;
    u8 mChip;
    u8 mTLBI;
    U16C mNoC0;

    BHTag(u8 t, u8 c, u32 tlbi)
      : mType((TagType) t)
      , mChip(c)
      , mTLBI(tlbi)
    {
      setXY();
    }

    constexpr BHTag(u8 t, u8 c)
      : mType((TagType) t)
      , mChip(c)
      , mTLBI(0)
      , mNoC0(0,0)
    { }

    static std::string t6adc(u8 c, u8 x, u8 y) {
      BHTag t(T6TADR,c,x,y);
      return t.to_string();
    }

    static std::string t6adt(u8 c, u32 tlbi) {
      BHTag t(T6TADR,c,tlbi);
      return t.to_string();
    }

    BHTag(u8 t, u8 c, u8 x, u8 y)
      : mType((TagType) t)
      , mChip(c)
      , mNoC0(x,y)
    {
      setTLBI();
    }

    bool operator==(const BHTag & other) const {
      return
        mType == other.mType &&
        mChip == other.mChip &&
        mTLBI == other.mTLBI &&
        mNoC0 == other.mNoC0;
    }

    void setTLBI() { mTLBI = U16C::makeTLBIFromNoCCoord(U16C(mNoC0.x,mNoC0.y)); }
    void setXY() { mNoC0 = U16C::makeNoCCoordFromTLBI(mTLBI); }

    std::string to_string() const {
      char buf[8];
      switch (mType) {
      case TagType::UNINIT: buf[0] = '?'; buf[1] = '!'; break; // try to break stuff in python/css
      case TagType::T6TADR: buf[0] = 'a'; buf[1] = 't'; break; // 'a'ddress of 't'ile
      case TagType::BHCADR: buf[0] = 'b'; buf[1] = 'h'; break; // 'b'lack'h'ole chip level source
      case TagType::APPDBG: buf[0] = 'g'; buf[1] = 'd'; break; // 'g'lobal 'd'ebug source of some kind
      case TagType::PYTHON: buf[0] = 'p'; buf[1] = 'y'; break; // 'py'thon source of some kind
      case TagType::HOSTCT: buf[0] = 'h'; buf[1] = 'c'; break; // 'h'ost 'c'ontext of some kind
      default: buf[0] = '#'; buf[1] = '#'; break; // try to break stuff in python/css
      }
      std::to_chars(buf+2,buf+3,mChip,10);
      std::to_chars(buf+3,buf+4,mNoC0.x,36);
      std::to_chars(buf+4,buf+5,mNoC0.y,36);
      return std::string(buf,5);
    }

    std::string to_repr() const {
      return "<T6:type="+std::to_string(mType)
        +",chip="+ std::to_string(mChip)
        +",x=" + std::to_string(mNoC0.x)
        +",y=" + std::to_string(mNoC0.y)
        +",tlbi=" + std::to_string(mTLBI)
        +">";
    }
    static void pybindings(py::module & m) {
      py::class_<BHTag> t6(m,"BHTag");
      t6.def(py::init<const u32,const u32,const u32,const u32>());
      t6.def_readwrite("chip", &BHTag::mChip);
      t6.def_property("x",
                      [](const BHTag& t6) { return t6.mNoC0.x; },
                      [](BHTag& t6, u8 val) { t6.mNoC0.x = val; });
      t6.def_property("y",
                      [](const BHTag& t6) { return t6.mNoC0.y; },
                      [](BHTag& t6, u8 val) { t6.mNoC0.y = val; });
      t6.def_readonly("tlbi", &BHTag::mTLBI);
      t6.def("setTLBI",&BHTag::setTLBI);
      t6.def("__str__",&BHTag::to_string);
      t6.def("__repr__",&BHTag::to_repr);
    }
  };
}

namespace std {
  template<>
  struct hash<MFM::BHTag> {
    size_t operator()(const MFM::BHTag & t) const {
      return
        (((((((t.mType << 1) + t.mChip) << 1) + t.mTLBI) << 1)
          + t.mNoC0.x) << 1) + t.mNoC0.y;
    }
  };
}




  

