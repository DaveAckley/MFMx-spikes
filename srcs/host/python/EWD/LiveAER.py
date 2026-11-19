"""LiveAER -- a rough, live mAER for the video overlay.

Source: MFMx.liveAERSnapshot(), the latest cumulative events (C+E of each
hub's '.ev' mark) with that mark's own hub clock in ms, kept by the host
transport thread (srcs/host/include/LiveAER.h).  Hub marks are lossy, so this
is a REFERENCE figure; scripts/aer/aer.py is the measurement.

Rates use each hub's OWN clock between two of its marks, so how stale the
latest mark is does not matter.  Per hub: events gained / seconds, over
  window  the last W seconds of that hub's clock (1 and 5 minutes),
  seed    since the autoseed (the snapshot in which the host first saw the
          '.as' mark is the baseline: everything before it counts as zero).
mAER = mean over hubs of that rate, per site (138x108 sites per hub), x1000,
the same wall-clock AER that aer.py calls 'wall'.  A hub that has not logged
for a while is dropped from the mean (its count shows in the 'h' figure).
"""
import collections
import time

SITES_PER_HUB = 138 * 108       # T6GRID_BASE_WIDTH * T6GRID_BASE_HEIGHT
WINDOWS = (60, 300)             # seconds
STALE_SECS = 90.0               # host seconds without a new mark
MIN_SPAN_FRAC = 0.5             # a window needs this much of itself filled


class LiveAER:
    def __init__(self, snapshot, clock=time.monotonic, windows=WINDOWS,
                 gap=1.0):
        self.snapshot = snapshot          # () -> (seeded, [(key, events, ms)])
        self.clock = clock
        self.windows = tuple(windows)
        self.gap = gap                    # min host secs between snapshots
        self.hist = {}                    # key -> deque of (events, ms, host_t)
        self.seed_base = None             # key -> (events, ms) at first sight
        self.last = None

    def update(self):
        """Take a snapshot if it is time.  Returns True if one was taken."""
        now = self.clock()
        if self.last is not None and now - self.last < self.gap:
            return False
        self.last = now
        seeded, hubs = self.snapshot()
        keep = max(self.windows) * 1000
        for key, ev, ms in hubs:
            h = self.hist.setdefault(key, collections.deque())
            if h and ms == h[-1][1]:
                continue                  # no new mark from this hub
            if h and ms < h[-1][1]:
                h.clear()                 # hub restarted: its clock went back
            h.append((ev, ms, now))
            while len(h) >= 2 and h[1][1] <= ms - keep:
                h.popleft()               # keep one anchor older than a window
        if seeded and self.seed_base is None:
            self.seed_base = {k: (h[-1][0], h[-1][1])
                              for k, h in self.hist.items() if h}
        return True

    def _fresh(self, now):
        return {k: h for k, h in self.hist.items()
                if h and now - h[-1][2] <= STALE_SECS}

    @staticmethod
    def _mean_maer(rates):
        if not rates:
            return None
        return 1000.0 * sum(rates) / len(rates) / SITES_PER_HUB

    def window_maer(self, secs):
        """Mean mAER over the last `secs` of each hub's clock, or None."""
        rates = []
        for h in self._fresh(self.clock()).values():
            ev1, ms1, _ = h[-1]
            old = None
            for e in h:
                if e[1] <= ms1 - secs * 1000:
                    old = e               # latest entry at least W old
            if old is None:
                old = h[0]
            span = (ms1 - old[1]) / 1000.0
            if span < secs * MIN_SPAN_FRAC:
                continue
            rates.append((ev1 - old[0]) / span)
        return self._mean_maer(rates)

    def seed_maer(self):
        """Mean mAER since the autoseed, or None before it."""
        if self.seed_base is None:
            return None
        rates = []
        for k, h in self._fresh(self.clock()).items():
            ev1, ms1, _ = h[-1]
            ev0, ms0 = self.seed_base.get(k, (h[0][0], h[0][1]))
            if ms1 <= ms0:
                continue
            rates.append((ev1 - ev0) / ((ms1 - ms0) / 1000.0))
        return self._mean_maer(rates)

    def hubs(self):
        return len(self._fresh(self.clock()))

    def text(self):
        """One short line for the overlay, '' until any hub has reported."""
        n = self.hubs()
        if n == 0:
            return ""

        def f(v):
            return "--" if v is None else "%d" % round(v)
        parts = ["%s/%dm" % (f(self.window_maer(w)), w // 60)
                 if w % 60 == 0 else "%s/%ds" % (f(self.window_maer(w)), w)
                 for w in self.windows]
        return "mAER %s %s/seed %dh" % (" ".join(parts), f(self.seed_maer()), n)
