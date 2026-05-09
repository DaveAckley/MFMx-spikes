/* -*- C++ -*- */
namespace MFM {

  template <u8 BLOCK_COUNT>
  void EwpEP<BLOCK_COUNT>::initEwpEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  template <u8 BLOCK_COUNT>
  bool EwpEP<BLOCK_COUNT>::recvTC(EwpBlock & car, u8 carindex) {
    typename Super::L1Data::CarIdxRB & crbi = this->getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crbi.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    crbi.add(carindex); //notify hB

    return true;
  }

  template <u8 BLOCK_COUNT>
  EwpBlock * EwpEP<BLOCK_COUNT>::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    HBASSERT_LS(this->getSrcBlockCodeIndex(),255);
    return &this->getCarStg().getTC(carindex);
  }
}

