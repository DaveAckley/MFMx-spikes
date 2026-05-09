#include "EwpBlock.h"
#include "FastNC.h" // for EPFuncPtr
#include "T6CellO.h"

namespace MFM {
  T6EPL1Data<EwpBlockStg,1> theEwpL1Data;

  using EwpEP1 = EwpEP<1>;

  //  EwpBlockStg theEwpBlockCars[1];
  //  AtomicLock theEwpBlockLock[1];
  //  EwpEP::CarIdxs theEwpBlockIdxs[1];

  FAST_LOCAL(EwpEP1,myEwpEPNC,n);

  struct CellOInfo {
    T6CellO mCellO;
    U8C mOurCP;
    U8C mHubCP;
    U8C mHubNoC0;
  };
  FAST_LOCAL(CellOInfo,fCOINC,n);

  static void findHub() {
    // find ourselves and set up
    T6CellO & cello = fCOINC.mCellO;
    if (!cello.init()) FAIL(NO_MATCH); // we need a hub and stuff, right? right?

    //HBMARK;

    // find our hub coords
    fCOINC.mHubCP = cello.getCellPofImage(ImageCode::IC_HUB); // search cell for hub
    LOGPVAL(fCOINC.mHubCP);
    MFM_API_ASSERT(cello.isValidCP(fCOINC.mHubCP),NOT_FOUND);
    fCOINC.mHubNoC0 = cello.getNoC0ofCellP(fCOINC.mHubCP);

    //    HBPVAL(fCOINC.mHubNoC0);
    //    HBPVAL(fCOINC.mCellO.mImageTypeIndex);
    
    // OK. mCellO.mImageTypeIndex is our blockidx for HUB's BC_EWHUB
  }

  static bool manageEwpNC(bool doInit) {
    bool ret = false;
    if (unlikely(doInit)) {
      LOGMARK;

      theEwpL1Data.reset();     // zero all
      auto & theEwpBlockCars = theEwpL1Data.mTheTCStorages;

      ///// BIRTH
      findHub(); // find my feet find my face

      // Set up cars
      for (u32 i = 0; i < sizeof(theEwpBlockCars)/sizeof(theEwpBlockCars[0]); ++i) {
        EwpBlockStg & ebs = theEwpBlockCars[i];
        for (u32 c = 0; c < ebs.getCarCount(); ++c) {
          EwpBlock & eb = ebs.getTC(c);
          eb.init();
        }
      }
      // Cars are now initted

      // Set up our endpoint: Source { EWPCARS, 0 }
      myEwpEPNC.initEwpEP({ BC_EWPCARS, 0 }, false, theEwpL1Data);
      HBPTAG(ewCFD, fCOINC.mCellO.mImageTypeIndex);

      // Set up our endpoint: Destination { EWHUB, ourtypeidx }
      myEwpEPNC.configureDest(fAll.mNoC0, fCOINC.mHubNoC0, { BC_EWHUB, fCOINC.mCellO.mImageTypeIndex });
      myEwpEPNC.activate();
      ret = true;
      LOGMARK;

    } else {

      //// LIFE
      //      SNAP(5,HBMARK);
      if (myEwpEPNC.updateOps()) {
        ret = true;
        //        HBMARK;
      }
    }
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr ewpEPPtr = &manageEwpNC;

}
