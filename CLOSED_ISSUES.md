# CLOSED ISSUES (the wins)

What used to be on OPEN_ISSUES.md and is not any more.  Newest first, short.
Move an entry here in the SAME commit that closes it (CLAUDE.md sec 2); keep
the measured numbers, drop the blow-by-blow (git log has that).  Entries
marked PROVISIONAL stay until they have gone a few runs without coming back.

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
