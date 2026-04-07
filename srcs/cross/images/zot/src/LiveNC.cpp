#include "DefaultLives.h" // for liveNC
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastNC.h" // for fNC
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "ZotBlock.h"
#include "Printf.h"

namespace MFM {

  ZotBlockStg theZotBlockCarsIO[2];
  AtomicLock theZotBlockIOLock[2];
  ZotEP::CarIdxs theZotBlockIdxs[2];

  struct FastNC {
    void init() {
      memset_s(this,'\0',sizeof(*this));
    }
    //NoCs mNoCs;

    /// ZOT SPECIFIC
    ZotEP mMyZotEPIN;
    ZotEP mMyZotEPOUT;
  };
  FAST_LOCAL(FastNC,fNC,nc);

  void initZotBlocks() {
    memset_s(&theZotBlockCarsIO[0],'\0',sizeof(theZotBlockCarsIO));
    memset_s(&theZotBlockIOLock[0],'\0',sizeof(theZotBlockIOLock));
    memset_s(&theZotBlockIdxs[0],'\0',sizeof(theZotBlockIdxs));

    fNC.mMyZotEPIN.init(BC_ZOTBLOCK, ZOTBLOCKS_OUT_IDX, true, // note 2nd arg reversed! it's the dest!
                        theZotBlockCarsIO[ZOTBLOCKS_IN_IDX],
                        theZotBlockIOLock[ZOTBLOCKS_IN_IDX],
                        theZotBlockIdxs[ZOTBLOCKS_IN_IDX]);
    fNC.mMyZotEPOUT.init(BC_ZOTBLOCK, ZOTBLOCKS_IN_IDX, false, // note 2nd arg reversed! it's the dest!
                         theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX],
                         theZotBlockIOLock[ZOTBLOCKS_OUT_IDX],
                         theZotBlockIdxs[ZOTBLOCKS_OUT_IDX]);

#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      char buf[100];
      npf_snprintf(buf,100," zung 0x%p 0x%p 0x%p / 0x%p 0x%p 0x%p.",
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX],
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(0),
                   &theZotBlockCarsIO[ZOTBLOCKS_IN_IDX].getTC(1),
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX],
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX].getTC(0),
                   &theZotBlockCarsIO[ZOTBLOCKS_OUT_IDX].getTC(1));
      theHostBlock.packString(buf);
    }
#endif

  }

  void updateZotBlocks() {

    fNC.mMyZotEPIN.updateOps();
    fNC.mMyZotEPOUT.updateOps();
    if (false) {
      static u32 once = 0;
      if (once < 10) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('!',hartChar(fAll.mHartNum));
        ++once;
      }
    }
  }

  int liveNC(HostBlock & hb) {

    if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
    hb.addBytes('L',hartChar(fAll.mHartNum));
    fNC.init();

    hb.addBytes('N',hartChar(fAll.mHartNum));
    initZotBlocks();

    hb.addBytes('C',hartChar(fAll.mHartNum));

    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    hb.addBytes('5',hartChar(fAll.mHartNum));
    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      if ((++spin & 0xfffff) == 0) {
        hb.hartbeat(fAll.mHartNum);
      }
      updateZotBlocks();
    }
    return 0;
  }
}
