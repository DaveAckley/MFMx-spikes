#pragma once /* -*- C++ -*- */

#include <string>

#include "itype.h"
using namespace MFM;

struct S32C {
  s32 x,y;
  constexpr S32C() : x(0) , y(0) { } 
  constexpr S32C(s32 mx, s32 my) : x(mx), y(my) { }

  std::string to_repr() const {
    return
      std::string("<S32c:x=") + std::to_string(x) +
      ",y=" + std::to_string(y) + ">";
  }

  // EQUALITY
  bool operator==(const S32C & other) const { return x == other.x && y == other.y; }
  bool operator!=(const S32C & other) const { return x != other.x || y != other.y; }
  // PAIRWISE OPS
  S32C operator+(const S32C & other) const { return S32C(x+other.x,y+other.y); }
  S32C operator-(const S32C & other) const { return S32C(x-other.x,y-other.y); }
  S32C operator*(const S32C & other) const { return S32C(x*other.x,y*other.y); }
  S32C operator/(const S32C & other) const { return S32C(x/other.x,y/other.y); }
  // SCALAR OPS
  S32C operator+(const u32 s) const { return S32C(x+s,y+s); }
  S32C operator+(const s32 s) const { return S32C(x+s,y+s); }
  S32C operator-(const u32 s) const { return S32C(x-s,y-s); }
  S32C operator-(const s32 s) const { return S32C(x-s,y-s); }
  S32C operator*(const u32 s) const { return S32C(x*s,y*s); }
  S32C operator*(const s32 s) const { return S32C(x*s,y*s); }
  S32C operator/(const u32 s) const { return S32C(x/s,y/s); }
  S32C operator/(const s32 s) const { return S32C(x/s,y/s); }
  
  u32 manhattanLength() const { return abs(x) + abs(y); }
  u32 euclideanSquaredLength() const { return x*x + y*y; }

  u32 manhattanDistance(const S32C & other) const { return (*this - other).manhattanLength(); }
  u32 euclideanSquaredDistance(const S32C & other) const { return (*this - other).euclideanSquaredLength(); }

  // ASGN OPS
  S32C & operator=(const S32C & other) { x = other.x; y = other.y; return *this; }
  S32C & operator+=(const S32C & other) { x += other.x; y += other.y; return *this; }
  S32C & operator-=(const S32C & other) { x -= other.x; y -= other.y; return *this; }
  S32C & operator*=(const S32C & other) { x *= other.x; y *= other.y; return *this; }
  S32C & operator/=(const S32C & other) { x /= other.x; y /= other.y; return *this; }
  
};
