#include "SharedEPs.h"

namespace MFM {
  void InterHubEP::initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    HBXTAG(InHuInit,&l1data);
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  bool InterHubEP::recvTC(InterHubBlock & car, u8 carindex) {
    //    HBPTAG(IHRTC,carindex);

    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crbi.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    //    HBPTAG(IHCV,carindex);    
    //    HBPVAL(&crbi);
    crbi.add(carindex); //notify hB
    //    HBPTAG(IHB2HB,&crbi);

    return true;
  }

  InterHubBlock * InterHubEP::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    //HBPTAG(IHGotp,&this->getCarStg());
    return &this->getCarStg().getTC(carindex);
  }

}
