"""Mock of the MFMx pybind11 module -- just the surface mfmxctl needs.

Purpose: develop and test the Python side (REPL, bolt sequencing, timeouts)
with no Blackhole hardware and no tt-kmd.  Nothing here touches a device.

QuietBox flavours (async, legacy, and fire = async + firePHASER), chosen at
import time by env var MFMX_MOCK_API:

  async  (default)  PROPOSED interface:
                      shootPHASER(cmd, args) -> nonce     (returns immediately)
                      phaserStatus() -> [(chip, nonce, onPhase, [lamer tlbis])]
                      shutdown()
  legacy            CURRENT interface (as of branch terminate-textual-with-
                    prejudice): shootPHASER(cmd, args) -> None and BLOCKS per
                    chip until all tiles ack or legacy_timeout elapses; no
                    phaserStatus(), no shutdown().

Simulated tiles: each chip has NTILES (140) T6s.  A bolt makes every tile ack
after a random per-command delay, except 'lamers' which never ack.  Tiles only
ack if the chip has reached HAS_T6_CODE_RUNNING.  Everything is computed from
time.monotonic() on demand -- the mock starts no threads.

Test knobs live on MFMx._mock (see class _Mock).
"""
import enum
import os
import random
import tempfile
import time

MAX_BLACKHOLES = 4
U32_MAX = 0xFFFFFFFF


class _Mock:
    """Test/control knobs.  Not part of the real module."""

    def __init__(self):
        self.ntiles = 140
        self.rng = random.Random(1234)
        self.clock = time.monotonic
        self.sleep = time.sleep
        # per-tile ack delay ranges (seconds), by command name
        self.delays = {
            "HOLD_AT_BIRTH": (0.3, 1.0),
            "NEW_LEADER": (1.0, 3.0),   # tiles do real work before acking
            "RUN_EVENTS": (0.02, 0.3),  # tiles ack quickly
        }
        self.lamers = {}                # chip -> set(tlbi) that never ack
        self.cmd_lamers = {}            # (cmd name, chip) -> set(tlbi): never ack THAT command
        self.slow = {}                  # chip -> {tlbi: extra seconds before acking}
        self.legacy_timeout = 2.0       # real C++ is ~40s/chip; scaled down
        self.step_delay = 0.0     # seconds per phase step in changeStep
        self.simdir = None

    # -- convenience for tests / the 'mock' REPL command --
    def set_delay(self, cmd, lo, hi):
        if cmd not in self.delays:
            raise KeyError(cmd)
        self.delays[cmd] = (float(lo), float(hi))

    def set_lamers(self, chip, tlbis, cmd=None):
        """Tiles that never ack (any command, or only `cmd`)."""
        s = set(int(t) for t in tlbis)
        if cmd is None:
            self.lamers[int(chip)] = s
        else:
            if cmd not in self.delays:
                raise KeyError(cmd)
            self.cmd_lamers[(cmd, int(chip))] = s

    def set_slow(self, chip, tlbis, secs):
        """Tiles that ack `secs` later than everyone else (all commands)."""
        d = self.slow.setdefault(int(chip), {})
        for t in tlbis:
            d[int(t)] = float(secs)

    def clear_lamers(self):
        self.lamers.clear()
        self.cmd_lamers.clear()
        self.slow.clear()

    def summary(self):
        return (f"ntiles={self.ntiles} delays={self.delays} "
                f"lamers={ {c: sorted(s) for c, s in self.lamers.items()} } "
                f"cmd_lamers={ {k: sorted(s) for k, s in self.cmd_lamers.items()} } "
                f"slow={self.slow} legacy_timeout={self.legacy_timeout} api={API}")


_mock = _Mock()
API = os.environ.get("MFMX_MOCK_API", "async").lower()
if API not in ("async", "legacy", "fire"):
    raise RuntimeError(f"MFMX_MOCK_API must be async, legacy or fire, not {API!r}")


# ---------------------------------------------------------------- module funcs
def getSimDir():
    if _mock.simdir is None:
        d = os.environ.get("MFMX_MOCK_SIMDIR") or tempfile.mkdtemp(prefix="mfmx-mock-")
        os.makedirs(os.path.join(d, "misc"), exist_ok=True)
        _mock.simdir = d.rstrip("/") + "/"
    return _mock.simdir


def getVersion():
    return "mock-0.1"


