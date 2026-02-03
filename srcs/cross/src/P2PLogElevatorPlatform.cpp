#include "P2PLogElevatorPlatform.h"
#include <string.h>
#include "Fail.h"
#include "FATAL.h"
#include "BaseCar.h"
#include "T6ElevatorTransport.h"
#include "FastT0.h" // for millisElapsed
#include "FastLocal.h" // for fAll
#include "Printf.h" // for DP

namespace MFM {
  P2PLogElevatorPlatform::P2PLogElevatorPlatform()
    : mRemoteBaseAddress(0u)
    , mCars(0)
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  void P2PLogElevatorPlatform::initCars(LogCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) {
    MFM_API_ASSERT_NONNULL(stg);
    MFM_API_ASSERT_NONNULL(meta);
    AtomicScopeLock guard(getPlatformLock());
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mCars = stg;
    mCarMetadata = meta;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteaddr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(LogCar));
    memset_s(mCarMetadata,0u,count*sizeof(BaseCarMetadata));
  }

  bool P2PLogElevatorPlatform::sendByte(u8 byte) {
    for (u32 tries = 0u; tries < mCarCount; ++tries) {
      LogCarStorage::LogCar * lcp = getCurrentCarIfAny();
      if (!lcp) FAIL(INCOMPLETE_CODE);
      if (lcp->getCarState() != CarState::OPEN) return false;
      LogBlock & lb = lcp->getContent();
      BaseCarMetadata & carmeta = mCarMetadata[mCurrentCarIdx];
      if (lb.isEmpty())         // first customer!
        carmeta.mOccupiedTime = millisElapsed(); // note the time!

      u32 room = lb.spaceRemaining();
      bool hasroom = room > 0u;
      bool shipearly = room <= 32 && byte == '\n';
      if (hasroom) {            // room for at least one more
        lb.addByte(byte);
        if (!shipearly)         // unless nearly full and just shipped \n
          return true;          // keep loading
      }
      // current car is pretty packed, so close it
      lcp->setCarState(CarState::CLOSED,CarType::STANDARD); 
      advanceToNextCar();       // look for anther car
      if (hasroom && shipearly) 
        return true;            // this was an early ship; done
    }                           // else hope there's another car
    return false; // we're blown.
  }

  P2PLogElevatorPlatform::LogCar * P2PLogElevatorPlatform::getCurrentCarIfAny() {
    if (mCurrentCarIdx >= mCarCount) return 0;
    return &mCars[mCurrentCarIdx];
  }

  bool P2PLogElevatorPlatform::update(T6ElevatorTransport & et) {
    MFM_API_ASSERT_NONNULL(mCarMetadata);
DIEWAY();
    AtomicScopeLock guard(getPlatformLock());

    extern HostBlock theHostBlock;
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::TRYING; 

SHOWADDR(mCarMetadata);
    bool ret = false;
DIEWAY();
    for (u32 c = 0u; c < mCarCount; ++c) {
      LogCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
SHOWADDR(mCarMetadata[c]);
      CarState cs = car.getCarState();
DIEWAY();

      if (cs == CarState::UNUSED) {
        // start with all cars on T6
        // host: OUTBOUND_DEPARTED means already gone
        // cross: OUTBOUND_DEPARTED means just arrived
        car.setCarState(CarState::OUTBOUND_DEPARTED, CarType::STANDARD);
DIEWAY();
        continue;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress
        if (false) et.notice("incomplete car\n");
DIEWAY();
        continue;
      }

      switch (cs) {
      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
DIEWAY();
        if (isArriving(cs)) {   // You Have Arrived
          carmeta.mArrivalTime = millisElapsed(); // note the time
SHOWADDR(carmeta.mArrivalTime);
DIEWAY();

          CarSig sig = car.getHeader();
          if (sig.mCarType == CarType::EMPTY) {
DIEWAY();
            { 
              static u8 once;
              if (false && once < 5u) {
                CarSig sig = car.getHeader();
                et.notice("%d-SCS(%x,%x,%x,%x)\n",once,
                          sig.mCarMagic, sig.mCarNonce,
                          sig.mCarState, sig.mCarType);
                if (sig.mCarType == CarType::STANDARD) {
                  CarSig fut = car.getStandardFooter();
                  et.notice("FUT(%x,%x,%x,%x)=%d\n",
                            fut.mCarMagic, fut.mCarNonce,
                            fut.mCarState, fut.mCarType,car.isComplete());
                }
                ++once;
              }
            }
DIEWAY();
SHOWADDR(car);
            car.getContent().reset();         // clean out whole content
DIEWAY();
          }
DIEWAY();
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now its standard
DIEWAY();
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
DIEWAY();
        
        if (car.readyToClose(carmeta,millisElapsed())) {
DIEWAY();
          if (car.isEmpty())
            car.setCarState(CarState::CLOSED,CarType::EMPTY); 
          else
            car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
DIEWAY();
        }
        break;

      case CarState::CLOSED:
DIEWAY();
        ret = et.ship(car, c); // success advances to departing state
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
DIEWAY();
        break;
      }
    }

DIEWAY();
    
    return ret;
  }
}

