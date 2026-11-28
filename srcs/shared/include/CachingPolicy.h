#pragma once    /* -*- C++ -*- */
#include "itype.h"
#include "MDist.h"         //< for Dir8, Corner4
#include "NgbCacheMgr.h"   //< for the grid and band sizes
#include "HostFlags.h"
#include "HubEdges.h"     //< HubEdges::PE_* permanent-edge bits

namespace MFM {

  /* CACHING POLICY: how a supercycle decides, for each of our eight
     neighbors, whether we send our edge SITES, receive a neighbor's
     into our CACHES, or do nothing, and which part of our grid our event
     centers may use.  Two policies:

       CP_SUPERCELL   The original.  The NEW_LEADER argument is a LEADER
                      CODE, a position (x = bit 0, y = bit 1) in each
                      2x2 supercell of hubs.  The leader receives all
                      eight bands and runs events over its whole grid;
                      the other three hubs send their bands to it and
                      avoid them.

       CP_FOURCORNERS Every hub does the same.  The argument is a Corner4
                      (MDist.h): the corner we expand TOWARD.  We receive
                      the two edge bands next to that corner and the
                      corner cache between them, from the neighbors that
                      lend them; we send the three opposite bands to the
                      neighbors that borrow them; the other two
                      diagonals do not exchange.  Events run over our own
                      area shifted toward the corner.

       CP_EWSNAP      Four corners on the EW lattice (EWLattice.h, branch
                      diagonal-tiles): same roles and Corner4 argument,
                      but every set (owned, borrowed, lent, event area)
                      is whole EWs, event centers come from per-corner
                      center tables, and bands are the minimal disjoint
                      ones.  Only bounds()/plan() below are shared; the
                      bands and centers live in CacheLoader and Grid.

       CP_EWSNAP10    CP_EWSNAP on radius-10 tiles (EWLattice10, 221
                      sites): every set is whole tiles, so hub edges
                      have longer straight 45-degree runs; events still
                      use radius-4 windows.

     The policy is chosen per run by the host (mfmxctl 'policy', two
     HostFlags bits), so both are always compiled in.  No virtual
     functions (T6 code has no vtables): the policies are structs of
     static functions behind a CRTP base, and CachingPolicies picks one
     with a switch.  Pure geometry on purpose: the host tests it
     (scripts/cachepolicy/cachepolicy_test.cpp). */

  enum CachePolicyId : u8 {
    CP_SUPERCELL = 0,
    CP_FOURCORNERS = 1,
    CP_EWSNAP = 2,
    CP_EWSNAP10 = 3,
    CP_COUNT
  };

  inline CachePolicyId cachePolicyFromHostFlags(u8 hostflags) {
    u8 id = (u8) ((hostflags >> 4) & 0x3u);
    return id < CP_COUNT ? (CachePolicyId) id : CP_SUPERCELL;
  }

  inline u8 cachePolicyToHostFlags(CachePolicyId id) {
    return (u8) (((u8) id & 0x3u) << 4);
  }

  inline const char * cachePolicyName(CachePolicyId id) {
    return id == CP_FOURCORNERS ? "fourcorners"
         : id == CP_EWSNAP ? "ewsnap"
         : id == CP_EWSNAP10 ? "ewsnap10"
         : "supercell";
  }

  /* Policies whose NEW_LEADER argument is a Corner4. */
  inline bool isCornerPolicy(CachePolicyId id) {
    return id == CP_FOURCORNERS || id == CP_EWSNAP || id == CP_EWSNAP10;
  }

  /* Tile radius of an EW-lattice policy (EWLattice.h), 0 for the
     others. */
  inline u8 ewTileRadius(CachePolicyId id) {
    return id == CP_EWSNAP ? 4u : id == CP_EWSNAP10 ? 10u : 0u;
  }

  /* What we do with the neighbor in one Dir8 direction. */
  enum class DirPlan : u8 {
    NONE,                       //< no exchange this supercycle
    SEND_SITES,                 //< RUN_EVENTS: we send our site band; SAVE: we receive it back
    RECV_CACHES                 //< RUN_EVENTS: we receive into our cache; SAVE: we send it back
  };

  /* Event center bounds: x in [x0,x1), y in [y0,y1), in our grid. */
  struct EventBounds {
    u16 x0, y0, x1, y1;
  };

  /* What the caller knows that CP_SUPERCELL needs (CP_FOURCORNERS only
     looks at the argument and the direction). */
  struct PolicyCtx {
    bool weLead;                //< we are the supercell leader of this cycle
    bool peerLeads;             //< the neighbor in this direction is
    bool noLeader;              //< no argument yet (before the first NEW_LEADER)
  };

  template <class Derived>
  struct CachingPolicyBase {
    enum : u16 {
      WINDOW_RADIUS = 4,        //< an event window reaches this far from its center
      BW = NgbCacheMgr::BW, BH = NgbCacheMgr::BH,
      OW = NgbCacheMgr::OW, OH = NgbCacheMgr::OH,
      TW = NgbCacheMgr::TW, TH = NgbCacheMgr::TH,
    };

