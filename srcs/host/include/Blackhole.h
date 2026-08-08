#pragma once   /* -*- C++ -*- */
#include <atomic>
#include "itype.h"
#include "SimConstants.h"
#include "OurTLBs.h"
#include "CodeManager.h"
#include "BHTag.h"
#include "BHLog.h"
#include "AtomicLock.h"
#include "ImageManager.h"
#include "CommsModule.h"
#include "QuietBox.h"

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
namespace py = pybind11;

#define BLACKHOLE_PHASES()                     \
  XX(UNINITTED)                                \
  XX(HAS_CHIP_NUM)                             \
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
  class Blackhole {
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

    Blackhole(unsigned chip = 0u)
      : mCurrentPhase(Phase::HAS_CHIP_NUM)
      , mChipNum(chip)
      , mOurTLBs(*this)
      , mQuitTransportThread(false)
      , mCodeManager(mChipNum,mOurTLBs)
      , mTransportThreadMutex("TSPO")
      , mTheImageManager(ImageManager::getTheImageManager())
    {
      QuietBox::get().addBlackhole(*this);
    }

    ~Blackhole() ;

    std::string to_repr() const {
      std::string ret = "<Blackhole#";
      ret.append(std::to_string(mChipNum));
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

    u32 getChipNumber() const {
      return mChipNum;
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
      BHTag tag(TagType::APPDBG,mChipNum,0,0);
      BHLog & bhl = BHLog::getTheBHLog();
      va_list args;
      va_start(args, fmt);
      bhl.vprintf(tag,fmt,args);
      va_end(args);
    }

    void * getHostRAMPtrIfAny() { return mOurTLBs.hostRAMPtr(); }

    OurTLBs & getOurTLBs() { return mOurTLBs; }

    CodeManager & getCodeManager() { return mCodeManager; }

  private:
    void phaseAdvance() ;
    void phaseRetreat() ;

    bool openChip() ;
    bool closeChip() ;

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
    u32 mChipNum;
    s32 mChipFD;

    OurTLBs mOurTLBs;

    void _stopTransportThread();
    std::unique_ptr<std::thread> mTransportThreadPtr;
    std::atomic<bool> mQuitTransportThread;
    AtomicLock mTransportThreadMutex;

    std::string mMFMxCodePath;

    CodeManager mCodeManager;
    ImageManager & mTheImageManager;
    
  public:
    s32 scanHubGrid() { return mCodeManager.scanHubGrid(); }
    
    static void pybindings(py::module & m) {
      py::class_<Blackhole> bh(m,"Blackhole");

      py::native_enum<Blackhole::Phase> bhp(m,"Phase","enum.IntEnum");
      bhp.value("UNINITTED", Blackhole::Phase::UNINITTED);
      bhp.value("HAS_CHIP_NUM", Blackhole::Phase::HAS_CHIP_NUM);
      bhp.value("HAS_OPEN_DEVICE", Blackhole::Phase::HAS_OPEN_DEVICE);
      bhp.value("HAS_ALLOCATED_TLBS", Blackhole::Phase::HAS_ALLOCATED_TLBS);
      bhp.value("HAS_CONFIGURED_TLBS", Blackhole::Phase::HAS_CONFIGURED_TLBS);
      bhp.value("HAS_ALLOCATED_HOST_RAM", Blackhole::Phase::HAS_ALLOCATED_HOST_RAM);
      bhp.value("HAS_T6_TILES_RESET", Blackhole::Phase::HAS_T6_TILES_RESET);
      bhp.value("HAS_T6_CODE_DEPLOYED", Blackhole::Phase::HAS_T6_CODE_DEPLOYED);
      bhp.value("HAS_TRANSPORT_THREAD", Blackhole::Phase::HAS_TRANSPORT_THREAD);
      bhp.value("HAS_T6_CODE_RUNNING", Blackhole::Phase::HAS_T6_CODE_RUNNING);
      bhp.value("HAS_T6_EVENT_WINDOWS", Blackhole::Phase::HAS_T6_EVENT_WINDOWS);
      bhp.export_values();
      bhp.finalize();

      bh.def(py::init<const u32>());
      bh.def("__repr__", &Blackhole::to_repr,py::call_guard<py::gil_scoped_release>());
      bh.def("getChipNumber", &Blackhole::getChipNumber,py::call_guard<py::gil_scoped_release>());
      bh.def("setStartDecayType", &Blackhole::setStartDecayType,py::call_guard<py::gil_scoped_release>());
      //      bh.def("getPhase", [](Blackhole& b) { return (u32) b.getPhase(); },py::call_guard<py::gil_scoped_release>());
      bh.def("getPhase", &Blackhole::getPhase,py::call_guard<py::gil_scoped_release>());
      bh.def("setPhase", [](Blackhole& b, int j ) {
        if (j > Phase::UNINITTED && j <= Phase::HAS_T6_CODE_RUNNING) {
          if (j != b.getPhase()) {
            b.changePhase((Phase) j);
            return true;
          }
          return false;
        }
        HOST_FATAL(ILLEGAL_ARGUMENT,"Unknown or unhandled phase %d",j);
      },py::call_guard<py::gil_scoped_release>());
      bh.def("monitorFleet", &Blackhole::monitorFleet,py::call_guard<py::gil_scoped_release>());

      bh.def("open", [](Blackhole& b) { b.changePhase(Phase::HAS_OPEN_DEVICE); },py::call_guard<py::gil_scoped_release>());
      bh.def("allocateTLBs", [](Blackhole& b) { b.changePhase(Phase::HAS_ALLOCATED_TLBS); },py::call_guard<py::gil_scoped_release>());
      bh.def("configureTLBs", [](Blackhole& b) { b.changePhase(Phase::HAS_CONFIGURED_TLBS); },py::call_guard<py::gil_scoped_release>());
      bh.def("allocateHostRAM", [](Blackhole& b) { b.changePhase(Phase::HAS_ALLOCATED_HOST_RAM); },py::call_guard<py::gil_scoped_release>());
      //bh.def("setMFMxDefaultCodePath", &Blackhole::setMFMxDefaultCodePath,py::call_guard<py::gil_scoped_release>());
      //bh.def("layoutImages", [](Blackhole& b) { b.changePhase(Phase::HAS_T6_CODE_DEPLOYED); },py::call_guard<py::gil_scoped_release>());
      bh.def("startMachine", [](Blackhole& b) { b.changePhase(Phase::HAS_T6_CODE_RUNNING); },py::call_guard<py::gil_scoped_release>());
      bh.def("startEWProcessing", [](Blackhole& b) { b.changePhase(Phase::HAS_T6_EVENT_WINDOWS); },py::call_guard<py::gil_scoped_release>());
      //

      bh.def("addCommsModule", &Blackhole::addCommsModule,py::call_guard<py::gil_scoped_release>());

      bh.def("configureT6ImageForHostComms",&Blackhole::configureT6ImageForHostComms,
             py::call_guard<py::gil_scoped_release>());

      // bh.def("setHostMemoryBaseAddress",&Blackhole::setHostMemoryBaseAddress,
      //        py::call_guard<py::gil_scoped_release>());

      bh.def("runSlowScans", &Blackhole::runSlowScans,py::call_guard<py::gil_scoped_release>());

      bh.def("getT6Key",[](Blackhole& b, u32 chipnum, u32 col, u32 row) {
        return BHTag(TagType::T6TADR,chipnum, col, row);
      },py::call_guard<py::gil_scoped_release>());

      bh.def("stopEWProcessing", [](Blackhole& b) { b.changePhase(Phase::HAS_T6_CODE_RUNNING); },py::call_guard<py::gil_scoped_release>());
      bh.def("stopMFMxCode", [](Blackhole& b) { b.changePhase(Phase::HAS_T6_TILES_RESET); },py::call_guard<py::gil_scoped_release>());
      bh.def("clearMFMxDefaultCodePath", &Blackhole::clearMFMxDefaultCodePath,py::call_guard<py::gil_scoped_release>());
      bh.def("deallocateHostRAM", [](Blackhole& b) { b.changePhase((Phase) (Phase::HAS_ALLOCATED_HOST_RAM-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("unconfigureTLBs", [](Blackhole& b) { b.changePhase((Phase) (Phase::HAS_CONFIGURED_TLBS-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("freeTLBs", [](Blackhole& b) { b.changePhase((Phase) (Phase::HAS_ALLOCATED_TLBS-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("close", [](Blackhole& b) { b.changePhase((Phase) (Phase::HAS_OPEN_DEVICE-1)); },py::call_guard<py::gil_scoped_release>());
    }
    
  };
}