# ------------------------------------------------------------------- coords
class _Coord:
    def __init__(self, x=0, y=0):
        self.x, self.y = x, y

    def __str__(self):
        return f"({self.x},{self.y})"

    __repr__ = __str__


class U8C(_Coord):
    pass


class S32C(_Coord):
    pass


# --------------------------------------------------------------- image manager
class _IMState:   # ImageManager instances are facades over one global state
    def __init__(self):
        self.modules, self.images, self.cells, self.layouts = {}, {}, {}, {}
        self.active = None


_IM = _IMState()


class CommsModule:
    def __init__(self, name):
        self.name, self.blocks = name, []

    def requireCommBlockNamed(self, b):
        self.blocks.append(b)

    def getName(self):
        return self.name

    def __repr__(self):
        return f"<CommsModule {self.name} {self.blocks}>"


class T6Image:
    def __init__(self, name, code, path):
        self.name, self.code, self.path = name, code, path

    def getBinWord(self, i):
        try:
            with open(self.path, "rb") as f:
                f.seek(4 * i)
                b = f.read(4)
            return int.from_bytes(b.ljust(4, b"\0"), "little")
        except OSError:
            return 0

    def getImageCode(self):
        return self.code

    def getBinFileSize(self):
        try:
            return os.path.getsize(self.path)
        except OSError:
            return 0

    def getBinFilePath(self):
        return self.path

    def __repr__(self):
        return f"<T6Image {self.name} code={self.code}>"


class Cell:
    def __init__(self, name, size):
        self.name, self.size, self.images = name, size, []

    def addImage(self, pos, label):
        self.images.append((pos, label))

    def toString(self):
        return repr(self)

    def __repr__(self):
        return f"<Cell {self.name} {self.size} {self.images}>"


class Layout:
    def __init__(self, name, default_image):
        self.name, self.default_image = name, default_image
        self.chips, self.cells = [], []

    def addBlackholeChip(self, n):
        self.chips.append(int(n))

    def addCell(self, c, name):
        self.cells.append((c, name))

    def getActiveBHChips(self):
        return list(self.chips)

    def getName(self):
        return self.name

    def getDefaultImage(self):
        return self.default_image

    def __repr__(self):
        return f"<Layout {self.name} chips={self.chips}>"


def _bang(d, key, what):
    if key not in d:
        raise RuntimeError(f"{what} '{key}' not found")
    return d[key]


class ImageManager:
    def makeCommsModule(self, name):
        _IM.modules[name] = CommsModule(name)
        return _IM.modules[name]

    def getCommsModule(self, name):
        return _bang(_IM.modules, name, "CommsModule")

    def makeT6Image(self, name, code, path):
        _IM.images[name] = T6Image(name, code, path)
        return _IM.images[name]

    def getT6Image(self, name):
        return _bang(_IM.images, name, "T6Image")

    def makeCell(self, name, size):
        _IM.cells[name] = Cell(name, size)
        return _IM.cells[name]

    def getCell(self, name):
        return _bang(_IM.cells, name, "Cell")

    def makeLayout(self, name, default_image):
        _IM.layouts[name] = Layout(name, default_image)
        return _IM.layouts[name]

    def setActiveLayout(self, name):
        _bang(_IM.layouts, name, "Layout")
        _IM.active = name

    def getActiveLayout(self):
        if _IM.active is None:
            raise RuntimeError("no active layout")
        return _IM.layouts[_IM.active]


