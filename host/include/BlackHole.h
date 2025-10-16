#pragma once   /* -*- C++ -*- */
#include "itype.h"
#include "OurTLBs.h"
#include "CodeManager.h"

#include <pybind11/pybind11.h>
namespace py = pybind11;

namespace MFM {
  class BlackHole {
  public:
    enum Phase : u8 {
      UNINITTED = 0,
      HAS_CARD_NUM,             // 1
      HAS_OPEN_DEVICE,          // 2
      HAS_ALLOCATED_TLBS,       // 3
      HAS_CONFIGURED_TLBS,      // 4
      HAS_ALLOCATED_HOST_RAM,   // 5
      HAS_TRANSPORT_THREAD,     // 6
      HAS_T6_TILES_RESET,       // 7
      HAS_T6_CODE_DEPLOYED,     // 8
      HAS_T6_CODE_RUNNING,      // 9
    };
    BlackHole(unsigned card = 0u)
      : mCardNum(card)
      , mCurrentPhase(Phase::HAS_CARD_NUM)
      , mCodeManager(mOurTLBs)
    { }
    u32 getCardNumber() const {
      return mCardNum;
    }
    Phase changePhase(Phase newPhase) ;
    Phase getPhase() const { return mCurrentPhase; }
  private:
    void phaseAdvance() ;
    void phaseRetreat() ;

    bool openCard() ;
    bool closeCard() ;

    bool allocateTLBs() ;
    bool deallocateTLBs() ;

    bool configureTLBs() ;
    bool unconfigureTLBs() ;

    static const u32 HOST_RAM_PER_BH = 1u<<13;
    bool allocateHostRAM() ;
    bool deallocateHostRAM() ;

    bool startTransportThread() ;
    bool stopTransportThread() ;

    bool setMFMxCodePath(std::string path) ;
    void clearMFMxCodePath() { mMFMxCodePath.clear(); }

    bool resetTheFleet() ;

    bool deployTheCode() ;

    bool releaseTheHounds() ;

    Phase mCurrentPhase;
    u32 mCardNum;
    s32 mCardFD;

    OurTLBs mOurTLBs;
    std::unique_ptr<std::thread> mTransportThread;
    bool mQuitTransportThread;
    std::string mMFMxCodePath;

    CodeManager mCodeManager;
    
  public:
    static void pybindings(py::module & m) {
      py::class_<BlackHole> bh(m,"BlackHole");
      py::class_<BlackHole::Phase> bhp(m,"BHPhase");
      bh.def(py::init<const u32>());
      bh.def("getCardNumber", &BlackHole::getCardNumber);
      bh.def("getPhase", [](BlackHole& b) { return (u32) b.getPhase(); });
      bh.def("setPhase", [](BlackHole& b, int j ) {
        if (j > Phase::UNINITTED && j <= Phase::HAS_T6_CODE_RUNNING) {
          if (j != b.getPhase()) {
            b.changePhase((Phase) j);
            return true;
          }
          return false;
        }
        HOST_FATAL(ILLEGAL_ARGUMENT,"Unknown or unhandled phase %d",j);
      });

      bh.def("open", [](BlackHole& b) { b.changePhase(Phase::HAS_OPEN_DEVICE); });
      bh.def("allocateTLBs", [](BlackHole& b) { b.changePhase(Phase::HAS_ALLOCATED_TLBS); });
      bh.def("configureTLBs", [](BlackHole& b) { b.changePhase(Phase::HAS_CONFIGURED_TLBS); });
      bh.def("allocateHostRAM", [](BlackHole& b) { b.changePhase(Phase::HAS_ALLOCATED_HOST_RAM); });
      bh.def("setMFMxCodePath", &BlackHole::setMFMxCodePath);
      bh.def("deployMFMxCode", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_CODE_DEPLOYED); });
      bh.def("startMFMxCode", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_CODE_RUNNING); });

      bh.def("stopMFMxCode", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_TILES_RESET); });
      bh.def("clearMFMxCodePath", &BlackHole::clearMFMxCodePath);
      bh.def("deallocateHostRAM", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_ALLOCATED_HOST_RAM-1)); });
      bh.def("unconfigureTLBs", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_CONFIGURED_TLBS-1)); });
      bh.def("freeTLBs", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_ALLOCATED_TLBS-1)); });
      bh.def("close", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_OPEN_DEVICE-1)); });

    }
    
  };
}
