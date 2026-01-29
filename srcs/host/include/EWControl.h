#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U16C.h"
#include "EventWindow.h"
#include "HostUtils.h"
#include "P4Atom.h"
#include "Random.h"
#include "TransportBlock.h"
#include "BHTag.h"
#include "OurMutex.h"
#include "S32C.h"
#include "S8C.h"
#include "U8C.h"
#include "STVL.h"

#include <time.h>     /* For time() */

#include <pybind11/pybind11.h>
namespace py = pybind11;

#include <charconv> // for to_chars

namespace MFM {

  struct HostRandom : public Random {
    HostRandom()
      : Random((u32) time(NULL))
    { }
  };
  extern thread_local HostRandom myPRNG;

  struct EWControl {
    static EWControl & getTheEWControl() ;
  private:
    static EWControl theEWControl;
    EWControl() ;
  public:
    ~EWControl() ;
    static constexpr u32 GRID_WIDTH = 1920u;//*2u/3u;
    static constexpr u32 GRID_HEIGHT = 1080u;//*2u/3u;

    S32C getGridSize() const { return S32C(GRID_WIDTH,GRID_HEIGHT); }

    static constexpr s32 GRID_XMIN = -(s32)GRID_WIDTH/2;
    static constexpr s32 GRID_YMIN = -(s32)GRID_HEIGHT/2;

    static constexpr s32 GRID_XMAX = (GRID_WIDTH+1)/2;
    static constexpr s32 GRID_YMAX = (GRID_HEIGHT+1)/2;
    
    typedef STVL<GRID_XMIN,GRID_YMIN,GRID_XMAX,GRID_YMAX,4u,5'000'000u> EWLocker;
    EWLocker mEWLocker;

    std::atomic<bool> mEWsActive;
    P4Atom mGrid[GRID_WIDTH+1u][GRID_HEIGHT+1u];

    std::unique_ptr<std::thread> mEWRunnerThreadPtr;
    std::atomic<bool> mEWRunnerThreadAlive;
    OurMutex mEWRunnerThreadMutex;

    S32C mMin, mMax;                // current sampling bounds

    TimeStamp mStartTime;
    u64 mEventCount;
    u64 mEventCentersPicked, mEventCentersLockedOut;
    u64 mEWCentersBySampling, mEWCentersByEnumeration;
    u64 mEventCenterCommitsAttempted;
    u64 mEWUnchangedCommits, mEWLateCommits, mEWGoodCommits;
    u64 mEWGoodUnlocks, mEWFailedUnlocks;

    ////
    void initGrid() ;

    void runEWThread() ;

    //    S32C pickEWCenter() ;

    inline bool validCoord(S32C c) const {
      return
        c.x >= GRID_XMIN && c.x <= GRID_XMAX &&
        c.y >= GRID_YMIN && c.y <= GRID_YMAX;
    }

    inline P4Atom getAtom(S32C from) const {
      if (!validCoord(from)) return P4Atom::makeAtom(P4Atom::INACCESSIBLE_TYPE);
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

    bool isActive() const { return mEWsActive.load(); }
    bool setActive(bool newActive) {
      bool ret = mEWsActive;
      mEWsActive.store(newActive);
      return ret;
    }

    std::string doNuke(bool large) ;

    std::string doSeed() ;

    S32C randomCoordInBounds() ;

    static constexpr S32C ASPECT_RATIO = {2,1};

    std::string_view statsLine() const ;

    struct BackgroundPadder {
      S32C asize;
      S32C acorner;
      S32C agdpt;
      S32C steps;

      void init(S32C as, S32C ac) ;

      void at(S32C pt) { agdpt = pt; }

      static s32 absMod(s32 v, s32 m) {
        bool n = v < 0;
        if (n) v = -v;
        s32 r = v%m;
        if (n) r = -r;
        return r;
      }

      std::string padChr(const char ch) const ;
    };

    std::string_view renderGridWindow(S32C corner, S32C size, s32 zoom) ;

    bool pickEWCenter(S32C & occupied, EWLocker::Entry & token) ;

    void fillEW(S32C center, EventWindow & ew) ;

    bool tryLoadEWCar(EWCarStorage::EWCar & ec) ;

    void printActiveGrid() ;
    
    s32 tryCommitEWCar(BHTag tag, EWCarStorage::EWCar & ec) {
      EWBlock & eb = ec.getContent();
      S32C center(eb.mHiddenXPos,eb.mHiddenYPos);
      return commitEWIfPossible(tag, center, eb.mSTVLTime, eb.mOld, eb.mNew);
    }

    s32 commitEWIfPossible(BHTag tag, S32C center, TimeStamp when, EventWindow & oldew, EventWindow & newew) ;

    std::string to_string() const {
      return to_repr();
    }

    std::string to_repr() const {
      char buf[1024];
      snprintf(buf,sizeof(buf),"<EWControl:%s,[%d,%d]..[%d,%d]>",
               isActive() ? "ACTIVE" : "pause",
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

      py::class_<U8C> u8c(m,"U8C");
      u8c.def(py::init<>());
      u8c.def(py::init<const u8,const u8>());
      u8c.def_readwrite("x", &U8C::x);
      u8c.def_readwrite("y", &U8C::y);
      u8c.def("__repr__",&U8C::to_repr);

      py::class_<S8C> s8c(m,"S8C");
      s8c.def(py::init<>());
      s8c.def(py::init<const s8,const s8>());
      s8c.def_readwrite("x", &S8C::x);
      s8c.def_readwrite("y", &S8C::y);
      s8c.def("__repr__",&S8C::to_repr);
      
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
      ewc.def("getGridSize",&EWControl::getGridSize);
      ewc.def("randomCoordInBounds",&EWControl::randomCoordInBounds);
      ewc.def("renderGridWindow",&EWControl::renderGridWindow);
      ewc.def("statsLine",&EWControl::statsLine);
      ewc.def("pickEWCenter",[](EWControl & ewc) {
        S32C ret;
        EWLocker::Entry token;
        bool got = ewc.pickEWCenter(ret,token);
        return got ? py::cast(ret) : pybind11::none();
      });
      ewc.def("getAtom",&EWControl::getAtom);
      ewc.def("setAtom",&EWControl::setAtom);
      ewc.def("makeAtom",&EWControl::makeAtom);
      ewc.def("isActive",&EWControl::isActive);
      ewc.def("setActive",&EWControl::setActive);
      ewc.def("doNuke",&EWControl::doNuke);
      ewc.def("doSeed",&EWControl::doSeed);
    }
  };
}

