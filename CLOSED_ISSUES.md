# CLOSED ISSUES (the wins)

What used to be on OPEN_ISSUES.md and is not any more.  Newest first, short.
Move an entry here in the SAME commit that closes it (CLAUDE.md sec 2); keep
the measured numbers, drop the blow-by-blow (git log has that).  Entries
marked PROVISIONAL stay until they have gone a few runs without coming back.

## 2026-11-21  Compressed report stream breaks (PROVISIONAL)
Cause: the host's return write of an empty ACB car (header, payload prefix,
footer, through a write-combining PCIe window into the hub's L1) could become
visible at the hub out of order.  The hub saw header == footer, wiped and
reused the car, and the late payload words (flags 0, length 0) landed in the
middle of its next fill, so the car held bytes the compressor never sent: LZ
desync, the hub shows one packet with a bad shadow checksum (.zw/.zn, .zl with
found length 0 mid-fill, recvcurrent 0).  Not the hub's recvTC wipe on the
current car, not a duplicate host return (DUPRET never fired), not transit.
Fix (host only, OurTLBs::writeToWordsOrdered for the ACB return): payload and
ankle words first, a store fence, then footer and header (4573687 had two
readbacks, 243bc0a fences only).  Results on the 140 hubs x 4 chips stress
(config/seam16.cmd and seam17.cmd, `seed 25 4`, ~3 breaks per hour before):
seam16-14 1 h clean, seam17-10 3 h clean (readbacks), seam17-11 3 h clean
(fences only, 10934 s, AER 0.945/0.949/0.949): 7 clean hours, no DESYNC, no
PKTSUM, no .zl/.zw, no ZHD file.  Before: seam16-10 4, seam16-12 2, seam16-13
3 breaks in an hour each; unstressed 4 hubs in 3 h (seam12).
Kept: the restart protocol (recovered every break), the per-packet trailer
(seq + shadow Fletcher-16), the .zl/.zr/.zs/.zn/.zw marks, DUPRET (host
refuses a return for a car that is not out).  PROVISIONAL until a few more
long runs stay clean.  Still unexplained: unstressed at323/at2e9 `zhd break`
no-show (see OPEN_ISSUES if it comes back).

## 2026-11-19  aer.py poisoned by torn counter marks
Run 002202 read 22,110,145,435,224 mAER: a hub log with a truncated hex field
(counter went backwards, delta mod 2**64).  aer.py now repairs those marks
(9 of them in 9 hubs in that run; true figure 107 AER total, 30 AER over the
last minute while the grid was still only 54 percent full).  The live AER
got the same guard.

## 2026-11-19  Live mAER on the stream
Rough live event rate on the video and in `status`, from a host tap on the
hubs' `.ev` marks (no log-file tailing).  Visible on the stream at first
build.  Its accuracy check is still on OPEN_ISSUES.md.

## 2026-11-19  Packet exchange ten times faster (sleep 10M -> 0)
A "DEBUG give hn a chance" busy-wait of 10,000,000 cycles after EVERY packet
moved was the whole cost of the exchange.  Steps, one-hour runs, every one
clean (cars out == in == noc, txfail/nocfail 0, no retrain, FEC uncorrected
0, no BADC, no bolt TIMEOUT):
  - 10M: 64 ms per packet, NEW_LEADER 1.79 s, RUN_EVENTS 2.43 s, duty 74%,
    wall AER 1166 mAER.
  - 1M:  6.4 ms per packet, bolts 0.28 / 0.36 s, duty 94%.
  - 100k: 0.8 ms, duty 97%, wall AER about 1570.
  - 0 (now): 0.2 ms per packet, slowest hub 0.005 s save / 0.011 s load.
    Duty and AER did not move again: the exchange is no longer the
    bottleneck (what is left is the host bolt path and the dwell).
Tools born on the way: `.xb/.xe` marks, scripts/xchg/xchg.py, scripts/aer/.

## 2026-11-19  ZHD counter zero-init
`#N` in the HAR lines is now a sane running count (61cb6de).

## 2026-11-19  aer.py hub-drop bias
aer.py lined cycles up by mark number and dropped every hub whose count of
.es/.ev cycles differed by more than 2: run 022401 kept 37 of 140 hubs, 121915
110, 113217 44, and the 3 h honest run 124337 ONE (the '.es' marks are lost
8% of the time, '.ev' a few).  The busy hubs lose the most marks, so the kept
sample leaned toward sparse, high-AER hubs (aer.py 5 min 36.0 vs live 13.4).
Fixed: cycles are lined up by TIME (hubs of a chip share a clock to ~40 ms, so
each chip's cycles are the clusters of its hubs' '.ev' times); a lost mark is
filled in (time from the chip, cumulative counters interpolated, so totals stay
exact); chips line up by cycle number (warning if they disagree); only a hub
that really stopped logging early is dropped.  Run 124337 now reads all 140
hubs: wall 958.6, running 986.1 mAER (live 0.956-0.958).  Run 121915 987.7 wall
(was 996.5 from 110 hubs).  Figures from earlier runs that used a hub subset
(022401, 113217, and any other with a 'left out' line) are unreliable; rerun
aer.py on the run directory if one matters (run 113217 old mode: 81690 mAER
from 44 hubs, 58230 from all 140).  Tests: scripts/aer/aer_test.py (44).