    static DirPlan plan(u8 arg, Dir8 d8, const PolicyCtx & c) {
      return Derived::planImpl(arg, d8, c);
    }

    static EventBounds bounds(u8 arg, const PolicyCtx & c) {
      return Derived::boundsImpl(arg, c);
    }

    /* Before the first argument: stay clear of the caches. */
    static EventBounds boundsAvoidingCaches() {
      return { (u16) (OW + WINDOW_RADIUS), (u16) (OH + WINDOW_RADIUS),
               (u16) (TW - OW - WINDOW_RADIUS), (u16) (TH - OH - WINDOW_RADIUS) };
    }
  };

  struct SuperCellPolicy : CachingPolicyBase<SuperCellPolicy> {
    static DirPlan planImpl(u8, Dir8, const PolicyCtx & c) {
      if (c.peerLeads) return DirPlan::SEND_SITES;
      if (c.weLead) return DirPlan::RECV_CACHES;
      return DirPlan::NONE;     // two followers
    }

    static EventBounds boundsImpl(u8, const PolicyCtx & c) {
      if (c.noLeader) return boundsAvoidingCaches();
      if (c.weLead)             // own the caches
        return { WINDOW_RADIUS, WINDOW_RADIUS,
                 (u16) (TW - WINDOW_RADIUS), (u16) (TH - WINDOW_RADIUS) };
      // a follower avoids its own edge bands, which the leader uses
      return { (u16) (OW + OW + WINDOW_RADIUS), (u16) (OH + OH + WINDOW_RADIUS),
               (u16) (TW - OW - OW - WINDOW_RADIUS),
               (u16) (TH - OH - OH - WINDOW_RADIUS) };
    }
  };

  struct FourCornersPolicy : CachingPolicyBase<FourCornersPolicy> {
    static bool toEast(u8 corner) { return corner == C4_SE || corner == C4_NE; }
    static bool toSouth(u8 corner) { return corner == C4_SE || corner == C4_SW; }

    static Dir8 cornerDir8(u8 corner) {
      switch (corner) {
      case C4_SE: return D8_SE;
      case C4_SW: return D8_SW;
      case C4_NW: return D8_NW;
      default:    return D8_NE;
      }
    }

    /* The three directions we BORROW from for this corner. */
    static bool borrowsFrom(u8 corner, Dir8 d8) {
      if (d8 == cornerDir8(corner)) return true;
      if (d8 == (toEast(corner) ? D8_ET : D8_WT)) return true;
      return d8 == (toSouth(corner) ? D8_ST : D8_NT);
    }

    /* The part of an EDGE band this corner exchanges.  The full bands
       (NgbCacheMgr SITE_RANGES / CACHE_RANGES) run the whole side, so
       each contains the two corner squares at its ends; but the
       borrower's events never reach the square at the end AWAY from
       the corner (boundsImpl keeps them OW + OW + WINDOW_RADIUS clear),
       and that square belongs to another borrower this cycle (the
       diagonal, or the other edge band).  Exchanging it anyway gave one
       owner's corner square three borrowers, and at SAVE the last
       packet won, usually a stale unused copy (OPEN_ISSUES 'CORNER
       NOTCHES', run 20261127-163420).  Row bands (NT, ST) lose the
       west square when we expand east, else the east one; column bands
       (WT, ET) the north square when we expand south, else the south
       one.  Sites and caches of a band share those coordinates, so both
       ends of a link trim alike.  Diagonals are whole squares already.
       Every site is now borrowed by at most one hub per cycle.
       EXCEPT at a PERMANENT edge (permEdges, HubEdges::PE_*): if the side
       toward the square has no hub, nobody else can borrow it, and our
       events DO reach it (withPermanentEdges runs them to the last real
       site), so it stays in the band.  A row band's ends lie toward W
       and E, a column band's toward N and S; both ends of a link sit in
       the same column (row) of hubs, so they see the same edges.  (The
       first version trimmed there too: borrowers at the array's outer
       edges evolved and painted copies nobody loaded or saved, turds
       after a nuke along the bottom of run 20261127 bebd68e.) */
    static U8CRange trimBand(u8 corner, Dir8 d8, U8CRange r,
                             u8 permEdges) {
      if (d8 == D8_NT || d8 == D8_ST) {
        if (toEast(corner)) {
          if ((permEdges & HubEdges::PE_W) == 0u) r.start.x = (u8) (OW + OW);
        } else {
          if ((permEdges & HubEdges::PE_E) == 0u)
            r.stop.x = (u8) (TW - OW - OW);
        }
      } else if (d8 == D8_WT || d8 == D8_ET) {
        if (toSouth(corner)) {
          if ((permEdges & HubEdges::PE_N) == 0u) r.start.y = (u8) (OH + OH);
        } else {
          if ((permEdges & HubEdges::PE_S) == 0u)
            r.stop.y = (u8) (TH - OH - OH);
        }
      }
      return r;
    }

