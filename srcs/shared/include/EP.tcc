/* -*- C++ -*- */

namespace MFM {

  template <class SUB, class CAR_CONTENT>
  bool EP<SUB,CAR_CONTENT>::updateOps() {
    AtomicScopeLock guard(getPlatformLock());

    bool ret = false;
    for (u32 c = 0u; c < self().getCarCount(); ++c) {
      CARTYPE * carp = self().getCurrentCarIfAny();
      if (!carp) continue;
      u32 carnum = self().getCurrentCarIndex();
      CARTYPE& car = *carp;
      CarOpsTimers & cartms = getCurrentCarOps();
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        // start with all cars on T6
        // host: OUTBOUND_DEPARTED means already gone
        // cross: OUTBOUND_DEPARTED means just arrived
        car.setCarState(CarState::OUTBOUND_DEPARTED, CarType::STANDARD);
        continue;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress
        if (false) self().logTo().printf("incomplete car\n");
        continue;
      }

      switch (cs) {
      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          cartms.mArrivalTime = millisElapsed(); // note the time
          CarSig sig = car.getHeader();
          if (sig.mCarType == CarType::EMPTY) {
            car.getContent().reset();         // clean out whole content
          }
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now its standard
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        if (car.readyToClose(cartms,millisElapsed())) {
          if (car.isEmpty())
            car.setCarState(CarState::CLOSED,CarType::EMPTY); 
          else
            car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        ret = ship(car, carnum); // success advances to departing state
        break;
      }
    }
    return ret;
  }

}
