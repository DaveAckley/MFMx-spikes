"""machine_boot.py -- Machine methods that configure, reset, init, start, stop,
close and shut down the machine."""
import os
import subprocess
import time

from machine_util import CACHE_POLICIES, DEFAULT_E1IMAGE, parse_ethseams


class BootMixin:
    """Machine methods: boot and lifecycle."""

    # ---- boot: configure -> chip reset -> init ----------------------------------
    # NB: the order mirrors the old Textual app exactly: configure (which already opens the
    # devices and allocates TLBs via setStep) happens BEFORE 'tt-smi -r'.  That
    # looks odd -- see the notes in the hand-off message; kept as-is on purpose.
    def configure(self):
        """Port of EWD.configureImageManager()."""
        M = self.M
        cfg = self.config
        cfg.load()
        if self.layout:
            have = list(cfg.hash["layout"].keys())
            if self.layout not in have:
                raise RuntimeError(f"unknown layout '{self.layout}'; the config has {have}")
            cfg.hash["Config"]["activeLayout"] = self.layout
        if self.extra_bindirs:
            cfg.hash["Config"]["bindirs"] = self.extra_bindirs + list(cfg.hash["Config"]["bindirs"])
        im = self.im = M.ImageManager()
        for k, v in cfg.hash.get("module", {}).items():
            hm = im.makeCommsModule(k)
            for b in v.get("blocks", []):
                hm.requireCommBlockNamed(b)
        keys = []
        for k, v in cfg.hash["image"].items():
            found = None
            for bindir in cfg.hash["Config"]["bindirs"]:
                bd = os.path.abspath(bindir.replace("$script_dir", self.scriptDir, 1))
                if not os.path.isdir(bd):
                    continue
                maybe = f"{bd}/{v['binfile']}"
                if os.path.isfile(maybe):
                    # as in the old Textual app: every match is (re)made, so the LAST match wins
                    img = im.makeT6Image(k, v["code"], maybe)
                    for i, tag in ((5, "BINPOS"), (6, "BINP2S"), (7, "BINP3S")):
                        self.logkt("KEYK", f"{tag} {k} {hex(img.getBinWord(i))}")
                    keys.append(k)
                    found = maybe
            if found:
                self.trace(f"image {k}: {found}")
            else:
                self.say(f"binfile {v['binfile']} for image {k} not found in any bindir")
        for k, c in cfg.hash["cell"].items():
            cell = im.makeCell(k, M.U8C(*c["size"]))
            if c.get("fill"):
                im.getT6Image(c["fill"])
                cell.addImage(M.U8C(255, 255), c["fill"])
            for x, v in (c.get("image") or {}).items():
                y = next(iter(v.keys()))
                val = v[y]
                x = 255 if x == "all" else int(x)
                y = 255 if y == "all" else int(y)
                im.getT6Image(val)
                cell.addImage(M.U8C(x, y), val)
        layouts = cfg.hash["layout"]
        for k, l in layouts.items():
            lay = im.makeLayout(k, l["defaultImage"])
            for bhc in l["bhchips"]:
                lay.addBlackholeChip(int(bhc))
            if l.get("fill"):
                im.getCell(l["fill"])
                lay.addCell(255, l["fill"])
            for c, cellname in (l.get("cell") or {}).items():
                lay.addCell(255 if c == "all" else int(c), cellname)
        act = cfg.hash["Config"]["activeLayout"]
        modules = layouts[act].get("hostModules", [])
        im.setActiveLayout(act)
        chips = im.getActiveLayout().getActiveBHChips()
        self.seams = parse_ethseams(layouts[act].get("ethseams"), list(chips))
        self._set_seed_chips(layouts[act].get("seedchips"), list(chips))
        self.e1image = self.find_e1image(layouts[act].get("e1image", DEFAULT_E1IMAGE))
        self.bhs = [M.Blackhole(i) for i in chips]
        for bh in self.bhs:                  # far enough to configure modules
            bh.setStep(M.Step.HAS_ALLOCATED_TLBS)
        for bh in self.bhs:
            for hmname in modules:
                bh.addCommsModule(im.getCommsModule(hmname))
        self.configured = True
        return f"layout '{act}', chips {list(chips)}, images {keys}, modules {modules}"

    @staticmethod
    def _set_seed_chips(seedchips, chips):
        """A layout's seedchips = [0] limits autoseeding to those chips (read by
        CodeManager from MFMX_AUTOSEED_CHIPS at deploy); absent = every chip
        seeds, the old behavior."""
        if seedchips is None:
            os.environ.pop("MFMX_AUTOSEED_CHIPS", None)
            return
        sc = [int(c) for c in seedchips]
        for c in sc:
            if c not in chips:
                raise ValueError(f"seedchips: chip {c} is not in the layout's bhchips {chips}")
        os.environ["MFMX_AUTOSEED_CHIPS"] = ",".join(str(c) for c in sc)

    def find_e1image(self, name):
        """Resolve the E1 image: a path as given, or a bare name searched in
        the config's bindirs like the T6 images (last match wins).  If it is
        not found, return the first candidate path so the host's 'cannot
        open E1 image' error names a useful place."""
        name = name.replace("$script_dir", self.scriptDir, 1)
        if os.sep in name:
            return os.path.abspath(name)
        found = first = None
        for bindir in self.config.hash["Config"]["bindirs"]:
            bd = os.path.abspath(bindir.replace("$script_dir", self.scriptDir, 1))
            first = first or os.path.join(bd, name)
            if os.path.isfile(os.path.join(bd, name)):
                found = os.path.join(bd, name)
        return found or first or os.path.abspath(name)

    def reset_chips(self):
        if not self.do_reset:
            return "chip reset skipped"
        r = subprocess.run([self.tt_smi, "-r"])
        return f"tt-smi -r exit {r.returncode}"

    def _seam_ends(self):
        """Every (chip, ethx) end of every declared seam link, sorted."""
        return sorted({(c, x) for a, _d, b, xs in self.seams
                       for x in xs for c in (a, b)})

    def _links_down(self, h, ends):
        """The ends whose port status is not 1 (Up), as (chip, ethx, port);
        port is None if the chip could not be read."""
        down = []
        for chip, x in ends:
            r = h.portStatus(chip, x)
            if not r.get("ok"):
                down.append((chip, x, None))
            elif r["port"] != 1:
                down.append((chip, x, r["port"]))
        return down

    def preflight_links(self):
        """After a chip reset the base firmware trains every Ethernet link
        in the background, and some links come up late or not at all (the
        intermittent 'link not Up (port status 2)').  Before any E1 is
        touched, wait up to link_wait (+ link_linger) seconds for every
        declared link end to report Up; if some do not, run 'tt-smi -r'
        again, up to link_resets times, then give up naming the links.
        Read-only.

        Timing: each round notes, per end, the seconds from the start of
        the round (just after the reset) to the first poll that saw it Up;
        one 'link timing' line per round (all ends in the log, kind
        LINKTIME).  link_linger adds seconds to every round's wait, to see
        whether stragglers ever come Up late (a round that makes it during
        the linger is accepted; the line says how late)."""
        h = self.M.EthSeamHost.get()
        ends = self._seam_ends()
        resets = 0
        t0 = time.monotonic()
        while True:
            tr = time.monotonic()
            first_up = {}
            deadline = tr + self.link_wait + self.link_linger
            while True:
                down = self._links_down(h, ends)
                el = time.monotonic() - tr
                downset = {(c, x) for c, x, _p in down}
                for e in ends:
                    if e not in downset and e not in first_up:
                        first_up[e] = el
                if not down or time.monotonic() >= deadline:
                    break
                time.sleep(self.link_poll)
            h.releaseIdlePorts()    # no TLB window may outlive a reset
            self._say_link_timing(resets + 1, ends, first_up, down)
            if not down:
                note = f", after {resets} extra reset(s)" if resets else ""
                return (f"link preflight: all {len(ends)} link ends Up "
                        f"({time.monotonic() - t0:.1f}s{note})")
            names = " ".join(f"bh{c}:eth{x}(port {p})" for c, x, p in down)
            if resets >= self.link_resets or not self.do_reset:
                raise RuntimeError(
                    f"link preflight: {len(down)} of {len(ends)} link ends "
                    f"still not Up after {resets} extra chip reset(s) and "
                    f"{self.link_wait + self.link_linger:.0f}s each: {names}")
            resets += 1
            self.say(f"link preflight: not Up after "
                     f"{self.link_wait + self.link_linger:.0f}s: "
                     f"{names}; chip reset {resets} of {self.link_resets}")
            self.reset_chips()

    def _say_link_timing(self, rnd, ends, first_up, down):
        """One summary line per preflight round; every end's time to the log."""
        times = sorted(first_up.values())
        detail = " ".join(f"bh{c}:eth{x}={first_up[(c, x)]:.1f}s"
                          for c, x in ends if (c, x) in first_up)
        stuck = " ".join(f"bh{c}:eth{x}" for c, x, _p in down)
        self.logkt("LINKTIME", f"round {rnd}: {detail}; down: {stuck or 'none'}")
        if times:
            s = (f"link timing round {rnd}: {len(times)}/{len(ends)} Up, "
                 f"first {times[0]:.1f}s, last {times[-1]:.1f}s")
        else:
            s = f"link timing round {rnd}: 0/{len(ends)} Up"
        late = [(e, t) for e, t in first_up.items() if t > self.link_wait]
        if late:
            s += "; LATE (after the normal wait): " + " ".join(
                f"bh{c}:eth{x}={t:.1f}s" for (c, x), t in sorted(late))
        self.say(s)

    def init_box(self):
        M = self.M
        self.im = M.ImageManager()
        self.ewc = M.EWControl.getEWControl()
        self.qb = M.QuietBox.get()
        self.logkt("QB", f"constants: {self.qb.getConstants()}")
        self.logkt("QB", f"ranges: {self.qb.getRanges()}")
        self.qb.init()
        self.qb.mDrawGrid = True             # the old app's default checkbox value
        self.async_api = hasattr(self.qb, "phaserStatus")
        self.has_fire_phaser = hasattr(self.qb, "firePHASER")
        self.inited = True
        if self.async_api:
            return "async bolt API (phaserStatus available)"
        elif self.has_fire_phaser:
            return ("non-blocking firePHASER available: bolts are non-blocking; "
                    "start() will pre-settle HOLD_AT_BIRTH")
        else:
            return ("LEGACY blocking shootPHASER: bolts freeze this shell (and its ticks) "
                    "until consensus or the C++ timeout")

    def boot(self):
        out = [self.configure(), self.reset_chips()]
        if self.seams:
            out.append(self.preflight_links())
        out.append(self.init_box())
        return "\n".join(out)

    def need_booted(self):
        if not (self.configured and self.inited):
            raise RuntimeError("not booted: run 'boot' first")

    # ---- lifecycle ----------------------------------------------------------------
    def steps(self):
        return [(bh.getChipNumber(), bh.getStep()) for bh in self.bhs]

    @staticmethod
    def pname(p):
        return getattr(p, "name", str(p))

    def start(self):
        """old start(): startEWProcessing on every chip (walks all BH init steps).
        When firePHASER is available, fires HOLD_AT_BIRTH to L1 first so T6s
        pick it up during their init (between CODE_RUNNING and EVENT_WINDOWS).
        On return, hold_settled=True means the sequencer can skip the cycle's
        HOLD_AT_BIRTH phase and go straight to NEW_LEADER."""
        self.need_booted()
        self.hold_settled = False
        out = []
        if self.seams:
            out.append(self._declare_seams())
        self.events_running = self.events      # what the hubs now do (AER label)
        for bh in self.bhs:
            if self.decay is not None:
                bh.setStartDecayType(int(self.decay))
            if hasattr(bh, "setCachePolicy"):
                bh.setCachePolicy(CACHE_POLICIES.index(self.policy))
            if hasattr(bh, "setEveryEvent"):
                bh.setEveryEvent(self.events == "honest")
            elif self.events != "shortcut":
                raise RuntimeError(f"this MFMx module cannot set the events mode "
                                   f"(wanted {self.events}); rebuild the host")
            if hasattr(bh, "setElideCaches"):
                bh.setElideCaches(self.elide)
            elif self.elide:
                raise RuntimeError("this MFMx module cannot set cache elision; "
                                   "rebuild the host")
            if not hasattr(bh, "setCachePolicy") and self.policy != "supercell":
                raise RuntimeError(f"this MFMx module cannot set the cache policy "
                                   f"(wanted {self.policy}); rebuild the host")
            t = time.monotonic()
            if self.has_fire_phaser:
                # Advance to HAS_T6_CODE_DEPLOYED: TLBs are now configured and
                # T6 code is in L1, but T6 cores are still in reset.
                # firePHASER needs the multicast TLB (AHAX_TLBI_L1_MULTI) set up;
                # calling it earlier (before HAS_CONFIGURED_TLBS) segfaults.
                bh.setStep(self.M.Step.HAS_T6_CODE_DEPLOYED)
            else:
                bh.startEWProcessing()
            out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())} "
                       f"({time.monotonic() - t:.1f}s)")
        if self.seams:
            # every chip is deployed, no T6 is released yet
            out.append(self._bring_up_seams())
        if self.has_fire_phaser:
            # T6 cores are in reset: safe to write HOLD_AT_BIRTH to L1 now.
            # T6s will see it the moment they're released by startEWProcessing.
            self.say("start: firing HOLD_AT_BIRTH phase to L1 before T6 release")
            self.qb.firePHASER(self.M.PhaserBolt.Cmd.HOLD_AT_BIRTH, [0])
            for bh in self.bhs:
                t = time.monotonic()
                bh.startEWProcessing()   # starts transport thread, releases T6s
                out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())} "
                           f"({time.monotonic() - t:.1f}s)")
            # Poll for acks: T6s run birth code, see HOLD_AT_BIRTH, ack, and HOLD.
            # A re-fire is NOT harmless: a hub that already took the first
            # HOLD_AT_BIRTH asserts ILLEGAL_STATE (_switchToExiting, same cmd)
            # and wedges.  Hub init grows with the grid (5 s was too tight at
            # OW=20: one hub late -> re-fire -> every hub wedged), so wait long.
            t0 = time.monotonic()
            deadline = t0 + 60.0
            counts = {}
            while time.monotonic() < deadline:
                counts = self._parse_on_phases()
                if counts and all(n >= self.t6s for n in counts.values()):
                    break
                time.sleep(0.1)
            out.append(f"HOLD_AT_BIRTH ack wait: {time.monotonic() - t0:.1f}s")
            all_acked = bool(counts) and all(n >= self.t6s for n in counts.values())
            if all_acked:
                self.hold_settled = True
                out.append(f"HOLD_AT_BIRTH phase pre-settled: "
                           f"{'/'.join(str(n) for n in counts.values())} acked; "
                           "cycle will begin at NEW_LEADER")
            else:
                # Do NOT re-fire: tiles that already hold it assert
                # ILLEGAL_STATE and wedge.  Stragglers show up as lamers of
                # the first NEW_LEADER instead.
                self.hold_settled = True
                out.append(f"HOLD_AT_BIRTH phase: only {counts} acked "
                           f"(expected {self.t6s}/chip); NOT re-firing it, "
                           "cycle will begin at NEW_LEADER and name any laggards")
        out.append(self.write_pkts())
        return "\n".join(out)

    def consume_hold_settled(self):
        """Return True (once) if start() pre-settled HOLD_AT_BIRTH; clear the flag."""
        r, self.hold_settled = self.hold_settled, False
        return r

    def stop(self):
        """stopEWProcessing only if already at EVENT_WINDOWS: the C++ call
        retreats-or-ADVANCES to CODE_RUNNING, so calling it too early would
        bring a chip UP."""
        self.need_booted()
        out = []
        for bh in self.bhs:
            if bh.getStep() >= self.M.Step.HAS_T6_EVENT_WINDOWS:
                bh.stopEWProcessing()
                out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())}")
            else:
                out.append(f"bh{bh.getChipNumber()} not at event windows "
                           f"({self.pname(bh.getStep())}); left alone")
        if self.seams:
            out.append(self._down_seams())
        return "\n".join(out)

    def close(self):
        """Full retreat of every chip (resets fleet, joins transport thread, ...)."""
        self.need_booted()
        out = []
        for bh in self.bhs:
            bh.close()
            out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())}")
        return "\n".join(out)

    # ---- shutdown --------------------------------------------------------------------
    def shutdown(self, close=False):
        """Best-effort orderly teardown; every step is attempted."""
        out = []

        def step(name, fn):
            try:
                r = fn()
                out.append(f"{name}: {r if r else 'ok'}")
            except Exception as e:
                out.append(f"{name}: FAILED {type(e).__name__}: {e}")

        if self.liveaer is not None:         # the last live AER, for the record
            def final():
                text = self.liveaer.text(self.aer_label())
                if not text:
                    return "no hub had reported"
                self.logkt("LIVEAER", f"{self._run_secs():.0f}s FINAL {text}")
                return text
            step("live AER final", final)

        if self.configured and self.inited:
            if any(p >= self.M.Step.HAS_T6_EVENT_WINDOWS for _c, p in self.steps()):
                step("stop events", self.stop)
            if close:
                step("close chips", self.close)
        if self.qb is not None and hasattr(self.qb, "shutdown"):
            step("quietbox shutdown", self.qb.shutdown)
        return "\n".join(out)
