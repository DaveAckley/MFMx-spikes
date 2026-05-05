#include "Blackhole.h"
#include "HostCommsMap.h"
#include "Fail.h"
#include <fcntl.h>
#include <fstream>
#include "HostUtils.h" // for sleepUsec, strcmp
#include "FailStrings.h" // for sleepUsec
#include "t6-exports.h" // for transportblock_*
#include "BlockCode.h"

namespace MFM {

  Blackhole::Phase Blackhole::changePhase(Blackhole::Phase newPhase) {
    BHLOGprintf("Elapsed %0.3f sec\n", millisElapsed()/1000.0);
    Phase ret = mCurrentPhase;
    while (mCurrentPhase < newPhase) phaseAdvance();
    while (mCurrentPhase > newPhase) phaseRetreat();
    return ret;
  }

  s32 Blackhole::runSlowScans(u32 count) { //< return something after count HostBlock scans
    if (false) {
      s32 hackret = mCodeManager.scanHubGrid();
      if (hackret != 0)
        Eprintf("BH%d DISPLAYED SOMETHING?! (%d)\n",mChipNum,hackret);
    }
    if (mCurrentPhase < Phase::HAS_T6_CODE_DEPLOYED)
      return U32_MAX;
    s32 tot = 0;
    for (u32 i = 0u; i < count; ++i) 
      tot += mCodeManager.slowScanHostBlocks();
    return tot;
  }

  u32 Blackhole::monitorFleet() {
    if (mCurrentPhase < Phase::HAS_T6_CODE_DEPLOYED)
      return U32_MAX;
    u32 newfails = mCodeManager.newFails([] (BHTag tag, HostBlock & hb, u8 oldf, u8 newf) -> void {
      const u32 BUF_SIZE = 500u;
      char buf[BUF_SIZE];
      u32 bufNext = 0u;
      BHLog & bhl = BHLog::getTheBHLog();
      u8 oldbuf[8], newbuf[8];
      interpretFailBits(oldf,oldbuf,8);
      interpretFailBits(newf,newbuf,8);
      bufNext += snprintf(&buf[bufNext],BUF_SIZE-bufNext,"NEWFAIL %s -> %s",oldbuf,newbuf);
      for (u32 h = 0u; h < 5u; ++h) {
        u8 bit = ((u8)1u)<<h;
        if ((newf&bit) != 0u && (oldf&bit) == 0u) {
          interpretFailBits(bit,newbuf,8);
          bufNext += snprintf(&buf[bufNext],BUF_SIZE-bufNext," %s:%s",newbuf,
                              getFailCodeString((FAILCode) hb.mPerHartStatus[h]));
        }
      }
      bhl.printf(tag,"%s\n",buf);
    });
    return newfails;
  }

