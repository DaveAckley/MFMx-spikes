#pragma once  /* -*- C++ -*- */

/* NukeBlast -- the geometry of `nuke`, shared by the hub image and its
 * host-side tests.
 *
 * A blast is a disc in GLOBAL site coordinates (DemoGlobal.h: 1918 x 1080
 * sites, hubs globally contiguous): center (mX,mY), radius^2 mR2.  It is
 * shot at the hubs inside a NEW_LEADER bolt (words 2..4 of that bolt: x, y,
 * radius^2; radius^2 == 0 means no blast) and every hub applies it at the
 * exit of NEW_LEADER: after the caches are saved, before the next ones are
 * loaded, with all events stopped (PhaseSteppers.cpp).
 *
 * Every hub works on its WHOLE T6Grid, caches included, and decides per site
 * from that site's global coordinate alone.  A cache site is a copy of a
 * neighbor's self site and has that site's global coordinate, so all copies
 * of a site agree without any hub talking to another.  (Caches that the
 * next corner keeps instead of reloading stay right for the same reason.)
 */

#include "itype.h"
#include "DemoGlobal.h"

namespace MFM {

  struct NukeBlast {
    s32 mX;                     //< center, global site x
    s32 mY;                     //< center, global site y
    s32 mR2;                    //< radius squared, in sites; <= 0 means none

    bool isNone() const { return mR2 <= 0; }

    /* Is the site at global (gx,gy) inside the disc (strictly)? */
    bool hits(s32 gx, s32 gy) const {
      const s32 dx = gx - mX;
      const s32 dy = gy - mY;
      return dx * dx + dy * dy < mR2;
    }

    /* Could the disc touch the box [x0,x1) x [y0,y1)?  Cheap reject test:
       true if the point of the box nearest the center is inside the disc. */
    bool mayTouchBox(s32 x0, s32 y0, s32 x1, s32 y1) const {
      const s32 nx = (mX < x0) ? x0 : (mX > x1 - 1) ? x1 - 1 : mX;
      const s32 ny = (mY < y0) ? y0 : (mY > y1 - 1) ? y1 - 1 : mY;
      return hits(nx, ny);
    }
  };

  /* Global site coordinates of the first SELF site of the hub at hub
     position (hubx,huby) within chip `chip` (hub 0,0 is the chip's first). */
  inline void hubSelfOriginGlobal(u32 chip, u32 hubx, u32 huby,
                                  s32 & ox, s32 & oy) {
    const U16C corigin = DG::getChipOrigin(chip);
    const U16C base = DG::getSingleT6GridBaseSize();
    ox = (s32) corigin.x + (s32) (hubx * base.x);
    oy = (s32) corigin.y + (s32) (huby * base.y);
  }

}
