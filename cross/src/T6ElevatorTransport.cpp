#include "T6ElevatorTransport.h"
#include "Fail.h"
#include "FastLocal.h" // for sleepCycles
#include "Printf.h"

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

  void T6ElevatorTransport::init(HostBlock & hb, TransportBlock & tb) {
    mHostBlockPtr = &hb;
    mTransportBlockPtr = &tb;
    u64 noc = hb.getHostNocAddr();
    mP2PLogCarManager.initCars((LogCarStorage::LogCar*) tb.mLogCarStorageT6Ptr,
                               LogCarStorage::CAR_COUNT,
                               noc + (tb.mLogCarStorageT6Ptr - tb.mLogCarStorageT6Ptr),
                               false);
    mP2PEWCarManager.initCars((EWCarStorage::EWCar*) tb.mEWCarStorageT6Ptr,
                              EWCarStorage::CAR_COUNT,
                              noc + (tb.mEWCarStorageT6Ptr - tb.mLogCarStorageT6Ptr),
                              false);
  }

  bool T6ElevatorTransport::allClear() {
    u32 v = *NOC_CMD_CTRL;
    return 0u==(v&1);
  }

  s32 T6ElevatorTransport::initiateWrite(u32 * data, u32 count, U16C xy, u64 destaddr) {
    if (!allClear()) return -1; // Not ready
    u32 byteCount = count * 4;
    if (byteCount == 0 || byteCount > (1<<14))
      return -2;                // EINVAL: bad size

    // TARG (lo+mid) is the source in our L1. TARG hi is our raw NoC coord
    u32 targlo = (u32) data;
    u32 targmid = 0u;
    u32 targhi = ((mHostBlockPtr->mYPos&0x3f)<<6)|(mHostBlockPtr->mXPos&0x3f);

    // In general:
    //   RET (lo+mid) is the dest addr, RET hi is the raw NoC coord of the dest tile
    // For initiateWriteToHost specifically:
    //   RET (lo+mid) is the dest in Host RAM, RET hi is the raw NoC coord of the PCIe tile ?
    u32 retlo = (u32) (destaddr&0xffffffff);
    u32 retmid = (u32) ((destaddr>>32)&0xffffffff);
    u32 rethi = ((xy.y&0x3f)<<6)|(xy.x&0x3f);
    u32 noc_ctrl = (2<<0);      // write request

    // Set up the registers
    *NOC_TARG_ADDR_LO = targlo;
    *NOC_TARG_ADDR_MID = targmid;
    *NOC_TARG_ADDR_HI = targhi;

    *NOC_RET_ADDR_LO = retlo;
    *NOC_RET_ADDR_MID = retmid;
    *NOC_RET_ADDR_HI = rethi;

    *NOC_CTRL = 2;              // write request
    *NOC_AT_LEN_BE = byteCount;

    // We are ready to initiate the NoC transaction?
    *NOC_CMD_CTRL = 1;            // THE BIRD IS AWAY
    u32 readback = *NOC_CMD_CTRL;  // read it back for memory ordering?
    return 0;
  }

  bool T6ElevatorTransport::updateTransportBlock() {
    bool didWork = !allClear();
    if (!didWork) didWork = mP2PLogCarManager.update(*this);
    if (!didWork) didWork = mP2PEWCarManager.update(*this);
    if (!didWork) sleepCycles(1'000'000u);
    return didWork;
  }

  bool T6ElevatorTransport::ship(LogCarStorage::LogCar & lc, u32 carnum) {
    if (!allClear()) {
      DP.printf("NORG\n");
      return false; // Opps not ready
    }
    u64 v = mHostBlockPtr->getHostNocAddr();
    v += ((u64) mHostBlockPtr->mTLBI)*(1<<13)+carnum*sizeof(lc);
    lc.setCarState(CarState::INBOUND_DEPARTED);
    s32 status = initiateWriteToHost((u32*) &lc, sizeof(lc),  v);
    DP.printf("CRAG %d 0x%016llx:0x%08x.\n",
              mHostBlockPtr->mTLBI, v,
              *(u32*) &lc);
    return status==0;
  }
  bool T6ElevatorTransport::ship(EWCarStorage::EWCar & lc, u32 carnum) {
    FAIL(UNSUPPORTED_OPERATION);
  }

}
