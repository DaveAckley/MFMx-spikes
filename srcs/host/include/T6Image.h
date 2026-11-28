#pragma once   /* -*- C++ -*- */
#include "itype.h"
#include "ImageBlock.h"
#include "CellBlock.h"
#include "HostUtils.h"
#include "BlockCode.h"
#include "PHASER.h"
#include "DefinedConstants.h"
#include "PT_InterHub.h"   // for InterHubStorage car size/count (PKTS.txt)
#include "dev_mem_map.h"    // MEM_L1_SIZE (L1_IMAGE_LIMIT)

#include <tuple>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
namespace py = pybind11;

namespace MFM {
  class T6Image {
  public:
    T6Image() { }
    //T6Image(std::string ikey, u8 imageCode, std::string path) { init(ikey, imageCode, path); }
    void init(std::string ikey, u8 imageCode, std::string path) ;
    void loadBinFile() ;
    std::string getName() const { return mIKey; }
    std::string getBinFilePath() const { return mBinFilePath; }
    std::string to_repr () const ;

    u32 getBinFileSize() const { return mBinFileSize; }
    const char * getPointerIntoTheBinFile(u32 byteaddr) const {
      return getTheBinFile() + byteaddr;
    }
    const char * getTheBinFile() const { return mTheBinFile.get(); }
    const u32 * getBinFileWords() const { return (u32 *) getTheBinFile(); }

    u32 getHostBlockAddr() const { return mHostBlockAddr; }

    //Tue Aug 18 09:26:46 2026 WAS:    static constexpr u32 WORD_OF_PTR_TO_IMAGE_BLOCK_HEADER = 6;
    static constexpr u32 WORD_OF_PTR_TO_IMAGE_BLOCK_HEADER = 44; // aka 0xb0>>2 : pushed to make room for 64B PHASER

    const ImageBlockHeader & getImageBlockHeader() const {
      MFM_API_ASSERT_NONNULL(mTheBinFile);
      const u32 * pibh = &getBinFileWords()[WORD_OF_PTR_TO_IMAGE_BLOCK_HEADER];
      return *(const ImageBlockHeader*) pibh;
    }

    std::string reportImageBlock() const {
      std::string ret = getName() + ":ImageBlock";
      ImageBlockHeader ibh = getImageBlockHeader();
      if (!ibh.isValid()) return ret + "=INVALID";
      u32 ec = ibh.getEntriesCount();
      ret += "[ic="+std::to_string(ibh.getImageCode());
      ret += ",ec="+std::to_string(ec);
      ret += "]";
      for (u32 e = 0u; e < ec; ++e) {
        const ImageBlockAddr *piba = getConstImageBlockAddrAtIndexIfAny(e);
        if (!piba) FAIL(ILLEGAL_STATE);
        ret += "\n "+std::to_string(e) + ": " + iba_to_string(*piba);
      }
      return ret;
    }

    ImageBlockAddr * getImageBlockAddrForBlockCodeIfAny(BlockCode blockcode) const {
      ImageBlockHeader ibh = getImageBlockHeader();
      if (!ibh.isValid()) return 0;
      u32 ec = ibh.getEntriesCount();
      for (u32 e = 0u; e < ec; ++e) {
        ImageBlockAddr * iba = getImageBlockAddrAtIndexIfAny(e);
        if (!iba || !iba->isValid()) continue;
        if (iba->getBlockCode() == blockcode)
          return iba;
      }
      return 0;
    }

    const ImageBlockAddr * getConstImageBlockAddrAtIndexIfAny(u32 idx) const {
      const ImageBlockHeader ibh = getImageBlockHeader();
      if (!ibh.isValid()) return 0;
      if (idx >= ibh.getEntriesCount()) return 0;
      const u32 * ibaw = &getBinFileWords()[WORD_OF_PTR_TO_IMAGE_BLOCK_HEADER+1+2*idx];
      return (const ImageBlockAddr*) ibaw;
    }

    ImageBlockAddr * getImageBlockAddrAtIndexIfAny(u32 idx) const {
      const ImageBlockAddr *iba = getConstImageBlockAddrAtIndexIfAny(idx);
      return (ImageBlockAddr*) iba;
    }

    u32 getBinWord(u32 wordAddr) const {
      if (wordAddr*4 >= getBinFileSize()) return U8_MAX;
      return getBinFileWords()[wordAddr];
    }

#if 0
    u8 getImageCode() const {
      ImageBlockHeader ibh = getImageBlockHeader();
      return ibh.mImageCode;
    }
#else
    u8 getImageCode() const {
      return mImageCode;
    }
#endif

