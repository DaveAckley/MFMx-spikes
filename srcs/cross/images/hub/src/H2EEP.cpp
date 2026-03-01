#include "H2EEP.h"
#include "FastLocal.h" // for fAll
#include "FastT0.h" // for millisElapsed

namespace MFM {
  void H2EEP::init() {
    memset_s(this,'\0',sizeof(*this));
  }

  Printer & H2EEP::to_repr(Printer & p) const {
    p.printf("<H2EL: rba=0x%llx cp=0x%p cm=0x%p c#=%u cci=%u in=%d>\n",
             mRemoteBaseAddress,
             mCars,
             mCarMetadata,
             mCarCount,
             mCurrentCarIdx,
             mIsIn);
    return p;
  }

  void H2EEP::initCars(EWCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) {
    MFM_API_ASSERT_NONNULL(stg);
    MFM_API_ASSERT_NONNULL(meta);
    DP.printf("H2EL10\n");
    //    AtomicScopeLock guard(getPlatformLock());
    DP.printf("H2EL11\n");
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mCars = stg;
    mCarMetadata = meta;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteaddr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(EWCar));
    memset_s(mCarMetadata,0u,count*sizeof(BaseCarMetadata));
    DP.printf("H2EL12\n");
  }

  bool H2EEP::sendCar() {
    FAIL(INCOMPLETE_CODE);
    return false; // we're blown.
  }

  H2EEP::EWCar * H2EEP::getCurrentCarIfAny() {
    DIEWAY();
    if (mCurrentCarIdx >= mCarCount) return 0;
    DIEWAY();
    return &mCars[mCurrentCarIdx];
  }

  bool H2EEP::update() {
    MFM_API_ASSERT_NONNULL(mCarMetadata);
    C9printf("H2ELU\n");
    //AtomicScopeLock guard(getPlatformLock());

    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {

      EWCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        C9printf("H2ELU\n");
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
              DP.printf("(%d,%d)BADCAR#%d h:%02x%02x.%02x%02x f:%02x%02x.%02x%02x %u\n",
                        fAll.mPos.x,fAll.mPos.y, c,
                        hdr.mCarMagic, hdr.mCarNonce, hdr.mCarState, hdr.mCarType,
                        fut.mCarMagic, fut.mCarNonce, fut.mCarState, fut.mCarType,
                        spin);
          }
        }
        continue;
      }

      switch (cs) {

      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          carmeta.mArrivalTime = millisElapsed(); // note the time
          C9printf("H2ELU 0x%08x %d @ %u\n",&car,c,carmeta.mArrivalTime);
    
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now itz open for bidniss
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        C9printf("H2ELU(%d)\n",c);
        
        if (car.readyToClose(carmeta,millisElapsed())) {
          C9printf("H2ELU%u(%d)\n",c);
          car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        //ret = et.ship(car, c); // success advances to departing state
        C9printf("H2ELU ret = et.ship(car, c)\n");
        break;
      }
    }

    return ret;
  }

}

