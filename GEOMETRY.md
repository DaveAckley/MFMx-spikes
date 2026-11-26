# GEOMETRY (how MFMx tiles the grid onto the QuietBox)

BRANCH diagonal-tiles: the ring is 28 since 2026-11-26 (EWLattice10
STORE_MARGIN = SHIFT 18 + T 10; it was 22 = 18 + 4 for the step 4 runs
and the 3 h soak), so the T6Grid is 193 x 164 (379,828 bytes) for every
policy there; the numbers below were written for OW = OH = 20 (177 x
148) unless they say SHIFT 18, ewsnap or ewsnap10.

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
between two cards, 3E with the center diagonals.  Channels per link with
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
- No diamond (any radius, any center offset) and no square can be
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
  0.69 (snapped shifts; 0.71 with plain shifts), but every interior
  EW is whole: packing 0.988 like ew-blocks.
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
- EWLattice.h (branch diagonal-tiles, 2026-11-25) implements rect-snap
  with SHIFT = 18 (two close-packed EW layers).  Every band is the set
  of EWs whose centers lie in a rectangle (intersect(neighbor's rect,
  own rect moved by the Corner4 shift)); CenterIter walks those centers
  row-major (first x of a row: x = 31 * (C0 - 5y) mod 41, then +41) and
  each EW's 41 sites go in residue order, the same at both ends.  A
  permanent side reaches SHIFT + RADIUS past the grid.  ewlattice_test
  (real 14 x 10 hubs, all four corners): every site has one owner, every
  center is in exactly one event area, area = kept + one band per
  neighbor, each band equals the lender's own brute-force list in the
  same order, all stored sites inside storeBox.  Measured: interior hubs
  own 360-362 EWs, borrow 98-101 EWs per condition; storeBox 181 x 152 =
  27,512 sites (today 177 x 148 = 26,196); 51,109 EWs touch the grid.
- Step 2 (2026-11-25): geomlab's `ewlattice` layout is EWLattice.h's
  own answer (ewlattice_test --dump: owner and per-Corner4 area hub of
  every grid site), and `--crosscheck` compares it with the lab's
  independent rect-snap at the header's SHIFT: 0 sites differ in the
  owner map and in all four conditions (the same check at shift 20
  differs on ~62k sites per condition, so it can fail); it is in
  run_native_tests.sh as geomlab_crosscheck.  Measured at SHIFT 18:
      layout     dead% ring xchg  ngb hops pack  runs/hub-corner
      rect        11.7 0.69 4137  7.0 1    0.876 101 (1 per row)
      ewlattice   11.7 0.62 3754  7.0 1    0.988 217 (max 253)
  Exchange is 9% below today; dead area does not depend on the shift.
  Union of stored sites per hub is at most 24,928, under today's
  26,196, but a rectangular store needs the 181 x 152 storeBox.
- CENTER TABLES (random centers stay: MFM picks centers in an
  unspecified order, no site starved; dead sites are accepted).  A hub
  can pick uniformly among exactly its live centers for the current
  corner from a run list: the live centers as runs of consecutive x
  (row y, x0, length) in row-major order, plus a running count.  Pick
  r < N (one draw), binary-search the counts (8 steps for 256 runs),
  site = (x0 + r - count, y).  The lab measures the run lists per hub
  and corner (`center table:` note lines):
      rect         1 run per row,  101 runs per hub-corner (max 124)
      rect-snap  <=17 per row,     217 runs per hub-corner (max 256)
      dia-multi48 <=3 per row,     187 runs per hub-corner (max 305)
  (rect-snap's extra runs are the rows that cut through the sawtooth
  teeth of its top and bottom edges.)  At ~5 bytes per run that is
  about 1.3 KB per corner, 5 KB per hub for four corners.  Built at
  hub init: per corner, label each site with the hub whose shifted
  rectangle holds its EW center (TABLE lookup above), then keep sites
  whose 41 window sites all carry this hub's label (or are off-grid),
  about 4 x 26k x 41 checks.  never% = 0 in every layout: every site is
  a live center in at least one corner.  N differs between hubs (edge
  hubs have more live centers), so per-site event rates differ a
  little between hubs, as they do today at permanent edges.