# ------------------------------------------------------------------ eth seams
class EthSeamHost:
    """Mock of the host-process eth-seam owner (host/include/EthSeamHost.h).
    Models only what machine.py relies on: declare before deploy, bring up
    between T6 code deploy and T6 release, tear down.  Knob:
    MFMx._mock.ethseam_fail = "text" makes bringUpSeams fail with that text."""
    _inst = None
    CABLING_EW = (6, 7, 10, 11)
    CABLING_NS = (3, 4, 12, 13)

    @staticmethod
    def get():
        if EthSeamHost._inst is None:
            EthSeamHost._inst = EthSeamHost()
        return EthSeamHost._inst

    def __init__(self):
        self.decls = []             # (chipA, dir4, chipB, [x])
        self.up = []                # (chipA, chipB, x)
        self.events = []            # call log, for the tests

    def clearSeams(self):
        self.events.append("clearSeams")
        self.decls = []

    def declareSeam(self, chipA, dir4, chipB, ethx):
        self.events.append(f"declare {chipA} {dir4} {chipB} {list(ethx)}")
        if not 0 <= dir4 <= 3:
            return "bad direction"
        if chipA == chipB:
            return "a seam needs two different chips"
        cab = self.CABLING_EW if dir4 in (1, 3) else self.CABLING_NS
        if not ethx or len(ethx) > 4 or len(set(ethx)) != len(ethx) \
           or any(x not in cab for x in ethx):
            return "a link is not in the cabling list for that direction"
        for a, d, b, _x in self.decls:
            if {a, b} == {chipA, chipB}:
                return "seam already declared that way"
        # a chip must not be deployed yet: CodeManager reads the mask at deploy
        for c in (chipA, chipB):
            bh = _QB.chips.get(c)
            if bh is not None and bh.step >= Step.HAS_T6_CODE_DEPLOYED:
                return f"MOCK: chip {c} already deployed; declare before deploy"
        self.decls.append((chipA, dir4, chipB, list(ethx)))
        return ""

    def bringUpSeams(self, image, flags=2, ringCars=4, force=False, smoke=True):
        self.events.append(f"bringUp {image}")
        if not self.decls:
            return "no seams declared"
        fail = getattr(_mock, "ethseam_fail", "")
        if fail:
            return fail
        if not os.path.isfile(image):
            return f"cannot open E1 image {image}"
        for a, _d, b, xs in self.decls:
            for c in (a, b):
                bh = _QB.chips.get(c)
                if bh is None or bh.step < Step.HAS_T6_CODE_DEPLOYED:
                    return f"MOCK: chip {c} T6 images not deployed yet"
                if bh.step >= Step.HAS_T6_CODE_RUNNING:
                    return f"MOCK: chip {c} T6s already released"
            if len(xs) < 2:      # what the real planner/E1 does: > NCH_MAX channels
                return "bh%d:eth%d: channel number 11 >= NCH_MAX" % (a, xs[0])
        self.up = [(a, b, x) for a, _d, b, xs in self.decls for x in xs]
        return ""

    def tearDownAll(self):
        self.events.append("tearDownAll")
        self.up = []

    def chipClosing(self, chip):
        self.up = [u for u in self.up if chip not in (u[0], u[1])]

    def status(self):
        if not self.up:
            return "no eth-seam links"
        return "".join(f"link bh{a} <-> bh{b} eth{x} UP\n" for a, b, x in self.up)

    def channelsOf(self, chip, ethx):
        if not any(chip in (a, b) and ethx == x for a, b, x in self.up):
            return []
        return [0, 1, 2]

    def channelStat(self, chip, ethx, channel, word):
        """Fake counters that grow with each call, so tests can see them move:
        out/in/noc (words 8, 9, 20) count up, the failure words stay 0."""
        if not self.channelsOf(chip, ethx):
            return -1
        self.stat_calls = getattr(self, "stat_calls", 0) + 1
        return (10 * self.stat_calls + channel) if word in (8, 9, 20) else 0

    def portStatus(self, chip, ethx):
        """Preflight read.  Knob: the env var MFMX_MOCK_LINK_DOWN_POLLS=N
        makes the first N reads report port 2 (not Up)."""
        self.events.append(f"portStatus {chip} {ethx}")
        left = getattr(self, "down_polls", None)
        if left is None:
            left = int(os.environ.get("MFMX_MOCK_LINK_DOWN_POLLS", "0"))
        if left > 0:
            self.down_polls = left - 1
            return {"ok": 1, "port": 2}
        self.down_polls = 0
        return {"ok": 1, "port": 1}

    def releaseIdlePorts(self):
        self.events.append("releaseIdlePorts")
        return not self.up

    def health(self, chip, ethx):
        if not any(chip in (a, b) and ethx == x for a, b, x in self.up):
            return {"ok": 0}
        return {"ok": 1, "port": 1, "heartbeat": 1000 + chip, "retrain": 0,
                "fecCorrected": 12345, "fecUncorrected": 0}


# --------------------------------------------------------------------- phases
class Step(enum.IntEnum):
    UNINITTED = 0
    HAS_CHIP_NUM = 1
    HAS_OPEN_DEVICE = 2
    HAS_ALLOCATED_TLBS = 3
    HAS_CONFIGURED_TLBS = 4
    HAS_ALLOCATED_HOST_RAM = 5
    HAS_T6_TILES_RESET = 6
    HAS_T6_CODE_DEPLOYED = 7
    HAS_TRANSPORT_THREAD = 8
    HAS_T6_CODE_RUNNING = 9
    HAS_T6_EVENT_WINDOWS = 10


