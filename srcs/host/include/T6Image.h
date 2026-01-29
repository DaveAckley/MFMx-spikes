#pragma once   /* -*- C++ -*- */
#include "itype.h"
#include "ImageBlock.h"
#include "HostUtils.h"

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
    const char * getTheBinFile() const { return mTheBinFile.get(); }
    const u32 * getBinFileWords() const { return (u32 *) getTheBinFile(); }

    u32 getHostBlockAddr() const { return mHostBlockAddr; }
    ImageBlockHeader getImageBlockHeader() const {
      ImageBlockHeader ibh;
      ibh.init(&getBinFileWords()[5]);
      return ibh;
    }

    u8 getImageCode() const {
      ImageBlockHeader ibh = getImageBlockHeader();
      return ibh.mImageCode;
    }

    ~T6Image() { }

    std::string to_string() const {
      std::string ret = "<T6Image:";
      ret.append(mIKey);
      ret.append(">");
      return ret;
    }

    static std::string iba_to_string(ImageBlockAddr & iba) {
      if (iba.goodMagic())
        return "<ImageBlockAddr:type="+std::to_string(iba.getBlockType())
          +",len="+std::to_string(iba.getArrayLength())
          +",addr=0x"+ toHex(iba.getBlockAddr())
          +">";
      return "<ImageBlockAddr:invalid,"+std::to_string(iba.mIBAMagic)
        +","+std::to_string(iba.getBlockType())
        +","+std::to_string(iba.getArrayLength())
        +",0x"+ toHex(iba.getBlockAddr())
        +">";

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
      iba.def("getBlockType", &ImageBlockAddr::getBlockType,py::call_guard<py::gil_scoped_release>());
      iba.def("getBlockAddr", &ImageBlockAddr::getBlockAddr, py::call_guard<py::gil_scoped_release>());
      iba.def("getArrayLength", &ImageBlockAddr::getArrayLength, py::call_guard<py::gil_scoped_release>());

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
      t6.def("getImageBlockHeader", &T6Image::getImageBlockHeader,py::call_guard<py::gil_scoped_release>());
      t6.def("getImageCode", &T6Image::getImageCode,py::call_guard<py::gil_scoped_release>());


      iba.def("__repr__",[](ImageBlockAddr& iba) { return T6Image::iba_to_string(iba); },
              py::call_guard<py::gil_scoped_release>());
      
    }
  };
}
