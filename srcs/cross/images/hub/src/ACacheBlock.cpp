#include "EP_ACacheBlock.h"
#include "ACacheBlock.h"
#include "HartTasks.h" // for HTFuncPtr
#include "T6CellO.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  //  T6EPL1Data<ACacheBlockStg,1> theACacheBlockL1Data;
  //  ACacheBlockL1Control theACacheBlockL1Control;

  //  FAST_LOCAL_ARRAY(ACacheBlockEP,1,myACacheBlockEPNC,n);

  static bool manageACacheBlockNC(bool doInit) {
    bool ret = false;

    if (unlikely(doInit)) {
      HBNOTE("ACBARO");
      //// ONE-TIME INITS
      theACacheBlockL1Data.reset(); // zero all

      ACacheBlockEP & acbep = myACacheBlockEPNC;
      bool weAreIn = false;
      acbep.initACacheBlockEP({ BC_ACACHEBLOCK, (u8) 0 }, weAreIn, theACacheBlockL1Data);
      ACacheBlockStg & acbstg = theACacheBlockL1Data.mTheTCStorages[0];
      HBPTAG(acbepind,&acbstg);
      for (u32 c = 0; c < acbstg.getCarCount(); ++c) {
        ACacheBlock & acb = acbstg.getTC(c);
        acb.init();
      }
      HostBlock & hb = theHostBlock;
      U8C ournoc0 = hb.mNoC0;
      acbep.configureDest(ournoc0, PCIeTILE_NOC0, { BC_ACACHEBLOCK, 0u });

      if (theACacheBlockL1Data.getPublicEPState(0) != EPState::CONFIGURED) FAIL(INCOMPLETE_CODE);
      myACacheBlockEPNC.activate(); // release the hounds

      HBMARK;
    } else {
      //HBXTAG(ACBLIV,0);
      //// LIVING
      ACacheBlockEP & myACBEPNC = myACacheBlockEPNC;
      if (!myACBEPNC.isInitted()) FAIL(INCOMPLETE_CODE);
      if (myACBEPNC.updateOps()) {
        ret = true;
      }
    }

    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr ACacheBlockEPPtr = &manageACacheBlockNC;
}
