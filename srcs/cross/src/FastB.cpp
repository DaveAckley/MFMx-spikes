#include "FastLocal.h"
#include "FastB.h"
#include "LiveB.h"
#include "Printf.h"
#include "ImageBlock.h"
#include "BlockCode.h"
#include "T6CellO.h"

namespace MFM {

  int hartMainB(HostBlock & hb) {
    //    DIEWAY();

    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    //DP.printf("B#%d(%d,%d)\n",hb.mTLBI,hb.mPos.x,hb.mPos.y);

    // hb.mCommonArgs[0] reserved for nonce (used by T2)
    // hb.mCommonArgs[1] reserved for start decay type
    // hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;
    //hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    XXX_DEBUG_FUNC(__FILE__,__LINE__);

    DIEWAY();

    {
      T6CellO cello;
      if (cello.init()) {
        U8C hubc = cello.getXYofImage(ImageCode::IC_HUB);
        U8C hubnoc = cello.getNoC0ofXY(hubc);
        DP.printf("T6CO"
                  //" a=0x%x"
                  " up=(%u,%u)"
                  " c#=(%u,%u)"
                  " cp=(%u,%u)"
                  " h0=(%u,%u)"
                  " ix=%u"
                  "\n",
                  //cello.mCellBlockAddr,
                  cello.mUsCT6.x,cello.mUsCT6.y,
                  cello.mCellNum.x,cello.mCellNum.y,
                  cello.mUsCellPos.x,cello.mUsCellPos.y,
                  hubnoc.x,hubnoc.y,
                  cello.mImageTypeIndex
                  );
      } else {
        DP.printf("T6CONO\n");
      }
    }
    {
      extern ImageBlockHeader theImageBlock;
      //      ImageBlockHeader & ibh = *(ImageBlockHeader*) (u32*) &theImageBlock;
      ImageBlockHeader & ibh = theImageBlock;
      // report cell block info too
      //      CellBlock & cb = theCellBlock;
      ImageBlockAddr ibac = ibh.findIBAIfAny(BlockCode::BC_CELLBLOCK);
      if (ibac.isValid()) {
        u32 * bkptr = (u32*) ibac.mBlockAddr;
        CellBlock & cb = *(CellBlock *) bkptr;
        DP.printf("XIBH v=%d, ic=%d, ec=%d, cba=0x%p, cbv=%d, ct=%u, cs=(%u,%u)\n",
                  ibh.isValid(), ibh.mImageCode, ibh.mEntries,
                  &cb,
                  cb.isValid(),
                  cb.getCellType(),
                  cb.getCellSize().x, cb.getCellSize().y);
      } else DP.printf("NO CB\n");
    }
    
    return liveB(hb);          // go do your hart B thing you
  }
}