Not modelled yet: cache elision, exchange packets (vs sites), the real
hub's u8 coordinate box, non-uniform shifts.

### STEP 4: first QB runs (2026-11-26)

Same script both runs (seam4, FB1 honest, elide on, dwell 4 8, cycle
repeat on, no seed, no nukes, 900 s, then a quiet pause and zoom
specks), same build (48f3a20 / dbdad67, rings 22), only the policy
differs:

    run                     AERre 1m/5m/all     bolts NL/RE s  stale@900s  specks/turds
    234532 ewsnap           0.910/0.914/0.919   0.116/0.108    1,704,826   18 / 0
    000500 fourcorners      0.914/0.917/0.922   0.116/0.101    1,064,534   22 / 0
    165051 fourcorners r20  0.932/0.933/0.936   0.113/0.098      782,404   (none taken)

0 timeouts and 0 desynced streams in all three; `.ct` on all 140 hubs
(ewsnap), live centers 9,888..18,445 per corner.  Reading: ewsnap costs
~0.3% AER against fourcorners on the same build (inside run-to-run
noise as far as known); most of the drop from 0.936 is the build (ring
22 and the rest), not ewsnap.  Stale (dropped late) reports are ~1.6x
fourcorners', all dropped correctly (0 turds); during the quiet pause
fourcorners still gained 16,593 stale, ewsnap none.  3 H SOAK (run
20261126-005008, 8d8b7a0, ewsnap, FB1 shortcut/AERse, `autonuke on 1
100 100 1000` = 200 blasts of radius 100-1000, one manual `seed 25 4`
at scy 32 after the first die-off): CLEAN for 10,871 s: 1,720 + 1,719
bolts, consensus mean 0.118 / 0.107 s, max 0.265, none over 0.3 s, 0
timeouts, 140 streams 0 desynced 0 restarts, feed 150.5 per 30 s (one
12 s gap, at the first quiet pause).  FB1 went EXTINCT at about 3,950 s
(scy ~575; AERse then sits at the empty-grid 109-110 to the end), as
liv expected under those nukes; the first hourly specks (just before)
saw 8 lone atoms, 0 turds, all STILL with current SELF history; the
later two saw 0 occupied sites.  Stale reports: 12,103,440 of 738M
(1.65%), all before the extinction (flat after), per hub 0.93-2.16%
(sd 0.23%): spread evenly over hubs, so structural (owner SAVE report
plus borrower report of the same change), not a local fault.  So the
soak proves ewsnap stable over 3 h of heavy nuking; it says nothing
about long-run fill or turds on a full grid.  WRITABLE SITES
(lab, 2026-11-26): under ewsnap every site lies in a lattice EW wholly
inside one hub's area, whose center is therefore live, so a full-window
physics (FB4) can write EVERY site in EVERY cycle: 0 sites uncovered by
any live window, vs 4,680 (0.23%) in rect's worst cycle (the corners of
each event area, where live centers are >= 8 away).  'Dead' under
ewsnap means only 'cannot be a center this cycle'.  SITE INTENSITY
(liv's measure; geomlab `intensity()`, notes line and NAME-intensity.png):
per site and cycle, how many live centers' windows contain it.  41 deep
in an area; 1 on the last site before a straight orthogonal edge; 5 on
a straight 45-degree edge (the 5 sites of a window face); 0 = unwritable.
Whole grid, per cycle:  rect P(0) 0.23% P(1) 2.91% P(<=4) 6.02%;
ewsnap 0 / 0.49% / 3.46%; dia-multi48 0 / 0.09% / 0.42%; dia-brick
0 / 0.08% / 0.37%.  BIG-TILE SNAP (liv's axis, geomlab snap4/6/10/15):
hubs keep their rectangles but own close-packed radius-T diamonds by
their centers (lattice (T,T+1),(-(T+1),T)); events stay radius 4; T >= 4
keeps every site writable.  Longer 45-degree runs as T grows:
    T    P(1)   P(<=4)  dead%  sites/hub min..max   EW pack  store max
    4    0.49%  3.46%   11.7   14760..14842         0.988    24928
    6    0.26%  2.31%   11.6   14620..14980         0.887    24990
    10   0.16%  1.48%   11.5   14201..15209         0.907    25636
    15   0.11%  1.03%   11.2   13468..16136         0.897    26936
Only T = 4 keeps hub edges on the EW lattice (whole EWs, packing 0.988);
any T > 4 gives that up (no diamond is EW-tileable, sec 10 above) and
hub loads spread (cv 0.002 -> 0.035).  By eye (liv):
less tile 'boxiness', FB1 colonies less affected by hub boundaries,
zig-zag edges visible at times.

### EWSNAP10: radius-10 tiles in the hub (2026-11-26; BUILT, NOT RUN)

`policy ewsnap10` (CachePolicyId 3, the last id the two HostFlags bits
allow) is ewsnap on radius-10 tiles (221 sites, lattice (10,11),
(-11,10)).  EWLattice.h is now a template on the tile radius,
EWLatticeT<T>: EWLattice = <4>, EWLattice10 = <10>; what does not
depend on T (Pt, Rect, shifts, accessibleRect, CenterTable, the radius-4
WINDOW offsets) is in EWGeom.  Live centers are still sites whose whole
radius-4 window lies in the area (buildCenterTable now checks the
WINDOW, not the tile).  The hub picks T at init (GridManager::mEWTile
from ewTileRadius(policy)); GridManager::buildCenterTablesT<L> and
ownsSiteT<L>, CacheLoader initEWBand<L>, and EWAnyBandWalk (holds a T=4
and a T=10 walk, mTile says which) carry it.  Cost: STORE_MARGIN = 18 +
10 = 28, so the ring is 28 for EVERY policy on this build (T6Grid
193 x 164; hub image ends at 0xfa474 of the 0x120000 L1, about 154 KB
free; +76 KB of .bss); ewsnap and fourcorners runs on this build are
not directly comparable with the ring-22 runs.  No-starvation needs
SHIFT > T + 4 (static_assert): the snapped edge's non-center zone
reaches about T + 4 either side and the area edge moves 2 SHIFT between
opposite corners, so T <= 13 with SHIFT 18.  ewlattice_test (T = 4, 8,
10; ~50 s) checks everything it checked for T = 4 plus a new full-box
check that every grid site is a live center in some corner (0 starved
for all three):
    T    sites  owned/hub (interior)  borrowed/cond  store    max runs  live/corner
    4    41     14760..14842          98..101        181x152  253       9871..18445
    8    145    14645..14935          27..29         189x160  256       9737..18513
    10   221    14365..15028          15..22         193x164  261       9444..18692
`--dump FILE T` dumps T = 4, 8 or 10; ewoverlay owner_map('ewsnap10')
matches the T = 10 dump (0 sites differ), `grid edges on` draws the
T = 10 zig-zags.  T = 8 would be one more typedef and a policy id (none
left in the two HostFlags bits).

### STEP 3: rect-snap in the hub (branch diagonal-tiles; BUILT, RUN)

Status 2026-11-25: A-G built as planned.  Cross build of hub and ewp
clean; hub L1 ends at 0xe2c9c of 0x120000 (~245 KB free; the center
tables are 5 x 1608 B, their build scratch 6878 B); native suite 27/27;
mock selftest scenario cache_policy checks `policy ewsnap` -> policy=2.
Not yet run on the QB (step 4).  The plan as written:

From reading CacheLoader.cpp, Grid.cpp (seekRandomNonEmptySite), T6Grid.h,
CachingPolicy.h, HostFlags.h, machine_boot.py.

A. A THIRD CACHE POLICY, not a compile flag.  HostFlags already carries a
   2-bit policy id (HBF_CACHEPOL0/1); CP_EWSNAP = 2 (`policy ewsnap` in
   mfmxctl).  One image runs fourcorners or ewsnap per run, so the A/B
   comparison uses the same build.  Its Dir8 roles (borrow 3, lend 3)
   and NEW_LEADER Corner4 argument are four corners'; sequencer.py's
   `cycle repeat` check must accept it too.
B. STORAGE.  T6GRID_OVERLAP 20 -> 22 (= STORE_MARGIN), so the T6Grid is
   the 181 x 152 storeBox for both policies; HubGeometry.py, the
   BlockCode.h T6GRID literal (12*181*152+4 = 330,148) and
   hubgeom_check follow; static_assert OVERLAP >= STORE_MARGIN.  Side
   effect: fourcorners on this branch runs with 22-wide rings (an OW
   change like 10 -> 15 -> 20 before), so the baseline is rerun on this
   build.  L1: +16 KB grid, +~8 KB per-site tables, ~10 KB center
   tables, of ~278 KB free (check hub.map).
C. BANDS.  Disjoint minimal bands, band(H, N, c) = N's EWs inside H's
   event area: no site travels to two neighbors (today's rect strips
   overlap at the corner squares: W, N and NW each get a copy of a SELF
   corner and all three send it back at SAVE).  Elision then keeps a
   direction only when its band rectangle is the same under the old and
   new corner (same corner: all; others: none at first).  A hub needs a
   neighbor's rectangle: same as its own shifted by the hub pitch, with
   only the permanent sides that run parallel to the offset (they are
   shared); the native test checks this against every hub's own view
   for the 1-, 2- and 4-chip layouts.
D. WALKING A BAND (CacheLoader).  A resumable EWLattice::BandWalk
   (CenterIter + site index k + ordinal) yields hub-local coordinates
   of the ACCESSIBLE sites of the band's EWs in canonical order.
   Accessible = not beyond a permanent side of the walking hub (the
   rule isAccessible already uses, NOT the global grid: a 1-chip run's
   edge is mid-grid); both ends agree because the sides involved are
   shared (tested).  CacheLoader keeps a U8CRange iterator for the old
   policies and a BandWalk for ewsnap.  The packet's mStartsAt check
   (BADC) becomes the walk ordinal (u16 packed into the U8C).  unload's
   "into SELF" test (DL report vs quiet) becomes "an EW we own".
E. EVENT CENTERS.  Five center tables (no corner yet, SE, SW, NW, NE)
   built at hub init by EWLattice code: label each stored site with the
   hub whose area holds its EW center, keep the sites whose 41 window
   sites carry our label or are inaccessible, store runs (y, x0, len)
   plus running counts; capacity ~320 runs per table, overflow is an
   ENGINE_FAIL at init.  seekRandomNonEmptySite draws r < N and
   binary-searches when the policy is ewsnap; mark `.ct:N0:N1:N2:N3:N4`
   (live centers per table) and the init time.
F. HOST.  mfmxctl `policy ewsnap` (machine_util, cmd_machine, --policy),
   Blackhole.setCachePolicy comment, l1grid.py (OW 22),
   cachepolicy_test at OW 22.  Display crop uses OW automatically; a
   hub's sawtooth tip sites outside its rectangle are labelled RING by
   who/specks (cosmetic, noted).
G. TESTS FIRST.  EWLattice additions (neighbor rects, BandWalk,
   accessibility, center-table builder) native-tested before any hub
   edit; then cross build hub + ewp; native suite; mock selftest.
   Step 4: one experiment.cmd running fourcorners and ewsnap back to
   back on the same build (AER, bolts, xchg.py, zhd, turds, `.ct`).

## 11. Open questions

- What OW = 20 buys over a smaller ring (sec 5).
- Whether any non-2x2 cell has ever run with the current hub image
  (believed not).
- Global coords are u16 (1920 x 1080 space) and global hub coords u8;
  fine for one QuietBox, not checked for more.
