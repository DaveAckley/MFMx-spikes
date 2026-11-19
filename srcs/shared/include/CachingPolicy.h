#pragma once    /* -*- C++ -*- */
#include "itype.h"
#include "MDist.h"         //< for Dir8, Corner4
#include "NgbCacheMgr.h"   //< for the grid and band sizes
#include "HostFlags.h"

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

     The policy is chosen per run by the host (mfmxctl 'policy', two
     HostFlags bits), so both are always compiled in.  No virtual
     functions (T6 code has no vtables): the policies are structs of
     static functions behind a CRTP base, and CachingPolicies picks one
     with a switch.  Pure geometry on purpose: the host tests it
     (scripts/cachepolicy/cachepolicy_test.cpp). */

  enum CachePolicyId : u8 {
    CP_SUPERCELL = 0,
    CP_FOURCORNERS = 1,
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
    return id == CP_FOURCORNERS ? "fourcorners" : "supercell";
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
      return id == CP_FOURCORNERS ? FourCornersPolicy::plan(arg, d8, c)
                                  : SuperCellPolicy::plan(arg, d8, c);
    }

    static EventBounds bounds(CachePolicyId id, u8 arg, const PolicyCtx & c) {
      return id == CP_FOURCORNERS ? FourCornersPolicy::bounds(arg, c)
                                  : SuperCellPolicy::bounds(arg, c);
    }
  };
}
