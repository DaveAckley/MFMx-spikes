#pragma once  /* -*- C++ -*- */
#include <unordered_map>
#include <vector>

#include "itype.h"
#include "U8C.h"
#include "T6Image.h"
#include "HostUtils.h"
//#include "BHTag.h"

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
namespace py = pybind11;

namespace MFM {

  template <class ITEM> 
  struct ItemFactory {
    std::string to_string() const {
      std::string ret = "<";
      ret.append(demangleCpp(typeid(ITEM).name()));
      ret.append(" ");
      ret.append(items_to_string());
      ret.append("> ");
      return ret;
    }

    typedef std::unordered_map<std::string,ITEM> Items;
    Items mItemMap;
    std::string items_to_string() const {
      std::string ret;
      for (const auto& [ key, value ] : mItemMap) {
        ret.append(key);
        ret.append(":");
        ret.append(value.to_string());
        ret.append(" ");
      }
      return ret;
    }

    u32 size() const { return (u32) mItemMap.size(); }
    bool definedKey(std::string name) {
      return mItemMap.find(name) != mItemMap.end();
    }
    ITEM& makeItem(std::string newname) {
      MFM_API_ASSERT(!definedKey(newname),DUPLICATE_ENTRY);
      mItemMap.try_emplace(newname); // oy modrun
      return mItemMap[newname];
    }
    ITEM* getItemIfAny(std::string name) {
      if (!definedKey(name)) return 0;
      return &getItem(name);
    }
    ITEM& getItem(std::string name) {
      MFM_API_ASSERT(definedKey(name),UNKNOWN_ELEMENT);
      return mItemMap[name];
    }
  };

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
      char buf[1024];
      char *cur = buf, *end = buf + sizeof(buf);
      cur += snprintf(cur,end - cur,"<Layout:%s [",
                      mName.c_str());
      u32 count = 0u;
      for (u32 i = 0u; i < MAXBHCARDS; ++i) {
        if (!(mCardFlags&(1u<<i))) continue;
        if (count) cur += snprintf(cur, end-cur, " ");
        cur += snprintf(cur, end-cur, "%u:%s",
                        i,mCellForBHCard[i].c_str());
        ++count;
      }
      cur += snprintf(cur,end - cur,"]>");
      return std::string(buf);
    }
  };

  struct Cell {
    std::string mName;
    U8C mSize;
    typedef std::vector<std::string> ImageVec; // 2D->imagename
    typedef std::vector<ImageVec> ImageGrid;
    ImageGrid mImageGrid;

    std::string getXY(u32 x, u32 y) const {
      MFM_API_ASSERT(x < mSize.x && y < mSize.y, ILLEGAL_ARGUMENT);
      return mImageGrid[y][x];
    }

    void setXY(u32 x, u32 y, std::string v) {
      MFM_API_ASSERT(x < mSize.x && y < mSize.y, ILLEGAL_ARGUMENT);
      mImageGrid[y][x] = v;
    }

    U8C getSize() const { return mSize; }
    
    void init(std::string name, U8C size) {
      mName = name;
      mSize = size;
      mImageGrid = ImageGrid(mSize.y,ImageVec(mSize.x,"."));
    }

    void addImage(U8C c, std::string imagename) {
      u32 minx, maxx;
      if (c.x == U8_MAX) { minx = 0u; maxx = mSize.x; }
      else { minx = c.x; maxx = c.x + 1u; }
      u32 miny, maxy;
      if (c.y == U8_MAX) { miny = 0u; maxy = mSize.y; }
      else { miny = c.y; maxy = c.y + 1u; }

      for (u32 x = minx; x < maxx; ++x)
        for (u32 y = miny; y < maxy; ++y)
          setXY(x,y,imagename);
    }

    std::string to_string() const { return toString(); }
    std::string to_repr() const {
      return "<Cell:" + toString() + ">";
    }

    std::string toString() const {
      std::string ret = mName;
      ret.append("[");
      for (u32 y = 0u; y < mSize.y; ++y) {
        for (u32 x = 0u; x < mSize.x; ++x) {
          if (x) ret.append(" ");
          std::string ikey = getXY(x,y);
          if (ikey.size() == 0u) ret.append("-");
          else ret.append(ikey);
        }
        ret.append("\n");
      }
      ret.append("]");
      return ret;
    }
  };

  struct BlackHole; // FORWARD

  struct ImageManager {
    static ImageManager& getTheImageManager() ;

    bool deployTo(BlackHole & bh) ; //< using the configured active layout..

    T6Image& makeT6Image(std::string ikey, u8 imageCode, std::string path) ;
    T6Image& getT6Image(std::string ikey) ;
    T6Image* getT6ImageIfAny(std::string ikey) ;

    typedef ItemFactory<T6Image> T6ImageMap;
    T6ImageMap mT6ImageMap;     //< std::string -> T6Image&
    T6ImageMap & getT6Images() { return mT6ImageMap; } 

    typedef ItemFactory<Cell> CellMap;
    CellMap mCellMap;           //< std::string -> Cell&
    CellMap & getCells() { return mCellMap; } 

    typedef ItemFactory<Layout> LayoutMap;
    LayoutMap mLayoutMap;       //< std::string -> Layout&
    LayoutMap & getLayouts() { return mLayoutMap; } 

    std::string mActiveLayout;
    void setActiveLayout(std::string layoutname) {
      if (!getLayouts().definedKey(layoutname))
        FAIL(ILLEGAL_ARGUMENT);
      mActiveLayout = layoutname;
    }
    std::string getActiveLayout() const { return mActiveLayout; }

    /*
    typedef std::unique_ptr<T6Image> T6ImagePtr;
    typedef std::vector<T6ImagePtr> ImageVector;
    ImageVector mImageVector;
    */

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
      {
        py::class_<Cell> c(m,"Cell");
        c.def("addImage", &Cell::addImage,py::call_guard<py::gil_scoped_release>());
        c.def("toString", &Cell::toString,py::call_guard<py::gil_scoped_release>());
        c.def("__repr__", &Cell::to_repr,py::call_guard<py::gil_scoped_release>());
      }
    }
  };

  struct NSIM { /// urgh non-singleton ImageManager
    NSIM() { }

    ImageManager & g() { return ImageManager::getTheImageManager(); }
    const ImageManager & g() const { return ImageManager::getTheImageManager(); }

    T6Image& makeT6Image(std::string ikey, u8 imageCode, std::string path) {
      return g().makeT6Image(ikey,imageCode,path);
    }
    T6Image& getT6Image(std::string ikey) { return g().getT6Image(ikey); }
    Layout& makeLayout(std::string name, std::string defaultimage) {
      Layout& ret = g().getLayouts().makeItem(name);
      ret.init(name,defaultimage);
      return ret;
    }
    Layout& setActiveLayout(std::string name) {
      g().setActiveLayout(name);
      return g().getLayouts().getItem(name);
    }

    Layout& getActiveLayout() {
      std::string name = g().getActiveLayout();
      return g().getLayouts().getItem(name);
    }

    Cell& makeCell(std::string name, U8C size) {
      Cell& ret = g().getCells().makeItem(name);
      ret.init(name,size);
      return ret;
    }

    Cell& getCell(std::string name) {
      return g().getCells().getItem(name);
    }

    std::string to_repr() {
      std::string ret = "<ImageManager ";
      ImageManager & im = g();
      ret.append(im.getT6Images().to_string());
      ret.append(im.getCells().to_string());
      ret.append(im.getLayouts().to_string());
      ret.append(">");
      return ret;
    }

    static void pybindings(py::module & m) {
      {
        py::class_<NSIM> n(m,"ImageManager");
        n.def(py::init<>());
        n.def("getActiveLayout", &NSIM::getActiveLayout,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("setActiveLayout", &NSIM::setActiveLayout,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("makeT6Image", &NSIM::makeT6Image,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("makeLayout", &NSIM::makeLayout,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("makeCell", &NSIM::makeCell,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("getCell", &NSIM::getCell,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("getT6Image",py::overload_cast<std::string>(&NSIM::getT6Image),
              py::call_guard<py::gil_scoped_release>(),py::return_value_policy::reference);
        //        n.def("getT6Image",py::overload_cast<u32>(&NSIM::getT6Image),
        //                 py::call_guard<py::gil_scoped_release>(),py::return_value_policy::reference);

        n.def("__repr__",&NSIM::to_repr, py::call_guard<py::gil_scoped_release>());
      }
    }
  };

}
