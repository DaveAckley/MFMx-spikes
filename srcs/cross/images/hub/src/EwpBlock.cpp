#include "EwpBlock.h"
#include "HartTasks.h" // for HTFuncPtr
#include "T6CellO.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll
#include "T6EPs.h"

namespace MFM {
  T6EPL1Data<EwpBlockStg,8> theEwpL1Data;
  // EwpBlockStg theEwpBlockCars[8];
  // AtomicLock theEwpBlockLock[8];
  // EwpEP::CarIdxs theEwpBlockIdxs[8];

  FAST_LOCAL_ARRAY(EwpEP<8>,8,myEwpEPArray,n);
  static bool manageHubNC(bool doInit) {

    bool ret = false;
    if (unlikely(doInit)) {
      HBNOTE("EWHUBARO");
      //// ONE-TIME INITS
      T6CellO cello;
      if (!cello.init()) FAIL(NO_MATCH);
    
      theEwpL1Data.reset();     // zero all
      
      // Enumerate the EWPs in my (Platonic) cell
      const CellBlock & cb = cello.getOurCB();
      u32 ncount = 0u;
      for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
        for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
          U8C atcp(x,y);
          if (cello.getImageCodeAtCellP(atcp) == ImageCode::IC_EWP) {
            // we are not ready for more than 8 ewps in our cell
            HBASSERT_LS(ncount, 8u); 
            u32 n = ncount++;
            EwpEP<8> & ewpnc = myEwpEPArray[n];
            //HBPVAL(n);
            HBPVAL(cello.getNoC0ofCellP(atcp));
            ewpnc.initEwpEP({ BC_EWHUB, (u8) n }, true, theEwpL1Data);
            ewpnc.configureDest(fAll.mNoC0, cello.getNoC0ofCellP(atcp), { BC_EWPCARS, 0 });
            
            EwpBlockStg & ebs = ewpnc.getCarStg();
            HBPTAG(PRCFG,&ebs);
            for (u32 c = 0; c < ebs.getCarCount(); ++c) {
              EwpBlock & eb = ebs.getTC(c);
              eb.init();
            }
            ret = true;
          }
        }
      }
      HBNOTE("EWHF");
      for (u32 n = 0; n < 8; ++n) {
        if (theEwpL1Data.getPublicEPState(n) != EPState::CONFIGURED) continue;
        //HBPVAL(n);
        myEwpEPArray[n].activate();
      }
    } else {
            
      //// LIVING
      for (u32 n = 0; n < 8; ++n) {
        EwpEP<8> & ewpnc = myEwpEPArray[n];
        if (!ewpnc.isActive()) continue;
        if (ewpnc.updateOps()) ret = true;
      }
    }
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr hubEPPtr = &manageHubNC;
}
