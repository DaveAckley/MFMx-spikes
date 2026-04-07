/* -*- C++ -*- */
namespace MFM {

  template <u8 BLOCK_COUNT>
  void EwpEP<BLOCK_COUNT>::initEwpEP(BlockCode destbc, bool isin, typename Super::L1Data & l1data) {
    HBMARK;
    this->initT6EP(destbc, isin, l1data);
    HBMARK;
  }
     
  template <u8 BLOCK_COUNT>
  bool EwpEP<BLOCK_COUNT>::recvTC(EwpBlock & car, u8 carindex) {
    typename Super::L1Data::CarIdxRB & crb = this->getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crb.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    HBNOTE("ERCV");    
    HBPVAL(&crb);
    crb.add(carindex); //notify hB
    HBPVAL(carindex);

    return true;
  }

  template <u8 BLOCK_COUNT>
  EwpBlock * EwpEP<BLOCK_COUNT>::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    HBASSERT_LS(this->getDestBlockCodeIndex(),255);
    return &this->getCarStg().getTC(carindex);
  }
}

