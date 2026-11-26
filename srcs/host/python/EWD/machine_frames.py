"""machine_frames.py -- Machine methods that draw the video frame: the live AER
line, the real frame and the idle frame."""
import time

from machine_util import AER_LABELS


class FrameMixin:
    """Machine methods: video frames."""

    def _run_secs(self):
        return (time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns) / 1e9

    def aer_label(self):
        """AERse / AERre for the event counting method the hubs run now."""
        return AER_LABELS.get(getattr(self, "events_running", self.events), "AER")

    def _live_line(self):
        """The live AER line for the overlay ('' until any hub has
        reported; see LiveAER.py).  Never raises: the overlay must not cost
        a frame."""
        if self.liveaer is None:
            return ""
        try:
            self.liveaer.update()
            text = self.liveaer.text(self.aer_label())
            now = time.monotonic()
            if text and (self.live_logged is None
                         or now - self.live_logged >= self.live_log_secs):
                self.live_logged = now
                self.logkt("LIVEAER", f"{self._run_secs():.0f}s {text}")
            return text
        except Exception as e:
            self.liveaer = None
            self.say(f"live AER disabled: {e}")
            return ""

    def frame(self):
        if self.feed is None:
            return "no feed"
        tf = time.perf_counter()
        img = self.ewc.renderGraphicsGridWindowToImage()
        ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns
        arr = img.asNP()
        # `grid edges on`: hub ownership edges painted on a COPY (show);
        # zoom still scans the unpainted image, so the lines never count
        # as occupied sites
        show = arr
        try:
            show = self.edges.apply(arr, getattr(self, "policy", None))
        except Exception as e:            # never lose the stream over it
            self.edges.on = False
            self.say(f"grid edges disabled: {type(e).__name__}: {e}")
        if self.zoom.mode == "off":
            arr = show
        if self.zoom.mode != "off":
            try:
                # one thread: notice a finished bolt between the zoom stages,
                # and skip the box scan while one is pending (its cost would
                # only delay seeing the bolt finish)
                b = self.bolt
                pending = b is not None and b.state == "pending"
                arr = self.zoom.apply(arr, between=self.poll_bolt,
                                      scan=not pending, show=show)
            except Exception as e:           # never lose the stream over zoom
                self.zoom.set_mode("off")
                self.say(f"zoom disabled: {type(e).__name__}: {e}")
        self.feed.scyFn = getattr(self, "scy_fn", None)
        self.feed.sendGraphicsFrame(arr, ns,
                                    f"mfmxctl/{self.mfmxVersion}",
                                    self._live_line())
        self.frames_real += 1
        if self.zoom.mode != "off":
            self.zoom.note_cost("frame", tf)

    def idle_frame(self):
        """Placeholder while there are no simulation frames (not booted yet).  The
        feed's own overlays (UTC clock, LIVE timer) tick on it, so a stream that
        shows this means ffmpeg + server are fine and only the sim is missing."""
        ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns
        self.feed.sendGraphicsFrame(self.feed.idle_image(), ns,
                                    f"mfmxctl/{self.mfmxVersion}  NO SIM FRAMES (not booted)")
        self.frames_idle += 1

    def tick_frame(self):
        if self.feed is None:
            return
        if self.inited:
            self.frame()
        elif hasattr(self.feed, "idle_image"):
            self.idle_frame()
