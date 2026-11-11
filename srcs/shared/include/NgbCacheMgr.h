#pragma once   /* -*- C++ -*- */
#include "itype.h"
#include "MDist.h"  // for Dir8
#include "UxC.h"    // for U16CRange
#include "Fail.h"   // for MFM_API_
#include "XUtils.h" // for memset_s
#include "DemoGlobal.h" // for sizes

namespace MFM {

  /*
    T6GRID_BASE_WIDTH = 138
    T6GRID_BASE_HEIGHT = 108
    T6GRID_OVERLAP_WIDTH = 10
    T6GRID_OVERLAP_HEIGHT = 10
    T6GRID_WIDTH = 158
    T6GRID_HEIGHT = 128
  */

  struct NgbCacheMgr {
    enum : u8 {
      BW = DG::T6GRID_BASE_WIDTH,
      BH = DG::T6GRID_BASE_HEIGHT,
      OW = DG::T6GRID_OVERLAP_WIDTH,
      OH = DG::T6GRID_OVERLAP_HEIGHT,
      TW = DG::T6GRID_WIDTH,
      TH = DG::T6GRID_HEIGHT,
    };

    using Dir8Ranges = U8CRange[D8_NE+1];

    static constexpr const Dir8Ranges CACHE_RANGES = {
      //  (start,          end]
      { { OW, 0 },        {TW-OW, OH} }, // NT
      { { 0, 0 },         {OW, OH} },    // NW
      { { 0, OH },        {OW, TH-OH} }, // WT
      { { 0, TH-OH },     {OW, TH} },    // SW
      { { OW, TH-OH },    {TW-OW, TH} }, // ST
      { { TW-OW, TH-OH }, {TW, TH} },    // SE
      { { TW-OW, OH },    {TW, TH-OH} }, // ET
      { { TW-OW, 0 },     {TW, OH} },    // NE
    };

    using Dir8Offsets = U8C[D8_NE+1];

    static constexpr const Dir8Offsets CACHE_TO_SITE_OFFSETS = {
      { 0, OH },           // NT
      { OW, OH },          // NW
      { OW, 0 },           // WT
      { OW,(u8)-OH },      // SW
      { 0,(u8)-OH },       // ST
      {(u8)-OW,(u8)-OH },  // SE
      {(u8)-OW, 0 },       // ET
      {(u8)-OW, OH },      // NE
    };

    static constexpr const Dir8Ranges SITE_RANGES = {
      U8CRange(CACHE_RANGES[D8_NT],CACHE_TO_SITE_OFFSETS[D8_NT]),
      U8CRange(CACHE_RANGES[D8_NW],CACHE_TO_SITE_OFFSETS[D8_NW]),
      U8CRange(CACHE_RANGES[D8_WT],CACHE_TO_SITE_OFFSETS[D8_WT]),
      U8CRange(CACHE_RANGES[D8_SW],CACHE_TO_SITE_OFFSETS[D8_SW]),
      U8CRange(CACHE_RANGES[D8_ST],CACHE_TO_SITE_OFFSETS[D8_ST]),
      U8CRange(CACHE_RANGES[D8_SE],CACHE_TO_SITE_OFFSETS[D8_SE]),
      U8CRange(CACHE_RANGES[D8_ET],CACHE_TO_SITE_OFFSETS[D8_ET]),
      U8CRange(CACHE_RANGES[D8_NE],CACHE_TO_SITE_OFFSETS[D8_NE]),
    };

    static constexpr U8C mapDestCToUs(Dir8 ourD8, bool weLead, U8C c) {
      Dir8 oppD8 = oppositeDir8(ourD8);
      if (weLead) {             // their sites to our cache
        U8C zc = c - SITE_RANGES[oppD8].start;
        return CACHE_RANGES[ourD8].start + zc;
      } else {                  // their cache to our sites
        U8C zc = c - CACHE_RANGES[oppD8].start;
        return SITE_RANGES[ourD8].start + zc;        
      }
    }

    static constexpr const U8CRange HIDDEN_RANGE = { { 2*OW,2*OH }, { TW-2*OW, TH-2*OH } };

    static std::string dumpCacheRanges() { return dumpRanges(CACHE_RANGES,"cache"); }
    static std::string dumpSiteRanges() { return dumpRanges(SITE_RANGES,"sites"); }
    static std::string dumpAllRanges() {
      U8CRange r(U8C(1,3),U8C(4,5));
      std::string ret = r.to_string() + " {";
      for (U8C i : r) {
        ret += " " + i.to_string();
      }
      ret += " }\n";
      ret +=
        dumpCacheRanges() +
        dumpSiteRanges() +
        "hidden:" + HIDDEN_RANGE.to_string() + "\n";
      return ret;
    }

    static std::string dumpRanges(const Dir8Ranges & ranges, const char * name) {
      std::string ret = "";
      for (u32 i = D8_NT; i <= D8_NE; ++i) {
        ret += std::string(dir8ToByteString((Dir8) i))+" "+ name+": ";
        ret += ranges[i].to_string();
        ret += "\n";
      }
      return ret;
    }

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    enum Mode : u8;             // forward
#define ALL_NGB_CACHE_MGR_MODES()  \
    XX(SOLO,own sites)             \
    XX(LEAD,expanded sites)        \
    XX(FOLLOW,limited sites)       \
    XX(NONE,no sites)              \
    /*end*/

    enum Op : u8;               // forward
#define ALL_NGB_CACHE_MGR_OPS()    \
    XX(SEND_OWN_SITES)             \
    XX(RECV_OWN_SITES)             \
    XX(SEND_NGB_SITES)             \
    XX(RECV_NGB_SITES)             \
    /*end*/

    Mode mMode;
    Op mOp;
    u32 mIndex;
    u32 mCount;

    //////
    enum Mode : u8 {
#define XX(N,C) MODE_##N,
      ALL_NGB_CACHE_MGR_MODES()
#undef XX
      MAX_MODE
    };

    static constexpr const char * MODE_NAMES[] = {
#define XX(N,C) ""#N,
      ALL_NGB_CACHE_MGR_MODES()
#undef XX
      ""
    };

    static const char * getModeName(Mode m) {
      MFM_API_ASSERT(m < MAX_MODE, ILLEGAL_ARGUMENT);
      return MODE_NAMES[m];
    }


    enum Op : u8 {
#define XX(N) MODE_##N,
      ALL_NGB_CACHE_MGR_OPS()
#undef XX
      MAX_OP
    };

    static constexpr const char * OP_NAMES[] = {
#define XX(N) ""#N,
      ALL_NGB_CACHE_MGR_OPS()
#undef XX
      ""
    };

    static const char * getOpName(Op o) {
      MFM_API_ASSERT(o < MAX_OP, ILLEGAL_ARGUMENT);
      return OP_NAMES[o];
    }
  };
}
