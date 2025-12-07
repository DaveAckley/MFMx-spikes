/* -*- mode:C++ -*- */

/*
  MDist.h Support for Manhattan distance calculations
  Copyright (C) 2014 The Regents of the University of New Mexico.  All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
  USA
*/

/**
   \file MDist.h Support for Manhattan distance calculations
   \author Trent R. Small.
   \author David H. Ackley.
   \date (C) 2014 All rights reserved.
   \lgpl
*/
#ifndef MDIST_H
#define MDIST_H

#include "itype.h"
#include "Point.h"
#include "Random.h"
#include "Dirs.h"

namespace MFM
{

  /**
   * Compute the number of sites within Manhattan distance 'radius' of a
   * given center site.  WARNING: Macro expands argument 'radius' twice!
   * Avoid side-effects!
   */
#define EVENT_WINDOW_SITES(radius) ((((radius)*2+1)*((radius)*2+1))/2+1)

  /**
   * An enumeration of several kinds of Manhattan Distance table
   * sizes.
   */
  typedef enum
  {
    MANHATTAN_TABLE_RADIUS_0 = 0,
    MANHATTAN_TABLE_RADIUS_1,
    MANHATTAN_TABLE_RADIUS_2,
    MANHATTAN_TABLE_RADIUS_3,
    MANHATTAN_TABLE_RADIUS_4,
    MANHATTAN_TABLE_SHORT = MANHATTAN_TABLE_RADIUS_2,
    MANHATTAN_TABLE_LONG = MANHATTAN_TABLE_RADIUS_4,
    MANHATTAN_TABLE_EVENT = MANHATTAN_TABLE_RADIUS_4
  } TableType;

  enum Dir4 {
    D4_N = 0,
    D4_W = 1,
    D4_S = 2,
    D4_E = 3,
  };
  
  static Dir4 nextCCWDir(const Dir4 from) { return (Dir4) ((from+1u)&0x3); }
  static Dir4 nextCWDir(const Dir4 from) {  return (Dir4) ((from+3u)&0x3); }

  enum Dir8 {
    D8_NT = 0,
    D8_NW = 1,
    D8_WT = 2,
    D8_SW = 3,
    D8_ST = 4,
    D8_SE = 5,
    D8_ET = 6,
    D8_NE = 7,
  };

  static u8 dir4ToByteCode(const Dir4 d4) { return (u8) "NWSE"[d4]; }
  static Dir8 dir4ToDir8(const Dir4 d4) { return (Dir8) (d4*2u); }

  static const char * dir8ToByteString(const Dir8 d8) {
    return "NT8\0NW8\0WT8\0SW8\0ST8\0SE8\0ET8\0NE8" + d8*4u;
  }
  static bool dir8ToDir4(const Dir8 d8, Dir4 & d4) {
    if (d8&1) return false;
    d4 = (Dir4) (d8/2u);
    return true;
  }

  enum Corner4 {
    C4_SE = 0,
    C4_SW = 1,
    C4_NW = 2,
    C4_NE = 3,

    C4_MIN = C4_SE,
    C4_MAX = C4_NE,
  };

  static const Dir4 clockwiseDir4FromCorner4(const Corner4 c4) {
    return (Dir4) (C4_NE - c4); // SE:E, SW:S, NW:W, NE:N
  }

  static const Corner4 clockwiseCorner4(const Corner4 c4) {
    return (Corner4) (((u8)c4+1u)&0x3); // SE:SW, SW:NW, NW:NE, NE:SE
  }

  static const Corner4 counterClockwiseCorner4(const Corner4 c4) {
    return (Corner4) (((u8)c4+3u)&0x3);
  }

  static const char * corner4ToByteString(const Corner4 c4) {
    return "SE\0SW\0NW\0NE" + (u8)c4*3u;
  }

  /**
   * A singleton class consisting of many utilities used for
   * calculating Many-kinds-of Distances, including Manhattan distance
   * and euclidean squared distance.
   */
  template <u32 R>
  class MDist
  {
  private:
    MDist(const MDist &) ;  // Singleton: Declare away copy ctor
    MDist & operator=(const MDist &) ; // Don't want this either

  public:
    /**
     * The diameter of an EventWindow of Radius R .
     */
    static const u32 EVENT_WINDOW_DIAMETER = R*2+1;

    /**
     * Access the singleton MDist of any given size.
     */
    static const MDist<R> & get();

    static const MDist<R> THE_INSTANCE;

    /**
     * Fills a given SPoint with a random Von Neumann unit vector.
     *
     * @param pt The SPoint to fill with a random Von Neumann unit vector.
     *
     * @param random The PRNG used to determine which unit vector to
     *               fill \c pt with.
     */
    void FillRandomSingleDir(SPoint& pt,Random & random) const;

    /**
     * Gets the area of a Manhattan Distance circle of a given radius .
     *
     * @param maxRadius The radius of the Manhattan Distance circle to
     *                  examine.
     */
    u32 GetTableSize(u32 maxRadius) const;

    /**
     * Get the lowest index corresponding to a distance of \c radius.
     * Useful for starting an event window iteration at a given distance
     *
     * \param radius The radius to find the lowest index of.
     * \sa GetLastIndex
     * \sa GetPoint
     */
    u32 GetFirstIndex(const u32 radius) const
    {
      MFM_API_ASSERT_ARG(radius < sizeof(m_firstIndex)/sizeof(m_firstIndex[0]));
      return m_firstIndex[radius];
    }

