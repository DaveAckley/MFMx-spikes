#include "QuietBox.h"
#include "Blackhole.h"
#include "HostUtils.h"
#include "ImageCode.h"

namespace MFM {
  BGRImageHD QuietBox::t6gridRenderBlock;

  QuietBox theQuietBox;

  QuietBox::QuietBox() = default;

  QuietBox & QuietBox::get() {
    return theQuietBox;
  }

  void QuietBox::addBlackhole(Blackhole & bh) {
    u32 chipnum = bh.getChipNumber();
    MFM_API_ASSERT(chipnum < MAX_BLACKHOLES,OUT_OF_ROOM);
    MFM_API_ASSERT(mBHPtrs[chipnum] == 0,DUPLICATE_ENTRY);
    mBHPtrs[chipnum] = &bh;
  }

  Blackhole * QuietBox::getBlackholeIfPresent(u32 chipnum) const {
    MFM_API_ASSERT(chipnum < MAX_BLACKHOLES,OUT_OF_ROOM);
    return mBHPtrs[chipnum];
  }
    
  std::string QuietBox::to_repr() const {
    std::string ret = "<QuietBox";
    for (u32 i = 0; i < MAX_BLACKHOLES; ++i) {
      if (getBlackholeIfPresent(i))
        ret += " BH" + std::to_string(i);
      else
        ret += " _";
    }
    ret += ">";
    return ret;
  }

  std::string DG::to_string(const DG::Coord c) {
    std::string ret = "@(";
    ret += std::to_string(c.x);
    ret += "," + std::to_string(c.y);
    ret += ")";
    return ret;
  }

  std::string DG::to_string(const DG::Address a) {
    std::string ret = "@@(";
    if (!a.isValid()) {
      ret += "invalid)";
      return ret;
    }
    ret += "bh#" + std::to_string(a.mChipNum);
    ret += " c#" + std::to_string(a.mCellNum.x);
    ret += "," + std::to_string(a.mCellNum.y);
    ret += " s#" + std::to_string(a.mT6GridC.x);
    ret += "," + std::to_string(a.mT6GridC.y);
    ret += ")";
    return ret;
  }

