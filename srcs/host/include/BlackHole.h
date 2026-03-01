#pragma once   /* -*- C++ -*- */
#include <atomic>
#include "itype.h"
#include "OurTLBs.h"
#include "CodeManager.h"
#include "BHTag.h"
#include "BHLog.h"
#include "OurMutex.h"
#include "ImageManager.h"
#include "CommsModule.h"

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
namespace py = pybind11;

#define BLACKHOLE_PHASES()                     \
  XX(UNINITTED)                                \
  XX(HAS_CARD_NUM)                             \
  XX(HAS_OPEN_DEVICE)                          \
  XX(HAS_ALLOCATED_TLBS)                       \
  XX(HAS_CONFIGURED_TLBS)                      \
  XX(HAS_ALLOCATED_HOST_RAM)                   \
  XX(HAS_T6_TILES_RESET)                       \
  XX(HAS_T6_CODE_DEPLOYED)                     \
  XX(HAS_TRANSPORT_THREAD)                     \
  XX(HAS_T6_CODE_RUNNING)                      \
  XX(HAS_T6_EVENT_WINDOWS)                 

namespace MFM {
  class BlackHole {
  public:
    enum Phase : u8 {

#define XX(name) name,
      BLACKHOLE_PHASES()
#undef XX

      PHASE_COUNT
    };

    static constexpr const char *(PHASE_NAMES[PHASE_COUNT]) = {

#define XX(name) #name,
        BLACKHOLE_PHASES()
#undef XX      

      };

    static const char * nameOfPhase(Phase p) {
      MFM_API_ASSERT(p < PHASE_COUNT, ILLEGAL_ARGUMENT);
      return PHASE_NAMES[p];
    }

    BlackHole(unsigned card = 0u)
      : mCardNum(card)
      , mCurrentPhase(Phase::HAS_CARD_NUM)
      , mQuitTransportThread(false)
      , mCodeManager(mCardNum,mOurTLBs)
      , mTransportThreadMutex("TSPO")
      , mTheImageManager(ImageManager::getTheImageManager())
    {
    }

    ~BlackHole() ;

    std::string to_repr() const {
      std::string ret = "<BlackHole#";
      ret.append(std::to_string(mCardNum));
      ret.append(" ph=");
      ret.append(nameOfPhase(getPhase()));
      ret.append(" cm=");
      ret.append(mOurTLBs.getHostCommsMap().to_repr());
      ret.append(">");
      return ret;
    }

    bool configureT6ImageForHostComms(T6Image & t6i) ;

    void setHostMemoryBaseAddress() {
      mOurTLBs.setHostMemoryBaseAddress();
    }

    void addCommsModule(CommsModule & cm) {
      mOurTLBs.addCommsModule(cm);
    }

    u32 getCardNumber() const {
      return mCardNum;
    }
    void setStartDecayType(u16 decaytype) {
      mCodeManager.setStartDecayType(decaytype);
    }
    Phase changePhase(Phase newPhase) ;
    Phase getPhase() const { return mCurrentPhase; }
    const char * nameOfCurrentPhase() const { return nameOfPhase(mCurrentPhase); }

    u32 monitorFleet() ; //< return count of new failures
    s32 runSlowScans(u32 count) ; //< return something after count HostBlock scans
    s32 deployRISCVCodeFromImage(T6Image& img, u8 toTLBI) ;

    void BHLOGprintf(const char * fmt, ...) {
      BHTag tag(TagType::APPDBG,mCardNum,0,0);
      BHLog & bhl = BHLog::getTheBHLog();
      va_list args;
      va_start(args, fmt);
      bhl.vprintf(tag,fmt,args);
      va_end(args);
    }

    // XXX WAS: static constexpr u32 HOST_RAM_PER_BH = 1u<<13;
    //static constexpr u32 HOST_RAM_PER_BH = 1u<<14; // we can fir 16KB*140, but it seems super slow??
    static constexpr u32 HOST_RAM_PER_BH = 1u<<13; // so stay here for now wtf wtf?
    static constexpr u32 MIN_GTEED_HOST_RAM_PER_BH = 1u<<13;

    void * getHostRAMPtrIfAny() { return mOurTLBs.hostRAMPtr(); }

  private:
    void phaseAdvance() ;
    void phaseRetreat() ;

    bool openCard() ;
    bool closeCard() ;

    bool allocateTLBs() ;
    bool deallocateTLBs() ;

    bool configureTLBs() ;
    bool unconfigureTLBs() ;

    bool allocateHostRAM() ;
    bool deallocateHostRAM() ;

    bool startTransportThread() ;
    bool stopTransportThread() ;

    bool setMFMxDefaultCodePath(std::string path) ;
    void clearMFMxDefaultCodePath() { mMFMxCodePath.clear(); }

    bool resetTheFleet() ;

    bool layoutImages() ;

    bool releaseTheHounds() ;

    Phase mCurrentPhase;
    u32 mCardNum;
    s32 mCardFD;

    OurTLBs mOurTLBs;

    void _stopTransportThread();
    std::unique_ptr<std::thread> mTransportThreadPtr;
    std::atomic<bool> mQuitTransportThread;
    OurMutex mTransportThreadMutex;

    std::string mMFMxCodePath;

    CodeManager mCodeManager;
    ImageManager & mTheImageManager;
    
