"""machine.py -- the MFMx-facing side of mfmxctl.

Ports what the old Textual apps (nu1010-nu1012, removed; see git history)
did in EWD.configureImageManager/run/start/stop/slowScan, minus Textual,
and adds bolt tracking.

BOLTS.  Two QuietBox flavours exist (see mock/mfmx/MFMx.py):
  async API   shootPHASER() returns a nonce at once; phaserStatus() reports
              per-chip acks.  We poll it from a tick and own the timeouts.
  legacy API  shootPHASER() blocks (currently up to ~40s per chip, GIL held,
              no ticks, no Ctrl-C).  We still work, but loudly say so.
Per-command timeouts are DEFAULT GUESSES (HOLD/NEW_LEADER slow, RUN_EVENTS
fast); tune with the 'bolt timeout' command.
"""
import collections
import os
import time

import LiveAER
from zoomview import ZoomView

# Re-exported here: the commands, the sequencer and the selftest take these
# from `machine`, wherever they now live.
from machine_util import (AER_LABELS, CACHE_POLICIES, DEFAULT_E1IMAGE,
                          ETH_DIRS, EVENT_MODES, NullFeed, b36, fmt_ints,
                          leximit, parse_ethseams, parse_events, parse_policy,
                          parse_tile_range, parse_tile_tag)
from machine_bolts import (Bolt, BoltMixin, DEFAULT_TIMEOUTS,
                           FALLBACK_TIMEOUT)
from machine_boot import BootMixin
from machine_seams import SeamMixin
from machine_watch import WatchMixin
from machine_frames import FrameMixin


class Machine(BootMixin, SeamMixin, WatchMixin, FrameMixin, BoltMixin):
    """The MFMx-facing machine: state, boot, bolts, monitoring and frames
    (the methods live in the machine_*.py mixins)."""

    def __init__(self, mfmx, config, say, *, script_dir, logprefix="mfmxctl",
                 t6s_per_chip=140, verbose=False, decay=None,
                 tt_smi="/opt/tenstorrent/pipx/bin/tt-smi", reset_chips=True,
                 extra_bindirs=(), slowscan_gap=0.2, layout=None,
                 link_wait=6.0, link_resets=10, link_linger=0.0, policy="fourcorners",
                 events="shortcut", elide=False):
        self.M = mfmx
        self.config = config
        self.say = say
        self.scriptDir = script_dir          # RTMPFeed reads ewd.scriptDir
        self.logprefix = logprefix
        self.t6s = t6s_per_chip
        self.verbose = verbose
        self.decay = decay
        self.policy = parse_policy(policy)   # caching policy, applied at next 'start'
        self.events = parse_events(events)   # shortcut|honest AER, applied at next 'start'
        self.elide = bool(elide)             # keep bands shared by consecutive corners (hub flag)
        self.tt_smi, self.do_reset = tt_smi, reset_chips
        self.extra_bindirs = list(extra_bindirs)
        self.slowscan_gap = slowscan_gap
        self.layout = layout                 # overrides [Config] activeLayout
        self.link_wait = link_wait           # preflight: secs to wait for Up
        self.link_resets = link_resets       # preflight: extra tt-smi -r tries
        self.link_linger = link_linger       # preflight: extra secs a round keeps polling
        self.link_poll = 0.5                 # preflight: secs between polls
        self.simDir = mfmx.getSimDir()
        self.dump_count = 0                   # dumpl1 commands that wrote files
        self.last_dump_dir = None             # newest dumpl1 directory
        self.mfmxVersion = mfmx.getVersion()
        snap = getattr(mfmx, "liveAERSnapshot", None)   # absent in old/mock MFMx
        self.liveaer = LiveAER.LiveAER(snap) if snap else None
        self.live_log_secs = 60.0            # misc/*-LIVEAER.txt: one line this often
        self.live_logged = None              # host monotonic time of the last one
        self.bhs, self.qb, self.ewc, self.im = [], None, None, None
        self.seams = []                      # parsed ethseams of the active layout
        self.e1image = ""                    # E1 router image for the seams
        self.configured = self.inited = False
        self.async_api = False
        self.has_fire_phaser = False   # True when qb.firePHASER() is available
        self.hold_settled = False      # True when start() pre-settled HOLD_AT_BIRTH
        self.feed = None
        self.zoom = ZoomView()
        from ewoverlay import EdgeOverlay
        self.edges = EdgeOverlay()      # `grid edges on`: hub boundaries
        self.frames_real = self.frames_idle = 0
        self.t0_ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        self.timeouts = dict(DEFAULT_TIMEOUTS)
        self.diagnose = True                 # slowscan right after a bolt timeout
        self.start_scan_ticks = None         # callback: turn the periodic slowscan on
        self.bolt = None
        self.bolt_history = collections.deque(maxlen=20)
        self.bolt_seq = 0
        self.scan_count = 0
        self.last_scan = None

    # ---- logging ---------------------------------------------------------------
    def logkt(self, key, text):              # RTMPFeed/FontCache call this
        path = f"{self.simDir}misc/{self.logprefix}-{key}.txt"
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "a") as f:
            f.write(f"{key}:{text}\n")

    def trace(self, *a):
        if self.verbose:
            print(*a)

    # ---- status ---------------------------------------------------------------------
    def status(self):
        lines = []
        lines.append(f"cache policy: {self.policy}")
        lines.append(f"events mode: {self.events}")
        lines.append(f"cache elision: {'on' if self.elide else 'off'}")
        lines.append(f"booted: configured={self.configured} inited={self.inited} "
                     f"api={'async' if self.async_api else 'firePHASER' if self.has_fire_phaser else 'legacy' if self.inited else '-'}")
        if self.bhs:
            lines.append("steps: " + "  ".join(f"bh{c}={self.pname(p)}" for c, p in self.steps()))
        if self.inited:
            qb = self.qb
            lines.append(f"quietbox: onPhases {qb.onPhases()}  indices {qb.phaseIndices()}  "
                         f"log {qb.loggingSummary()}")
            lines.append(f"ewc: {self.ewc.statsLine()}")
        b = self.bolt
        lines.append("bolt: " + (b.describe(time.monotonic()) if b else "none yet"))
        lines.append("bolt timeouts: " + " ".join(f"{k}={v:g}s" for k, v in self.timeouts.items())
                     + f"  diagnose-on-timeout={self.diagnose}")
        lines.append(f"slowscans run: {self.scan_count}  last: {self.last_scan}")
        if self.seams:
            lines.append(f"eth-seams: {self._seam_text()}")
        if self.feed is not None:
            lines.append(f"feed: {type(self.feed).__name__}  frames sent: "
                         f"{self.frames_real} sim, {self.frames_idle} placeholder")
        if self.liveaer is not None:
            self.liveaer.update()
            lines.append(f"live: {self.liveaer.text(self.aer_label()) or 'AER: no hub has reported'}")
        return "\n".join(lines)