globals().update(Step.__members__)   # export_values()


class Blackhole:
    def __init__(self, chipnum):
        if chipnum >= MAX_BLACKHOLES:
            raise RuntimeError("OUT_OF_ROOM")
        if chipnum in _QB.chips:
            raise RuntimeError("DUPLICATE_ENTRY")
        self.chip = int(chipnum)
        self.step = Step.HAS_CHIP_NUM
        self.decay = None
        self.scans = 0
        self.modules = []
        _QB.chips[self.chip] = self

    # internal: C++ changeStep advances AND retreats to reach the target
    def _change(self, target):
        while self.step != target:
            if _mock.step_delay:
                _mock.sleep(_mock.step_delay)
            self.step = Step(self.step + (1 if self.step < target else -1))

    def __repr__(self):
        return f"<Blackhole:{self.chip} {self.step.name}>"

    def getChipNumber(self):
        return self.chip

    def getStep(self):
        return self.step

    def setStartDecayType(self, t):
        self.decay = int(t)

    def getNonPhaseTLBIs(self):
        """TLBIs that never acked the last bolt (the real one returns the
        non-acking list); here: this chip's lamers."""
        return sorted(_mock.lamers.get(self.chip, set()))

    def setStep(self, j):
        if Step.UNINITTED < j <= Step.HAS_T6_CODE_RUNNING:
            if j != self.step:
                self._change(Step(j))
                return True
            return False
        raise RuntimeError(f"Unknown or unhandled init step {j}")

    def addCommsModule(self, hm):
        self.modules.append(hm)

    def startEWProcessing(self):
        self._change(Step.HAS_T6_EVENT_WINDOWS)

    def stopEWProcessing(self):      # NB: advances if below CODE_RUNNING!
        self._change(Step.HAS_T6_CODE_RUNNING)

    def close(self):
        EthSeamHost.get().chipClosing(self.chip)
        self._change(Step.HAS_CHIP_NUM)

    def monitorFleet(self):
        return U32_MAX if self.step < Step.HAS_T6_CODE_DEPLOYED else 0

    def runSlowScans(self, count):
        if self.step < Step.HAS_T6_CODE_DEPLOYED:
            return -1                 # C++ returns U32_MAX as s32
        self.scans += count
        return 0


# ------------------------------------------------------------------ PHASER
class PhaserBolt:
    class Cmd(enum.Enum):
        HOLD_AT_BIRTH = 1
        NEW_LEADER = 2
        RUN_EVENTS = 3


for _n, _m in PhaserBolt.Cmd.__members__.items():    # export_values()
    setattr(PhaserBolt, _n, _m)


class _ChipPhaser:
    def __init__(self):
        self.nonce = 0
        self.ack_at = {}              # tlbi -> time or None (never)

    def fire(self, chip, cmd_name):
        self.nonce = (self.nonce + 1) & 0xFF
        lo, hi = _mock.delays[cmd_name]
        now = _mock.clock()
        runs = chip.step >= Step.HAS_T6_CODE_RUNNING
        bad = (_mock.lamers.get(chip.chip, set())
               | _mock.cmd_lamers.get((cmd_name, chip.chip), set()))
        slow = _mock.slow.get(chip.chip, {})
        self.ack_at = {}
        for t in range(_mock.ntiles):
            if not runs or t in bad:
                self.ack_at[t] = None
            else:
                self.ack_at[t] = (now + lo + (hi - lo) * _mock.rng.random()
                                  + slow.get(t, 0.0))

    def row(self, chipnum):
        now = _mock.clock()
        lam = sorted(t for t in range(_mock.ntiles)
                     if self.ack_at.get(t) is None or self.ack_at[t] > now)
        return (chipnum, self.nonce, _mock.ntiles - len(lam), lam)