    static DirPlan planImpl(u8 corner, Dir8 d8, const PolicyCtx &) {
      if (borrowsFrom(corner, d8)) return DirPlan::RECV_CACHES;
      if (borrowsFrom(corner, oppositeDir8(d8))) return DirPlan::SEND_SITES;
      return DirPlan::NONE;
    }

    static EventBounds boundsImpl(u8 corner, const PolicyCtx & c) {
      if (c.noLeader || corner > C4_MAX) return boundsAvoidingCaches();
      EventBounds b;
      // Toward the corner we own the borrowed cache (down to the grid
      // edge); away from it we stay clear of the band we lend.
      if (toEast(corner)) {
        b.x0 = OW + OW + WINDOW_RADIUS;       b.x1 = TW - WINDOW_RADIUS;
      } else {
        b.x0 = WINDOW_RADIUS;                 b.x1 = TW - OW - OW - WINDOW_RADIUS;
      }
      if (toSouth(corner)) {
        b.y0 = OH + OH + WINDOW_RADIUS;       b.y1 = TH - WINDOW_RADIUS;
      } else {
        b.y0 = WINDOW_RADIUS;                 b.y1 = TH - OH - OH - WINDOW_RADIUS;
      }
      return b;
    }
  };

  struct CachingPolicies {
    static DirPlan plan(CachePolicyId id, u8 arg, Dir8 d8, const PolicyCtx & c) {
      return isCornerPolicy(id) ? FourCornersPolicy::plan(arg, d8, c)
                                : SuperCellPolicy::plan(arg, d8, c);
    }

    /* The range a loader walks for direction d8 (the sites we lend or
       the cache we borrow into, `full`), under policy id and argument
       arg, for a hub with permanent edges permEdges.  CP_FOURCORNERS
       trims edge bands (FourCornersPolicy::trimBand); the ewsnap policies
       walk their own bands (CacheLoader initEWBand). */
    static U8CRange bandRange(CachePolicyId id, u8 arg, Dir8 d8,
                              U8CRange full, u8 permEdges) {
      return id == CP_FOURCORNERS
        ? FourCornersPolicy::trimBand(arg, d8, full, permEdges)
        : full;
    }

    /* CACHE ELISION: may direction d8 stay put across a change of argument
       from prev to next?  The borrower keeps its cache, the lender its
       (untouched) sites, and neither the SAVE nor the next LOAD is
       needed, so the band must be the SAME band under both arguments.
       Corner policies only, and only on a repeated corner: ewsnap's
       minimal bands and four corners' trimmed bands (bandRange) change
       with the corner.  (Until 2026-11-27 four corners kept any
       direction whose role was unchanged, so e.g. SE -> SW kept the
       north neighbor's full-width S band, corner square included, while
       the diagonal had changed that square: stale copies, OPEN_ISSUES
       'CORNER NOTCHES'.)  Every hub computes the same answer for its end
       of a link, and the opposite end gets the opposite role. */
    static bool keepAcross(CachePolicyId id, u8 prev, u8 next, Dir8 d8) {
      if (ewTileRadius(id) != 0u || id == CP_FOURCORNERS) {
        // the bands change with the corner: only a repeat keeps them
        PolicyCtx c0;
        c0.weLead = false;
        c0.peerLeads = false;
        c0.noLeader = false;
        return prev == next && plan(id, next, d8, c0) != DirPlan::NONE;
      }
      return false;
    }

    static EventBounds bounds(CachePolicyId id, u8 arg, const PolicyCtx & c) {
      return isCornerPolicy(id) ? FourCornersPolicy::bounds(arg, c)
                                : SuperCellPolicy::bounds(arg, c);
    }

    /* A PERMANENT edge (no neighbor hub, HubEdges.h) is not a dead band:
       event centers run right up to its last real site, whatever the
       policy said for that side (the window then reaches into the
       INACCESSIBLE ring).  Sides with a neighbor keep the policy's
       bounds, which stay a full window radius clear of temporary bands. */
    static EventBounds withPermanentEdges(EventBounds b, u8 permEdges) {
      enum : u16 {
        OW = NgbCacheMgr::OW, OH = NgbCacheMgr::OH,
        TW = NgbCacheMgr::TW, TH = NgbCacheMgr::TH,
      };
      if (permEdges & HubEdges::PE_W) b.x0 = OW;
      if (permEdges & HubEdges::PE_E) b.x1 = (u16) (TW - OW);
      if (permEdges & HubEdges::PE_N) b.y0 = OH;
      if (permEdges & HubEdges::PE_S) b.y1 = (u16) (TH - OH);
      return b;
    }
  };
}
