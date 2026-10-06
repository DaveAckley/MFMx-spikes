#pragma once  /* -*- C++ -*- */

#include <functional>
#include <unordered_map>

#include "itype.h"
#include <string>
#include "MDist.h"
#include "S8C.h"

namespace MFM {

  template <class UTYPEC>
  struct UxC {
    static constexpr u8 CBIT_SIZE = sizeof(UTYPEC)*8;
    static constexpr UTYPEC CMIN_VAL = 0u;
    static constexpr UTYPEC CMAX_VAL = (1u<<CBIT_SIZE)-1u;
    static constexpr UxC UxC_MAX_VAL = { CMAX_VAL,CMAX_VAL };

    UTYPEC x, y;

    UxC() = default;
    UxC(const UxC&) = default;
    
    constexpr UxC(UTYPEC ax, UTYPEC ay) : x(ax), y(ay) { }
    //    U8C(S8C s) ;

    //    bool toS8C(S8C& s) ;

    void reset() { x = y = 0u; }

    constexpr bool isMaxed() const { return x == CMAX_VAL && y == CMAX_VAL; }
    constexpr u32 length() const { return x + y; }
    constexpr u32 manhattanDistance(const UxC & other) const {
      return
        ((x < other.x) ? other.x - x : x - other.x) +
        ((y < other.y) ? other.y - y : x - other.y);
    }
    constexpr bool operator==(const UxC & other) const { return x==other.x && y==other.y; }
    constexpr bool operator!=(const UxC & other) const { return x!=other.x || y!=other.y; }

    constexpr bool operator>(const UxC & other) const { return x>other.x && y>other.y; }
    constexpr bool operator<(const UxC & other) const { return x<other.x && y<other.y; }
    constexpr bool operator>=(const UxC & other) const { return x>=other.x && y>=other.y; }
    constexpr bool operator<=(const UxC & other) const { return x<=other.x && y<=other.y; }

    constexpr UxC operator*(const UxC & other) const { return UxC(x * other.x, y * other.y); }
    constexpr UxC operator/(const UxC & other) const { return UxC(x / other.x, y / other.y); }
    constexpr UxC operator%(const UxC & other) const { return UxC(x % other.x, y % other.y); }
    constexpr UxC operator+(const UxC & other) const { return UxC(x + other.x, y + other.y); }
    constexpr UxC operator-(const UxC & other) const { return UxC(x - other.x, y - other.y); }

    //    U8C operator+(const S8C & s8) const ;
    //    S8C operator-(const UxC & other) const { return S8C(((s32)x) - other.x, ((s32)y) - other.y); }
    //    bool addTo(const S8C & s8) ;

    std::string to_string() const {
      return
        std::string("(") + std::to_string(x) + "," + std::to_string(y) + ")";
    }

    std::string to_repr() const {
      return
        std::string("<U") + std::to_string(CBIT_SIZE) +
        std::string(":x=") + std::to_string(x) +
        ",y=" + std::to_string(y) +
        ">";
    }

    static UxC makeNoCCoordFromOtherNoC(UxC otherNoCCoord) {
      otherNoCCoord.x = 16u-otherNoCCoord.x;
      otherNoCCoord.y = 11u-otherNoCCoord.y;
      return otherNoCCoord;
    }

    static UxC makeNoC0CoordFromCT6Coord(UxC ct6c) { //< or (255,255) if invalid
      if (!onBoardCT6Coord(ct6c)) return UxC_MAX_VAL;
      UxC ret = ct6c;
      ret.x++;
      if (ret.x > 7u) ret.x += 2u;
      ret.y += 2u;
      return ret;
    }

    static UxC makeCT6CoordFromNoC0Coord(UxC nocc) { //< or (255,255) if !T6
      if (!isNoC0CoordAT6(nocc)) return UxC_MAX_VAL;
      UxC ct6 = nocc;
      ct6.y -= 2u;
      if (ct6.x > 9u) ct6.x -= 2u;
      ct6.x--;
      return ct6;
    }

    static UxC makeCT6CoordFromTLBI(uint32_t tlbidx) {
      UxC ret;
      ret.x = tlbidx%14u;
      ret.y = tlbidx/14u;
      return ret;
    }

    static UxC makeUxCNoCCoordFromTLBI(uint32_t tlbidx) {
      UxC ret = makeCT6CoordFromTLBI(tlbidx);
      ret.x++;
      if (ret.x > 7u) ret.x += 2u;
      ret.y += 2u;
      return ret;
    }

    static bool onBoardCT6Coord(UxC c) { return c.x < 14 && c.y < 10; }

    static bool onBoardNoC0Coord(UxC nocc) { return nocc.x <= 16u && nocc.y <= 11u; }

    static bool isNoC0CoordAT6(UxC nocc) {
      if (nocc.y < 2u || nocc.y > 11u) return false;
      if (nocc.x < 1u || nocc.x > 16u) return false;
      if (nocc.x > 7u && nocc.x < 10u) return false;
      return true;
    }

    /** TLBI-only computation works regardless of NoC coords */
    static bool makeNgbTLBIInDir(Dir4 dir, u32 tlbi, u32 & tlbiresult) {
      switch (dir) {
      case D4_N: if (tlbi < 14u) return false; tlbiresult = tlbi - 14u; break;
      case D4_S: if (tlbi > 125) return false; tlbiresult = tlbi + 14u; break;
      case D4_E: if ((tlbi % 14u) == 13u) return false; tlbiresult = tlbi + 1u; break;
      case D4_W: if ((tlbi % 14u) == 0) return false; tlbiresult = tlbi - 1u; break;
      }
      return true;
    }

