#include "ImageManager.h"
#include "Blackhole.h"

namespace MFM {
  ImageManager & ImageManager::getTheImageManager() {
    static ImageManager theInstance;
    return theInstance;
  }

  bool ImageManager::runtimeConfigureT6Image(T6Image& t6i, Blackhole & bh) {
    u32 bhc = bh.getChipNumber();
    Layout & l = getLayouts().getItem(mActiveLayout); // or bang
    bool ret = true;

    {
      // CONFIGURE CELLBLOCK
      CellBlock * cbp = t6i.getCellBlockInBinFileIfAny();
      if (!cbp) { Eprintf("NO CELLBLOCK\n"); return false; }
      Eprintf("CellBlock: %s\n",cbp->reportCellBlock().c_str());

      std::string cellname = l.mCellForBHChip[bhc];
      Eprintf("BH#%u, Cell: %s\n", bhc, cellname.c_str());

      Cell & thecell = getCells().getItem(cellname);
      CellBlock tcb;
      bool cbb = thecell.configureCellBlock(*this, tcb);
      bool cfgd = t6i.configureCellBlockIfNeeded(tcb);
      Eprintf("Cell: %s cfgs b%d c%d\n",
              tcb.reportCellBlock().c_str(),
              cbb, cfgd);
      ret &= cfgd;
    }

    {
      // CONFIGURE IMAGEBLOCKADDRS FOR HOSTCOMMS
      // <moved to blackhole>
      // <moved back here>
      ret &= bh.configureT6ImageForHostComms(t6i);
    }
    return ret;
  }

  bool ImageManager::deployTo(Blackhole & bh) {
    u32 bhc = bh.getChipNumber();

    Layout & l = getLayouts().getItem(mActiveLayout); // or bang
    Eprintf("IM: Deploying layout '%s' to BH#%u\n",
                   l.getName().c_str(),
                   bhc);

    bh.setHostMemoryBaseAddress(); // based on already-configured HostCommsMap

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
    Eprintf("Default ImageBlock: %s\n",t6i->reportImageBlock().c_str());

    runtimeConfigureT6Image(*t6i,bh); // configure default image for bhchip and comms

    bh.deployRISCVCodeFromImage(*t6i,U8_MAX); // multicast away!

    std::string theCellName = l.mCellForBHChip[bhc];

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
      
      runtimeConfigureT6Image(t6i,bh); // configure override image for bh and comms

      bh.deployRISCVCodeFromImage(t6i,tlbi); // narrowcast
    }
    Eprintf("Overrode %u of %u with new orders\n",overrides,tot);

    /// PROBABLY DOESN'T BELONG HERE BUT TOMORROW IS T2SDAY
    {
      QuietBox & qb = QuietBox::get();
      qb.loadMaps(*this,bhc);
    }
    
    return true;
  }

  CommsModule& ImageManager::makeCommsModule(std::string ikey) {
    CommsModuleMap & map = getCommsModules();
    CommsModule & ret = map.makeItem(ikey); //< bang if exists
    ret.init(ikey);
    Eprintf("HOSTOMODULO: %s\n",ret.getName().c_str());
    return ret;
  }

  CommsModule* ImageManager::getCommsModuleIfAny(std::string ikey) {
    return getCommsModules().getItemIfAny(ikey);
  }

  CommsModule& ImageManager::getCommsModule(std::string ikey) {
    return getCommsModules().getItem(ikey);
  }

  T6Image& ImageManager::makeT6Image(std::string ikey, u8 imageCode, std::string path) {
    T6ImageMap & map = getT6Images();
    T6Image & ret = map.makeItem(ikey); //< bang if exists
    ret.init(ikey,imageCode,path);
    mIKeyFromImageCode[imageCode] = ikey;
    Eprintf("IMAGEBLOCKO: %s\n",ret.reportImageBlock().c_str());

    return ret;
  }

  T6Image* ImageManager::getT6ImageIfAny(std::string ikey) {
    return getT6Images().getItemIfAny(ikey);
  }

  T6Image& ImageManager::getT6Image(std::string ikey) {
    return getT6Images().getItem(ikey);
  }

  bool Cell::configureCellBlock(ImageManager & im, CellBlock & cb) const {
    cb.init();
    cb.mCellType = this->mCellType;
    cb.setCellSize(this->mSize);
    cb.setLayoutOffset({0,0}); // layout offset NYI
    cb.setCellStride(cb.mCellSize); // customized stride NYI
    for (u8 y = 0u; y < mSize.y; ++y) {
      for (u8 x = 0u; x < mSize.x; ++x) {
        std::string imgname = getXY(x,y);
        T6Image *pt6 = im.getT6ImageIfAny(imgname);
        if (pt6)
          cb.setCellPos({x,y},pt6->getImageCode());
        else if (imgname != std::string(NO_IMAGE)) return false;
      }
    }
    cb.setChecksum();
    return true;
  }

#if 0
  void HostRAMConfig::reset() {
    memset_s(mHostRAMChunkOffsets,U8_MAX,sizeof(mHostRAMChunkOffsets));
    mHostRAMChunkOffsets[0] = 0u;
  }

  void HostRAMConfig::configure(CommsModule & hm) {
    
  }
#endif
  
}
