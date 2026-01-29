#include "ImageManager.h"
#include "BlackHole.h"

namespace MFM {
  ImageManager & ImageManager::getTheImageManager() {
    static ImageManager theInstance;
    return theInstance;
  }

  bool ImageManager::deployTo(BlackHole & bh) {
    u32 bhc = bh.getCardNumber();

    Layout & l = getLayouts().getItem(mActiveLayout); // or bang
    Eprintf("IM: Deploying layout '%s' to BH#%u\n",
                   l.getName().c_str(),
                   bhc);

    if (!l.layoutAppliesToBHC(bhc)) {
      Eprintf("IM: BH#%u not used in layout '%s'; skipping\n",
                     bhc,
                     l.getName().c_str());
      return false; // not our monkey
    }
    
    std::string defimage = l.getDefaultImage();
    Eprintf("IM: Default image is '%s'\n",
                   defimage.c_str());

    // XXX MULTICAST TO THE FLEET
    T6Image * t6i = getT6ImageIfAny(defimage);
    if (!t6i) {
      Eprintf("IM: Error: No image found named '%s'\n",
              defimage.c_str());
      return false;
    }
    
    bh.deployRISCVCodeFromImage(*t6i,U8_MAX); // multicast away!

    std::string theCellName = l.mCellForBHCard[bhc];

    if (theCellName.size() == 0u) {
      Eprintf("IM: Error: No cell found for BH%u in layout '%s'[\n%s\n]\n",
              bhc, l.getName().c_str(), l.to_string().c_str());
      return false;
    }

    Eprintf("IM: Tiling BH#%u with cell '%s'\n",bhc,theCellName.c_str());
    
    // for each tlbi:
    // - find 'CT6' coord
    // - access cell by stride
    // - find target image
    // - compare to defimage
    // - if different, unicast img to tlbi

    Cell * cellp = getCells().getItemIfAny(theCellName);
    if (!cellp) {
      Eprintf("IM: Error: No cell named '%s' found\n",theCellName.c_str());
      return false;
    }

    const Cell & theCell = *cellp;
    U8C cs = theCell.getSize();
    if (cs.x == 0u || cs.y == 0u) {
      Eprintf("IM: Error: Cell '%s' has zero area (%u,%u)\n",
              theCellName.c_str(), cs.x,cs.y);
      return false;
    }
    // let's precheck all the cell entries
    {
      bool bad = false;
      for (u32 x = 0u; x < cs.x; ++x) {
        for (u32 y = 0u; y < cs.y; ++y) {
          std::string ikey = theCell.getXY(x,y);
          if (!getT6ImageIfAny(ikey)) {
            Eprintf("IM: Error: Position (%u,%u) has unknown image '%s'\n",
                    x, y, ikey.c_str());
            bad = true;
          }
        }
      }
      if (bad) return false;
    }

    u32 tot = 0u;
    u32 overrides = 0u;
    for (u32 tlbi = OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
         tlbi <= OurTLBs::AHAX_TLBI_L1_LAST_UNI;
         ++tlbi) {
      ++tot;
      U8C c = U8C::makeCT6CoordFromTLBI(tlbi);
      U8C cc(c % cs);
      T6Image & t6i = getT6Image(theCell.getXY(cc.x,cc.y));
      if (t6i.getName() == defimage) continue;
      ++overrides;
      
      bh.deployRISCVCodeFromImage(t6i,tlbi); // narrowcast
    }
    Eprintf("Overrode %u of %u with new orders\n",overrides,tot);
    return true;
  }

  T6Image& ImageManager::makeT6Image(std::string ikey, u8 imageCode, std::string path) {
    T6ImageMap & map = getT6Images();
    T6Image & ret = map.makeItem(ikey); //< bang if exists
    ret.init(ikey,imageCode,path);
    return ret;
  }

  T6Image* ImageManager::getT6ImageIfAny(std::string ikey) {
    return getT6Images().getItemIfAny(ikey);
  }

  T6Image& ImageManager::getT6Image(std::string ikey) {
    return getT6Images().getItem(ikey);
  }
}