  ImageBlockAddr * QuietBox::findBCInCell(ImageManager & im, const BlockCode bc, const CellBlock cb, U8C & cellp) {
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C atcp(x,y);
        u8 ic = cb.getImageAtCellPos(atcp);
        std::string ikey = im.getIKeyIfAny(ic);
        T6Image & img = im.getT6Image(ikey); // or bang
        ImageBlockAddr * ibap = img.getImageBlockAddrForBlockCodeIfAny(BC_T6GRID);
        if (ibap) {
          cellp = atcp;
          return ibap;
        }
        /*
        Eprintf("\n %s (%u,%u) ic %u %s = %p\n",
                __FUNCTION__,
                x,y,ic,ikey.c_str(),ibap);
        */
      }
    }

    return 0;               // blockcode not found
  }

  void QuietBox::loadMaps(ImageManager & im, u32 bhc) {
    Blackhole * bhp = getBlackholeIfPresent(bhc);
    MFM_API_ASSERT_NONNULL(bhp);
    Blackhole & bh = *bhp;
    OurTLBs & tlbs = bh.getOurTLBs();

    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         ++tlbi) {
      OurTLBs::TLBInfo & info = tlbs.getTLBInfo(tlbi);
      const T6Image & image = info.getDeployedImageOrDie();
      u32 ic = image.getImageCode();
      if (ic != IC_HUB) continue;

      CellBlock cb = image.copyCellBlockOrDie();
      U8C stride = cb.mCellStride;
      U16C c = DG::getChipOrigin(bhc); 
      U16C o = c + DG::getTLBIOrigin(tlbi,stride); 
      
      const T6GridInfo & t6gi = getT6GridInfoFor(DG::Coord(o.x+13,o.y+11)); // not origin for test
      Eprintf("loadMaps(BH#%u,%u,%s) co(%u,%u) -> %u@0x%08x\n",
              bhc,
              tlbi,
              getNameFromImageCode((ImageCode) ic),
              t6gi.mT6GridOrigin.x,t6gi.mT6GridOrigin.y,
              t6gi.mTLBI, t6gi.mT6GridL1Base
              );
    }
      /*      
    XXX WRITE ME
      call getT6GridInfoFor
      for t6grid [0,0] of each hub
    have it populate both maps
      */
  }

  const T6GridInfo & QuietBox::getT6GridInfoFor(const DG::Coord to) {
    const U16C t6siz = DG::getSingleT6GridBaseSize();
    const T6GridIndex t6grididx = T6GridIndex(to.x / t6siz.x, to.y / t6siz.y);
    const DG::Coord t6origin = DG::Coord(t6grididx.x * t6siz.x - 4, t6grididx.y * t6siz.y - 4); // HACK4XXX
    auto item = mT6GridInfoByCoordMap.find(t6grididx);
    if (item != mT6GridInfoByCoordMap.end()) return item->second; // Hit!

    // Need to do the lookup
    T6GridInfo value;
    
    do { // so we can break

      value.mT6GridOrigin = t6origin; // begin populating
      value.mDGAddress = DG::mapCoordToAddress(t6origin); // store t6grid origin, not 'to' directly
      if (!value.mDGAddress.isValid()) break;

      Blackhole * bhp = getBlackholeIfPresent(value.mDGAddress.mChipNum);
      if (!bhp) { value.mDGAddress.mValidAddr = false; break; }
      
      NSIM nsim;
      Blackhole & bh = *bhp;
      ImageManager & im = nsim.g();
      Layout & layout = nsim.getActiveLayout();
      std::string cellname = layout.getCellnameForChip(value.mDGAddress.mChipNum);
      Cell * cellp = im.getCells().getItemIfAny(cellname);
      MFM_API_ASSERT_NONNULL(cellp);
      CellBlock cb;
      if (!cellp->configureCellBlock(im,cb)) FAIL(INCOMPLETE_CODE);
      // find first cellp that supports BC_T6GRID
      // die if none
      U8C cellc;
      ImageBlockAddr * ibap = findBCInCell(im,BC_T6GRID,cb,cellc);
      MFM_API_ASSERT_NONNULL(ibap);
      
      // map cellp to (ct6 and then) tlbi
      U8C celloriginct6 = value.mDGAddress.mCellNum * cb.mCellStride;
      U8C gridct6 = celloriginct6 + cellc;
      u32 tlbi = U8C::makeTLBIFromCT6Coord(gridct6);

      // find actual l1 address
      u32 t6gridl1base = ibap->mBlockAddr;

      // load cache
      value.mTLBI = tlbi;
      value.mT6GridL1Base = t6gridl1base;
    } while (0);
    mT6GridInfoByCoordMap[t6origin] = value;
    ChipAndTLBI key(value.mDGAddress.mChipNum,value.mTLBI);
    mT6GridInfoByChipAndTLBIMap[key] = &mT6GridInfoByCoordMap[t6origin];
    return mT6GridInfoByCoordMap[t6origin];
  }

  bool QuietBox::storeP4Atom(const DG::Coord to, const P4Atom atom) {
    DG::Address a = DG::mapCoordToAddress(to);
    if (!a.isValid()) return false;
    Blackhole * bhp = getBlackholeIfPresent(a.mChipNum);
    if (!bhp) return false;
    NSIM nsim;
    Blackhole & bh = *bhp;
    ImageManager & im = nsim.g();
    Layout & layout = nsim.getActiveLayout();
    std::string cellname = layout.getCellnameForChip(a.mChipNum);
    Cell * cellp = im.getCells().getItemIfAny(cellname);
    MFM_API_ASSERT_NONNULL(cellp);
    CellBlock cb;
    if (!cellp->configureCellBlock(im,cb)) FAIL(INCOMPLETE_CODE);
    // find first cellp that supports BC_T6GRID
    // die if none
    U8C cellc;
    ImageBlockAddr * ibap = findBCInCell(im,BC_T6GRID,cb,cellc);
    MFM_API_ASSERT_NONNULL(ibap);

    // map cellp to (ct6 and then) tlbi
    U8C celloriginct6 = a.mCellNum * cb.mCellStride;
    U8C gridct6 = celloriginct6 + cellc;
    u32 tlbi = U8C::makeTLBIFromCT6Coord(gridct6);

    // find actual l1 address
    u32 t6gridl1base = ibap->mBlockAddr;
    u32 atomoffset = T6Grid::getByteOffsetToT6GridAtom(a.mT6GridC);
    u32 destbyteaddr = t6gridl1base + atomoffset;

    // INITIATE THE FUCKING WRITE!
    OurTLBs & ourtlbs = bh.getOurTLBs();
    ourtlbs.writeToWords(tlbi,destbyteaddr, (u32*) &atom, sizeof(P4Atom)/sizeof(u32));
    
    Eprintf("\nstoreP4Atom(%s aka %s, ..) cn:%s -> tlbi(%u) [0x%08x+%u]\n",
            DG::to_string(to).c_str(),
            DG::to_string(a).c_str(),
            cellname.c_str(),
            tlbi,
            t6gridl1base,
            atomoffset
            );
    return ibap!=0;
  }
}
