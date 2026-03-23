#include "E2HEP.h"
#include "T6ElevatorTransport.h"
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
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  Printer & E2HEP::print(Printer &p, BaseCar<EWBlock>& bc) const {
    p.printf("<EWCar 0x%p>\n",&bc);
    return p;
  }
  Printer & E2HEP::to_repr(Printer & p) const {
    p.printf("<E2HL: u#%u:%u,%u ews=0x%x/0x%x cp=0x%p cm=0x%p c#=%u cci=%u in=%d>\n",
             mOurEWPIndex,
             mHubNoC0.x,mHubNoC0.y,
             mEWHUBAddress,
             mEWCarStorage,
             mCars,
             mCarMetadata,
             mCarCount,
             mCurrentCarIdx,
             mIsIn);
    return p;
  }

  void E2HEP::initCars(u32 ewpidx,
                       U8C ournoc0,
                       EWCar * stg, BaseCarMetadata * meta, u32 count,
                       U8C hubnoc0, u32 hubewhubblockaddr,
                       bool isIn) {
    MFM_API_ASSERT_NONNULL(stg);
    MFM_API_ASSERT_NONNULL(meta);
    AtomicScopeLock guard(getPlatformLock());
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mOurEWPIndex = ewpidx;
    mOurNoC0 = ournoc0;
    mCars = stg;
    mCarMetadata = meta;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mHubNoC0 = hubnoc0;
    mEWHUBAddress = hubewhubblockaddr;
    mEWCarStorage = mEWHUBAddress + mOurEWPIndex * sizeof(EWCarStorage);
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(EWCar));
    memset_s(mCarMetadata,0u,count*sizeof(BaseCarMetadata));
    P.printf("E2HEPIC ");
    to_repr(DP);
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
    static u32 spin = 0u;
    bool report = (++spin % 100'000'000u) == 0;
    //C9printf("E2HU10\n");
    MFM_API_ASSERT_NONNULL(mCarMetadata);
    AtomicScopeLock guard(getPlatformLock());
    //C9printf("E2HU11\n");

    //////
    if (mOurNoC0.x <= 3u && mOurNoC0.y <= 4) {
    static CarState oldstates[2] = {(CarState)8,(CarState)8};
    bool chg = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      EWCar& car = mCars[c];
      if (oldstates[c] != car.getCarState()) {
        chg = true; break;
      }
    }
    if (chg) {
      P.printf("CRPT ewp#%u %u,%u-> hub %u,%u",
                mOurEWPIndex,
                mOurNoC0.x,mOurNoC0.y,
                mHubNoC0.x,mHubNoC0.y);
      for (u32 c = 0u; c < mCarCount; ++c) {
        EWCar& car = mCars[c];
        BaseCarMetadata & carmeta = mCarMetadata[c];
        CarState cs = car.getCarState();
        if (oldstates[c] != cs) {
          P.printf(" %u:%s",c, NRI3::getCarStateName(oldstates[c]));
          oldstates[c] = cs;
          P.printf("->%s",NRI3::getCarStateName(cs));
        } else P.printf(" %u:%s",c,NRI3::getCarStateName(cs));
      }
      P.printf("\n");
    }
    }
    /////

    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {

      EWCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        // start with all EW cars cross/image side but (empty and) ready to go
        // host: OUTBOUND_DEPARTED means already gone
        // cross: OUTBOUND_DEPARTED means just arrive
        //et.notice("EWINIT %d of %d\n",c,mCarCount);
        car.setCarState(CarState::CLOSED, CarType::EMPTY);
        if (report) C9printf("REPE2HU12 #%u s%u t%u\n",
                 c,car.getCarState(),car.getCarType());
        continue;
      }

      if (!car.isComplete()) {     // urgh we could be racing with inbound delivery or outbound shipping prep
        C9printf("E2HU1210\n");
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
      if (report) C9printf("REPE2HU13 c#%u cs%u\n",c,cs);
      switch (cs) {

      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          carmeta.mArrivalTime = millisElapsed(); // note the time
          C9printf("E2HUARR 0x%08x %d @ %u\n",&car,c,carmeta.mArrivalTime);
          
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now itz open for bidniss
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        C9printf("E2HU EWLOPN(%d)\n",c);

        // JUST CLOSE EM DAMMIT LET'S SEE SHIPPPPPPING
        if (true || car.readyToClose(carmeta,millisElapsed())) {
          C9printf("E2HU EWCLSR(%d)\n",c);
          car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        {
          static u32 shipt = 0u;
          if (shipt++ % 100u == 0u)
            C9printf("ESHIPT %u\n",shipt);
          //ret = et.ship(car, c); // success advances to departing state
          //ret = NRI3::initiateWriteToT6(ARGS?);
          CarType ct = car.getCarType();
          car.setCarState(CarState::INBOUND_DEPARTED,ct);
          C9printf("E2HU SHIIIP #%u t%d\n",c,ct);
          U8C sourcenoc0 = mOurNoC0;
          u32 * sourcedata = (u32*) &car;
          u32 wordcount = sizeof(EWCar)/4u;
          U8C destnoc0 = mHubNoC0;
          u32 destaddr = computeCarDestination(c);
          s32 res = NRI3::initiateWriteToT6(sourcenoc0, sourcedata, wordcount, destnoc0, destaddr);
          if (false) C9printf("NRI3::initiateWriteToT6((%u,%u),0x%p,%u,(%u,%u),0x%x) = %d\n",
                   sourcenoc0.x,sourcenoc0.y,
                   sourcedata,
                   wordcount,
                   destnoc0.x,destnoc0.y,
                   destaddr,
                   res
                   );
          break;
        }
      }
    }
    return ret;
  }

}

