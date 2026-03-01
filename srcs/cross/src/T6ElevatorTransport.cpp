#include "T6ElevatorTransport.h"
#include "Fail.h"
#include "FastLocal.h" // for sleepCycles
#include "Printf.h"
#include "SimConstants.h" // for HOST_RAM_PER_BH
#include <stdarg.h>

namespace MFM {
  //// ALL DEFS HERE ARE REQUEST INITIATOR #0 ON NOC 0!
  static volatile u32 * const NOC_TARG_ADDR_LO =  NIU_ADDRESS(0x00,0,0);
  static volatile u32 * const NOC_TARG_ADDR_MID = NIU_ADDRESS(0x04,0,0);
  static volatile u32 * const NOC_TARG_ADDR_HI =  NIU_ADDRESS(0x08,0,0);

  static volatile u32 * const NOC_RET_ADDR_LO =   NIU_ADDRESS(0x0C,0,0);
  static volatile u32 * const NOC_RET_ADDR_MID =  NIU_ADDRESS(0x10,0,0);
  static volatile u32 * const NOC_RET_ADDR_HI =   NIU_ADDRESS(0x14,0,0);

  static volatile u32 * const NOC_CTRL =          NIU_ADDRESS(0x1C,0,0);
  static volatile u32 * const NOC_AT_LEN_BE =     NIU_ADDRESS(0x20,0,0);

  static volatile u32 * const NOC_AT_DATA =       NIU_ADDRESS(0x28,0,0);

  static volatile u32 * const NOC_CMD_CTRL =      NIU_ADDRESS(0x40,0,0);

  LogCarMetadata theLogCarMetadata;
  EWCarMetadata theEWCarMetadata;

  void T6ElevatorTransport::init(HostBlock & hb, TransportBlock & tb) {
    //DIEWAY();
// SHOWADDR(hb);
// SHOWADDR(tb);
    mHostBlockPtr = &hb;
    mTransportBlockPtr = &tb;
// SHOWADDR(mHostBlockPtr);    
// SHOWADDR(mTransportBlockPtr);    
    u64 noc = hb.getHostNocAddr();
DIEWAY();
   mP2PLogTransport.initCars((LogCarStorage::LogCar*) tb.mLogCarStorageT6Ptr,
                              &theLogCarMetadata.mLogData[0],
                              LogCarStorage::CAR_COUNT,
                              noc + (tb.mLogCarStorageT6Ptr - tb.mLogCarStorageT6Ptr),
                              false);
 SHOWADDR(mP2PLogTransport);    
DIEWAY();

    mP2PEWTransport.initCars((EWCarStorage::EWCar*) tb.mEWCarStorageT6Ptr,
                             &theEWCarMetadata.mEWData[0],
                             EWCarStorage::CAR_COUNT,
                             noc + (tb.mEWCarStorageT6Ptr - tb.mLogCarStorageT6Ptr),
                             false);
 SHOWADDR(mP2PEWTransport);
 SHOWADDR(theEWCarMetadata);
DIEWAY();
  }

  bool T6ElevatorTransport::allClear() {
DIEWAY();
    u32 v = *NOC_CMD_CTRL;
    return 0u==(v&1);
  }

