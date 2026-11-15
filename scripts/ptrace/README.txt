ptrace -- packet-trace analyzer for MFMx run logs
(7-bit ASCII; python3 stdlib only; read-only; never touches hardware)

USAGE
  scripts/ptrace/ptrace.py RUNDIR [--traces] [--lanes] [--eps N] [--maxlat N]

  RUNDIR is a simulation log root, e.g. MFMX-20261114-010305, containing
    tiles/atXYZ.mfmk   per-tile expanded marks (name: 'at0' + x + y,
                       digits 0-9a-g = base 17, NoC0 coords)
    misc/mfmxctl-BOLT.txt   lamer TLBIs (used to annotate tile status)
    misc/PKTS.txt      (optional) slot layout dump, see below
  It does NOT read all.mfmk.

MARKS (structured tags; HBF_STAG_PTRACE gates them via logTagCheck)
  {fid:line tick chip x,y hH TAG:payload}
  .ps  send      OUR+SIZE[#NN]->DEST(x,y)    InterHubEP::shipTC
  .pr  recv      OUR+SIZE[#NN]<-DIR          InterHubEP::recvTC
  .pcs hub send  same layout as .ps          InterHub.cpp (corner hub)
  .pcr hub recv  same layout as .pr          InterHub.cpp (corner hub)
  .pw  waypoint  OUR+SIZE#NN[ msg]           (proposed; analyzer-side ready)
                 fid:line of the mark = which code waypoint was reached;
                 msg = tiny free text (no '}').  Needs a once-per-
                 (slot,nonce,waypoint) guard on the T6 side to avoid
                 flooding from polling loops.
  OUR/DEST are ptrTag4 = (addr>>4)&0xffff.  #NN = optional 2-hex
  TCMarker nonce (header, via getPacketNonce()).  Emitted by the T6 code
  since the commit "PTRACE marks: add #nn nonce" (needs a cross rebuild;
  older logs have no #nn and still parse).  The analyzer uses it as an
  exact key when present on both ends.

MATCHING (first cut, no nonce)
  Slots are 1-car point-to-point, so sends to an endpoint (tile,addr) and
  recvs at it are 1:1 in order.  Order-match, with sizes and a learned
  direction table as vetoes.  Ticks are checked only coarsely: observed
  send->recv latency is 0 ticks for every pair in run 20261114-010305.
  Unmatched marks are classed as unobservable (far tile logged no .p
  marks) vs possible loss.  Hub relays link .pcr -> .pcs per slot.

PKTS.txt (written by Machine.start() -> write_pkts(); plain text, one run)
  PKTS:V1
  BC name=CORNERHUB storcnt=4 storsz=16640 carsz=4160 carcnt=4
  BC name=INTERHUB storcnt=8 storsz=16640 carsz=4160 carcnt=4
  TILE chip=0 tlbi=101 noc=6,5 tag=at065 img=HUB
  BLK chip=0 tlbi=101 bc=INTERHUB addr=0x37220
  BLK chip=0 tlbi=101 bc=CORNERHUB addr=0x26d60
  Source: deployed T6Image ImageBlockAddr entries (addr, storage count) via
  T6Image::getPacketBlocks(); car size/count from the TCStorage type, in
  T6Image::getCarInfoForBlockCode() (explicit switch: add a case when another
  blockcode carries packets).  Inter-T6 destinations are NOT dumped yet; the
  analyzer still learns directions from the marks.  UNTESTED on real host
  build; analyzer parse path tested on a hand-made file.  BLK addr is assumed
  to be the T6 address the marks log; if autofit still needs a shift with a
  real PKTS.txt, that assumption is wrong.  Without PKTS.txt the analyzer
  falls back to built-in defaults (derived from run 20261114-010305).

KNOWN OBSERVATIONS (run 20261114-010305; hypotheses, not conclusions)
  - All 9 tiles with .p marks are RUN_EVENTS lamers; 13 other lamers and
    all 13 acking hubs logged no .p marks.
  - .pr appears only at even IHUB storages; odd (diagonal) storages send
    but never log a recv.  Intended: diagonal shots yield .ps, .pcr,
    .pcs, .pr.
  - Every tile's .p stream ends within ticks 30539..33665.

RUN 20261114-082557 (first run with #nn nonces)
  - Nonce reaches the receiver: ps->pr 91/91 equal, ps->pcr 16/16,
    .pcr->.pcs relay 30/30.  So (tile, slot, nonce) is a usable key.
  - Per-slot .ps nonce advances by exactly 4 between consecutive sends
    (29/29 steps).  Unwrapping the 8-bit nonce within a slot is easy.
  - Diagonal shots complete only 3 of 4 marks: all 30 .pcs target odd
    (diagonal) IHUB storages; 8 of them go to tiles that logged .p marks
    and none of those 8 produced a .pr (0 .pr at any odd storage).
  - Image rebuild moved slot bases by +2 ptrTag4 units vs the first run;
    the analyzer auto-fits a uniform shift when no PKTS.txt is present.
    This is why PKTS.txt is needed: the layout is build-dependent.

WAYPOINTS (.pw) IN THE ANALYZER
  Parsed and kept out of send/recv matching.  Report sections:
    - per-site counts (fid:line, msg)
    - for packets with no recv, the last waypoint seen at the destination
      slot with nonce n or n+1 (arrival / after re-close); "no waypoint
      seen" if none.  --traces appends up to 3 waypoints (@{fid:line msg})
      to each chain's end.
  Tested only on synthetic .pw marks derived from run 20261114-082557.

CLOCK SKEW (run 20261114-230036, 27 full cycles, 66k .p marks)
  Tile clocks differ by up to ~25 ticks on one chip, so recv-before-send
  by up to ~25 is normal.  --eps now defaults to 40 (was 2, which made ~25%
  of pairs look lost).  At eps>=30: 32771/33022 sends matched, nonce agreement
  100% (ps->pr 20812/20812, relays 6007/6007); the residue is the last ~5k
  ticks of the run.

ATOM CENSUS (hub image; analyzer section "atom census")
  Dotted, non-.p tags: always logged, even with HBF_NO_UTAG.
    .ae:TOT:INT:BAND:CACHE  hex; non-empty atoms at end of RUN_EVENTS
                            (before SAVE; leader caches hold the leader's work)
    .an:TOT:INT:BAND:CACHE  hex; same after SAVE, at exiting NEW_LEADER
    .at:2=n,4=n,5=n,o=n     hex; types DReg(2), FB1(4), FB4(5), other; follows .an
    .as:T                   autoseed fired (T = atom type placed)
  Regions, full-grid coords: INT = self sites >= OVERLAP deep (touchable by
  follower events); BAND = outer OVERLAP ring of self (cache-exchanged; only
  a leader's events touch it); CACHE = copies outside self.
  .an of the first NEW_LEADER bolt is the pre-seed state; .ae of cycle k pairs
  with .an of cycle k+1 for the "SAVE changed" comparison.
