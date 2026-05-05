#pragma once   /* -*- C++ -*- */
#include "itype.h"
#include "ImageBlock.h"
#include "CellBlock.h"
#include "HostUtils.h"
#include "BlockCode.h"

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
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

    const ImageBlockHeader & getImageBlockHeader() const {
      MFM_API_ASSERT_NONNULL(mTheBinFile);
      const u32 * pibh = &getBinFileWords()[5];
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
      const u32 * ibaw = &getBinFileWords()[6+2*idx]; // HARDCODED IMAGEBLOCK ADDRESS
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
    
  private:
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


      iba.def("__repr__",[](ImageBlockAddr& iba) { return T6Image::iba_to_string(iba); },
              py::call_guard<py::gil_scoped_release>());
      
    }
  };
}