# ----------------------------------------------------------------- QuietBox
class _QBCore:
    def __init__(self):
        self.chips = {}
        self.phasers = {}
        self.mDrawGrid = False
        self._inited = False
        self._ihh_suspended = True
        self._closed = False

    def get(self):
        return self

    def init(self):
        if self._inited:
            raise RuntimeError("QB::IHHThread already running")
        self._inited = True

    def suspendIHH(self, suspend):
        self._ihh_suspended = bool(suspend)

    def getConstants(self):
        return "(mock constants)"

    def getRanges(self):
        return "(mock ranges)"

    def onPhases(self):
        return "/".join(
            str(self._row(c)[2]) if c in self.chips else "x"
            for c in range(MAX_BLACKHOLES))

    def phaseIndices(self):
        return "/".join("0" if c in self.chips else "x" for c in range(MAX_BLACKHOLES))

    def loggingSummary(self):
        return "0B"

    def __str__(self):
        return self.__repr__()

    def __repr__(self):
        return "<QuietBox " + " ".join(
            f"BH{c}" if c in self.chips else "_" for c in range(MAX_BLACKHOLES)) + ">"

    # -- shared internals --
    def _row(self, c):
        return self.phasers.setdefault(c, _ChipPhaser()).row(c)

    def _check_args(self, cmd, args):
        if not isinstance(cmd, PhaserBolt.Cmd):
            raise TypeError("shootPHASER(): cmd must be a PhaserBolt.Cmd")
        args = list(args)
        if not all(isinstance(a, int) for a in args):
            raise TypeError("shootPHASER(): args must be ints")
        if self._closed:
            raise RuntimeError("QuietBox shut down")

    def _fire_chip(self, c, cmd):
        bh = self.chips[c]
        if bh.step < Step.HAS_CONFIGURED_TLBS:
            raise RuntimeError(f"chip {c} has no configured TLBs")
        self.phasers.setdefault(c, _ChipPhaser()).fire(bh, cmd.name)


class _QBAsync(_QBCore):
    def shootPHASER(self, cmd, args=()):
        self._check_args(cmd, args)
        for c in sorted(self.chips):
            self._fire_chip(c, cmd)
        return max((self.phasers[c].nonce for c in self.chips), default=0)

    def phaserStatus(self):
        return [self._row(c) for c in sorted(self.chips)]

    def shutdown(self):
        self._closed = True


class _QBFire(_QBAsync):
    """The async API plus the non-blocking firePHASER the real QuietBox has;
    machine.start() then takes its deploy / fire / release path (the one the
    eth-seam hooks need)."""
    def firePHASER(self, cmd, args=()):
        return self.shootPHASER(cmd, args)


class _QBLegacy(_QBCore):
    def shootPHASER(self, cmd, args=()):
        self._check_args(cmd, args)
        order = sorted(self.chips)
        _mock.rng.shuffle(order)
        for c in order:
            self._fire_chip(c, cmd)
            deadline = _mock.clock() + _mock.legacy_timeout
            while self._row(c)[2] < _mock.ntiles and _mock.clock() < deadline:
                _mock.sleep(0.02)
        return None


_QB = {"async": _QBAsync, "fire": _QBFire, "legacy": _QBLegacy}[API]()


class QuietBox:
    @staticmethod
    def get():
        return _QB


# ----------------------------------------------------------------- EWControl
class _Img:
    def __init__(self, n):
        self.n = n

    def asNP(self):
        try:
            import numpy as np
        except ImportError:             # mock only: real asNP() returns a numpy array
            return _FakeArr((108, 192, 3))
        a = np.zeros((108, 192, 3), np.uint8)
        a[:, :, 0] = (self.n * 7) & 0xFF
        return a


class _FakeArr:
    """numpy stand-in (shape only) so the mock runs without numpy."""
    def __init__(self, shape):
        self.shape = shape


class _EWC:
    def __init__(self):
        self.active = False
        self.frames = 0

    def getGridSize(self):
        return S32C(256, 128)

    def renderGraphicsGridWindowToImage(self):
        self.frames += 1
        return _Img(self.frames)

    def renderGridWindow(self, pos, siz, zoom):
        return "(mock grid)"

    def statsLine(self):
        return f"mock frames={self.frames}"

    def setActive(self, v):
        self.active = bool(v)

    def isActive(self):
        return self.active

    def doSeed(self, t):
        return f"Seed@{_mock.rng.randrange(256)},{_mock.rng.randrange(128)}"

    def doNuke(self, big):
        return ("NUKE!" if big else "nuke!") + f"@{_mock.rng.randrange(256)},{_mock.rng.randrange(128)}"


_EWC_INST = _EWC()


class EWControl:
    @staticmethod
    def getEWControl():
        return _EWC_INST