    static UxC switchNoCCoord(UxC c) {
      return UxC(16u-c.x, 11u-c.y);
    }

    static u32 makeTLBIFromCT6Coord(UxC c) {
      u32 ret = c.y*14u + c.x;
      return ret;
    }

    static u32 makeTLBIFromNoCCoord(UxC c) {
      c.y -= 2u;
      if (c.x > 9u) c.x -= 2u;
      c.x--;
      return makeTLBIFromCT6Coord(c);
    }

#if 0
    static u32 makeTLBIFromNoC1Coord(UxC c) {
      return makeTLBIFromNoC0Coord(switchNoCCoord(c));
    }
#endif

    static u32 makeNoCNodeIdFromNoCCoord(UxC c) {
      return (u32) (((c.y&0x3f)<<6)|(c.x&0x3f));      
    }

    static UxC makeNoCCoordFromNoCNodeId(u32 nodeid) {
      UxC ret;
      ret.x = nodeid&0x3f;
      ret.y = (nodeid>>6)&0x3f;
      return ret;
    }

    static UxC makeSuperCellCoordFromLeaderCode(u32 leadercode) {
      UxC ret;
      ret.x = (leadercode>>0)&1;
      ret.y = (leadercode>>1)&1;
      return ret;
    }

    static u8 makeLeaderCodeFromSuperCellCoord(UxC sc) {
      if (sc.x > 1 || sc.y > 1) return U8_MAX;
      u8 ret = 0;
      if (sc.x == 1) ret |= 1<<0;
      if (sc.y == 1) ret |= 1<<1;
      return ret;
    }

    static UxC makeNoCCoordFromTLBI(u32 tlbi) {
      UxC ret = makeCT6CoordFromTLBI(tlbi);
      ret.x++;
      if (ret.x > 7u) ret.x += 2u;
      ret.y += 2u;
      return ret;
    }
    
    struct Hash {
      size_t operator()(const UxC& c) const noexcept {
        return std::hash<UTYPEC>()(c.x)*std::hash<UTYPEC>()(c.y);
      }
    };
  };

  using U8C =  UxC<u8>;
  using U16C = UxC<u16>;
  using U32C = UxC<u32>;

  template <class UTYPEC>
  struct UxCRange {

    using UC = UxC<UTYPEC>;
    UC start;                   // INCLUSIVE
    UC stop;                     // EXCLUSIVE

    struct iterator {
      iterator()
        : range(UxCRange())
        , at(0,0)
      { }

      iterator(UxCRange & r) : iterator(r,r.start) { }

      iterator(UxCRange & r, UC sat)
        : range(r)
        , at(sat)
      {
        MFM_API_ASSERT(range.area() > 0,ILLEGAL_ARGUMENT);
      }

      iterator & operator=(const iterator & other) = default;

      UC operator*() const { return at; }
      iterator operator++(int) {
        iterator ret = *this;
        ++(*this);
        return ret;
      }

      iterator& operator++() {
        if (++at.x >= range.stop.x) {
          ++at.y;
          at.x = range.start.x;
        }
        return *this;
      }
      bool operator!=(const iterator& other) const {
        return other.range != range || other.at != at;
      }

      bool atEnd() const { return at.x == range.start.x && at.y == range.stop.y; }

      UxCRange range;
      UC at;
    };

    iterator begin() { return iterator(*this); }
    iterator end() { return iterator(*this,UC(start.x,stop.y)); }

    UxCRange() = default;

    constexpr UxCRange(UC s, UC e)
      : start(s)
      , stop(e)
    { }

    constexpr UxCRange(const UxCRange base, const UC offset)
      : start(base.start + offset)
      , stop(base.stop + offset)
    { }

    void reset() {
      start.reset();
      stop.reset();
    }

    void init(UC s) {
      UC e = s+UC(1,1);
      init(s,e);
    }

    void init(UC s, UC e) {
      start = s;
      stop = e;
    }

    UC dims() const {
      return UC(stop.x - start.x, stop.y - start.y);
    }

    u32 area() const {
      return
        (stop.x - start.x) *
        (stop.y - start.y);
    }

    bool contains(const UC c) const {
      return c >= start && c < stop;
    }

    bool operator==(const UxCRange & other) const {
      return start==other.start && stop==other.stop;
    }

    bool operator!=(const UxCRange & other) const {
      return !(*this==other);
    }
    
    std::string to_string() const {
      return
        std::string("(") + std::to_string(start.x) + "," + std::to_string(start.y) +
        ".." + std::to_string(stop.x) + "," + std::to_string(stop.y) + "]";
    }
  };

  using U8CRange = UxCRange<u8>;
  using U16CRange = UxCRange<u16>;
  using U32CRange = UxCRange<u32>;
}

namespace std {
  template<class UTYPEC>
  struct hash<MFM::UxC<UTYPEC>> {
    size_t operator()(const MFM::UxC<UTYPEC>& c) const noexcept {
      return typename MFM::UxC<UTYPEC>::Hash()(c);
    }
  };
}

