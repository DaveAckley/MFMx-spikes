#include <stdio.h>         /* -*- C++ -*- */
#include "MDist.h"
#include "Fail.h"
//#include "Logger.h"

namespace MFM {

  template<u32 R>
  void MDist<R>::init() const {
    /* This is once-only so let's be very stupid.  (We'll want to
       precompile all this out later anyway.)  We store the points in
       just one table, sorted by their lengths, and remember where the
       different lengths begin.  This lets us index and select offsets
       of any given length, or contiguous range of lengths, from zero up
       to the R. */

    MFM_API_ASSERT_STATE(!mMDistInitted);

    /* Init the reverse lookup table to all 'illegal' */
    for (u32 x = 0; x<EVENT_WINDOW_DIAMETER; ++x)
    {
      for (u32 y = 0; y<EVENT_WINDOW_DIAMETER; ++y)
      {
        m_pointToIndex[x][y] = -1;
      }
    }

    u32 next = 0;
    const SPoint center(R,R);

    // For every manhattan length from small to large
    for (u32 length = 0; length<=R; ++length)
    {
      m_firstIndex[length] = next;

      // For each maximum length up to the manhattan length
      for (u32 max = 0; max <= length; ++max)
      {
        // Scan the whole event window
        for (u32 x = 0; x<EVENT_WINDOW_DIAMETER; ++x)
        {
          for (u32 y = 0; y<EVENT_WINDOW_DIAMETER; ++y)
          {
            Point<s32> p(x,y);
            p.Subtract(center);

            // And pick up available points that fit in both length bounds
            if (m_pointToIndex[x][y] < 0
                && p.GetManhattanLength() <= length
                && p.GetMaximumLength() <= max)
            {
              m_indexToPoint[0][next] = p.GetX();
              m_indexToPoint[1][next] = p.GetY();
              m_pointToIndex[x][y] = next;
              ++next;
            }
          }
        }
      }
    }
    m_firstIndex[R+1] = next;
    if (next != EVENT_WINDOW_SITES(R))
      FAIL(ILLEGAL_STATE);

    _initEscapesByDirTable();
    _initHorizonsByDirTable();
    _initRasterTables();
    _initESLTables();

    mMDistInitted = true; // And Don't You Ever Come Back Here
  }

  template<u32 R>
  void MDist<R>::_initRasterTables()
  {
    /* Yet more once-only and eventually to be pre-compiled into
       const, so for here and now let's see how unbelievably slow and
       obvious we can make this.
    */
    const MDist<R> md;
    const s32 SR = (s32) R;
    u32 rasterIndex = 0;
    for (s32 y = -SR; y <= SR; ++y)
    {
      for (s32 x = -SR; x <= SR; ++x)
      {
        const SPoint s(x,y);
        s32 sn = md.getSiteNumber(s);
        if (sn >= 0)
        {
          m_rasterToSiteNum[rasterIndex] = sn;
          ++rasterIndex;
        }
      }
    }

    MFM_API_ASSERT_STATE(rasterIndex == ARRAY_LENGTH);

    for (u32 i = 0; i < ARRAY_LENGTH; ++i)
    {
      m_siteNumToRaster[i] = ARRAY_LENGTH;
    }
    for (u32 i = 0; i < ARRAY_LENGTH; ++i)
    {
      m_siteNumToRaster[m_rasterToSiteNum[i]] = i;
    }
    for (u32 i = 0; i < ARRAY_LENGTH; ++i)
    {
      MFM_API_ASSERT_STATE(m_siteNumToRaster[i] < ARRAY_LENGTH);
    }

  }