    /**
     * Get the highest index corresponding to a distance of \c radius.
     * Useful for ending an event window iteration at a given distance
     *
     * \param radius The radius to find the highest index of.
     * \sa GetFirstIndex
     */
    u32 GetLastIndex(const u32 radius) const
    {
      return GetFirstIndex(radius+1)-1;
    }

    /**
     * Get the lowest index that is no less than a Euclidean Squared
     * Length distance of \c eslRadius.  For starting an ESL-based
     * event window iteration at a given distance
     *
     * \param eslRadius The ESL radius to find the lowest index of.
     * \sa GetLastESLIndex
     * \sa GetPoint
     */
    u32 GetFirstESLIndex(const u32 eslRadius) const
    {
      for (u32 i = 0; i < sizeof(m_firstESLValue)/sizeof(m_firstESLValue[0]); ++i)
      {
        if (m_firstESLValue[i] >= eslRadius)
          return m_firstESLIndex[i];
      }
      FAIL(UNREACHABLE_CODE);
    }

    /**
     * Get the highest index corresponding to a Euclidean Squared
     * Length distance of no more than \c eslRadius.  Useful for
     * ending an event window iteration at a given distance
     *
     * \param eslRadius The esl radius to find the highest index of.
     * \sa GetFirstESLIndex
     */
    u32 GetLastESLIndex(const u32 eslRadius) const
    {
      return GetFirstESLIndex(eslRadius+1)-1;
    }

    u32 GetSiteCount() const { return ARRAY_LENGTH; }

    /**
       Get the relative coordinates of a given \c siteNumber, with
       siteNumber 0 representing the center at (0,0).  For legal
       siteNumbers, this method is inverted by GetSiteNumber()

       \param siteNumber The siteNumber, from 0 to ARRAY_LENGTH - 1

       \return The corresponding point

       \fails ILLEGAL_ARGUMENT if siteNumber is greater than or equal
       to ARRAY_LENGTH

       \sa GetSiteNumber
       \sa FromPoint
     */
    const SPoint & GetPoint(const u32 siteNumber) const
    {
      MFM_API_ASSERT_ARG(siteNumber < ARRAY_LENGTH);
      return m_indexToPoint[siteNumber];
    }

    /**
     * Convert a relative offset to the corresponding site number, if
     * possible.  Returns -1 if the given offset cannot be expressed
     * as a max length radius bond.
     */
    s32 GetSiteNumber(const SPoint & offset) const
    {
      return FromPoint(offset, R);
    }

    /**
     * Convert a raster scan index to the corresponding site number,
     * if possible.  Returns -1 if the given index does not correspond
     * to a sitenum
     */
    s32 GetSiteNumberFromRasterIndex(const u32 index) const
    {
      if (index >= ARRAY_LENGTH) return -1;
      return m_rasterToSiteNum[index];
    }

    /**
     * Convert a site number to its corresponding raster index, if
     * possible.  Returns -1 if the given site number index does not
     * correspond to a raster index
     */
    s32 GetRasterIndexFromSiteNumber(const u32 sitenum) const
    {
      if (sitenum >= ARRAY_LENGTH) return -1;
      return m_siteNumToRaster[sitenum];
    }

    /**
     * Return the coding of offset as a bond if possible.  Returns -1 if
     * the given offset cannot be expressed as a max length radius bond.
     */
    s32 FromPoint(const SPoint& offset, u32 radius) const;

    /*
     * Fills pt with the point represented by bits.
     * Uses a 4-bit rep if maxRadius less than 3
     */
    void FillFromBits(SPoint& pt, u8 bits, u32 maxRadius) const;

    MDist();

  private:
    static const u32 ARRAY_LENGTH = EVENT_WINDOW_SITES(R);

    static inline u32 ManhattanArea(u32 maxDistance)
    {
      return EVENT_WINDOW_SITES(maxDistance);
    }

    void InitRasterTables();
    u8 m_rasterToSiteNum[ARRAY_LENGTH];
    u8 m_siteNumToRaster[ARRAY_LENGTH];

    void InitESLTables();
    u8 m_siteNumToESLNum[ARRAY_LENGTH];
    u8 m_eSLNumToSiteNum[ARRAY_LENGTH];
    u8 m_firstESLValue[2*R+2];  // cutoff distances for ESL rings
    u8 m_firstESLIndex[2*R+2];

    Point<s32> m_indexToPoint[ARRAY_LENGTH];
    s32 m_pointToIndex[EVENT_WINDOW_DIAMETER][EVENT_WINDOW_DIAMETER];

    u32 m_firstIndex[R+2];  // m_firstIndex[R+1] holds 'lastIndex[R]'

    void InitEscapesByDirTable();
    u8 m_escapesByDirection[Dirs::DIR_COUNT][ARRAY_LENGTH];

    void InitHorizonsByDirTable();
    u8 m_horizonsByDirection[Dirs::DIR_COUNT][ARRAY_LENGTH];

  };

  template <u32 R>
  const MDist<R> MDist<R>::THE_INSTANCE;

} /* namespace MFM */

#include "MDist.tcc"

#endif /*MDIST_H*/
