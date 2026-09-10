#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "UxC.h" // for U16C
#include <string_view>

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/numpy.h>
namespace py = pybind11;

namespace MFM {
  template <u32 VAL>
  struct digitshelper { enum { result = 1u + digitshelper<VAL/10u>::result }; };

  template <>
  struct digitshelper<0> { enum { result = 0u }; };

  struct RGBPix {
    RGBPix() { mRGB[0] = mRGB[1] = mRGB[2] = 0u; }
    RGBPix(u8 r, u8 g, u8 b) { set(r,g,b); }
    u8 mRGB[3];
    u32 get() const { return (((mRGB[0]<<8)|mRGB[1])<<8)|mRGB[2]; }
    void set(u8 r, u8 g, u8 b) { mRGB[0] = r; mRGB[1] = g; mRGB[2] = b; }
    std::string to_repr() const {
      return std::string("<RGBPix")
        + " r=" + std::to_string(mRGB[0])
        + " g=" + std::to_string(mRGB[1])
        + " b=" + std::to_string(mRGB[2])
        + ">";
    }
  };

  template <u32 PIXWID,u32 PIXHGT>
  struct BGRImage {
    BGRImage() ;
    u8 mRaster[PIXHGT][PIXWID][3];

    U16C gridSize() const {
      U16C ret;
      ret.x = PIXWID;
      ret.y = PIXHGT;
      return ret;
    }

    RGBPix getPixel(U16C coord) const {
      RGBPix ret;
      if (coord.x < PIXWID && coord.y < PIXHGT) {
        ret.mRGB[0] = mRaster[coord.y][coord.x][2]; // convert from
        ret.mRGB[1] = mRaster[coord.y][coord.x][1]; 
        ret.mRGB[2] = mRaster[coord.y][coord.x][0]; // bgr to rgb
      }
      return ret;
    }
    void setPixel(U16C coord, RGBPix rgb) {
      if (coord.x < PIXWID && coord.y < PIXHGT) {
        mRaster[coord.y][coord.x][0] = rgb.mRGB[2]; // convert to
        mRaster[coord.y][coord.x][1] = rgb.mRGB[1]; // bgr24 in
        mRaster[coord.y][coord.x][2] = rgb.mRGB[0]; // the raster
      }
    }
    void drawCross(U16C coord, u8 size, RGBPix rgb) {
      for (s8 i = -size; i <= size; ++i) {
        s32 x = coord.x+i;
        s32 y = coord.y+i;
        if (x >= 0 && y >= 0) {
          setPixel(U16C((u16) x,coord.y),rgb);
          setPixel(U16C(coord.x,y),rgb);
        }
      }
    }

    void reset() ;

    std::string_view asSV() const {
      //      return std::string_view((const char *) &mHeader[0],sizeof(mHeader)+sizeof(mRaster)); 
      return std::string_view((const char *) &mRaster[0],sizeof(mRaster)); // Just send the raster for rawvideo 
    }

    static void pybindings(py::module & m) {

      py::class_<RGBPix> rgb(m,"RGBPix");
      rgb.def(py::init<>());
      rgb.def(py::init<u8,u8,u8>());
      rgb.def("get",&RGBPix::get,py::call_guard<py::gil_scoped_release>());
      rgb.def("__repr__",&RGBPix::to_repr);

      py::class_<BGRImage<1920,1080>> bgr(m,"BGRImageHD");
      bgr.def(py::init<>());
      bgr.def("reset", &BGRImage<1920,1080>::reset,py::call_guard<py::gil_scoped_release>());
      bgr.def("asSV", [](const BGRImage<1920,1080> &img) { return py::bytes(img.asSV()); },py::call_guard<py::gil_scoped_release>());
      bgr.def("asNP", [](BGRImage<1920,1080> &img) {
        std::vector<ssize_t> shape = {1080,1920,3};
        std::vector<ssize_t> strides = {1920*3,3,1};
        py::buffer_info binf(
            (uint8_t*) img.mRaster,
            sizeof(uint8_t),
            py::format_descriptor<uint8_t>::format(),
            3,
            shape,
            strides);
        return py::array_t<uint8_t*>(binf);
      },py::return_value_policy::reference_internal);
      bgr.def("gridSize", &BGRImage<1920,1080>::gridSize,py::call_guard<py::gil_scoped_release>());
      bgr.def("setPixel", &BGRImage<1920,1080>::setPixel,py::call_guard<py::gil_scoped_release>());
      bgr.def("getPixel", &BGRImage<1920,1080>::getPixel,py::call_guard<py::gil_scoped_release>());
    }
  };

  typedef BGRImage<1920,1080> BGRImageHD;

}

#include "BGRImage.tcc"


  
