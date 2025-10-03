#ifndef BHUMD_H          /* -*- mode: C++ -*- */
#define BHUMD_H

#include "Util.h"
#include "TTKMDStuff.h"
#include "Constants.h"
#include "BHUMD_Constants.h"
#include "HostBlock.h"
#include "RandMT.h"

namespace MFM {

  /** BlackHold User Mode Driver, "thin corsix style"
   */
  class BHUMD {
  public:
    BHUMD() ;
    ~BHUMD() ;

    const char * getDeviceName() const ;

    void setDeviceNumber(u32 deviceNumber) ;
    bool unsetDeviceNumberIfSet() ;

    void setTilePosition(u32 xpos, u32 ypos) ;
    void setRandomTilePosition(RandMT & rmt) ;

    s32 open() ;
    s32 close() ;

    s32 deployRISCVCodeFromFile(const char * path) ;
    s32 deployThisRISCVCode(const char * rvcode, u32 rvsize) ;

    s32 releaseTheHounds() ;
    s32 waitTilDone() ;

  private:
    s32 open_bh_pcie_device() ;
    s32 close_bh_pcie_device() ;

    s32 configure_tlb() ;
    s32 set_tlb_xy() ; // HERE'S WHERE (CURRENTLY SINGLE) TENSIX CORE IS SELECTED
    char* set_tlb_addr(u64 addr) ;

    void tlb_write_u32(u64 addr, u32 value) ;
    u32 tlb_read_u32(u64 addr) ;

    u32 mDevNum;
    bool mHasDevNum;
    bool mDevOpen;

    u32 mTileXPosition;
    u32 mTileYPosition;

    bool mCodeDeployed;
    u32 mDeployedCodeSize;

    bh_pcie_device_t mBHDev;
  };

  /*
  struct HostBlock {
    uint32_t mPerRiscArg[5];
    uint32_t mCommonArgs[3];
  };
  */
} // namespace MFM

#endif /* BHUMD_H */
