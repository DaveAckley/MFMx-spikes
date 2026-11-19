"""machine.py -- the MFMx-facing side of mfmxctl.

Ports what nu1011/nu1012 did in EWD.configureImageManager/run/start/stop/
slowScan, minus Textual, and adds bolt tracking.

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
import subprocess
import time

import LiveAER

DEFAULT_TIMEOUTS = {"HOLD_AT_BIRTH": 60.0, "NEW_LEADER": 60.0, "RUN_EVENTS": 10.0}
FALLBACK_TIMEOUT = 30.0


def fmt_ints(xs):
    """[1,2,3,7,9,10] -> '1-3,7,9-10'"""
    xs = sorted(xs)
    out, i = [], 0
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and xs[j + 1] == xs[j] + 1:
            j += 1
        out.append(str(xs[i]) if i == j else f"{xs[i]}-{xs[j]}")
        i = j + 1
    return ",".join(out)


class Bolt:
    def __init__(self, seq, cmd_name, args, timeout, now):
        self.seq, self.cmd, self.args = seq, cmd_name, list(args)
        self.timeout, self.fired = timeout, now
        self.done = None
        self.state = "pending"      # pending|consensus|timeout|superseded
        self.nonce = None
        self.rows = []              # [(chip, nonce, onPhase, lamers|None)]

    def elapsed(self, now):
        return (self.done if self.done is not None else now) - self.fired

    def lamers(self):
        return {c: l for (c, _n, _o, l) in self.rows if l}

    def describe(self, now):
        s = f"#{self.seq} {self.cmd}({','.join(map(str, self.args))}) {self.state.upper()}"
        s += f" {self.elapsed(now):.1f}s"
        if self.state == "pending":
            s += f"/{self.timeout:g}s"
        for c, _n, o, l in self.rows:
            if l is None:
                s += f" bh{c}:{o} acked"
            elif l:
                s += f" bh{c}:{o} acked, lamers {fmt_ints(l)}"
        return s


class NullFeed:
    """Stands in for RTMPFeed when there is no video server (mock mode)."""

    def __init__(self):
        self.frames = 0
        self.shape = None

    def sendGraphicsFrame(self, frame, simnanos, info, extra=""):
        self.frames += 1
        self.shape = getattr(frame, "shape", None)
        self.last_info = info
        self.last_extra = extra

    def idle_image(self):
        class _Idle:
            shape = (1080, 1920, 3)
        return _Idle()



# ---- eth-seam layout keys ----------------------------------------------------
# A layout may carry
#   ethseams = [ "0:E:1:6,7,10,11", "0:S:2:3,4,12,13" ]
#   e1image  = "e1fwd16.bin"                                    (optional)
#   seedchips = [0]       only these chips autoseed (default: all)   (optional)
# Each ethseams string is "chipA:DIR:chipB:x,x,..": chipB lies DIR (N, W, S or
# E, or 0-3) of chipA, joined by the ETH tiles at those NoC0 x coordinates.
# The x values must come from the cabling lists in shared/include/EthSeam.h
# (SEAM_LINKS_EW / SEAM_LINKS_NS); EthSeamHost.declareSeam enforces that.
# e1image is a bare file name searched in the same bindirs as the T6 images
# (the cross build puts e1fwd16.bin in build_cross/bin), or a path (with
# $script_dir expanded).
ETH_DIRS = {"N": 0, "W": 1, "S": 2, "E": 3}
DEFAULT_E1IMAGE = "e1fwd16.bin"


def parse_ethseams(specs, chips):
    """Parse a layout's ethseams strings into (chipA, dir4, chipB, [x..]).
    Raises ValueError on bad syntax or on a chip not in 'chips'."""
    seams = []
    for spec in specs or []:
        parts = str(spec).split(":")
        if len(parts) != 4:
            raise ValueError(f"ethseams '{spec}': want chipA:DIR:chipB:x,x,..")
        try:
            a, b = int(parts[0]), int(parts[2])
            d = parts[1].strip()
            d4 = ETH_DIRS[d.upper()] if d.upper() in ETH_DIRS else int(d)
            xs = [int(x) for x in parts[3].split(",") if x.strip()]
        except ValueError:
            raise ValueError(f"ethseams '{spec}': bad number or direction")
        if not 0 <= d4 <= 3:
            raise ValueError(f"ethseams '{spec}': direction must be N, W, S, E or 0-3")
        if not xs:
            raise ValueError(f"ethseams '{spec}': no links listed")
        for c in (a, b):
            if c not in chips:
                raise ValueError(f"ethseams '{spec}': chip {c} is not in the layout's bhchips {list(chips)}")
        seams.append((a, d4, b, xs))
    return seams


CACHE_POLICIES = ("supercell", "fourcorners")   # CachePolicyId 0, 1 (CachingPolicy.h)


def leximit(n):
    """Leximited decimal: digit count, then the digits, so names sort in
    numeric order as plain text: 0..9 -> 10..19, 10..99 -> 210..299,
    100..999 -> 3100..3999."""
    s = str(int(n))
    return f"{len(s)}{s}"


B36 = "0123456789abcdefghijklmnopqrstuvwxyz"


def b36(n):
    return B36[n]


def parse_tile_tag(text):
    """A log-style tile TAG: 'at' (optional), chip digit, NoC0 x and y as
    base 36 digits, e.g. at023 or 023.  Returns (chip, x, y)."""
    t = text.strip().lower()
    if t.startswith("at"):
        t = t[2:]
    if len(t) != 3 or t[0] not in B36[:10] or t[1] not in B36 or t[2] not in B36:
        raise ValueError(f"bad tile tag {text!r}: want atCXY (chip digit, x and y base 36), e.g. at023")
    return int(t[0]), B36.index(t[1]), B36.index(t[2])


def parse_tile_range(text):
    """TAG, or TAGLO-TAGHI meaning every chip, x and y from LO to HI
    (each coordinate independently).  Returns ((c0,c1),(x0,x1),(y0,y1))."""
    lo, sep, hi = text.partition("-")
    a = parse_tile_tag(lo)
    b = parse_tile_tag(hi) if sep else a
    for i, name in enumerate(("chip", "x", "y")):
        if a[i] > b[i]:
            raise ValueError(f"bad range {text!r}: {name} runs backwards")
    return (a[0], b[0]), (a[1], b[1]), (a[2], b[2])


def parse_policy(text):
    """'supercell' | 'fourcorners' (any unique prefix; 'corners' too) -> name.
    Raises ValueError."""
    t = str(text).strip().lower()
    if t in ("corners", "4corners", "fourcorner", "four-corners", "four"):
        return "fourcorners"
    hits = [p for p in CACHE_POLICIES if t and p.startswith(t)]
    if len(hits) != 1:
        raise ValueError(f"cache policy '{text}': want one of "
                         f"{', '.join(CACHE_POLICIES)}")
    return hits[0]


class Machine:
    def __init__(self, mfmx, config, say, *, script_dir, logprefix="mfmxctl",
                 t6s_per_chip=140, verbose=False, decay=None, alt_decay=None,
                 tt_smi="/opt/tenstorrent/pipx/bin/tt-smi", reset_chips=True,
                 extra_bindirs=(), slowscan_gap=0.2, layout=None,
                 link_wait=12.0, link_resets=5, policy="fourcorners"):
        self.M = mfmx
        self.config = config
        self.say = say
        self.scriptDir = script_dir          # RTMPFeed reads ewd.scriptDir
        self.logprefix = logprefix
        self.t6s = t6s_per_chip
        self.verbose = verbose
        self.decay, self.alt_decay = decay, alt_decay
        self.policy = parse_policy(policy)   # caching policy, applied at next 'start'
        self.tt_smi, self.do_reset = tt_smi, reset_chips
        self.extra_bindirs = list(extra_bindirs)
        self.slowscan_gap = slowscan_gap
        self.layout = layout                 # overrides [Config] activeLayout
        self.link_wait = link_wait           # preflight: secs to wait for Up
        self.link_resets = link_resets       # preflight: extra tt-smi -r tries
        self.link_poll = 0.5                 # preflight: secs between polls
        self.simDir = mfmx.getSimDir()
        self.dump_count = 0                   # dumpl1 commands that wrote files
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

    # ---- boot: configure -> chip reset -> init ----------------------------------
    # NB: the order mirrors nu1011 exactly: configure (which already opens the
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
                    # as in nu1011: every match is (re)made, so the LAST match wins
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
        touched, wait up to link_wait seconds for every declared link end
        to report Up; if some do not, run 'tt-smi -r' again, up to
        link_resets times, then give up naming the links.  Read-only."""
        h = self.M.EthSeamHost.get()
        ends = self._seam_ends()
        resets = 0
        t0 = time.monotonic()
        while True:
            deadline = time.monotonic() + self.link_wait
            while True:
                down = self._links_down(h, ends)
                if not down or time.monotonic() >= deadline:
                    break
                time.sleep(self.link_poll)
            h.releaseIdlePorts()    # no TLB window may outlive a reset
            if not down:
                note = f", after {resets} extra reset(s)" if resets else ""
                return (f"link preflight: all {len(ends)} link ends Up "
                        f"({time.monotonic() - t0:.1f}s{note})")
            names = " ".join(f"bh{c}:eth{x}(port {p})" for c, x, p in down)
            if resets >= self.link_resets or not self.do_reset:
                raise RuntimeError(
                    f"link preflight: {len(down)} of {len(ends)} link ends "
                    f"still not Up after {resets} extra chip reset(s) and "
                    f"{self.link_wait:.0f}s each: {names}")
            resets += 1
            self.say(f"link preflight: not Up after {self.link_wait:.0f}s: "
                     f"{names}; chip reset {resets} of {self.link_resets}")
            self.reset_chips()

    def init_box(self):
        M = self.M
        self.im = M.ImageManager()
        self.ewc = M.EWControl.getEWControl()
        self.qb = M.QuietBox.get()
        self.logkt("QB", f"constants: {self.qb.getConstants()}")
        self.logkt("QB", f"ranges: {self.qb.getRanges()}")
        self.qb.init()
        self.qb.mDrawGrid = True             # nu1011's default checkbox value
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
        """nu1011 start(): startEWProcessing on every chip (walks all BH init steps).
        When firePHASER is available, fires HOLD_AT_BIRTH to L1 first so T6s
        pick it up during their init (between CODE_RUNNING and EVENT_WINDOWS).
        On return, hold_settled=True means the sequencer can skip the cycle's
        HOLD_AT_BIRTH phase and go straight to NEW_LEADER."""
        self.need_booted()
        self.hold_settled = False
        out = []
        if self.seams:
            out.append(self._declare_seams())
        for bh in self.bhs:
            if self.decay is not None:
                bh.setStartDecayType(int(self.decay))
            if hasattr(bh, "setCachePolicy"):
                bh.setCachePolicy(CACHE_POLICIES.index(self.policy))
            elif self.policy != "supercell":
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
            deadline = time.monotonic() + 5.0
            counts = {}
            while time.monotonic() < deadline:
                counts = self._parse_on_phases()
                if counts and all(n >= self.t6s for n in counts.values()):
                    break
                time.sleep(0.1)
            all_acked = bool(counts) and all(n >= self.t6s for n in counts.values())
            if all_acked:
                self.hold_settled = True
                out.append(f"HOLD_AT_BIRTH phase pre-settled: "
                           f"{'/'.join(str(n) for n in counts.values())} acked; "
                           "cycle will begin at NEW_LEADER")
            else:
                out.append(f"HOLD_AT_BIRTH phase: only {counts} acked "
                           f"(expected {self.t6s}/chip); cycle will re-fire it")
        out.append(self.write_pkts())
        return "\n".join(out)

    # ---- PKTS.txt: packet-slot geometry for offline log analysis ----------------
    @staticmethod
    def tlbi_to_noc(t):
        """tlbi -> (x,y) NoC0 coords (UxC.h layout: 14 cols x 10 rows)."""
        row, col = divmod(t, 14)
        x = col + 1 + (2 if col + 1 > 7 else 0)
        return x, row + 2

    def write_pkts(self):
        """Write misc/PKTS.txt (plain text, once per start): per-blockcode car
        geometry and per-tile block addresses of packet-carrying blocks, from
        the deployed images.  Read by scripts/ptrace/ptrace.py."""
        try:
            lines = ["PKTS:V1"]
            bcs, tiles = {}, []
            for bh in self.bhs:
                chip = bh.getChipNumber()
                for t in range(self.t6s):
                    iname = bh.getTLBIImageName(t)
                    if not iname:
                        continue
                    blks = self.im.getT6Image(iname).getPacketBlocks()
                    x, y = self.tlbi_to_noc(t)
                    tiles.append(f"TILE chip={chip} tlbi={t} noc={x},{y} "
                                 f"tag={bh.getTLBITag(t)} img={iname}")
                    for (bn, addr, scnt, ssz, csz, ccnt) in blks:
                        n = bn[3:] if bn.startswith("BC_") else bn
                        bcs[n] = (scnt, ssz, csz, ccnt)
                        tiles.append(f"BLK chip={chip} tlbi={t} bc={n} "
                                     f"addr=0x{addr:x}")
            for n, (scnt, ssz, csz, ccnt) in sorted(bcs.items()):
                lines.append(f"BC name={n} storcnt={scnt} storsz={ssz} "
                             f"carsz={csz} carcnt={ccnt}")
            lines += tiles
            path = f"{self.simDir}misc/PKTS.txt"
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write("\n".join(lines) + "\n")
            return f"PKTS.txt: {len(bcs)} blockcodes, {len(tiles)} tile/block lines"
        except Exception as e:           # diagnostics must never break start()
            return f"PKTS.txt not written: {e!r}"

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

    # ---- eth-seams (see host/tools/ERISC_SEAM_DESIGN.md section 15) -------------
    def _seam_text(self):
        names = {v: k for k, v in ETH_DIRS.items()}
        return ", ".join(f"bh{a}-{names[d]}->bh{b} eth{','.join(map(str, xs))}"
                         for a, d, b, xs in self.seams)

    def _declare_seams(self):
        """Before ANY chip deploys its T6 images: CodeManager copies each
        chip's seam mask into the HostBlocks at deploy."""
        if not self.has_fire_phaser:
            raise RuntimeError(
                "ethseams need the firePHASER start path (the E1 links must come up "
                "between T6 code deploy and T6 release); this MFMx build lacks it")
        h = self.M.EthSeamHost.get()
        h.clearSeams()
        for a, d4, b, xs in self.seams:
            err = h.declareSeam(a, d4, b, xs)
            if err:
                raise RuntimeError(f"ethseam bh{a}->bh{b}: {err}")
        return f"eth-seams declared: {self._seam_text()}"

    def _bring_up_seams(self):
        """After every chip is deployed, before any T6 is released."""
        err = self.M.EthSeamHost.get().bringUpSeams(self.e1image)
        if err:
            raise RuntimeError(
                f"eth-seam bring-up failed ({err}); T6s were NOT released and no "
                f"link is left up; use 'stop'/'close' to leave the chips cleanly")
        return "eth-seams up:\n" + self.M.EthSeamHost.get().status().rstrip()

    def _down_seams(self):
        """After the T6s are stopped.  Harmless when nothing is up."""
        h = self.M.EthSeamHost.get()
        h.tearDownAll()
        return "eth-seams down"

    def seams_report(self):
        """Declared seams, link status, and per-link-end health counters."""
        if not self.seams:
            return "no ethseams in the active layout"
        h = self.M.EthSeamHost.get()
        lines = [f"declared: {self._seam_text()}",
                 f"e1image: {self.e1image}",
                 h.status().rstrip()]
        for a, _d, b, xs in self.seams:
            for x in xs:
                for chip in (a, b):
                    r = h.health(chip, x)
                    if not r.get("ok"):
                        lines.append(f"  bh{chip} eth{x}: no link")
                    else:
                        lines.append(
                            f"  bh{chip} eth{x}: port={r['port']} hb={r['heartbeat']} "
                            f"retrain={r['retrain']} fec corrected={r['fecCorrected']} "
                            f"uncorrected={r['fecUncorrected']}")
        lines.append(self.seam_counters())
        return "\n".join(lines)

    # E1 channel-block stat words (shared/include/EthSeam.h ChannelWord)
    SEAM_STATS = (("out", 8), ("in", 9), ("noc", 20),
                  ("txfail", 11), ("nocfail", 21), ("partial", 22))

    def seam_counters(self):
        """Per-channel E1 counters of every live link end.
        out = cars sent over the link, in = new cars seen from the link,
        noc = cars NoC-written into the local T6, plus the failure counts."""
        h = self.M.EthSeamHost.get()
        lines = ["channel counters (cars): out=sent to link  in=seen from link  "
                 "noc=written to local T6"]
        total = dict.fromkeys((n for n, _w in self.SEAM_STATS), 0)
        for a, _d, b, xs in self.seams:
            for x in xs:
                for chip in (a, b):
                    for ch in h.channelsOf(chip, x):
                        vals = {n: h.channelStat(chip, x, ch, w)
                                for n, w in self.SEAM_STATS}
                        if any(v > 0 for v in vals.values()):
                            lines.append(f"  bh{chip} eth{x} ch{ch}: " + " ".join(
                                f"{n}={v}" for n, v in vals.items()))
                        for n, v in vals.items():
                            total[n] += max(v, 0)
        lines.append("  total: " + " ".join(f"{n}={v}" for n, v in total.items()))
        return "\n".join(lines)

    def close(self):
        """Full retreat of every chip (resets fleet, joins transport thread, ...)."""
        self.need_booted()
        out = []
        for bh in self.bhs:
            bh.close()
            out.append(f"bh{bh.getChipNumber()} -> {self.pname(bh.getStep())}")
        return "\n".join(out)

    # ---- monitoring ---------------------------------------------------------------
    def slowscan(self, count=1):
        """nu1011 slowScan(): per chip, pace then runSlowScans(count).
        This is the hostblock-log path that still works when an 'hn' hart is hung."""
        self.need_booted()
        rets = []
        for bh in self.bhs:
            if self.slowscan_gap:
                time.sleep(self.slowscan_gap)
            rets.append(bh.runSlowScans(count))
        self.scan_count += 1
        self.last_scan = rets
        return rets

    def released(self):
        """True once every chip has been taken to CODE_RUNNING or beyond (the T6s
        have been released and have set up their hostblocks)."""
        return bool(self.bhs) and all(bh.getStep() >= self.M.Step.HAS_T6_CODE_RUNNING
                                      for bh in self.bhs)

    def dumpl1(self, items):
        """Write the whole L1 of every Tensix tile named by 'items' into a
        new directory <run>/dumps/N/ (N leximited, one per call), one
        file per tile named TAG.l1 (TAG as in the logs, e.g. at023).  Each
        item is a TAG or TAGLO-TAGHI (see parse_tile_range).  Reads only;
        safe while the tiles run.  Returns a summary line."""
        self.need_booted()
        ranges = [parse_tile_range(i) for i in items]
        todo = []
        for (c0, c1), (x0, x1), (y0, y1) in ranges:
            for c in range(c0, c1 + 1):
                for x in range(x0, x1 + 1):
                    for y in range(y0, y1 + 1):
                        if (c, x, y) not in todo:
                            todo.append((c, x, y))
        bybh = {bh.getChipNumber(): bh for bh in self.bhs}
        outdir, wrote, nottile, nochip, failed = None, 0, 0, set(), 0
        for c, x, y in todo:
            bh = bybh.get(c)
            if bh is None:
                nochip.add(c)
                continue
            if outdir is None:
                outdir = os.path.join(self.simDir, "dumps",
                                      leximit(self.dump_count))
                os.makedirs(outdir, exist_ok=True)
                self.dump_count += 1
            path = os.path.join(outdir, f"at{c}{b36(x)}{b36(y)}.l1")
            r = bh.dumpTileL1(x, y, path)
            if r == 1:
                wrote += 1
            elif r == 0:
                nottile += 1
            else:
                failed += 1
        text = f"dumpl1: {wrote} tile L1s written to {outdir}" if wrote else "dumpl1: nothing written"
        if nottile:
            text += f"; {nottile} not Tensix tiles"
        if failed:
            text += f"; {failed} FAILED (code not deployed yet, or file error)"
        if nochip:
            text += f"; no such chip: {sorted(nochip)}"
        return text

    def tick_slowscan(self):
        # Before the T6s are released their hostblocks hold no valid magic, so
        # every scan would report BAD MAGIC and dump L1 to disk.  Manual 'scan'
        # still works at any phase.
        if self.configured and self.inited and self.released():
            self.slowscan(1)

    def _run_secs(self):
        return (time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns) / 1e9

    def _live_line(self):
        """The live AER line for the overlay ('' until any hub has
        reported; see LiveAER.py).  Never raises: the overlay must not cost
        a frame."""
        if self.liveaer is None:
            return ""
        try:
            self.liveaer.update()
            text = self.liveaer.text()
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
        img = self.ewc.renderGraphicsGridWindowToImage()
        ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW) - self.t0_ns
        self.feed.sendGraphicsFrame(img.asNP(), ns,
                                    f"mfmxctl/{self.mfmxVersion}",
                                    self._live_line())
        self.frames_real += 1

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

    # ---- bolts ----------------------------------------------------------------------
    def timeout_for(self, cmd_name):
        return self.timeouts.get(cmd_name, FALLBACK_TIMEOUT)

    def fire(self, cmd_name, args=(), timeout=None):
        self.need_booted()
        try:
            cmd = getattr(self.M.PhaserBolt.Cmd, cmd_name)
        except AttributeError:
            raise ValueError(f"unknown PHASER command {cmd_name!r}; known: "
                             f"{', '.join(self.cmd_names())}") from None
        now = time.monotonic()
        prev = self.bolt
        if prev is not None and prev.state == "pending":
            prev.state, prev.done = "superseded", now
            self.say(f"bolt #{prev.seq} {prev.cmd} superseded before consensus")
        self.bolt_seq += 1
        b = Bolt(self.bolt_seq, cmd_name, args,
                 timeout if timeout is not None else self.timeout_for(cmd_name), now)
        self.bolt = b
        self.bolt_history.append(b)
        self.logkt("BOLT", f"FIRE {b.seq} {cmd_name} {b.args} timeout={b.timeout}")
        if self.async_api:
            b.nonce = self.qb.shootPHASER(cmd, list(b.args))
            self.poll_bolt()                 # may already be done (e.g. no chips)
        elif self.has_fire_phaser:
            self.qb.firePHASER(cmd, list(b.args))  # non-blocking L1 write
            # bolt stays "pending"; tick_bolts() settles via poll_bolt() -> onPhases()
        else:
            self.qb.shootPHASER(cmd, list(b.args))    # BLOCKS
            self._settle_legacy(b)
        return b

    def cmd_names(self):
        return [n for n in self.M.PhaserBolt.Cmd.__members__]

    def _settle_legacy(self, b):
        counts = self._parse_on_phases()
        bh_by_chip = {bh.getChipNumber(): bh for bh in self.bhs}
        b.rows = [(c, None, n,
                   None if n >= self.t6s
                   else list(bh_by_chip[c].getNonPhaseTLBIs()) if c in bh_by_chip
                   else ["?"])
                  for c, n in counts.items()]
        b.done = time.monotonic()
        b.state = "consensus" if all(n >= self.t6s for n in counts.values()) else "timeout"
        self._report(b)
        self._diagnose_if_timeout(b)

    def _parse_on_phases(self):
        out = {}
        for i, tok in enumerate(self.qb.onPhases().split("/")):
            if tok != "x":
                out[i] = int(tok)
        return out

    def poll_bolt(self):
        """Advance the pending bolt's state.  Returns True when it just finished.
        Supports three bolt modes:
          async_api      -- uses phaserStatus() (async C++ API)
          has_fire_phaser -- uses onPhases() polled by the transport thread
          legacy          -- bolt is already settled synchronously; no-op here"""
        b = self.bolt
        if b is None or b.state != "pending":
            return False
        now = time.monotonic()
        if self.async_api:
            b.rows = [tuple(r) for r in self.qb.phaserStatus()]
            if all(not l for (_c, _n, _o, l) in b.rows):
                b.state, b.done = "consensus", now
            elif now - b.fired > b.timeout:
                b.state, b.done = "timeout", now
            else:
                return False
        elif self.has_fire_phaser:
            # Transport thread keeps mOnPhase current; read it here.
            counts = self._parse_on_phases()
            if counts and all(n >= self.t6s for n in counts.values()):
                b.rows = [(c, None, n, []) for c, n in counts.items()]
                b.state, b.done = "consensus", now
            elif now - b.fired > b.timeout:
                counts = self._parse_on_phases()
                bh_by_chip = {bh.getChipNumber(): bh for bh in self.bhs}
                b.rows = [(c, None, n,
                           None if n >= self.t6s
                           else list(bh_by_chip[c].getNonPhaseTLBIs()) if c in bh_by_chip
                           else ["?"])
                          for c, n in counts.items()]
                b.state, b.done = "timeout", now
            else:
                return False
        else:
            return False   # legacy: bolt was settled synchronously by fire()
        self._report(b)
        self._diagnose_if_timeout(b)
        return True

    def _diagnose_if_timeout(self, b):
        if b.state == "timeout" and self.diagnose:
            if not self.released():
                self.say("  (no diagnosis slowscan: T6s not released yet; 'scan' to force one)")
                return
            try:
                self.say(f"  diagnosing: slowscan -> {self.slowscan(1)}")
                # One slowscan reads one tile per chip, round robin, so a
                # single scan rarely reaches the lamers: keep scanning (the
                # hostblock path still works when an hn hart is hung).
                if self.start_scan_ticks is not None and self.start_scan_ticks():
                    self.say("  slowscan tick turned ON to sweep every tile "
                             "(one per chip per tick; see all.mfmk)")
            except Exception as e:
                self.say(f"  diagnosis failed: {type(e).__name__}: {e}")

    def _report(self, b):
        now = time.monotonic()
        self.logkt("BOLT", f"DONE {b.describe(now)}")
        self.say(f"bolt {b.describe(now)}")

    def tick_bolts(self):
        self.poll_bolt()

    # ---- status ---------------------------------------------------------------------
    def status(self):
        lines = []
        lines.append(f"cache policy: {self.policy}")
        lines.append(f"booted: configured={self.configured} inited={self.inited} "
                     f"api={'async' if self.async_api else 'legacy' if self.inited else '-'}")
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
            lines.append(f"live: {self.liveaer.text() or 'AER: no hub has reported'}")
        return "\n".join(lines)

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
                text = self.liveaer.text()
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
