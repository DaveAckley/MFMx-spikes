#pragma once  /* -*- C++ -*- */
#include <unordered_map>
#include <vector>

#include "itype.h"
#include "UxC.h" // for U8C
#include "T6Image.h"
#include "CellBlock.h"
#include "HostUtils.h"
#include "BlockCode.h"
#include "Layout.h"
#include "HostCommsMap.h"
#include "CommsModule.h"

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
namespace py = pybind11;

namespace MFM {

  struct ImageManager; // FORWARD

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
      MFM_API_ASSERT(definedKey(name),NOT_FOUND);
      return mItemMap[name];
    }
  };

  struct Cell {
    static constexpr const char * NO_IMAGE = ".";
    std::string mName;
    u8 mCellType;               // just serial num from getCells() map
    U8C mSize;
    typedef std::vector<std::string> ImageVec; // 2D->imagename
    typedef std::vector<ImageVec> ImageGrid;
    ImageGrid mImageGrid;

    bool configureCellBlock(ImageManager & im, CellBlock & cb) const ;

    std::string getXY(u32 x, u32 y) const {
      MFM_API_ASSERT(x < mSize.x && y < mSize.y, ILLEGAL_ARGUMENT);
      return mImageGrid[y][x];
    }

    void setXY(u32 x, u32 y, std::string v) {
      MFM_API_ASSERT(x < mSize.x && y < mSize.y, ILLEGAL_ARGUMENT);
      mImageGrid[y][x] = v;
    }

    U8C getSize() const { return mSize; }
    
    void init(std::string name, U8C size, u8 cellType) {
      mName = name;
      mCellType = cellType;
      mSize = size;
      mImageGrid = ImageGrid(mSize.y,ImageVec(mSize.x,std::string(NO_IMAGE)));
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
      ret.append(".");
      ret.append(std::to_string(mCellType));
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

#if 0 // NOT NEEDED, CommsModule IS DOING IT
  struct HostRAMConfig {

    void reset() ;
    void configure(CommsModule & hm) ;
    u8 getHostRAMChunkOffset(BlockCode bc) const {
      MFM_API_ASSERT(bc > 0 && bc < sizeof(mHostRAMChunkOffsets),ILLEGAL_ARGUMENT);
      return mHostRAMChunkOffsets[bc];
    }
  };
#endif

  struct Blackhole; // FORWARD

  struct ImageManager {
    static ImageManager& getTheImageManager() ;

    bool deployTo(Blackhole & bh) ; //< using the configured active layout..
    bool runtimeConfigureT6Image(T6Image& t6i, Blackhole & bh) ; //< ditto + bh

    T6Image& makeT6Image(std::string ikey, u8 imageCode, std::string path) ;
    T6Image& getT6Image(std::string ikey) ;
    T6Image* getT6ImageIfAny(std::string ikey );

    typedef ItemFactory<T6Image> T6ImageMap;
    T6ImageMap mT6ImageMap;     //< std::string -> T6Image&
    T6ImageMap & getT6Images() { return mT6ImageMap; } 

    using IKeyFromImageCode = std::unordered_map<u32,std::string>;
    IKeyFromImageCode mIKeyFromImageCode;
    std::string getIKeyIfAny(u32 ic) {
      if (mIKeyFromImageCode.find(ic) == mIKeyFromImageCode.end())
        return "";
      return mIKeyFromImageCode[ic];
    }

    typedef ItemFactory<Cell> CellMap;
    CellMap mCellMap;           //< std::string -> Cell&
    CellMap & getCells() { return mCellMap; } 

    typedef ItemFactory<Layout> LayoutMap;
    LayoutMap mLayoutMap;       //< std::string -> Layout&
    LayoutMap & getLayouts() { return mLayoutMap; } 

    CommsModule& makeCommsModule(std::string ikey) ;
    CommsModule& getCommsModule(std::string ikey) ;
    CommsModule* getCommsModuleIfAny(std::string ikey) ;

    typedef ItemFactory<CommsModule> CommsModuleMap;
    CommsModuleMap mCommsModuleMap; //< std::string -> CommsModule&
    CommsModuleMap & getCommsModules() { return mCommsModuleMap; }

    std::string mActiveLayout;
    void setActiveLayout(std::string layoutname) {
      if (!getLayouts().definedKey(layoutname))
        FAIL(ILLEGAL_ARGUMENT);
      mActiveLayout = layoutname;
      //configureHostRAM();
    }
    std::string getActiveLayout() const { return mActiveLayout; }

    //void configureHostRAM() ; //< based on CommsModule of active layout

    static void pybindings(py::module & m) {
      {
        py::class_<CommsModule> l(m,"CommsModule");
        l.def("requireCommBlockNamed", &CommsModule::requireCommBlockNamed,py::call_guard<py::gil_scoped_release>());
        l.def("getName", &CommsModule::getName,py::call_guard<py::gil_scoped_release>());
        l.def("__repr__", &CommsModule::to_repr,py::call_guard<py::gil_scoped_release>());
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

    CommsModule& makeCommsModule(std::string ikey) {
      return g().makeCommsModule(ikey);
    }
    CommsModule& getCommsModule(std::string ikey) { return g().getCommsModule(ikey); }

    T6Image& getT6Image(std::string ikey) { return g().getT6Image(ikey); }

    T6Image& makeT6Image(std::string ikey, u8 imageCode, std::string path) {
      return g().makeT6Image(ikey,imageCode,path);
    }

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
      ImageManager::CellMap & cm = g().getCells();
      u32 entries = cm.size();
      MFM_API_ASSERT(entries <= U8_MAX,OUT_OF_RESOURCES);
      Cell& ret = cm.makeItem(name);
      ret.init(name,size,(u8) entries);
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

        n.def("makeCommsModule", &NSIM::makeCommsModule,
              py::call_guard<py::gil_scoped_release>(),
              py::return_value_policy::reference);

        n.def("getCommsModule", &NSIM::getCommsModule,
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