  template<u32 R>
  void MDist<R>::_initESLTables()
  {
    /* Yet more once-only and eventually to be pre-compiled into
       const, so for here and now let's see how unbelievably slow and
       obvious we can make this.
    */
    const MDist<R> md;
    u32 eslIndex = 0;
    u32 currentESL = U32_MAX;  // flag
    u32 firstESLIdx = 0;
    const s32 SR = (s32) R;
    for (u32 esl = 0; esl <= 2*R*R; ++esl)
    {
      // go left-to-right slowest and top-to-bottom fastest to follow
      // manhattan length scan order where possible
      for (s32 x = -SR; x <= SR; ++x)
      {
        for (s32 y = -SR; y <= SR; ++y)
        {
          const SPoint s(x,y);
          s32 sn = md.getSiteNumber(s);
          if (sn < 0) continue; // Not in window

          u32 thisESL = x*x + y*y;
          if (thisESL != esl) continue; // Not length we want

          // We have a site at this esl.  New length?
          if (currentESL != thisESL)
          {
            m_firstESLIndex[firstESLIdx] = eslIndex;
            m_firstESLValue[firstESLIdx] = thisESL;
            currentESL = thisESL;
            ++firstESLIdx;
          }

          m_eSLNumToSiteNum[eslIndex] = sn;
          ++eslIndex;
        }
      }
    }

    MFM_API_ASSERT_STATE(eslIndex == ARRAY_LENGTH);
    MFM_API_ASSERT_STATE(firstESLIdx == 2*R+1);

    m_firstESLIndex[firstESLIdx] = ARRAY_LENGTH;
    m_firstESLValue[firstESLIdx] = U8_MAX;

    for (u32 i = 0; i < ARRAY_LENGTH; ++i)
    {
      m_siteNumToESLNum[i] = ARRAY_LENGTH;
    }
    for (u32 i = 0; i < ARRAY_LENGTH; ++i)
    {
      m_siteNumToESLNum[m_eSLNumToSiteNum[i]] = i;
    }
    for (u32 i = 0; i < ARRAY_LENGTH; ++i)
    {
      MFM_API_ASSERT_STATE(m_siteNumToESLNum[i] < ARRAY_LENGTH);
    }

  }

  template<u32 R>
  void MDist<R>::_initEscapesByDirTable()
  {
    /* Again, this is once-only and eventually to be pre-compiled into
       const, so for here and now let's see how unbelievably slow and
       obvious we can make this.
    */
    const MDist<R> md;

    for (u32 d = Dirs::NORTH; d < Dirs::DIR_COUNT; ++d) // For each dir
    {
      // Get the delta to head in the given direction
      SPoint dirDelta;
      Dirs::FillDir(dirDelta, d, false); // Something in (-1 ,-1) to (1, 1)
      dirDelta /= 2;

      u32 countInDir = 0;       // Counts how many points we've stored for dir
      u32 deltasToEscape = 0;   // While gradually increasing this

      while (countInDir < ARRAY_LENGTH) // Until we're done for this dir
      {
        // On to next delta count
        ++deltasToEscape;

        // Rescan the entire event window, now looking for guys
        // exactly deltasToEscape away from escaping the event window,
        // if any, and add their indexes to the d direction of the
        // byDirection array.

        for (u32 idx = md.getFirstIndex(0); idx <= md.getLastIndex(R); ++idx)
        {
          SPoint rel = md.getPoint(idx);

          // How many times do we have to add dirDelta to rel for it
          // to no longer be in the event window?

          u32 deltas;
          for (deltas = 1; deltas <= 2*R; ++deltas)
          {
            SPoint off = rel + dirDelta * deltas;
            if (off.GetManhattanLength() > R)
              break;
          }

          // If that number of deltas is the count we're looking for,
          // then rel is the next point('s idx) to capture.

          if (deltas == deltasToEscape)
          {
            m_escapesByDirection[d][countInDir++] = (u8) idx;
          }
        }
      }
    }
  }

  template<u32 R>
  void MDist<R>::_initHorizonsByDirTable()
  {
    const MDist<R> md;
    /* Etc, ditto, unbelievably slow and obvious == good.
    */

    for (u32 d = Dirs::NORTH; d < Dirs::DIR_COUNT; ++d) // For each dir
    {
      // Get the delta to head in the given direction
      SPoint dirDelta;
      Dirs::FillDir(dirDelta, d, false); // Something in (-1 ,-1) to (1, 1)
      dirDelta /= 2;

      u32 countInDir = 0;       // Counts how many points we've stored for dir
      u32 deltasToHorizon = 0;  // While gradually increasing this

      while (countInDir < ARRAY_LENGTH) // Until we're done for this dir
      {
        // On to next delta count
        ++deltasToHorizon;

        // Rescan the entire event window, now looking for guys
        // exactly deltasToHorizon away from crossing the horizon line,
        // if any, and add their indexes to the d direction of the
        // byDirection array.

        // We want to alternate the horizon between even and odd so
        // the diagonals will iterate in alternating white and black
        // chess board strips, but we need to not do that for the
        // orthogonals, so we make this loop twice only for the
        // diagonals, which have a manhattan length of two.  (Well,
        // this surely ain't as obvious as hoped, but I'm not hanging
        // around to find the properly clever way.)

        for (u32 horizon = R + dirDelta.GetManhattanLength(); horizon >= R + 1; --horizon)
        {

          for (u32 idx = md.getFirstIndex(0); idx <= md.getLastIndex(R); ++idx)
          {
            SPoint rel = md.getPoint(idx);

            // How many times do we have to add dirDelta to rel for it
            // reach the horizon?

            u32 deltas;
            for (deltas = 1; deltas <= 2*R; ++deltas)
            {
              // Multiply by abs(dirDelta) to knock out 0 components
              SPoint off(rel.GetX() * abs(dirDelta.GetX()), rel.GetY() * abs(dirDelta.GetY()));

              // Then scale whatever's left
              off += dirDelta * deltas;

              if (off.GetManhattanLength() == horizon)
                break;
            }

            // If that number of deltas is the count we're looking for,
            // then rel is the next point('s idx) to capture.

            if (deltas == deltasToHorizon)
            {
              // We found the next guy.
              m_horizonsByDirection[d][countInDir++] = (u8) idx;
              //LOG.Debug("%d %d (%d,%d)",d,countInDir,rel.GetX(), rel.GetY());
            }
          }
        }
      }
    }
  }

