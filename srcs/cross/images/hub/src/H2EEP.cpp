#include "H2EEP.h"
#include "FastLocal.h" // for fAll
#include "FastT0.h" // for millisElapsed
#include "NRIUtils.h" // for NRI3

#define P DP
//#define P LOG

namespace MFM {
  void H2EEP::init() {
    memset_s(this,'\0',sizeof(*this));
  }

  Printer & H2EEP::to_repr(Printer & p) const {
    p.printf("<H2EL: ix=%u rba=0x%x cp=0x%p cm=0x%p c#=%u cci=%u in=%d>\n",
             mEWPIdx,
             mRemoteBaseAddress,
             mCars,
             mCarMetadata,
             mCarCount,
             mCurrentCarIdx,
             mIsIn);
    return p;
  }

  void H2EEP::initCars(U8C ournoc0, u8 ewpidx, U8C ewpnoc0, EWCar * stg, BaseCarMetadata * meta, u32 count, u32 remoteL1Addr, bool isIn) {
    MFM_API_ASSERT_NONNULL(stg);
    MFM_API_ASSERT_NONNULL(meta);
    //P.printf("H2EL10\n");
    //    AtomicScopeLock guard(getPlatformLock());
    //P.printf("H2EL11\n");
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mOurNoC0 = ournoc0;
    mEWPIdx = ewpidx;
    mEWPNoC0 = ewpnoc0;
    mCars = stg;
    mCarMetadata = meta;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteL1Addr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(EWCar));
    memset_s(mCarMetadata,0u,count*sizeof(BaseCarMetadata));
    if (false) P.printf("H2EL12 %d cm0x%x+%u\n",ewpidx,mCarMetadata,count*sizeof(BaseCarMetadata));
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
    static u32 spin = 0u;
    bool report = (++spin % 100'000'000u) == 0;
    if (report) C9printf("REPH2UPS %u %d\n",spin,mEWPIdx);
    MFM_API_ASSERT_NONNULL(mCarMetadata);
    //AtomicScopeLock guard(getPlatformLock());

    //////
    if (mOurNoC0.x <= 3u && mOurNoC0.y <= 4) {
    static CarState (oldstates[8])[2] = {{(CarState)8,(CarState)8}};
    bool chg = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      EWCar& car = mCars[c];
      if (oldstates[mEWPIdx][c] != car.getCarState()) {
        chg = true; break;
      }
    }
    if (chg) {
      P.printf("CRPT hub %u,%u-> ewp#%u %u,%u",
                mOurNoC0.x,mOurNoC0.y,
                mEWPIdx,
                mEWPNoC0.x,mEWPNoC0.y);
      for (u32 c = 0u; c < mCarCount; ++c) {
        EWCar& car = mCars[c];
        BaseCarMetadata & carmeta = mCarMetadata[c];
        CarState cs = car.getCarState();
        if (oldstates[mEWPIdx][c] != cs) {
          P.printf(" %u:%s",c, NRI3::getCarStateName(oldstates[mEWPIdx][c]));
          oldstates[mEWPIdx][c] = cs;
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
        //C9printf("H2ELUNUSED %u\n",c);
        // start with all EW cars EWP side
        // HUB: OUTBOUND_DEPARTED means already gone
        // EWP: CLOSED means ready to ship inbound
        //et.notice("EWINIT %d of %d\n",c,mCarCount);

        //Mon Mar 2 16:04:06 2026 Let's leave it unused given the EWP
        //should overwrite into it anyway
        //WAS: car.setCarState(CarState::OUTBOUND_DEPARTED, CarType::STANDARD);
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
              P.printf("(%d,%d)BADCAR#%d h:%02x%02x.%02x%02x f:%02x%02x.%02x%02x %u\n",
                        fAll.mPos.x,fAll.mPos.y, c,
                        hdr.mCarMagic, hdr.mCarNonce, hdr.mCarState, hdr.mCarType,
                        fut.mCarMagic, fut.mCarNonce, fut.mCarState, fut.mCarType,
                        spin);
          }
        }
        continue;
      }

      if (report) C9printf("REPH2ELU11 #%u cs%u\n", c, cs);

      switch (cs) {

      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          carmeta.mArrivalTime = millisElapsed(); // note the time
          P.printf("H2ELAR %u:%u,%u 0x%08x #%u s%u t%u @ %u\n",
                    mEWPIdx,
                    mEWPNoC0.x,mEWPNoC0.y,
                    &car, c,
                    car.getCarState(), car.getCarType(),
                    carmeta.mArrivalTime);
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now itz open for bidniss
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        //C9printf("H2EUO(%d)\n",c);
        
        // JUST CLOSE EM DAMMIT LET'S SEE SHIPPPPPPING
        if (true || car.readyToClose(carmeta,millisElapsed())) {
          //C9printf("H2EUC(%d)\n",c);
          car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        //ret = et.ship(car, c); // success advances to departing state
        //ret = NRI3::initiateWriteToT6(ARGS?);
        {
          static u32 shipt = 0u;
          if (shipt++ % 100u == 0u)
            C9printf("HSHIPT %u\n",shipt);

          CarType ct = car.getCarType();
          car.setCarState(CarState::OUTBOUND_DEPARTED,ct);
          C9printf("H2EU SHIIIP #%u t%d\n",c,ct);
          U8C sourcenoc0 = mOurNoC0;
          u32 * sourcedata = (u32*) &car;
          u32 wordcount = sizeof(EWCar)/4u;
          U8C destnoc0 = mEWPNoC0;
          u32 destaddr = computeCarDestination(c);
          s32 res = NRI3::initiateWriteToT6(sourcenoc0, sourcedata, wordcount, destnoc0, destaddr);
          if (report) C9printf("REPNRI3::initiateWriteToT6((%u,%u),0x%p,%u,(%u,%u),0x%x) = %d\n",
                   sourcenoc0.x,sourcenoc0.y,
                   sourcedata,
                   wordcount,
                   destnoc0.x,destnoc0.y,
                   destaddr,
                   res
                   );
        }
        break;
      }
    }

    return ret;
  }

}

