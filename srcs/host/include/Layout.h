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
    static constexpr u32 MAXBHCARDS = 4u;
    std::string mName;
    std::string mCellForBHCard[MAXBHCARDS];
    //std::string mDefaultCell;
    std::string mDefaultImage;
    u8 mCardFlags;              //< flags&(1<<cardnum) => use cardnum

    std::vector<u32> getActiveBHCards() const {
      std::vector<u32> ret;
      for (u32 i = 0u; i < MAXBHCARDS; ++i) {
        if (mCardFlags&(1u<<i)) {
          ret.push_back(i);
        }
      }
      return ret;
    }

    bool layoutAppliesToBHC(u32 bhcardnum) const {
      return mCardFlags&(1u<<bhcardnum);
    }

    std::string getName() const { return mName; }

    //std::string getDefaultCell() const { return mDefaultCell; }
    
    std::string getDefaultImage() const { return mDefaultImage; }
    
    void init(std::string name, std::string defaultimage) {
      mName = name;
      mCardFlags = 0u;
      mDefaultImage = defaultimage;
    }

    void addBlackHoleCard(u8 card) {
      mCardFlags |= 1u<<card;
    }

    void addCell(u8 card, std::string cellname) {
      u32 mincard, maxcard;
      if (card == U8_MAX) { mincard = 0u; maxcard = MAXBHCARDS; }
      else if (card < MAXBHCARDS) { mincard = card; maxcard = card + 1u; }
      else FAIL(ILLEGAL_ARGUMENT);
      
      for (u8 c = mincard; c < maxcard; ++c) {
        mCellForBHCard[c] = cellname; // whether or not mCardFlags?
      }
    }

    std::string to_string() const { return to_repr(); }
    
    std::string to_repr() const {
      std::string ret = "<Layout:";
      ret.append(mName);
      ret.append(" [");
      u32 count = 0u;
      for (u32 i = 0u; i < MAXBHCARDS; ++i) {
        if (!(mCardFlags&(1u<<i))) continue;
        if (count) ret.append(" ");
        ret.append("bh");
        ret.append(std::to_string(i));
        ret.append(":");
        ret.append(mCellForBHCard[i]);
        ++count;
      }
      ret.append("] ");
      ret.append(">");
      return ret;
    }

    static void pybindings(py::module & m) {
      {
        py::class_<Layout> l(m,"Layout");
        l.def("getActiveBHCards", &Layout::getActiveBHCards,py::call_guard<py::gil_scoped_release>());
        l.def("addCell", &Layout::addCell,py::call_guard<py::gil_scoped_release>());
        l.def("addBlackHoleCard", &Layout::addBlackHoleCard,py::call_guard<py::gil_scoped_release>());
        l.def("getName", &Layout::getName,py::call_guard<py::gil_scoped_release>());
        l.def("getDefaultImage", &Layout::getDefaultImage,py::call_guard<py::gil_scoped_release>());
        l.def("__repr__", &Layout::to_repr,py::call_guard<py::gil_scoped_release>());
      }
    }
  };
}