    ~T6Image() { }

    std::string to_string() const {
      std::string ret = "<T6Image:";
      ret.append(mIKey);
      ret.append(">");
      return ret;
    }

    static std::string iba_to_string(const ImageBlockAddr & iba) {
      return iba.to_repr();
    }
    

    /** Car geometry for blockcodes whose storages hold TC packets.
        Returns false if bc carries no packets.  EXTEND HERE when another
        blockcode becomes a packet carrier (see misc/PKTS.txt dump). */
    static bool getCarInfoForBlockCode(BlockCode bc, u32 & carsz, u32 & carcnt) {
      switch (bc) {
      case BC_INTERHUB:
      case BC_CORNERHUB:
        carsz = sizeof(InterHubStorage::CAR_TYPE);
        carcnt = InterHubStorage::CAR_COUNT;
        return true;
      default: return false;
      }
    }

    /** One tuple per packet-carrying block in the image:
        (name, T6 block addr, storage count, storage size, car size, car count) */
    std::vector<std::tuple<std::string,u32,u32,u32,u32,u32>> getPacketBlocks() const {
      std::vector<std::tuple<std::string,u32,u32,u32,u32,u32>> ret;
      ImageBlockHeader ibh = getImageBlockHeader();
      if (!ibh.isValid()) return ret;
      u32 ec = ibh.getEntriesCount();
      for (u32 e = 0u; e < ec; ++e) {
        const ImageBlockAddr * iba = getConstImageBlockAddrAtIndexIfAny(e);
        if (!iba || !iba->isValid()) continue;
        BlockCode bc = (BlockCode) iba->getBlockCode();
        u32 carsz = 0, carcnt = 0;
        if (!getCarInfoForBlockCode(bc, carsz, carcnt)) continue;
        ret.emplace_back(std::string(getNameFromBlockCode(bc)),
                         iba->getBlockAddr(), iba->getStorageCount(),
                         getStorageSizeFromBlockCode(bc), carsz, carcnt);
      }
      return ret;
    }

    u32 getCellBlockBinFileAddrIfAny() const {
      ImageBlockAddr *ibacb = getImageBlockAddrForBlockCodeIfAny(BlockCode::BC_CELLBLOCK);
      if (!ibacb) return 0u;
      return ibacb->mBlockAddr;
    }

    bool hasCellBlock() const {
      return 0!=getImageBlockAddrForBlockCodeIfAny(BlockCode::BC_CELLBLOCK);
    }

    CellBlock copyCellBlockOrDie() const {
      u32 cpa = getCellBlockBinFileAddrIfAny();
      MFM_API_ASSERT(cpa!=0,NOT_FOUND);
      return *(CellBlock*) getPointerIntoTheBinFile(cpa);
    }

    CellBlock * getCellBlockInBinFileIfAny() {
      u32 cpa = getCellBlockBinFileAddrIfAny();
      if (cpa == 0) return 0; // no cellblock in binfile
      return (CellBlock*) getPointerIntoTheBinFile(cpa);
    }

    PhaserBlock & getPhaserBlockInBinFile() {
      return *(PhaserBlock*) getPointerIntoTheBinFile(T6_PHASER_ADDR);
    }

    u32 getAddressInBinFileIfAny(void * hostaddr) {
      const char * b = getTheBinFile();
      u32 bs = getBinFileSize();
      u32 offset = ((const char *) hostaddr) - b;
      if (offset >= bs) offset = U32_MAX;
      return offset;
    }

    bool configureCellBlockIfNeeded(const CellBlock & cb) {
      MFM_API_ASSERT(cb.isValid(),ILLEGAL_ARGUMENT);
      CellBlock * rcp = getCellBlockInBinFileIfAny();
      if (!rcp) return false;   // no cell block to configure
      if (rcp->isValid() && rcp->getCellType() == cb.getCellType())
        return false;           // already matches cb
      *rcp = cb;                // now matches cb
      Eprintf("CFCBIN: @0x%x %s\n",getAddressInBinFileIfAny(rcp),rcp->reportCellBlock().c_str());
      return true;
    }
    
    bool configurePHASER(const PhaserBlock & pb) {
      PhaserBlock & pbp = getPhaserBlockInBinFile();
      pbp = pb;                // now matches pb
      return true;
    }

