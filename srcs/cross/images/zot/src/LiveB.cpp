#include "StandardLife.h"
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "ZotBlock.h"

#include "demo.h" // for demo stuff

namespace MFM {

  struct FastB {
  };
  FAST_LOCAL(FastB,fB,b);

  bool processZotCars(HostBlock & hb,bool inside) {
    HOOKIT();

    const u32 DIR_IDX = inside ? ZOTBLOCKS_IN_IDX : ZOTBLOCKS_OUT_IDX;

    if (!theZotBlockL1Data.isActive(DIR_IDX)) {
      HBNOTE("zPROCBLOC");
      EPState & pubstate = theZotBlockL1Data.getPublicEPState(DIR_IDX);
      HBPVAL(&pubstate);
      HBPVAL(getNameFromEPState(pubstate));
      return false;             // wait a bit
    }

    using ZotData = T6EPL1Data<ZotBlockStg,ZOTBLOCKS_DEMO_COUNT>;

    ZotData::CarIdxs & idxs = theZotBlockL1Data.mTheCarIdxs[DIR_IDX];
    ZotData::CarIdxRB & crbi = idxs.mTheIdxs[ZotData::CarIdxs::COMM2COMP];
    ZotData::CarIdxRB & crbo = idxs.mTheIdxs[ZotData::CarIdxs::COMP2COMM];
    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false;
    HBPTAG("zot got|",inside);

    ZotBlockStg & cars = theZotBlockL1Data.mTheTCStorages[DIR_IDX];
    HBASSERT_LT(carindex, cars.getCarCount());
    ZotBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(),TCState::OPEN); 
    ZotPayload & pay = car.payload();
    pay.update(inside); // kilroy was here

    HBPTAG(zot/PRECLOS,carindex);
    car.closeTC(sizeof(pay)); // every car is a full car
    MFM_API_ASSERT(!crbo.isFull(),ILLEGAL_STATE);
    crbo.add(carindex);
    HBPTAG(zot/AFTCLOS,&crbo);

    return true;
  }

  int initB() {
    auto ptpr = getPtrPair<L1Stuff,FastStuff,LdSectionName<'b'>>();
    HBPTAG(AKAPP,ptpr.mL1Pub);
    preloadT2Mailbox();
    return 0;
  }

  int liveB(HostBlock & hb) {
    if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop

    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      if ((++spin & 0xfff) == 0) {
        hb.hartbeat(fAll.mHartNum);
      }
      if (!processZotCars(hb,true) && !processZotCars(hb,false))
        breathe();
      
    }
    return 0;
  }
}
