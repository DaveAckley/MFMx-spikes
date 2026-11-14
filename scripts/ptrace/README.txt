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
  OUR/DEST are ptrTag4 = (addr>>4)&0xffff.  #NN = optional 2-hex
  TCMarker nonce; NOT YET EMITTED (proposed: insert after SIZE in all
  four formatters).  The analyzer uses it as an exact key when present.

MATCHING (first cut, no nonce)
  Slots are 1-car point-to-point, so sends to an endpoint (tile,addr) and
  recvs at it are 1:1 in order.  Order-match, with sizes and a learned
  direction table as vetoes.  Ticks are checked only coarsely: observed
  send->recv latency is 0 ticks for every pair in run 20261114-010305.
  Unmatched marks are classed as unobservable (far tile logged no .p
  marks) vs possible loss.  Hub relays link .pcr -> .pcs per slot.

PKTS.txt (proposed; host writes once at startup; parser written, untested)
  PKTS:V1 chip=0
  BC name=INTERHUB storcnt=8 storsz=16640 carsz=4160 carcnt=4
  BC name=CORNERHUB storcnt=4 storsz=16640 carsz=4160 carcnt=4
  TILE tlbi=101 noc=6,5 img=HUB
  BLK tlbi=101 bc=INTERHUB addr=0x37220
  BLK tlbi=101 bc=CORNERHUB addr=0x26d60
  BC values come from BLOCKCODE_LIST + the TC types.  Without PKTS.txt the
  analyzer falls back to built-in defaults (IHUB base 0x3722, CHUB base
  0x26d6 in ptrTag4 units), derived from run 20261114-010305 only.

KNOWN OBSERVATIONS (run 20261114-010305; hypotheses, not conclusions)
  - All 9 tiles with .p marks are RUN_EVENTS lamers; 13 other lamers and
    all 13 acking hubs logged no .p marks.
  - .pr appears only at even IHUB storages; odd (diagonal) storages send
    but never log a recv.  Intended: diagonal shots yield .ps, .pcr,
    .pcs, .pr.
  - Every tile's .p stream ends within ticks 30539..33665.