    /* Loadable physics (2026-11-28; BytePhys.h 'BCP', srcs/host/python/
       EWD/physics/README.md).  An image whose layout config says
       `physics = true` gets, written to L1 right after it, a BCP blob
       (attachPhysics) or an empty marker (attachNoPhysics: four zero
       words, so a blob left in L1 by an earlier run is never taken).
       The place is the end of the bin file rounded up to 16, which is
       where the image finds it (__hostblock_end, rounded the same way).
       The bin file and every HostBlock address are unchanged. */
    std::string attachPhysics(std::string path) ;  //< "" or why not
    void attachNoPhysics() ;
    bool takesPhysics() const { return mPhysicsWords.size() > 0u; }
    u32 getPhysicsAddr() const { return physicsAddr(); }
    const std::vector<u32> & getPhysicsWords() const {
      return mPhysicsWords;
    }
    std::string describePhysics() const ;

    /* The end of the L1 an image may use: _BUD.ld.in's MEMORY L1
       (three quarters of L1).  The image finds the same limit as the
       linker symbol __l1_image_limit. */
    static constexpr u32 L1_IMAGE_LIMIT = (MEM_L1_SIZE / 4u) * 3u;

  private:
    u32 physicsAddr() const { return ((u32) mBinFileSize + 15u) & ~15u; }
    std::vector<u32> mPhysicsWords; //< what goes at physicsAddr(), or none
    std::string mPhysicsPath;       //< where it came from ("" = marker)

    std::string mIKey;          // likes it
    std::string mBinFilePath;
    std::streamsize mBinFileSize;
    std::unique_ptr<char[]> mTheBinFile;
    u32 mHostBlockAddr;
    u8 mImageCode;
    
  public:
    static void pybindings(py::module & m) {
      py::class_<ImageBlockAddr> iba(m,"ImageBlockAddr");
      iba.def_static("make", &ImageBlockAddr::make,py::call_guard<py::gil_scoped_release>());
      iba.def("init", &ImageBlockAddr::init,py::call_guard<py::gil_scoped_release>());
      iba.def("getBlockCode", &ImageBlockAddr::getBlockCode,py::call_guard<py::gil_scoped_release>());
      iba.def("getBlockAddr", &ImageBlockAddr::getBlockAddr, py::call_guard<py::gil_scoped_release>());
      iba.def("getStorageCount", &ImageBlockAddr::getStorageCount, py::call_guard<py::gil_scoped_release>());

      py::class_<ImageBlockHeader> ibh(m,"ImageBlockHeader");
      ibh.def("isValid", &ImageBlockHeader::isValid,py::call_guard<py::gil_scoped_release>());
      ibh.def("getImageCode", &ImageBlockHeader::getImageCode,py::call_guard<py::gil_scoped_release>());
      ibh.def("getEntriesCount", &ImageBlockHeader::getEntriesCount,py::call_guard<py::gil_scoped_release>());

      py::class_<T6Image> t6(m,"T6Image");
      //t6.def(py::init<std::string,u8,std::string>());
      //t6.def(py::init<std::string,u8,std::string>());
      t6.def_static("iba_to_string",&T6Image::iba_to_string,py::call_guard<py::gil_scoped_release>());
      t6.def("__repr__",&T6Image::to_repr, py::call_guard<py::gil_scoped_release>());
      t6.def("getBinFilePath", &T6Image::getBinFilePath,py::call_guard<py::gil_scoped_release>());
      t6.def("getBinFileSize", &T6Image::getBinFileSize,py::call_guard<py::gil_scoped_release>());
      t6.def("getHostBlockAddr", &T6Image::getHostBlockAddr,py::call_guard<py::gil_scoped_release>());
      t6.def("getBinWord", &T6Image::getBinWord,py::call_guard<py::gil_scoped_release>());
      t6.def("getImageBlockHeader", &T6Image::getImageBlockHeader,py::call_guard<py::gil_scoped_release>());
      t6.def("getImageCode", &T6Image::getImageCode,py::call_guard<py::gil_scoped_release>());
      t6.def("getPacketBlocks", &T6Image::getPacketBlocks,py::call_guard<py::gil_scoped_release>());
      t6.def("attachPhysics", &T6Image::attachPhysics,py::call_guard<py::gil_scoped_release>());
      t6.def("attachNoPhysics", &T6Image::attachNoPhysics,py::call_guard<py::gil_scoped_release>());
      t6.def("takesPhysics", &T6Image::takesPhysics,py::call_guard<py::gil_scoped_release>());
      t6.def("getPhysicsAddr", &T6Image::getPhysicsAddr,py::call_guard<py::gil_scoped_release>());
      t6.def("describePhysics", &T6Image::describePhysics,py::call_guard<py::gil_scoped_release>());


      iba.def("__repr__",[](ImageBlockAddr& iba) { return T6Image::iba_to_string(iba); },
              py::call_guard<py::gil_scoped_release>());
      
    }
  };
}