  void Blackhole::phaseAdvance() {
    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("%d(%u) advancing from %s(%u)\n",mChipNum,bhl.getThrId(),
            nameOfCurrentPhase(),
            (u32) mCurrentPhase);
    bool worked;
    switch (mCurrentPhase) {
    case Phase::UNINITTED: FAIL(ILLEGAL_STATE);

    case Phase::HAS_CHIP_NUM: 
      worked = openChip();
      Eprintf("after openchip %u\n",(u32) worked);
      Eprintf("hbmagic+%d, mperhst[4]+%d, mpos+%d, mtlbi+%d, size+%d\n",
              offsetof(HostBlock, mHBMagic),
              offsetof(HostBlock, mPerHartStatus[4]),
              offsetof(HostBlock, mNoC0),
              offsetof(HostBlock, mTLBI),
              sizeof(HostBlock));
      if (worked) mCurrentPhase = Phase::HAS_OPEN_DEVICE;
      else FAIL(ILLEGAL_STATE);
      Eprintf("before bhlog (%u)\n",bhl.getThrId());
      BHLOGprintf("<Blackhole:%u> opened fd %d\n",mChipNum,mChipFD);
      Eprintf("after bhlog (%u)\n",bhl.getThrId());
      break;

    case Phase::HAS_OPEN_DEVICE:
      worked = allocateTLBs();
      Eprintf("after allocatetlbs (%u)\n",bhl.getThrId());
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> allocated TLBs\n",mChipNum);
      break;

    case Phase::HAS_ALLOCATED_TLBS:
      worked = configureTLBs();
      if (worked) mCurrentPhase = Phase::HAS_CONFIGURED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> configured TLBs\n",mChipNum);
      break;

    case Phase::HAS_CONFIGURED_TLBS:
      worked = allocateHostRAM();
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_HOST_RAM;
      else FAIL(ILLEGAL_STATE);

      BHLOGprintf("<Blackhole:%u> allocated %u (per T6) host RAM\n",
                  mChipNum, HOST_RAM_PER_BH);
      break;

    case Phase::HAS_ALLOCATED_HOST_RAM:
      worked = resetTheFleet();
      if (worked) mCurrentPhase = Phase::HAS_T6_TILES_RESET;
      BHLOGprintf("<Blackhole:%u> fleet holding at first positions\n", mChipNum);
      break;

    case Phase::HAS_T6_TILES_RESET:
      worked = layoutImages();
      if (worked) {
        mCurrentPhase = Phase::HAS_T6_CODE_DEPLOYED;
        BHLOGprintf("<Blackhole:%u> code delivered to the fleet\n", mChipNum);
      } else
        HOST_FATAL(ILLEGAL_STATE,"Failed to deploy code");
      break;

    case Phase::HAS_T6_CODE_DEPLOYED:
      worked = startTransportThread();
      if (worked) mCurrentPhase = Phase::HAS_TRANSPORT_THREAD;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> started transport thread\n", mChipNum);
      break;

    case Phase::HAS_TRANSPORT_THREAD:
      BHLOGprintf("<Blackhole:%u> transmitting the go code\n", mChipNum);
      worked = releaseTheHounds();
      if (worked) {
        mCurrentPhase = Phase::HAS_T6_CODE_RUNNING;
        BHLOGprintf("<Blackhole:%u> the fleet is operational\n", mChipNum);
      } else
        HOST_FATAL(ILLEGAL_STATE,"Failed to release the fleet");
      break;

    case Phase::HAS_T6_CODE_RUNNING:
      BHLOGprintf("<Blackhole:%u> event window processing begun\n", mChipNum);
      mCurrentPhase = Phase::HAS_T6_EVENT_WINDOWS;
      break;

    case Phase::HAS_T6_EVENT_WINDOWS:
      {
        static bool once;
        if (!once) {
          BHLOGprintf("<Blackhole:%u> event window processing begun\n", mChipNum);
          once = true;
        }
      }
      break;
      
    default:
      HOST_FATAL(ILLEGAL_STATE,"Unhandled phase %d", mCurrentPhase);
    }
  }

  void Blackhole::phaseRetreat() {
    BHLog & bhl = BHLog::getTheBHLog();
    if (mCurrentPhase >= Phase::HAS_T6_CODE_DEPLOYED &&
        monitorFleet() == 0u)
      Eprintf("(%u) No fail changes detected #%d\n",bhl.getThrId(),mChipNum);
    Eprintf("(%u) retreating from %s (%u)\n",bhl.getThrId(),
            nameOfCurrentPhase(),
            (u32) mCurrentPhase);
    bool worked;
    switch (mCurrentPhase) {
    case Phase::UNINITTED: FAIL(ILLEGAL_STATE);

    case Phase::HAS_CHIP_NUM: break; // got no down genes

    case Phase::HAS_OPEN_DEVICE:
      {
        s32 fd = mChipFD;
        worked = closeChip();
        if (worked) mCurrentPhase = Phase::HAS_CHIP_NUM;
        else FAIL(ILLEGAL_STATE);
        BHLOGprintf("<Blackhole:%u> closed fd %d\n",mChipNum,fd);
      }
      break;

    case Phase::HAS_ALLOCATED_TLBS:
      worked = deallocateTLBs();
      if (worked) mCurrentPhase = Phase::HAS_OPEN_DEVICE;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> deallocated TLBs\n",mChipNum);
      break;

    case Phase::HAS_CONFIGURED_TLBS:
      worked = unconfigureTLBs();
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> unconfigured TLBs\n",mChipNum);
      break;

    case Phase::HAS_ALLOCATED_HOST_RAM:
      worked = deallocateHostRAM();
      if (worked) mCurrentPhase = Phase::HAS_CONFIGURED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> deallocated %u (per T6) host RAM\n", mChipNum, HOST_RAM_PER_BH);
      break;

    case Phase::HAS_T6_TILES_RESET:
      worked = true; // don't unreset?
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_HOST_RAM; // WAS Phase::HAS_TRANSPORT_THREAD;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> (left fleet at reset)\n", mChipNum);
      break;

    case Phase::HAS_T6_CODE_DEPLOYED:
      worked = resetTheFleet(); // reset as 'undeploy'
      if (worked) mCurrentPhase = Phase::HAS_T6_TILES_RESET;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> (fleet reset to undeploy code)\n", mChipNum);
      break;

    case Phase::HAS_TRANSPORT_THREAD:
      worked = stopTransportThread();
      if (worked) mCurrentPhase = Phase::HAS_T6_CODE_DEPLOYED; // WAS Phase::HAS_ALLOCATED_HOST_RAM;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> stopped transport thread\n", mChipNum);
      break;

    case Phase::HAS_T6_CODE_RUNNING:
      // We'd like retreating from state to mean 'debug pause', but
      // the Blackhole doc for that appears to be so-far missing and
      // the wormhole doc has some not-encouraging stuff (e.g., can't
      // pause NC) that we'd rather not assume if we don't have to..
      // XXX no longer true: So just retreat here, for now, by bailing all the way back to reset.

      worked = resetTheFleet(); 
      if (worked) mCurrentPhase = Phase::HAS_TRANSPORT_THREAD;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<Blackhole:%u> (fleet reset to stop running code)\n", mChipNum);
      break;

    case Phase::HAS_T6_EVENT_WINDOWS:
      mCurrentPhase = Phase::HAS_T6_CODE_RUNNING;
      BHLOGprintf("<Blackhole:%u> event window processing ceased\n", mChipNum);
      break;

    default:
      HOST_FATAL(ILLEGAL_STATE,"Unhandled retreat from %d",mCurrentPhase);
    }
  }
  
