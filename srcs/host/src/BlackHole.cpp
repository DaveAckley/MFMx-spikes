#include "BlackHole.h"
#include "Fail.h"
#include <fcntl.h>
#include <fstream>
#include "HostUtils.h" // for sleepUsec
#include "FailStrings.h" // for sleepUsec
#include "t6-exports.h" // for transportblock_*

namespace MFM {

  BlackHole::Phase BlackHole::changePhase(BlackHole::Phase newPhase) {
    BHLOGprintf("Elapsed %0.3f sec\n", millisElapsed()/1000.0);
    Phase ret = mCurrentPhase;
    while (mCurrentPhase < newPhase) phaseAdvance();
    while (mCurrentPhase > newPhase) phaseRetreat();
    return ret;
  }

  s32 BlackHole::runSlowScans(u32 count) { //< return something after count HostBlock scans
    if (mCurrentPhase < Phase::HAS_T6_CODE_DEPLOYED)
      return U32_MAX;
    s32 tot = 0;
    for (u32 i = 0u; i < count; ++i) 
      tot += mCodeManager.slowScanHostBlocks();
    return tot;
  }

  u32 BlackHole::monitorFleet() {
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

  void BlackHole::phaseAdvance() {
    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("(%u) advancing from %u\n",bhl.getThrId(),(u32) mCurrentPhase);
    bool worked;
    switch (mCurrentPhase) {
    case Phase::UNINITTED: FAIL(ILLEGAL_STATE);

    case Phase::HAS_CARD_NUM: 
      worked = openCard();
      Eprintf("after opencard %u\n",(u32) worked);
      if (worked) mCurrentPhase = Phase::HAS_OPEN_DEVICE;
      else FAIL(ILLEGAL_STATE);
      Eprintf("before bhlog (%u)\n",bhl.getThrId());
      BHLOGprintf("<BlackHole:%u> opened fd %d\n",mCardNum,mCardFD);
      Eprintf("after bhlog (%u)\n",bhl.getThrId());
      break;

    case Phase::HAS_OPEN_DEVICE:
      worked = allocateTLBs();
      Eprintf("after allocatetlbs (%u)\n",bhl.getThrId());
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> allocated TLBs\n",mCardNum);
      break;

    case Phase::HAS_ALLOCATED_TLBS:
      worked = configureTLBs();
      if (worked) mCurrentPhase = Phase::HAS_CONFIGURED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> configured TLBs\n",mCardNum);
      break;

    case Phase::HAS_CONFIGURED_TLBS:
      worked = allocateHostRAM();
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_HOST_RAM;
      else FAIL(ILLEGAL_STATE);

      BHLOGprintf("<BlackHole:%u> allocated %u (per T6) host RAM\n", mCardNum, HOST_RAM_PER_BH);
      break;

    case Phase::HAS_ALLOCATED_HOST_RAM:
      worked = startTransportThread();
      if (worked) mCurrentPhase = Phase::HAS_TRANSPORT_THREAD;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> started transport thread\n", mCardNum);
      break;
      
    case Phase::HAS_TRANSPORT_THREAD:
      worked = resetTheFleet();
      if (worked) mCurrentPhase = Phase::HAS_T6_TILES_RESET;
      BHLOGprintf("<BlackHole:%u> fleet holding at first positions\n", mCardNum);
      break;

    case Phase::HAS_T6_TILES_RESET:
      worked = deployTheCode();
      if (worked) {
        mCurrentPhase = Phase::HAS_T6_CODE_DEPLOYED;
        BHLOGprintf("<BlackHole:%u> code delivered to the fleet\n", mCardNum);
      } else
        HOST_FATAL(ILLEGAL_STATE,"Failed to deploy code");
      break;

    case Phase::HAS_T6_CODE_DEPLOYED:
      BHLOGprintf("<BlackHole:%u> transmitting the go code\n", mCardNum);
      worked = releaseTheHounds();
      if (worked) {
        mCurrentPhase = Phase::HAS_T6_CODE_RUNNING;
        BHLOGprintf("<BlackHole:%u> the fleet is operational\n", mCardNum);
      } else
        HOST_FATAL(ILLEGAL_STATE,"Failed to release the fleet");
      break;

    default:
      HOST_FATAL(ILLEGAL_STATE,"Unhandled phase %d", mCurrentPhase);
    }
  }

  void BlackHole::phaseRetreat() {
    BHLog & bhl = BHLog::getTheBHLog();
    if (mCurrentPhase >= Phase::HAS_T6_CODE_DEPLOYED &&
        monitorFleet() == 0u)
      Eprintf("(%u) No fail changes detected #%d\n",bhl.getThrId(),mCardNum);
    Eprintf("(%u) retreating from %u\n",bhl.getThrId(),(u32) mCurrentPhase);
    bool worked;
    switch (mCurrentPhase) {
    case Phase::UNINITTED: FAIL(ILLEGAL_STATE);

    case Phase::HAS_CARD_NUM: break; // got no down genes

    case Phase::HAS_OPEN_DEVICE:
      {
        s32 fd = mCardFD;
        worked = closeCard();
        if (worked) mCurrentPhase = Phase::HAS_CARD_NUM;
        else FAIL(ILLEGAL_STATE);
        BHLOGprintf("<BlackHole:%u> closed fd %d\n",mCardNum,fd);
      }
      break;

    case Phase::HAS_ALLOCATED_TLBS:
      worked = deallocateTLBs();
      if (worked) mCurrentPhase = Phase::HAS_OPEN_DEVICE;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> deallocated TLBs\n",mCardNum);
      break;

    case Phase::HAS_CONFIGURED_TLBS:
      worked = unconfigureTLBs();
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> unconfigured TLBs\n",mCardNum);
      break;

    case Phase::HAS_ALLOCATED_HOST_RAM:
      worked = deallocateHostRAM();
      if (worked) mCurrentPhase = Phase::HAS_CONFIGURED_TLBS;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> deallocated %u (per T6) host RAM\n", mCardNum, HOST_RAM_PER_BH);
      break;

    case Phase::HAS_TRANSPORT_THREAD:
      worked = stopTransportThread();
      if (worked) mCurrentPhase = Phase::HAS_ALLOCATED_HOST_RAM;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> stopped transport thread\n", mCardNum);
      break;

    case Phase::HAS_T6_TILES_RESET:
      worked = true; // don't unreset?
      if (worked) mCurrentPhase = Phase::HAS_TRANSPORT_THREAD;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> (left fleet at reset)\n", mCardNum);
      break;

    case Phase::HAS_T6_CODE_DEPLOYED:
      worked = resetTheFleet(); // reset as 'undeploy'
      if (worked) mCurrentPhase = Phase::HAS_T6_TILES_RESET;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> (fleet reset to undeploy code)\n", mCardNum);
      break;

    case Phase::HAS_T6_CODE_RUNNING:
      // We'd like retreating from state to mean 'debug pause', but
      // the BlackHole doc for that appears to be so-far missing and
      // the wormhole doc has some not-encouraging stuff (e.g., can't
      // pause NC) that we'd rather not assume if we don't have to..
      // So just retreat here, for now, by bailing all the way back to reset.

      worked = resetTheFleet(); 
      if (worked) mCurrentPhase = Phase::HAS_T6_TILES_RESET;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> (fleet reset to stop running code)\n", mCardNum);
      break;
      
    default:
      HOST_FATAL(ILLEGAL_STATE,"Unhandled retreat from %d",mCurrentPhase);
    }
  }
  
  bool BlackHole::openCard() {
    char buf[100];
    snprintf(buf,100,"/dev/tenstorrent/%u", mCardNum);
    int fd = open(buf, O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    mCardFD = fd;
    return true;
  }

  bool BlackHole::closeCard() {
    close(mCardFD);
    mCardFD = -1;
    mOurTLBs.stopPretendingHostRAMisDeallocated();
    return true;
  }

  bool BlackHole::allocateTLBs() {
    mOurTLBs.setDeviceInfo(mCardNum,mCardFD);
    mOurTLBs.allocateTLBs();
    return true;
  }

  bool BlackHole::deallocateTLBs() {
    mOurTLBs.deallocateTLBs();
    mOurTLBs.setDeviceInfo(mCardNum,-1);
    return true;
  }

  bool BlackHole::configureTLBs() {
    mOurTLBs.configureTLBs();
    return true;
  }

  bool BlackHole::unconfigureTLBs() {
    mOurTLBs.unconfigureTLBs();
    return true;
  }

  bool BlackHole::allocateHostRAM() {
    constexpr bool cTRANSPORTBLOCKFITS = HOST_RAM_PER_BH >=
      (T6::transportblock_log_size + T6::transportblock_ew_size);
    COMPILATION_REQUIREMENT<cTRANSPORTBLOCKFITS>();
    mOurTLBs.allocateHostRAM(HOST_RAM_PER_BH);
    return true;
  }

  bool BlackHole::deallocateHostRAM() {
    mOurTLBs.deallocateHostRAM();
    return true;
  }

  bool BlackHole::startTransportThread() {
    mOurTLBs.resetTheFleet(); // XXX ARE WE SEEING FAILURE TO RESET?
    BHLOGprintf("<BlackHole:%u> PRESET THE FLEET\n",mCardNum);

    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("startTransportThread 10 (%u) PRE transmute\n",bhl.getThrId());

    // hold thread lock before fucking with the transport thread
    OurScopeLock guard(mTransportThreadMutex);

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
                    this->mCardNum,bhl.getThrId(),myGILState());
                
      for (MFM::u64 i = 0u; ++i != 0u; ) {
        if (this->mQuitTransportThread.load()) { // should we quit?
          Eprintf("TransportThread (%u) QUIT REQ\n",bhl.getThrId());
          if (false)
            BHLOGprintf("\n\n %d (%u) YAMINDA EXTERNAL QUITZOS BAHT (%" PRIu64 ")\n",this->mCardNum,bhl.getThrId(),i);
          break;
        }
        const MFM::u64 aBILLION = 1'000ul; //1'000'000'000ul;
        if (true && (i % aBILLION == 0)) {
          Eprintf("TransportThread (%u) BILLION\n",bhl.getThrId());
          if (false)
            BHLOGprintf("\n\n %d YAMINDA transport thread yo %uK %p\n",
                        this->mCardNum,
                        (MFM::u32) (i/aBILLION),
                        &this->mOurTLBs);
        }
        this->mOurTLBs.updateTransports();
      }
      Eprintf("TransportThread (%u) OUT %s\n",bhl.getThrId(),myGILState());
    });
    Eprintf("startTransportThread 14 (%u) NEW((%p)) %s OUT releasing transmut\n",
            bhl.getThrId(),
            mTransportThreadPtr.get(),
            myGILState());
    return true;
  }

  bool BlackHole::stopTransportThread() {
    _stopTransportThread();
    return true;
  }

  void BlackHole::_stopTransportThread() {
    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("stopTransportThread 10 (%u)\n",bhl.getThrId());

    // Get thread lock before fucking with transport thread
    //std::unique_lock<std::mutex> threadLock(mTransportThreadMutex);
    OurScopeLock guard(mTransportThreadMutex);

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

  bool BlackHole::resetTheFleet() {
    mOurTLBs.resetTheFleet();
    return true;
  }

  bool BlackHole::deployTheCode() {
    std::string path = mMFMxCodePath;
    if (path.empty()) {
      BHLOGprintf("Code path not set, cannot deploy\n");
      return false;
    }
    BHLOGprintf("Trying to deploy %s\n",path.c_str());
    s32 ret = mCodeManager.deployRISCVCodeFromFile(path.c_str());
    return ret == 0;
  }

  bool BlackHole::setMFMxCodePath(std::string path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); // Open in binary mode and at end

    if (!file.is_open()) {
      BHLOGprintf("Cannot read from code path '%s'",path.c_str());      
      return false;
    }
    file.close();
    mMFMxCodePath = path;
    return true;
  }

  bool BlackHole::releaseTheHounds() {
    mCodeManager.releaseTheHounds();
    return true;
  }

  BlackHole::~BlackHole() {
    BHLog & bhl = BHLog::getTheBHLog();
    Eprintf("BH DTORRRRR (%u) IN\n",bhl.getThrId());
    _stopTransportThread();
    Eprintf("BH DTORRRRR (%u) OUT\n",bhl.getThrId());
  }
}
