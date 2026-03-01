#include "E2HEP.h"
#include "T6ElevatorTransport.h"
#include "CrossUtils.h"         // for memset_s
#include "FastT0.h" // for millisElapsed

#include "FastLocal.h" // for fAll

namespace MFM {
  extern HostBlock theHostBlock;

  E2HEP::E2HEP()
    : mRemoteBaseAddress(0u)
    , mCars(0)
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  Printer & E2HEP::print(Printer &p, BaseCar<EWBlock>& bc) const {
    p.printf("<EWCar 0x%p>\n",&bc);
    return p;
  }
  Printer & E2HEP::to_repr(Printer & p) const {
    p.printf("<E2HL: rba=0x%llx cp=0x%p cm=0x%p c#=%u cci=%u in=%d>\n",
             mRemoteBaseAddress,
             mCars,
             mCarMetadata,
             mCarCount,
             mCurrentCarIdx,
             mIsIn);
    return p;
  }

  void E2HEP::initCars(EWCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) {
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
    memset_s(mCars,0u,count*sizeof(EWCar));
    memset_s(mCarMetadata,0u,count*sizeof(BaseCarMetadata));
  }

  bool E2HEP::sendCar() {
    FAIL(INCOMPLETE_CODE);
#if 0    
    for (u32 tries = 0u; tries < mCarCount; ++tries) {
      EWCarStorage::EWCar * lcp = getCurrentCarIfAny();
      if (!lcp) FAIL(INCOMPLETE_CODE);
      if (lcp->getCarState() != CarState::OPEN) return false;
      EWBlock & lb = lcp->getContent();
      u32 room = lb.spaceRemaining();
      if (room > 0u) { // room for one more
        lb.addByte(byte);
        return true;
      }
      // current car is fully packed, so close it
      lcp->setCarState(CarState::CLOSED,CarType::STANDARD); 
      advanceToNextCar();       // and hope for rooom in the next one
    }
#endif
    return false; // we're blown.
  }

  E2HEP::EWCar * E2HEP::getCurrentCarIfAny() {
    DIEWAY();
    if (mCurrentCarIdx >= mCarCount) return 0;
    DIEWAY();
    return &mCars[mCurrentCarIdx];
  }

  bool E2HEP::update() {
    C9printf("E2HU10\n");
    MFM_API_ASSERT_NONNULL(mCarMetadata);
    AtomicScopeLock guard(getPlatformLock());
    C9printf("E2HU11\n");

    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {

      EWCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        C9printf("E2HU12\n");
        // start with all EW cars host side
        // host: INBOUND_DEPARTED means just arrive
        // cross: INBOUND_DEPARTED means already gone
        //et.notice("EWINIT %d of %d\n",c,mCarCount);
        car.setCarState(CarState::INBOUND_DEPARTED, CarType::STANDARD);
        continue;
      }

      if (!car.isComplete()) {     // urgh we could be racing with inbound delivery or outbound shipping prep
        CarSig hdr = car.getHeader(); 
        if (hdr.mCarState != CarState::OUTBOUND_DEPARTED &&  // delivery in progress, just wait?
            hdr.mCarState != CarState::INBOUND_DEPARTED) {   // shipping out in progress, just wait?
          CarSig fut = car.getFooter();
          {
            static u32 spin;
            if ((spin++ & 0xfff) == 0u)
              C9printf("(%d,%d)BADCAR#%d h:%02x%02x.%02x%02x f:%02x%02x.%02x%02x %u\n",
                        fAll.mPos.x,fAll.mPos.y, c,
                        hdr.mCarMagic, hdr.mCarNonce, hdr.mCarState, hdr.mCarType,
                        fut.mCarMagic, fut.mCarNonce, fut.mCarState, fut.mCarType,
                        spin);
          }
        }
        continue;
      }
      C9printf("E2HU13 c#%u cs%u\n",c,cs);
      switch (cs) {

      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          carmeta.mArrivalTime = millisElapsed(); // note the time
          C9printf("E2HU P2PEWARR 0x%08x %d @ %u\n",&car,c,carmeta.mArrivalTime);
          
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now itz open for bidniss
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        C9printf("E2HU EWLOPN(%d)\n",c);

        if (car.readyToClose(carmeta,millisElapsed())) {
          C9printf("E2HU EWCLSR(%d)\n",c);
          car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        //ret = et.ship(car, c); // success advances to departing state
        C9printf("E2HU EWCLS ret = et.ship(car, c)\n");
        break;
      }
    }

    return ret;
  }

}

