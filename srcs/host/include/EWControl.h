#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U16C.h"
#include "EventWindow.h"
#include "HostUtils.h"
#include "P4Atom.h"
#include "Random.h"
#include "TransportBlock.h"
#include "BHTag.h"

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <charconv> // for to_chars

namespace MFM {

  struct S32C {
    s32 x,y;
    S32C() : x(0), y(0) { }
    S32C(s32 mx, s32 my) : x(mx), y(my) { }
    std::string to_repr() const {
      return
        std::string("<S32c:x=") + std::to_string(x) +
        ",y=" + std::to_string(y) + ">";
    }
  };

  struct EWControl {
    static EWControl & getTheEWControl() ;

    static constexpr u32 GRID_WIDTH = 1200u;
    static constexpr u32 GRID_HEIGHT = 1000u;

    static constexpr s32 GRID_XMIN = -(s32)GRID_WIDTH/2;
    static constexpr s32 GRID_YMIN = -(s32)GRID_HEIGHT/2;

    static constexpr s32 GRID_XMAX = (GRID_WIDTH+1)/2;
    static constexpr s32 GRID_YMAX = (GRID_HEIGHT+1)/2;
    
    bool mEWsActive;
    P4Atom mGrid[GRID_WIDTH][GRID_HEIGHT];

    S32C mMin, mMax;                // current sampling bounds

    Random mRandom;

    ////
    EWControl() ;
    void initGrid() ;

    //    S32C pickEWCenter() ;

    inline bool validCoord(S32C c) const {
      return
        c.x >= GRID_XMIN && c.x <= GRID_XMAX &&
        c.y >= GRID_YMIN && c.y <= GRID_YMAX;
    }

    inline P4Atom getAtom(S32C from) const {
      if (!validCoord(from)) return P4Atom();
      return mGrid[from.x - GRID_XMIN][from.y - GRID_YMIN];
    }

    inline P4Atom makeAtom(u16 type) const {
      return P4Atom::makeAtom(type);
    }

    inline bool setAtom(S32C to, P4Atom atom) {
      if (validCoord(to)) {
        mGrid[to.x - GRID_XMIN][to.y - GRID_YMIN] = atom;
        return true;
      }
      return false;
    }

    S32C randomCoordInBounds() ;

    bool pickEWCenter(S32C & occupied) ;

    void fillEW(S32C center, EventWindow & ew) ;

    bool tryLoadEWCar(EWCarStorage::EWCar & ec) ;

    bool tryCommitEWCar(BHTag tag, EWCarStorage::EWCar & ec) {
      EWBlock & eb = ec.getContent();
      S32C center(eb.mHiddenXPos,eb.mHiddenYPos);
      return commitEWIfPossible(tag, center, eb.mOld, eb.mNew);
    }

    bool commitEWIfPossible(BHTag tag, S32C center, EventWindow & oldew, EventWindow & newew) ;

    std::string to_string() const {
      return to_repr();
    }

    std::string to_repr() const {
      char buf[1024];
      snprintf(buf,sizeof(buf),"<EWControl:%s,[%d,%d]..[%d,%d]>",
               mEWsActive ? "ACTIVE" : "pause",
               mMin.x,mMin.y,
               mMax.x,mMax.x);
      return std::string(buf);
    }

    static void pybindings(py::module & m) {
      py::class_<S32C> s32c(m,"S32C");
      s32c.def(py::init<>());
      s32c.def(py::init<const s32,const s32>());
      s32c.def_readwrite("x", &S32C::x);
      s32c.def_readwrite("y", &S32C::y);
      s32c.def("__repr__",&S32C::to_repr);

      py::class_<P4Atom> p4(m,"P4Atom");
      p4.def("__repr__",[](const P4Atom& a) {
        const u32 BUF_SIZ=100;
        char buf[BUF_SIZ];
        snprintf(buf,BUF_SIZ,"<P4Atom:0x%04x+%04x.%08x.%08x>",
                 a.mParityAndType,a.mData0,
                 a.mStg[0],a.mStg[1]);
        return std::string(buf);
      });
      p4.def("isValid",&P4Atom::isValid);
      p4.def("getType",[](const P4Atom& a) {
        s32 ret = -1;
        if (a.isValid()) 
          ret = a.getType();
        return ret;
      });

      py::class_<EWControl> ewc(m,"EWControl");
      ewc.def_static("getEWControl",&EWControl::getTheEWControl, py::return_value_policy::reference);
      ewc.def("__str__",&EWControl::to_string);
      ewc.def("__repr__",&EWControl::to_repr);
      ewc.def("randomCoordInBounds",&EWControl::randomCoordInBounds);
      ewc.def("pickEWCenter",[](EWControl & ewc) {
        S32C ret;
        bool got = ewc.pickEWCenter(ret);
        return got ? py::cast(ret) : pybind11::none();
      });
      ewc.def("getAtom",&EWControl::getAtom);
      ewc.def("setAtom",&EWControl::setAtom);
      ewc.def("makeAtom",&EWControl::makeAtom);
      ewc.def("initGrid",&EWControl::initGrid);
    }
  };
}

