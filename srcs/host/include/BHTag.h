#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U16C.h"

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <charconv> // for to_chars

namespace MFM {
  enum TagType : u8 {
    UNINIT = 0u,            // unknown / illegal
    T6TADR = 1u,            // t6 tile address
    BHCADR = 2u,            // blackhole card address
    APPDBG = 3u,            // app-level (global) context
  };

  struct BHTag {
    TagType mType;
    u8 mCard;
    u8 mTLBI;
    U16C mPos;

    BHTag(u8 t, u8 c, u32 tlbi)
      : mType((TagType) t)
      , mCard(c)
      , mTLBI(tlbi)
    {
      setXY();
    }

    BHTag(u8 t, u8 c, u8 x, u8 y)
      : mType((TagType) t)
      , mCard(c)
      , mPos({x,y})
    {
      setTLBI();
    }

    void setTLBI() { mTLBI = U16C::makeTLBIFromNocCoord({mPos.x,mPos.y}); }
    void setXY() { mPos = U16C::makeNocCoordFromTLBI(mTLBI); }

    std::string to_string() const {
      char buf[8];
      switch (mType) {
      case TagType::UNINIT: buf[0] = '?'; buf[1] = '!'; break; // try to break stuff in python/css
      case TagType::T6TADR: buf[0] = 'a'; buf[1] = 't'; break; // 'a'ddress of 't'ile
      case TagType::BHCADR: buf[0] = 'b'; buf[1] = 'h'; break; // 'b'lack'h'ole card level source
      case TagType::APPDBG: buf[0] = 'g'; buf[1] = 'd'; break; // 'g'lobal 'd'ebug source of some kind
      default: buf[0] = '#'; buf[1] = '#'; break; // try to break stuff in python/css
      }
      std::to_chars(buf+2,buf+3,mCard,10);
      std::to_chars(buf+3,buf+4,mPos.x,36);
      std::to_chars(buf+4,buf+5,mPos.y,36);
      return std::string(buf,5);
    }

    std::string to_repr() const {
      return "<T6:type="+std::to_string(mType)
        +",card="+ std::to_string(mCard)
        +",x=" + std::to_string(mPos.x)
        +",y=" + std::to_string(mPos.y)
        +",tlbi=" + std::to_string(mTLBI)
        +">";
    }
    static void pybindings(py::module & m) {
      py::class_<BHTag> t6(m,"BHTag");
      t6.def(py::init<const u32,const u32,const u32,const u32>());
      t6.def_readwrite("card", &BHTag::mCard);
      t6.def_property("x",
                      [](const BHTag& t6) { return t6.mPos.x; },
                      [](BHTag& t6, u8 val) { t6.mPos.x = val; });
      t6.def_property("y",
                      [](const BHTag& t6) { return t6.mPos.y; },
                      [](BHTag& t6, u8 val) { t6.mPos.y = val; });
      t6.def_readonly("tlbi", &BHTag::mTLBI);
      t6.def("setTLBI",&BHTag::setTLBI);
      t6.def("__str__",&BHTag::to_string);
      t6.def("__repr__",&BHTag::to_repr);
    }
  };
}

