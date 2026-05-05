#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include <string>
#include <vector>

#include "Fail.h"

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
namespace py = pybind11;

namespace MFM {
  struct Layout {
    static constexpr u32 MAXBHCHIPS = 4u;
    std::string mName;
    std::string mCellForBHChip[MAXBHCHIPS];
    //std::string mDefaultCell;
    std::string mDefaultImage;
    u8 mChipFlags;              //< flags&(1<<chipnum) => use chipnum

    std::vector<u32> getActiveBHChips() const {
      std::vector<u32> ret;
      for (u32 i = 0u; i < MAXBHCHIPS; ++i) {
        if (mChipFlags&(1u<<i)) {
          ret.push_back(i);
        }
      }
      return ret;
    }

    bool layoutAppliesToBHC(u32 bhchipnum) const {
      return mChipFlags&(1u<<bhchipnum);
    }

    std::string getName() const { return mName; }

    //std::string getDefaultCell() const { return mDefaultCell; }
    
    std::string getDefaultImage() const { return mDefaultImage; }
    
    void init(std::string name, std::string defaultimage) {
      mName = name;
      mChipFlags = 0u;
      mDefaultImage = defaultimage;
    }

    void addBlackholeChip(u8 chip) {
      mChipFlags |= 1u<<chip;
    }

    std::string getCellnameForChip(u8 chip) const {
      MFM_API_ASSERT(chip < MAXBHCHIPS,ILLEGAL_ARGUMENT);
      return mCellForBHChip[chip];
    }

    void addCell(u8 chip, std::string cellname) {
      u32 minchip, maxchip;
      if (chip == U8_MAX) { minchip = 0u; maxchip = MAXBHCHIPS; }
      else if (chip < MAXBHCHIPS) { minchip = chip; maxchip = chip + 1u; }
      else FAIL(ILLEGAL_ARGUMENT);
      
      for (u8 c = minchip; c < maxchip; ++c) {
        mCellForBHChip[c] = cellname; // whether or not mChipFlags?
      }
    }

    std::string to_string() const { return to_repr(); }
    
    std::string to_repr() const {
      std::string ret = "<Layout:";
      ret.append(mName);
      ret.append(" [");
      u32 count = 0u;
      for (u32 i = 0u; i < MAXBHCHIPS; ++i) {
        if (!(mChipFlags&(1u<<i))) continue;
        if (count) ret.append(" ");
        ret.append("bh");
        ret.append(std::to_string(i));
        ret.append(":");
        ret.append(mCellForBHChip[i]);
        ++count;
      }
      ret.append("] ");
      ret.append(">");
      return ret;
    }

    static void pybindings(py::module & m) {
      {
        py::class_<Layout> l(m,"Layout");
        l.def("getActiveBHChips", &Layout::getActiveBHChips,py::call_guard<py::gil_scoped_release>());
        l.def("addCell", &Layout::addCell,py::call_guard<py::gil_scoped_release>());
        l.def("addBlackholeChip", &Layout::addBlackholeChip,py::call_guard<py::gil_scoped_release>());
        l.def("getName", &Layout::getName,py::call_guard<py::gil_scoped_release>());
        l.def("getDefaultImage", &Layout::getDefaultImage,py::call_guard<py::gil_scoped_release>());
        l.def("__repr__", &Layout::to_repr,py::call_guard<py::gil_scoped_release>());
      }
    }
  };
}
