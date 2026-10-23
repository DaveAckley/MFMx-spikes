#include "CacheLoader.h"
#include "Debug.h"
#include "InterHub.h"

namespace MFM {
  CacheLoaders theL1CacheLoaders;

  /** pack local sites into ihb */
  bool CacheLoader::loadPacketFromCache(GridManager & gm, InterHubBlock & ihb) {

    U8CRange::iterator & itr = mLocalItr;

    LOGPTAG(SHI2,itr.range); 
    if (itr.atEnd()) return false; // packet not touched

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();
    if (!patoms.isEmpty())
      LOGPTAG(CLLP_NOTEMPTY,patoms.mDataUsed);
    patoms.init();              // well it's empty now
    while (!itr.atEnd()) {
      if (patoms.isFull()) break; // Room For No More
      U8C ac = *itr++;
      P4Atom a = t6grid.getAtom(ac);
      patoms.storeAtomOrDie(a);
    }
    MFM_API_ASSERT(patoms.isFull() || itr.atEnd(),ILLEGAL_STATE);    // can't stop unless car full or itr done..

    LOGPTAG(SHI@,itr.at);
    u32 pktsize = patoms.getCurrentPayloadSize();
    ihb.closeTC(pktsize);
    return true;                // packet loaded
  }

  bool CacheLoader::tryLoad(GridManager & gm) { //< return true iff done
    U8CRange::iterator & itr = mLocalItr;
    if (itr.atEnd()) return true;
    Dir8 d8 = mLeaderDir8;
    LOGPTAG(ldrD8,dir8ToByteString(d8));

    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    using IHubData = InterHubEP::Super::L1Data;
    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[d8];
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[d8];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    
    if (crbo.isFull()) return false; // No Room At The Outt
    u8 carindex;
    if (crbi.remove(carindex)) return false; // No Cars Waiting
    InterHubBlock & ihb = ihs.getTC(carindex);

    LOGPTAG(SHI2,itr.range); 

    T6Grid & t6grid = gm.getT6GridOrDie();
    InterHubPayload & pay = ihb.payload();
    IHPAtoms & patoms = pay.asAtomsOrDie();
    patoms.init();
    while (!itr.atEnd()) {
      if (patoms.isFull()) break; // Room For No More
      U8C ac = *itr++;
      P4Atom a = t6grid.getAtom(ac);
      patoms.storeAtomOrDie(a);
    }
    if (patoms.isFull() || itr.atEnd()) {
      LOGPTAG(SHI@,itr.at);
      u32 pktsize = patoms.getCurrentPayloadSize();
      ihb.closeTC(pktsize);
      crbo.add(carindex);         // hand control back to comm
      LOGPTAG(SHIPT,pktsize);
    } else
      FAIL(ILLEGAL_STATE);    // can't stop unless car full or itr done..

    if (itr.atEnd()) LOGPTAG(SHIPT!,itr.at);

    return false;
  }
}