  bool Blackhole::openChip() {
    char buf[100];
    snprintf(buf,100,"/dev/tenstorrent/%u", mChipNum);
    int fd = open(buf, O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    mChipFD = fd;
    return true;
  }

  bool Blackhole::closeChip() {
    close(mChipFD);
    mChipFD = -1;
    mOurTLBs.stopPretendingHostRAMisDeallocated();
    return true;
  }

  bool Blackhole::allocateTLBs() {
    mOurTLBs.setDeviceInfo(mChipNum,mChipFD);
    mOurTLBs.allocateTLBs();
    return true;
  }

  bool Blackhole::deallocateTLBs() {
    mOurTLBs.deallocateTLBs();
    mOurTLBs.setDeviceInfo(mChipNum,-1);
    return true;
  }

  bool Blackhole::configureTLBs() {
    mOurTLBs.configureTLBs();
    return true;
  }

  bool Blackhole::unconfigureTLBs() {
    mOurTLBs.unconfigureTLBs();
    return true;
  }

  bool Blackhole::allocateHostRAM() {
    constexpr bool cTRANSPORTBLOCKFITS =
      HOST_RAM_PER_BH >= MIN_GTEED_HOST_RAM_PER_BH;
    COMPILATION_REQUIREMENT<cTRANSPORTBLOCKFITS>();
    mOurTLBs.allocateHostRAM(HOST_RAM_PER_BH);
    return true;
  }

  bool Blackhole::deallocateHostRAM() {
    mOurTLBs.deallocateHostRAM();
    return true;
  }

  bool Blackhole::startTransportThread() {
    mOurTLBs.resetTheFleet(); // XXX ARE WE SEEING FAILURE TO RESET?
    BHLOGprintf("<Blackhole:%u> PRESET THE FLEET\n",mChipNum);

    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("startTransportThread 10 (%u) PRE transmute\n",bhl.getThrId());

    // hold thread lock before fucking with the transport thread
    AtomicScopeLock guard(mTransportThreadMutex);

    Eprintf("startTransportThread 11 (%u) HAVE transmute\n",bhl.getThrId());
    if (mTransportThreadPtr) // already have a transport thread?
      HOST_FATAL(ILLEGAL_STATE,"Thread already running"); // uwack

    Eprintf("startTransportThread 12 (%u) HAVE transmute\n",bhl.getThrId());
    mQuitTransportThread.store(false); // set up for thread
    Eprintf("startTransportThread 13 (%u) PRE make thread HAVE transmute\n",bhl.getThrId());
    mTransportThreadPtr = std::make_unique<std::thread>([this]() {
      
      BHLog & bhl = BHLog::getTheBHLog();

      Eprintf("TransportThread (%u) STARTUP %s\n",bhl.getThrId(),myGILState());
      // WE DO NOT HOLD THE GIL AT THIS POINT
      if (false)
        BHLOGprintf("\n\n %d (%u) YAMINDA RUNNING ON INTERNAL POWER %s\n",
                    this->mChipNum,bhl.getThrId(),myGILState());
                
      for (MFM::u64 i = 0u; ++i != 0u; ) {
        if (this->mQuitTransportThread.load()) { // should we quit?
          Eprintf("TransportThread (%u) QUIT REQ\n",bhl.getThrId());
          if (false)
            BHLOGprintf("\n\n %d (%u) YAMINDA EXTERNAL QUITZOS BAHT (%" PRIu64 ")\n",this->mChipNum,bhl.getThrId(),i);
          break;
        }
        const MFM::u64 aMILLION = 1'000'000ul;
        if (true && (i % aMILLION == 0)) {
          Eprintf("TransportThread (%u) %u MILLION UPDATES\n",bhl.getThrId(),(MFM::u32) (i/aMILLION));
          if (false)
            BHLOGprintf("\n\n %d YAMINDA transport thread yo %uM %p\n",
                        this->mChipNum,
                        (MFM::u32) (i/aMILLION),
                        &this->mOurTLBs);
        }
        this->mOurTLBs.updateTransports(getPhase() >= Phase::HAS_T6_EVENT_WINDOWS);
      }
      Eprintf("TransportThread (%u) OUT %s\n",bhl.getThrId(),myGILState());
    });
    Eprintf("startTransportThread 14 (%u) NEW((%p)) %s OUT releasing transmut\n",
            bhl.getThrId(),
            mTransportThreadPtr.get(),
            myGILState());
    return true;
  }

  bool Blackhole::stopTransportThread() {
    _stopTransportThread();
    return true;
  }

  void Blackhole::_stopTransportThread() {
    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("stopTransportThread 10 (%u)\n",bhl.getThrId());

    // Get thread lock before fucking with transport thread
    //std::unique_lock<std::mutex> threadLock(mTransportThreadMutex);
    AtomicScopeLock guard(mTransportThreadMutex);

    Eprintf("stopTransportThread 11 (%u) %p\n",bhl.getThrId(),mTransportThreadPtr.get());
    if (mTransportThreadPtr && mTransportThreadPtr->joinable()) {

      Eprintf("stopTransportThread 12 (%u)\n",bhl.getThrId());
      mQuitTransportThread.store(true);

      Eprintf("stopTransportThread 13 (%u)\n",bhl.getThrId());
      //threadLock.unlock(); // release threadLock before join 'to prevent deadlock'
      Eprintf("stopTransportThread 14 (%u)\n",bhl.getThrId());

      //      py::gil_scoped_release releaseGilForJoin; // also drop GIL (or blow up if we don't have it)
      mTransportThreadPtr->join();              // bring it on in

      Eprintf("stopTransportThread 15 (%u)\n",bhl.getThrId());
      
      //threadLock.lock();                        // relock C++ lock

      Eprintf("stopTransportThread 16 (%u)\n",bhl.getThrId());
      mTransportThreadPtr.reset();              // thread gone
    }
    Eprintf("stopTransportThread 17 (%u) OUT\n",bhl.getThrId());
  }

  bool Blackhole::resetTheFleet() {
    mOurTLBs.resetTheFleet();
    return true;
  }

  bool Blackhole::layoutImages() {
    BHLOGprintf("Trying to deploy to BH#%u",mChipNum);
    return mTheImageManager.deployTo(*this);
  }

  s32 Blackhole::deployRISCVCodeFromImage(T6Image& img, u8 toTLBI) {
    /*
    Eprintf("BH#%u multicasting image %s, size %u, to the fleet\n",
            mChipNum,
            img.getName().c_str(),
            img.getBinFileSize());
    */
    return mCodeManager.deployRISCVCodeFromImage(img, toTLBI);
  }

  bool Blackhole::setMFMxDefaultCodePath(std::string path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); // Open in binary mode and at end

    if (!file.is_open()) {
      BHLOGprintf("Cannot read from code path '%s'",path.c_str());      
      return false;
    }
    file.close();
    mMFMxCodePath = path;
    return true;
  }

  bool Blackhole::releaseTheHounds() {
    mCodeManager.releaseTheHounds();
    return true;
  }

  bool Blackhole::configureT6ImageForHostComms(T6Image & t6i) {
    // NO: ALREADY DONE: (1) CONFIGURE HOSTCOMMS

    // (2) CONFIGURE T6IMAGE
    return mOurTLBs.configureT6ImageForHostComms(t6i);
  }
  
  Blackhole::~Blackhole() {
    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("BH DTORRRRR (%u) IN\n",bhl.getThrId());
    _stopTransportThread();
    Eprintf("BH DTORRRRR (%u) OUT\n",bhl.getThrId());
  }
}