  s32 T6ElevatorTransport::initiateWrite(u32 * data, u32 wordCount, U16C xy, u64 destaddr) {
DIEWAY();
    if (!allClear()) return -1; // Not ready
DIEWAY();
    u32 byteCount = wordCount * 4;
    if (byteCount == 0 || byteCount > (1<<14))
      return -2;                // EINVAL: bad size
DIEWAY();

    // TARG (lo+mid) is the source in our L1. TARG hi is our NoC0 coord as NodeId
    u32 targlo = (u32) data;
    u32 targmid = 0u;
    u32 targhi = ((mHostBlockPtr->mPos.x&0x3f)<<6)|(mHostBlockPtr->mPos.y&0x3f);
DIEWAY();

    // In general:
    //   RET (lo+mid) is the dest addr, RET hi is the NoC0 coord of the dest tile
    // For initiateWriteToHost specifically:
    //   RET (lo+mid) is the dest in Host RAM, RET hi is the NoC0 coord of the PCIe tile ?
    u32 retlo = (u32) (destaddr&0xffffffff);
    u32 retmid = (u32) ((destaddr>>32)&0xffffffff);
    u32 rethi = ((xy.y&0x3f)<<6)|(xy.x&0x3f);
    u32 noc_ctrl = (2<<0);      // write request
DIEWAY();

    // Set up the registers
    *NOC_TARG_ADDR_LO = targlo;
    *NOC_TARG_ADDR_MID = targmid;
    *NOC_TARG_ADDR_HI = targhi;

    *NOC_RET_ADDR_LO = retlo;
    *NOC_RET_ADDR_MID = retmid;
    *NOC_RET_ADDR_HI = rethi;
DIEWAY();

    *NOC_CTRL = 2;              // write request
    *NOC_AT_LEN_BE = byteCount;

    // We are ready to initiate the NoC transaction?
DIEWAY();
    *NOC_CMD_CTRL = 1;            // THE BIRD IS AWAY
    u32 readback = *NOC_CMD_CTRL;  // read it back for memory ordering?
DIEWAY();
    return 0;
  }

  void T6ElevatorTransport::notice(const char * fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    DP.vprintf(fmt,ap);
    //LOG.vprintf(fmt,ap);
    va_end(ap);
  }

  bool T6ElevatorTransport::updateTransportBlock() {
DIEWAY();
    if (!allClear()) return false;
DIEWAY();
    bool ret =
      mP2PLogTransport.update(*this);
DIEWAY();
      ret |= mP2PEWTransport.update(*this);
DIEWAY();
    return ret;
  }

  bool T6ElevatorTransport::ship(LogCarStorage::LogCar & lc, u32 carnum) {
DIEWAY();
    if (!allClear()) {
DIEWAY();
      DP.printf("NORG\n");
      return false; // Opps not ready
    }
DIEWAY();
    u64 v = mHostBlockPtr->getHostNocAddr();
DIEWAY();
    v += ((u64) mHostBlockPtr->mTLBI)*(HOST_RAM_PER_BH)+carnum*sizeof(lc);
DIEWAY();
SHOWADDR(lc);    
    CarSig csig = lc.getHeader();
    CarType ct = csig.getCarType();
DIEWAY();
    lc.setCarState(CarState::INBOUND_DEPARTED,ct);
DIEWAY();
    u32 wordcount = (ct == CarType::EMPTY) ? 2u : sizeof(lc)/4u;
    s32 status = initiateWriteToHost((u32*) &lc, wordcount, v);
DIEWAY();
    if (false)
      LOG.printf("CRAG %d 0x%016llx:0x%08x.\n",
                 mHostBlockPtr->mTLBI, v,
                 *(u32*) &lc);
DIEWAY();
    return status==0;
  }
  bool T6ElevatorTransport::ship(EWCarStorage::EWCar & ewc, u32 carnum) {
DIEWAY();
    if (!allClear()) {
DIEWAY();
      DP.printf("EWNORG\n");
      return false; // Opps not ready
    }
DIEWAY();

    u64 v = mHostBlockPtr->getHostNocAddr();
    v += ((u64) mHostBlockPtr->mTLBI)*HOST_RAM_PER_BH // host side base address
      + (mTransportBlockPtr->mEWCarStorageT6Ptr - mTransportBlockPtr->mLogCarStorageT6Ptr) // offset to EW
      + carnum*sizeof(ewc);
DIEWAY();
    CarSig csig = ewc.getHeader();
    CarType ct = csig.getCarType();
SHOWADDR(ewc);    
DIEWAY();
    ewc.setCarState(CarState::INBOUND_DEPARTED,ct);
DIEWAY();
    u32 wordcount = (ct == CarType::EMPTY) ? 2u : sizeof(ewc)/4u;
DIEWAY();
    s32 status = initiateWriteToHost((u32*) &ewc, wordcount, v);
DIEWAY();
    if (false) LOG.printf("EWSHIP CAR%d(%d) IN @0x%llx\n",carnum,status,v);
    return status==0;
  }

}
