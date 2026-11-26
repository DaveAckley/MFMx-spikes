# GEOMETRY (how MFMx tiles the grid onto the QuietBox)

DRAFT 2026-11-25, written from the code at b6aca6f (DemoGlobal.h,
NgbCacheMgr.h, CachingPolicy.h, HubEdges.h, EwpBlock.cpp, nu1012.dtoml)
and the docs.  Numbers below are for the CURRENT inputs (2x2 cells,
OW = OH = 20); they are descriptive, not inputs.  The inputs live in ONE
place, srcs/shared/include/DemoGlobal.h (Python mirror:
srcs/host/python/EWD/HubGeometry.py, checked by
scripts/hubgeom/hubgeom_check.sh).  Never write 137/108/177/148 as
literals in code.

See also: CLAUDE.md (doc map), OPEN_ISSUES.md (design history of four
corners, permanent edges, sawtooth), srcs/host/tools/ERISC_SEAM_DESIGN.md
(seams, sec 13 "Geometry (cell size) dependence").

## 1. Words

Two different things are both called "2x2".  Keep them apart:

- CELL: a rectangle of T6 tiles that holds ONE hub plus EWPs.  Today
  2x2 tiles (C2X2_EWP_HUB in nu1012.dtoml: fill EWP, hub at (1,1)), so
  1 hub + 3 EWPs.  Also called a REGION (REG, UREG) in the hub code.
- SUPERCELL: a 2x2 block of HUBS (so 4x4 tiles today).  Used only by the
  old CP_SUPERCELL caching policy (one leader per supercell) and by the
  global parity code (DG::getHubSuperCellCoord).  Called USCL in the hub
  code.  Four corners does not use it.

Other terms:

- T6: a Tensix tile.  A chip has 14 x 10 usable T6s.
- HUB: the T6 image that OWNS grid sites and runs the cache exchange.
  Only hubs hold grid.  35 per chip, 140 per box.
- EWP: event window processor, a T6 image with NO grid.  It receives an
  event window (EwpBlock) from its hub, runs physics, returns it.  A hub
  uses the EWPs of its OWN cell only (EwpBlock.cpp enumerates the cell;
  at most 8 are allowed).
- T6Grid: the hub's site array in L1, SELF plus its cache ring.  The
  name says T6 but only hubs have one.
- SELF: the sites a hub owns, BW x BH = 137 x 108.
- CACHE RING (overlap, OW/OH): OW = OH = 20 sites on every side of SELF,
  holding copies of neighbors' edge sites.
- BAND: one of the 8 pieces of the ring (4 edges, 4 corners), or the
  matching strip of SELF that a hub lends.
- WINDOW_RADIUS R = 4: an event window is the 41-site Manhattan diamond
  of radius 4 around its center.
- DEAD BAND: centers that no hub may use this supercycle (a policy
  artifact, temporary).
- PERMANENT EDGE: a hub side with no neighbor hub at all (array edge);
  its ring is INACCESSIBLE.

Coordinate systems (most are U8C/U16C = unsigned 8/16-bit (x,y)):

| name          | range         | what                                       |
|---------------|---------------|--------------------------------------------|
| NoC0          | 17 x 12       | physical tile coord; T6s, ETH (y=1), etc.  |
| CT6 (UCT6)    | 14 x 10       | T6-only chip coord; UxC.h converts to NoC0 |
| TLBI          | 0..139        | host's linear T6 index; UxC.h converts     |
| REG           | 7 x 5         | cell (= hub) coord on one chip, CT6 / 2    |
| global hub    | 14 x 10       | hub coord across the box; chip origins are |
|               |               | multiples of 7 x 5 (DG::getChipHubOrigin)  |
| T6Grid coord  | 177 x 148     | site in one hub's grid, ring included      |
| global site   | 1920 x 1080   | DGC; the display and nuke coordinate space |
| active site   | 1918 x 1080   | the part of DGC that some chip covers      |