  template<u32 R>
  u32 MDist<R>::getTableSize(u32 maxRadius) const
  {
    return EVENT_WINDOW_SITES(maxRadius);
  }

  template<u32 R> u8 MDist<R>::m_rasterToSiteNum[ARRAY_LENGTH];
  template<u32 R> u8 MDist<R>::m_siteNumToRaster[ARRAY_LENGTH];

  template<u32 R> u8 MDist<R>::m_siteNumToESLNum[ARRAY_LENGTH];
  template<u32 R> u8 MDist<R>::m_eSLNumToSiteNum[ARRAY_LENGTH];
  template<u32 R> u8 MDist<R>::m_firstESLValue[2*R+2];  // cutoff distances for ESL rings
  template<u32 R> u8 MDist<R>::m_firstESLIndex[2*R+2];

  template<u32 R> s32 MDist<R>::m_indexToPoint[2][ARRAY_LENGTH];
  template<u32 R> s32 MDist<R>::m_pointToIndex[EVENT_WINDOW_DIAMETER][EVENT_WINDOW_DIAMETER];

  template<u32 R> u32 MDist<R>::m_firstIndex[R+2];  // m_firstIndex[R+1] holds 'lastIndex[R]'

  template<u32 R> u8 MDist<R>::m_escapesByDirection[Dirs::DIR_COUNT][ARRAY_LENGTH];

  template<u32 R> u8 MDist<R>::m_horizonsByDirection[Dirs::DIR_COUNT][ARRAY_LENGTH];

  template<u32 R> bool MDist<R>::mMDistInitted;

  /*
  template<u32 R>
  const MDist<R>& MDist<R>::get()
  {
    return THE_INSTANCE.getm();
    }

  template<u32 R>
  MDist<R>& MDist<R>::getm()
  {
    if (!THE_INSTANCE_INITTED) {
      THE_INSTANCE.init();
      THE_INSTANCE_INITTED = true;
    }
    return THE_INSTANCE;
  }
  */

  template<u32 R>
  s32 MDist<R>::fromPoint(const SPoint& offset, u32 maxRadius) const
  {
    u32 x = (u32) (offset.GetX()+R);
    u32 y = (u32) (offset.GetY()+R);
    if (x >= EVENT_WINDOW_DIAMETER || y >= EVENT_WINDOW_DIAMETER)
      return -1;

    u32 idx = m_pointToIndex[x][y];

    // Ensure we're inside the allowed radius
    if (idx >= getFirstIndex(maxRadius+1))
      return -1;

    return (s32) idx;
  }

  template<u32 R>
  void MDist<R>::fillFromBits(SPoint& pt, u8 bits, u32 maxRadius) const
  {
    MFM_API_ASSERT_ARG(bits < ARRAY_LENGTH);

    pt.SetX(m_indexToPoint[0][bits]);
    pt.SetY(m_indexToPoint[1][bits]);
  }

  //static SPoint VNNeighbors[4];

  template<u32 R>
  void MDist<R>::fillRandomSingleDir(SPoint& pt,Random & random) const
  {
    switch(random.Create(4))
      {
      case 0: pt.Set(0, -1); break;
      case 1: pt.Set(0, 1);  break;
      case 2: pt.Set(-1, 0); break;
      case 3: pt.Set(1, 0); break;
      default: return;
      }
  }
} /* namespace MFM */
