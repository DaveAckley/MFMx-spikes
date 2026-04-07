#include "SharedEPs.h"

namespace MFM {
  void InterHubEP::initInterHubEP(BlockCode destbc, bool isin, typename Super::L1Data & l1data) {
    this->initT6EP(destbc, isin, l1data);
  }
     
  bool InterHubEP::recvTC(InterHubBlock & car, u8 carindex) {
    SNAP(6,HBNOTE("IHRTC"));

    Super::L1Data::CarIdxRB & crb = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crb.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    HBNOTE("IHCV");    
    HBPVAL(&crb);
    crb.add(carindex); //notify hB
    HBPVAL(carindex);

    return true;
  }

  InterHubBlock * InterHubEP::getCarPtrIfAny(u8 carindex) const {
    SNAP(6,HBNOTE("IHGCP"));
    if (carindex >= CAR_COUNT) return 0;
    return &this->getCarStg().getTC(carindex);
  }

}