## 2. From box to site (derivations)

    box image            1920 x 1080                (input)
    chip array           2 x 2                      (input)
    per chip, naive      960 x 540
    T6s per chip         14 x 10                    (input)
    cell                 2 x 2 T6s                  (input, hard-wired)
    hubs per chip        7 x 5 = 35
    SELF (BW x BH)       floor(960/7) x 540/5 = 137 x 108
    ring (OW, OH)        20, 20                     (input)
    T6Grid (TW x TH)     137+40 x 108+40 = 177 x 148
    sites per hub        14,796 owned; 26,196 stored (1.77x)
    sites per chip       959 x 540 = 517,860
    sites per box        1918 x 1080 = 2,071,440 (2 blank image columns)

Floor, not ceiling, so the hubs always fit the image (138 overran it by
12 columns).  static_asserts in DemoGlobal.h guard: hubs fit, no whole
spare column per hub, TW and TH <= 255 (u8 storage), 2*OW < BW.

Hubs are globally contiguous: hub columns and rows run straight across
chip boundaries (there is no CT6 column 0 hub; the hub sits at cell
(1,1), so the facing hub across a seam is two CT6 columns away and the
tiling stays uniform).  7 and 5 are odd, so anything that depends on hub
PARITY must count in global hub coordinates (that bug was found and
fixed for the supercell leader, ERISC_SEAM_DESIGN sec 16).

## 3. The T6Grid

Coordinates are T6Grid coords; ranges are [lo, hi).  NgbCacheMgr.h holds
the ranges (CACHE_RANGES, SITE_RANGES, HIDDEN_RANGE).

    x:  0      20     40                 137    157    177
        +------+------+------------------+------+------+  y: 0
        |  NW  |          NT cache              |  NE  |
        +------+------+------------------+------+------+     20
        |      |  NW  |   N band (lent)  |  NE  |      |
        |  WT  +------+------------------+------+  ET  |     40
        |      |      |                  |      |      |
        | cache|  W   |     HIDDEN       |  E   | cache|
        |      | band |  (never sent)    | band |      |
        |      |      |   97 x 68        |      |      |
        |      +------+------------------+------+      |    108
        |      |  SW  |   S band         |  SE  |      |
        +------+------+------------------+------+------+    128
        |  SW  |          ST cache              |  SE  |
        +------+------+------------------+------+------+    148

- The picture draws the 20 x 20 corner bands of SELF as separate boxes,
  but the edge bands run the FULL side: the N band is [20,157) x
  [20,40), 137 long, and contains the NW and NE corner bands.
- SELF = [20,157) x [20,128), 137 x 108.
- Cache ring = everything outside SELF.  Edge caches are 137 long (or
  108) by 20; corner caches are 20 x 20.
- A neighbor's cache in direction d holds a copy of OUR band facing it:
  the band is the cache range shifted inward by OW or OH
  (CACHE_TO_SITE_OFFSETS).  Edge bands and corner bands overlap at the
  corners of SELF, so a corner site of SELF has up to three copies in
  neighbors' caches.
- HIDDEN = [40,137) x [40,108), 6,596 sites (45% of SELF): never lent,
  never copied.
- Storage: 12 bytes per site (P4Atom), T6Grid = 12*177*148 + 4 = 314,356
  bytes (the literal in BlockCode.h is guarded by a hub static_assert).
  Per-site side tables on the hub: DLGridList 4 B/site (104,784 B),
  theSiteStamps u16/site (52,392 B).
- Display: a hub is drawn with its N and W ring cropped
  (DG::gridCoordToPixel: pixel = hub origin + grid coord - OW), so SELF
  lands on its own global site coords; its S and E ring overdraws the
  neighbor's pixels (which is why who/specks distinguish SELF and RING
  painters).

## 4. Caching policies (who borrows what, per supercycle)

