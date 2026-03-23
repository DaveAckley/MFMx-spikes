#include "E2HEP.h"
//#include "T6ElevatorTransport.h"
#include "CrossUtils.h"         // for memset_s
#include "FastT0.h" // for millisElapsed

#include "FastLocal.h" // for fAll
#include "NRIUtils.h" // for NRI3::

#define P DP
//#define P LOG

namespace MFM {
  extern HostBlock theHostBlock;

  E2HEP::E2HEP()
    : mOurEWPIndex(U32_MAX)
    , mEWHUBAddress(U32_MAX)
    , mEWCarStorage(U32_MAX)
    , mHubNoC0(U8C(U8_MAX,U8_MAX))
    , mCars(0)
    , mCurrentCarIdx(0u)
  { }

  void E2HEP::initCars(u32 ewpidx,
                       U8C ournoc0,
                       EWCar * stg, CarOpsTimers * ctms,
                       U8C hubnoc0, u32 hubewhubblockaddr) {
    MFM_API_ASSERT_NONNULL(stg);
    MFM_API_ASSERT_NONNULL(ctms);
    AtomicScopeLock guard(getPlatformLock());
    MFM_API_ASSERT_ARG(stg != 0);
    mOurEWPIndex = ewpidx;
    mOurNoC0 = ournoc0;
    mCars = stg;
    mCarOpsTimers = ctms;
    mCurrentCarIdx = 0u;
    mHubNoC0 = hubnoc0;
    mEWHUBAddress = hubewhubblockaddr;
    mEWCarStorage = mEWHUBAddress + mOurEWPIndex * sizeof(EWCarStorage);
    {
      u32 count = getCarCount();
      memset_s(mCars,0u,count*sizeof(EWCar));
      memset_s(mCarOpsTimers,0u,count*sizeof(CarOpsTimers));
    }
    P.printf("E2HEPIC ");
    to_repr(P);
  }

  bool E2HEP::update() {
    static u32 spin = 0u;
    bool report = (++spin % 100'000'000u) == 0;
    //C9printf("E2HU10\n");
    MFM_API_ASSERT_NONNULL(mCarOpsTimers);
    AtomicScopeLock guard(getPlatformLock());
    //C9printf("E2HU11\n");

    FAIL(INCOMPLETE_CODE); // DEIMPLEMENTED

    return false; // XXX NOT REACHED
  }

}

