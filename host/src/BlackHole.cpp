#include "BlackHole.h"
#include "Fail.h"
#include <fcntl.h>
#include <fstream>
#include "HostUtils.h" // for sleepUsec

namespace MFM {

  BlackHole::Phase BlackHole::changePhase(BlackHole::Phase newPhase) {
    Phase ret = mCurrentPhase;
    while (mCurrentPhase < newPhase) phaseAdvance();
    while (mCurrentPhase > newPhase) phaseRetreat();
    return ret;
  }

  void BlackHole::phaseAdvance() {
    bool worked;
    switch (mCurrentPhase) {
    case Phase::UNINITTED: FAIL(ILLEGAL_STATE);

    case Phase::HAS_CARD_NUM: 
      worked = openCard();
      if (worked) mCurrentPhase = Phase::HAS_OPEN_DEVICE;
      else FAIL(ILLEGAL_STATE);
      BHLOGprintf("<BlackHole:%u> opened fd %d\n",mCardNum,mCardFD);
      break;

    case Phase::HAS_OPEN_DEVICE:
      worked = allocateTLBs();
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
    mOurTLBs.allocateHostRAM(HOST_RAM_PER_BH);
    return true;
  }

  bool BlackHole::deallocateHostRAM() {
    mOurTLBs.deallocateHostRAM();
    return true;
  }

  bool BlackHole::startTransportThread() {
    mQuitTransportThread = false;
    mTransportThread = std::make_unique<std::thread>([this]() {
      sleepUsec(10'000);
      BHLOGprintf("\n\n %d YAMINDA RUNNING ON INTERNAL POWER\n",this->mCardNum);
                
      for (MFM::u64 i = 0u; ++i != 0u; ) {
        if (this->mQuitTransportThread) {
          BHLOGprintf("\n\n %d YAMINDA EXTERNAL QUITZOS BAHT (%" PRIu64 ")\n",this->mCardNum,i);
          break;
        }
        const MFM::u64 aBILLION = 1'000'000'000ul;
        if (false && (i % aBILLION == 0))
          BHLOGprintf("\n\n %d YAMINDA transport thread yo %uG %p\n",(MFM::u32) (i/aBILLION),
                      this->mCardNum,
                      &this->mOurTLBs);
        this->mOurTLBs.updateLogTransports();
      }
    });
    return true;
  }

  bool BlackHole::stopTransportThread() {
    BHLOGprintf("Stopping transport thread\n");
    mQuitTransportThread = true;
    mTransportThread->join();
    return true;
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
  
}