The host picks the policy per run (`policy`, HostFlags bits); the
NEW_LEADER bolt carries the per-cycle argument.  Each hub plans each
Dir8 as SEND_SITES, RECV_CACHES or NONE.  RUN_EVENTS entry LOADs
(lenders ship bands into borrowers' caches); NEW_LEADER entry SAVEs
(borrowers ship them back into the lenders' SELF).

FOUR CORNERS (CP_FOURCORNERS, default since 2026-11-19).  The argument is
a Corner4.  Every hub expands toward that corner: it borrows the two
edge caches next to it and the corner cache between them, and lends its
three opposite bands.  The other two diagonals do not exchange.  So
every interior hub moves one long band, one short band and one corner,
in and out, every cycle.  Event areas are same-size rectangles that
slide by +-OW / +-OH as the corner changes.

Event center bounds for corner SE (CachingPolicy.h; mirror for others):

    x in [2*OW + R, TW - R)  = [44, 173)   129 wide
    y in [2*OH + R, TH - R)  = [44, 144)   100 high
    12,900 centers per hub = 87% of SELF's 14,796

Toward the corner the hub runs into the borrowed cache up to R from the
grid edge (so its windows reach OW - R = 16 sites into the neighbor's
SELF); away from it, it stays R clear of the band it lends.

CACHE ELISION (`elide on`): a direction whose role is the same under the
previous and next corner is neither saved nor reloaded
(CachingPolicies::keepAcross).  Same corner keeps 6 directions, adjacent
2, opposite 0.  While a band is kept, the LENDER's copy of it is stale
by design and the borrower holds the truth.

SUPERCELL (CP_SUPERCELL, opt-out).  The argument is a leader position
in each 2x2 supercell of hubs.  The leader borrows all 8 caches and runs
centers over [R, TW-R) x [R, TH-R) = 169 x 140; the three followers lend
and stay inside [2OW+R, TW-2OW-R) = 89 x 60.  Uneven per-site event rate
and traffic; kept as a baseline.

Before the first NEW_LEADER every hub stays clear of all caches
(boundsAvoidingCaches).

## 5. Dead bands

Where two hubs' event areas meet, no center within R of the handoff line
may run: a dead strip 2R = 8 sites wide, whatever OW is.  Under four
corners it sits OW from the hub boundary, on the side of the lending
hub, and moves between two positions per axis (2*OW apart) as the
corner's component on that axis flips.  Per hub the dead cross is about
8/137 + 8/108 = 13% of the area at any moment (matches 87% above).  It
is uneven density, not lost AER: the same events land on fewer sites.
Analysis (scripts/deadzone/deadzone_model.py, OPEN_ISSUES "sawtooth"):
2R is a lower bound for ANY fixed partition; a zig-zag only reshapes
it.  Only dynamic claims or moving boundaries could beat or blur it.

What OW actually buys (as far as the code says): OW must exceed R so
centers can cross the hub boundary; beyond that, larger OW moves the
dead strip further from the boundary and lets windows reach deeper into
the neighbor, at the cost of L1 (ring) and exchange packets (about +50%
per band going 10 -> 20).  The dead AREA does not depend on OW.  Why 20
was chosen over 10 or 15 is not recorded.  OPEN.

## 6. Permanent edges

HubEdges.h: a neighbor hub exists if it is on this chip, or across a
live eth-seam (Dir4, or the two-hop diagonal crossing).  A Dir4 side
with no neighbor is a permanent edge (`.pe:MASK`): its ring is
INACCESSIBLE (reads see INACCESSIBLE_TYPE, writes are dropped, it is
never exchanged and stays EMPTY), and event centers run right up to the
last SELF site on that side (CachingPolicies::withPermanentEdges), so
windows there are partly inaccessible.  On seam4 (whole box): 44 edge
hubs, 4 corners.

## 7. Seams and geometry

The eth-seam layer depends on the cell shape only through E, the number
of edge hubs on a seam (E/W: one per cell row, 10 / cell height; N/S:
one per cell column, 14 / cell width).  A seam needs 3E-2 channels
between two cards, 3E with the centre diagonals.  Channels per link with
4 live links (from ERISC_SEAM_DESIGN sec 13):

| cell | E/W E | ch/link | N/S E | ch/link |
|------|-------|---------|-------|---------|
| 2x2  | 5     | 4       | 7     | 5-6     |
| 2x1  | 10    | 7-8     | 7     | 5-6     |
| 1x2  | 5     | 4       | 14    | 10-11   |
| 1x1  | 10    | 7-8     | 14    | 10-11   |

NCH_MAX is 8 in the E1 image; EthSeam.h allows up to 11.

## 8. Where the assumptions live (what a geometry change touches)

The 2x2 CELL is hard-wired here (grep `CELL_C2X2`, `uct6_per_ureg`,
`_2X2`):

- srcs/shared/include/DemoGlobal.h:96-97  BLACKHOLE_ACTIVE_HUB_* = the
  2X2 counts (a 3X3 pair exists, unused).
- srcs/host/python/EWD/HubGeometry.py  ACTIVE_HUB_* = T6 count // 2.
- srcs/cross/images/hub/src/InterHub.cpp:61, 108, 152-154, 429
  (uct6_per_ureg(2,2), ureg_per_uscl(2,2), HBASSERT_EQ on the stride,
  CELL_C2X2).
- srcs/cross/images/hub/src/CacheLoader.cpp:71-72 (80-81 are
  commented-out supercell lines).
- srcs/shared/include/HubEdges.h:34, 51, 55.
- srcs/shared/include/EthSeam.h:124, 395, 402 (CELL_C2X2 defined and
  used) and srcs/host/src/EthSeamHost.cpp:244, 429.
- Tests: srcs/host/tools/ethseam_test.cpp, ethlinks_test.cpp.
- Layouts: `cell.all = C2X2_EWP_HUB` in every nu10xx/mpmd18 layout.
  C1X1_ONE, C1X2_EWP_HUB, C2X1_EWP_HUB exist in nu1012.dtoml but are
  untried with the current hub (note C2X1 is fill HUB with one EWP).
The image already carries CellBlock mCellSize / mCellStride /
mLayoutOffset, from which the hub code's constants could be derived.

The 2x2 SUPERCELL of hubs: DG::getHubSuperCellCoord, the supercell
policy, HubValue parity in CodeManager.  Four corners does not need it.

Other limits that a bigger or different hub grid would hit:

- u8 grid coords: TW, TH <= 255 (static_assert; NgbCacheMgr enums,
  U8CRange).  AtomReport puts the change stamp in the high byte of
  mCoord.x, which assumes x < 256; the proposed 16-byte AtomReport uses
  U8C coords and reserves (0xff,0xff) as the epoch marker.
- Host painter word: gy needs 8 bits (rows >= 128 once broke it).
- Hub L1: the linker gives the hub 3/4 of the 1.5 MB L1 (0x120000);
  about 278 KB free after the stamps (hub.map).  T6Grid scales as
  12*TW*TH.
- At most 8 EWPs per cell (EwpBlock.cpp HBASSERT_LT).  A 3x3 cell would
  have exactly 8.
- Seams: NCH_MAX (sec 7).
- Display/AER: SITES_PER_HUB and hub pitch come from HubGeometry.py;
  zoom region / l1grid.py use the same numbers.

## 9. Budget at a glance (per chip, today)

- 140 T6s: 35 hubs, 105 EWPs (1 : 3).
- L1: 140 x 1.5 MB = 210 MB.  Grid: 35 x 314 KB = 11 MB.
- A hub with 3/4 L1 could hold roughly 6x today's SELF in a square-ish
  T6Grid, but TW, TH <= 255 caps a hub grid at about 255 x 255 = 65,025
  sites stored before the u8 coordinate assumptions must go.

## 10. Geometry lab: other tilings (scripts/geomlab)

`python3 scripts/geomlab/geomlab.py [--out DIR]` (numpy + PIL, host
only, ~20 s) lays candidate tilings on the real 1918 x 1080 grid and
the 14 x 10 physical hub array and measures them with ONE model (see
the script's header): each supercycle the whole partition slides by a
shift vector (four corners: (+-OW, +-OH); diamond layouts: the vertex
analog (+-20, 0), (0, +-20)); a center is live iff its window stays in
one part; a hub's ring is the union of what it borrows over the
shifts.  `--check` asserts that `rect` reproduces the real hub (store
177 x 148 = 26,196, ring 11,400); `--tileable` runs the exact EW-cover
test.  Pictures land in DIR/NAME.png (whole grid, first shift) and
DIR/NAME-zoom.png (one square per site, one panel per shift, around
`--zoom X,Y`, default a hub corner in chip 0; `--zsize`, `--zscale`).
Touching hubs always get different colors; dark = dead center.

First results (2026-11-25):

    layout       hubs sites/hub min/mean/max  cv   dead% never% ring xchg ngb  hops    pack
    rect          140 14796/14796/14796      .000  11.7  0.00  0.69 4137 7.0  1(  0)  0.876
    rect-snap     140 14760/14796/14842      .002  11.7  0.00  0.69 4137 7.0  1(  0)  0.988
    dia-brick     139  9970/14902/22020      .112   9.1  0.00  0.50 3169 7.3  4(244)  0.900
    dia-rotated    65   702/31868/45000      .524   5.2  0.00  0.28 4056 6.1  1(  0)  0.938
    dia-multi48   140 11424/14796/18432      .112  10.1  0.00  0.56 3468 5.6  2(  8)  0.894
    dia-fenced48  140 11424/14796/18432      .112  16.0  0.00  0.56 3468 5.6  2(  8)  0.835
    ew-blocks     139  8192/14902/23836      .101  10.0  0.00  0.53 3112 7.2  3(218)  0.988

(dia-rotated's 5.2% is 7.7% at today's hub size: it leaves 75 hubs
idle.  hops = max physical distance to a neighbor hub (pairs > 1).
pack = per-hub best EW lattice packing.)

What they say so far:
- Dead area goes with the boundary length counted as max(|dx|,|dy|)
  per segment (2R dead centers per site along it); for a fixed area
  the shortest such boundary is a diamond (Wulff shape of the L1
  window).  Ideal diamond 9.1% vs rect 11.7%; clumps of three or four
  small diamonds (multi48) land between, at 10.1%.
- Fencing every small diamond (events may not cross ANY diamond edge)
  is worse than today: 16.0%.
- No diamond (any radius, any centre offset) and no square can be
  covered exactly by EWs; only blocks on the EW lattice (tilted about
  51 degrees, stair-step edges) can (ew-blocks 0.988, edges only).
  Diamonds pack better than rectangles (0.90 vs 0.88).
- Brick rows and EW blocks do not match the physical hub array: up to
  3-4 hub hops to a neighbor on ~220-240 pairs.  The rotated array
  matches it exactly but wastes 75 of 140 hubs.  Several small
  diamonds per hub (multi48/50) keep every hub busy and neighbors
  within 2 hops, at a 0.11 cv in sites per hub (lumpy units).
- rect-snap (added 2026-11-25): today's hubs rounded to whole EWs (an
  EW of the perfect lattice belongs to the hub holding its center;
  the majority-of-sites rule differs on 1 EW in 410 at hub (2,2)).
  Same hub array, balance, neighbors and dead area as rect, ring
  0.69 (snapped shifts; 0.71 with plain shifts), but every interior EW is whole: packing 0.988 like ew-blocks.
  Hub edges become EW sawtooth (up to 4 sites past the rectangle).
  `--ewrect` / `--ewsnap` draw one hub before and after.
  The per-cycle event areas are snapped the same way (an EW is in hub
  h's area for corner s iff its center is in h's rectangle shifted by
  s), so EVERY owned, borrowed and lent set is a union of whole EWs
  (the lab checks it: `borrowed bands whole EWs: True`).  Interior
  hubs own 360-362 EWs and borrow / lend 108-111 EWs per cycle; the
  counts differ by hub (round-off) but are a fixed function of hub
  position, corner and lattice offset, so a hub can work them out at
  init.  Membership is arithmetic: the 41 values of (4*dx + 5*dy) mod
  41 over the EW's offsets are all distinct, so a site's offset to
  its EW center is TABLE[(4x + 5y - c0) mod 41] (c0 = lattice offset),
  and its band follows from which shifted rectangle the center is in.
  Dead centers: 11.7% for random centers (a window anywhere still
  straddles the sawtooth), but 0.0% for windows ON the lattice: every
  lattice EW lies wholly in one hub's area in every cycle.
- Diamond rings cost less (0.50-0.56 of SELF vs 0.69) for the same
  Manhattan reach, and fewer neighbors are borrowed from per cycle.
Not modelled yet: cache elision, exchange packets (vs sites), the real
hub's u8 coordinate box, non-uniform shifts.

## 11. Open questions

- What OW = 20 buys over a smaller ring (sec 5).
- Whether any non-2x2 cell has ever run with the current hub image
  (believed not).
- Global coords are u16 (1920 x 1080 space) and global hub coords u8;
  fine for one QuietBox, not checked for more.
