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

#define BLACKHOLE_STEPS()                     \
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
    enum Step : u8 {

#define XX(name) name,
      BLACKHOLE_STEPS()
#undef XX

      STEP_COUNT
    };

    static constexpr const char *(STEP_NAMES[STEP_COUNT]) = {

#define XX(name) #name,
        BLACKHOLE_STEPS()
#undef XX      

      };

    static const char * nameOfStep(Step p) {
      MFM_API_ASSERT(p < STEP_COUNT, ILLEGAL_ARGUMENT);
      return STEP_NAMES[p];
    }

    Blackhole(unsigned chip = 0u)
      : mCurrentStep(Step::HAS_CHIP_NUM)
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
      ret.append(nameOfStep(getStep()));
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
    s32 getChipFD() const { return mChipFD; }  //< device fd (valid from HAS_OPEN_DEVICE); the eth-seam port (EthSeamHostPy.h) borrows it
    void setStartDecayType(u16 decaytype) {
      mCodeManager.setStartDecayType(decaytype);
    }
    void setCachePolicy(u8 policy) {          //< 0 supercell, 1 fourcorners (CachingPolicy.h)
      mCodeManager.setCachePolicy(policy);
    }
    Step changeStep(Step newStep) ;
    Step getStep() const { return mCurrentStep; }
    const char * nameOfCurrentStep() const { return nameOfStep(mCurrentStep); }

    u32 monitorFleet() ; //< return count of new failures
    s32 runSlowScans(u32 count) ; //< return something after count HostBlock scans
    std::string dumpHubL1(u32 hubx, u32 huby) ; //< whole L1 of a hub to dumps/; path or ""
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

    /** Return TLBIs that have not yet acked the current phase (empty = all acked). */
    std::vector<u32> getNonPhaseTLBIs() { return mOurTLBs.getNonPhaseTLBIList(); }

    /** Return the BHTag string (e.g. "at023") for a TLBI on this chip. */
    std::string getTLBITag(u32 tlbi) { return BHTag::t6adt(mChipNum, tlbi); }

    /** Return the deployed image name for a TLBI, or "" if none deployed. */
    std::string getTLBIImageName(u32 tlbi) {
      const T6Image * img = mOurTLBs.getTLBInfo(tlbi).getDeployedImageIfAny();
      return img ? img->getName() : "";
    }

    CodeManager & getCodeManager() { return mCodeManager; }

  private:
    void stepAdvance() ;
    void stepRetreat() ;

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

    Step mCurrentStep;
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
    void writeCacheSites(QuietBox& qb, u32 currentLeader, bool tofollowers) {
      mCodeManager.writeCacheSites(qb, currentLeader, tofollowers);
    }

    s32 scanHubGrid() { return mCodeManager.scanHubGrid(); }
    
    static void pybindings(py::module & m) {
      py::class_<Blackhole> bh(m,"Blackhole");

      py::native_enum<Blackhole::Step> bhp(m,"Step","enum.IntEnum");
      bhp.value("UNINITTED", Blackhole::Step::UNINITTED);
      bhp.value("HAS_CHIP_NUM", Blackhole::Step::HAS_CHIP_NUM);
      bhp.value("HAS_OPEN_DEVICE", Blackhole::Step::HAS_OPEN_DEVICE);
      bhp.value("HAS_ALLOCATED_TLBS", Blackhole::Step::HAS_ALLOCATED_TLBS);
      bhp.value("HAS_CONFIGURED_TLBS", Blackhole::Step::HAS_CONFIGURED_TLBS);
      bhp.value("HAS_ALLOCATED_HOST_RAM", Blackhole::Step::HAS_ALLOCATED_HOST_RAM);
      bhp.value("HAS_T6_TILES_RESET", Blackhole::Step::HAS_T6_TILES_RESET);
      bhp.value("HAS_T6_CODE_DEPLOYED", Blackhole::Step::HAS_T6_CODE_DEPLOYED);
      bhp.value("HAS_TRANSPORT_THREAD", Blackhole::Step::HAS_TRANSPORT_THREAD);
      bhp.value("HAS_T6_CODE_RUNNING", Blackhole::Step::HAS_T6_CODE_RUNNING);
      bhp.value("HAS_T6_EVENT_WINDOWS", Blackhole::Step::HAS_T6_EVENT_WINDOWS);
      bhp.export_values();
      bhp.finalize();

      bh.def(py::init<const u32>());
      bh.def("__repr__", &Blackhole::to_repr,py::call_guard<py::gil_scoped_release>());
      bh.def("getChipNumber", &Blackhole::getChipNumber,py::call_guard<py::gil_scoped_release>());
      bh.def("setStartDecayType", &Blackhole::setStartDecayType,py::call_guard<py::gil_scoped_release>());
      bh.def("setCachePolicy", &Blackhole::setCachePolicy,py::call_guard<py::gil_scoped_release>());
      //      bh.def("getStep", [](Blackhole& b) { return (u32) b.getStep(); },py::call_guard<py::gil_scoped_release>());
      bh.def("getStep", &Blackhole::getStep,py::call_guard<py::gil_scoped_release>());
      bh.def("setStep", [](Blackhole& b, int j ) {
        if (j > Step::UNINITTED && j <= Step::HAS_T6_CODE_RUNNING) {
          if (j != b.getStep()) {
            b.changeStep((Step) j);
            return true;
          }
          return false;
        }
        HOST_FATAL(ILLEGAL_ARGUMENT,"Unknown or unhandled phase %d",j);
      },py::call_guard<py::gil_scoped_release>());
      bh.def("monitorFleet", &Blackhole::monitorFleet,py::call_guard<py::gil_scoped_release>());
      bh.def("getNonPhaseTLBIs", &Blackhole::getNonPhaseTLBIs,py::call_guard<py::gil_scoped_release>());
      bh.def("getTLBITag", &Blackhole::getTLBITag,py::call_guard<py::gil_scoped_release>());
      bh.def("getTLBIImageName", &Blackhole::getTLBIImageName,py::call_guard<py::gil_scoped_release>());

      bh.def("open", [](Blackhole& b) { b.changeStep(Step::HAS_OPEN_DEVICE); },py::call_guard<py::gil_scoped_release>());
      bh.def("allocateTLBs", [](Blackhole& b) { b.changeStep(Step::HAS_ALLOCATED_TLBS); },py::call_guard<py::gil_scoped_release>());
      bh.def("configureTLBs", [](Blackhole& b) { b.changeStep(Step::HAS_CONFIGURED_TLBS); },py::call_guard<py::gil_scoped_release>());
      bh.def("allocateHostRAM", [](Blackhole& b) { b.changeStep(Step::HAS_ALLOCATED_HOST_RAM); },py::call_guard<py::gil_scoped_release>());
      //bh.def("setMFMxDefaultCodePath", &Blackhole::setMFMxDefaultCodePath,py::call_guard<py::gil_scoped_release>());
      //bh.def("layoutImages", [](Blackhole& b) { b.changeStep(Step::HAS_T6_CODE_DEPLOYED); },py::call_guard<py::gil_scoped_release>());
      bh.def("startMachine", [](Blackhole& b) { b.changeStep(Step::HAS_T6_CODE_RUNNING); },py::call_guard<py::gil_scoped_release>());
      bh.def("startEWProcessing", [](Blackhole& b) { b.changeStep(Step::HAS_T6_EVENT_WINDOWS); },py::call_guard<py::gil_scoped_release>());
      //

      bh.def("addCommsModule", &Blackhole::addCommsModule,py::call_guard<py::gil_scoped_release>());

      bh.def("configureT6ImageForHostComms",&Blackhole::configureT6ImageForHostComms,
             py::call_guard<py::gil_scoped_release>());

      // bh.def("setHostMemoryBaseAddress",&Blackhole::setHostMemoryBaseAddress,
      //        py::call_guard<py::gil_scoped_release>());

      bh.def("runSlowScans", &Blackhole::runSlowScans,py::call_guard<py::gil_scoped_release>());
      bh.def("dumpHubL1", &Blackhole::dumpHubL1,py::call_guard<py::gil_scoped_release>());

      bh.def("getT6Key",[](Blackhole& b, u32 chipnum, u32 col, u32 row) {
        return BHTag(TagType::T6TADR,chipnum, col, row);
      },py::call_guard<py::gil_scoped_release>());

      bh.def("stopEWProcessing", [](Blackhole& b) { b.changeStep(Step::HAS_T6_CODE_RUNNING); },py::call_guard<py::gil_scoped_release>());
      bh.def("stopMFMxCode", [](Blackhole& b) { b.changeStep(Step::HAS_T6_TILES_RESET); },py::call_guard<py::gil_scoped_release>());
      bh.def("clearMFMxDefaultCodePath", &Blackhole::clearMFMxDefaultCodePath,py::call_guard<py::gil_scoped_release>());
      bh.def("deallocateHostRAM", [](Blackhole& b) { b.changeStep((Step) (Step::HAS_ALLOCATED_HOST_RAM-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("unconfigureTLBs", [](Blackhole& b) { b.changeStep((Step) (Step::HAS_CONFIGURED_TLBS-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("freeTLBs", [](Blackhole& b) { b.changeStep((Step) (Step::HAS_ALLOCATED_TLBS-1)); },py::call_guard<py::gil_scoped_release>());
      bh.def("close", [](Blackhole& b) { b.changeStep((Step) (Step::HAS_OPEN_DEVICE-1)); },py::call_guard<py::gil_scoped_release>());
    }
    
  };
}