  public:
    static void pybindings(py::module & m) {
      py::class_<BlackHole> bh(m,"BlackHole");

      py::native_enum<BlackHole::Phase> bhp(m,"Phase","enum.IntEnum");
      bhp.value("UNINITTED", BlackHole::Phase::UNINITTED);
      bhp.value("HAS_CARD_NUM", BlackHole::Phase::HAS_CARD_NUM);
      bhp.value("HAS_OPEN_DEVICE", BlackHole::Phase::HAS_OPEN_DEVICE);
      bhp.value("HAS_ALLOCATED_TLBS", BlackHole::Phase::HAS_ALLOCATED_TLBS);
      bhp.value("HAS_CONFIGURED_TLBS", BlackHole::Phase::HAS_CONFIGURED_TLBS);
      bhp.value("HAS_ALLOCATED_HOST_RAM", BlackHole::Phase::HAS_ALLOCATED_HOST_RAM);
      bhp.value("HAS_T6_TILES_RESET", BlackHole::Phase::HAS_T6_TILES_RESET);
      bhp.value("HAS_T6_CODE_DEPLOYED", BlackHole::Phase::HAS_T6_CODE_DEPLOYED);
      bhp.value("HAS_TRANSPORT_THREAD", BlackHole::Phase::HAS_TRANSPORT_THREAD);
      bhp.value("HAS_T6_CODE_RUNNING", BlackHole::Phase::HAS_T6_CODE_RUNNING);
      bhp.value("HAS_T6_EVENT_WINDOWS", BlackHole::Phase::HAS_T6_EVENT_WINDOWS);
      bhp.export_values();
      bhp.finalize();

      bh.def(py::init<const u32>());
      bh.def("__repr__", &BlackHole::to_repr,py::call_guard<py::gil_scoped_release>());
      bh.def("getCardNumber", &BlackHole::getCardNumber,py::call_guard<py::gil_scoped_release>());
      bh.def("setStartDecayType", &BlackHole::setStartDecayType,py::call_guard<py::gil_scoped_release>());
      //      bh.def("getPhase", [](BlackHole& b) { return (u32) b.getPhase(); },py::call_guard<py::gil_scoped_release>());
      bh.def("getPhase", &BlackHole::getPhase,py::call_guard<py::gil_scoped_release>());
      bh.def("setPhase", [](BlackHole& b, int j ) {
        if (j > Phase::UNINITTED && j <= Phase::HAS_T6_CODE_RUNNING) {
          if (j != b.getPhase()) {
            b.changePhase((Phase) j);
            return true;
          }
          return false;
        }
        HOST_FATAL(ILLEGAL_ARGUMENT,"Unknown or unhandled phase %d",j);
      },py::call_guard<py::gil_scoped_release>());
      bh.def("monitorFleet", &BlackHole::monitorFleet,py::call_guard<py::gil_scoped_release>());

      bh.def("open", [](BlackHole& b) { b.changePhase(Phase::HAS_OPEN_DEVICE); },py::call_guard<py::gil_scoped_release>());
      bh.def("allocateTLBs", [](BlackHole& b) { b.changePhase(Phase::HAS_ALLOCATED_TLBS); },py::call_guard<py::gil_scoped_release>());
      bh.def("configureTLBs", [](BlackHole& b) { b.changePhase(Phase::HAS_CONFIGURED_TLBS); },py::call_guard<py::gil_scoped_release>());
      bh.def("allocateHostRAM", [](BlackHole& b) { b.changePhase(Phase::HAS_ALLOCATED_HOST_RAM); },py::call_guard<py::gil_scoped_release>());
      //bh.def("setMFMxDefaultCodePath", &BlackHole::setMFMxDefaultCodePath,py::call_guard<py::gil_scoped_release>());
      //bh.def("layoutImages", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_CODE_DEPLOYED); },py::call_guard<py::gil_scoped_release>());
      bh.def("startMachine", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_CODE_RUNNING); },py::call_guard<py::gil_scoped_release>());
      bh.def("startEWProcessing", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_EVENT_WINDOWS); },py::call_guard<py::gil_scoped_release>());
      //

      bh.def("addCommsModule", &BlackHole::addCommsModule,py::call_guard<py::gil_scoped_release>());

      bh.def("configureT6ImageForHostComms",&BlackHole::configureT6ImageForHostComms,
             py::call_guard<py::gil_scoped_release>());

      // bh.def("setHostMemoryBaseAddress",&BlackHole::setHostMemoryBaseAddress,
      //        py::call_guard<py::gil_scoped_release>());

      bh.def("runSlowScans", &BlackHole::runSlowScans,py::call_guard<py::gil_scoped_release>());

      bh.def("getT6Key",[](BlackHole& b, u32 cardnum, u32 col, u32 row) {
        return BHTag(TagType::T6TADR,cardnum, col, row);
      },py::call_guard<py::gil_scoped_release>());

      bh.def("stopEWProcessing", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_CODE_RUNNING); },py::call_guard<py::gil_scoped_release>());
      bh.def("stopMFMxCode", [](BlackHole& b) { b.changePhase(Phase::HAS_T6_TILES_RESET); },py::call_guard<py::gil_scoped_release>());
      bh.def("clearMFMxDefaultCodePath", &BlackHole::clearMFMxDefaultCodePath,py::call_guard<py::gil_scoped_release>());
      bh.def("deallocateHostRAM", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_ALLOCATED_HOST_RAM-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("unconfigureTLBs", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_CONFIGURED_TLBS-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("freeTLBs", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_ALLOCATED_TLBS-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("close", [](BlackHole& b) { b.changePhase((Phase) (Phase::HAS_OPEN_DEVICE-1)); },py::call_guard<py::gil_scoped_release>());
    }
    
  };
}
