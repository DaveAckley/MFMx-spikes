/* -*- C++ -*- */

namespace MFM {

  template <class SUB,class CAR_TYPE,u32 CAR_COUNT>
  bool EP<SUB,CAR_TYPE,CAR_COUNT>::updateOps() {

    AtomicLock guard(getPlatformLock());
    
    if (false) {
      static bool once = false;
      if (!once) {
        extern HostBlock theHostBlock;

        theHostBlock.addByte('L'+getPlatformLock().peekLock());
        once = true;
      }
    }

    {
      extern HostBlock theHostBlock;
      theHostBlock.addString(getName());
    }
   
    bool ret = false;
    /// TRY RECEIVING ARRIVALS
    while (isValidIndex(mOldestDeparted)) { // oldest departed next to return
      CAR_TYPE & car = getCar(mOldestDeparted);
      TCSig sig = car.getHeader();
      TCState cs = sig.getTCState();
      if (!isArriving(cs)) break;
      if (!car.isComplete()) break;

      // Welcome! Let's get you set up here.
      TCOpsData & data = getTCOps(mOldestDeparted);
      data.mArrivalTime = millisElapsed();
      

    }

    /// TRY SHIPPING DEPARTURES
    while (isValidIndex(mOldestDeparted)) {
      if (xxx) break;
      yyy;
    }
    /// AND PROMENADE

    for (u32 c = 0u; c < CAR_COUNT; ++c) {
      CAR_TYPE * carp = self().getCurrentCarIfAny();
      if (!carp) continue;
      u32 carnum = self().getCurrentCarIndex();
      CAR_TYPE& car = *carp;
      TCOpsData & cartms = getCurrentCarOps();
      TCState cs = car.getTCState();

      if (cs == TCState::UNUSED) {
        // start with all cars on T6
        // host: OUTBOUND_DEPARTED means already gone
        // cross: OUTBOUND_DEPARTED means just arrived
        car.setTCState(TCState::OUTBOUND_DEPARTED, TCType::STANDARD);
        {
          extern HostBlock theHostBlock;
          theHostBlock.addByte('x'+carnum);
        }
        continue;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress
        {
          extern HostBlock theHostBlock;
          theHostBlock.addByte('?');
        }
        if (false) self().logTo().print("incomplete car\n");
        continue;
      }

        {
          extern HostBlock theHostBlock;
          theHostBlock.addByte('0'+carnum);
        }

      switch (cs) {
      case TCState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case TCState::INBOUND_DEPARTED:
      case TCState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          cartms.mArrivalTime = millisElapsed(); // note the time
          TCSig sig = car.getHeader();
          if (sig.mTCSType == TCType::EMPTY) {
            car.reset();         // clean out whole content
          }
          car.setTCState(TCState::OPEN,TCType::STANDARD); // now its standard
        } 
        // if isDeparting, wait for external developments
        break;

      case TCState::OPEN:
        if (car.readyToClose(cartms,millisElapsed())) {
          if (car.isEmpty())
            car.setTCState(TCState::CLOSED,TCType::EMPTY); 
          else
            car.setTCState(TCState::CLOSED,TCType::STANDARD); // Please Buckle Up
        }
        break;

      case TCState::CLOSED:
        ret = ship(car, carnum); // success advances to departing state
        break;
      }
    }
    return ret;
  }

}
