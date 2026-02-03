#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "U16C.h"
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
  struct PPMImage {
    PPMImage() ;
    typedef digitshelper<PIXWID> DWid;
    typedef digitshelper<PIXHGT> DHei;
    static constexpr u32 DWID = DWid::result;
    static constexpr u32 DHEI = DHei::result;

    u8 mHeader[3                // "P6\n"
               +DWID+1+DHEI+1   // + width + ' ' + height + '\n'
               +4];             // + "255\n" (with NO room for null byte!)
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
        for (u32 i = 0u; i < 3u; ++i) {
          ret.mRGB[i] = mRaster[coord.y][coord.x][i];
        }
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

      py::class_<PPMImage<1920,1080>> ppm(m,"PPMImageHD");
      ppm.def(py::init<>());
      ppm.def("reset", &PPMImage<1920,1080>::reset,py::call_guard<py::gil_scoped_release>());
      ppm.def("asSV", [](const PPMImage<1920,1080> &ppm) { return py::bytes(ppm.asSV()); },py::call_guard<py::gil_scoped_release>());
      ppm.def("asNP", [](PPMImage<1920,1080> &ppm) {
        std::vector<ssize_t> shape = {1080,1920,3};
        std::vector<ssize_t> strides = {1920*3,3,1};
        py::buffer_info binf(
            (uint8_t*) ppm.mRaster,
            sizeof(uint8_t),
            py::format_descriptor<uint8_t>::format(),
            3,
            shape,
            strides);
        return py::array_t<uint8_t*>(binf);
      },py::return_value_policy::reference_internal);
      ppm.def("gridSize", &PPMImage<1920,1080>::gridSize,py::call_guard<py::gil_scoped_release>());
      ppm.def("setPixel", &PPMImage<1920,1080>::setPixel,py::call_guard<py::gil_scoped_release>());
      ppm.def("getPixel", &PPMImage<1920,1080>::getPixel,py::call_guard<py::gil_scoped_release>());
    }
  };

  typedef PPMImage<1920,1080> PPMImageHD;

}

#include "PPMImage.tcc"


  
