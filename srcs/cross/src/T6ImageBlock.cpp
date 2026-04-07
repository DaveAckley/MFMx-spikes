#include "T6ImageBlock.h"
#include "Printf.h" // for DP
#include "T6CellO.h"

namespace MFM {

  bool T6ImageBlock::findBlockCodeInCell(bool skipUs, BlockCode bc, U8C & foundNoC0, ImageBlockAddr & iba) {
    T6CellO cello;
    bool initted = cello.init();
    MFM_API_ASSERT(initted,ILLEGAL_STATE);
    return findBlockCodeInCell(cello, skipUs, bc, foundNoC0, iba);
  }

  bool T6ImageBlock::findBlockCodeInCell(T6CellO & cello,
                                         bool skipUs, BlockCode bc, U8C & foundNoC0, ImageBlockAddr & foundiba) {
    // iterate over CellBlock sites
    //  read their ImageBlockHeader
    //  search their IBAs for bc
    //  if found, set foundNoC0, foundiba, and return true
    // return false

    // ITERATE OVER CELLBLOCK SITES
    U8C ournoc0 = cello.getNoC0ofUs();
    const CellBlock & cb = cello.getOurCB();
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C atcp(x,y);
        U8C theirnoc0 = cello.getNoC0ofCellP(atcp);
        if (theirnoc0 == ournoc0 && skipUs) // skip ourselves if requested
          continue;

        // Look for bc in them
        if (NRI3::findBlockCodeInNoC0(ournoc0, theirnoc0, bc, foundiba)) {
          foundNoC0 = theirnoc0;
          // foundiba already set
          return true;
        }
      }
    }

    // RETURN FALSE
    return false;               // blockcode not found
  }

#if 0
  bool T6ImageBlock::findBlockCodeInNoC0(U8C ournoc0, U8C theirnoc0, BlockCode bc, ImageBlockAddr & foundiba) {
    ImageBlockHeader ibh = NRI3::blockingReadImageBlockHeaderNoC0(ournoc0, theirnoc0);

    //  SEARCH THEIR IBAS FOR BC (always on noc0, using a lot more packets & bandwidth than needed, but hey..)
    for (u32 idx = 0u; idx < ibh.mEntries; ++idx) {
      ImageBlockAddr iba = NRI3::blockingReadImageBlockAddrNoC0(ournoc0, theirnoc0, idx);

      if (iba.mBlockCode == bc) { //  IF FOUND,
        //  SET FOUNDIBA AND RETURN TRUE
        foundiba = iba;
        return true;
      }
    }
    return false; // NOT FOUND
  }
#endif  

}

    
