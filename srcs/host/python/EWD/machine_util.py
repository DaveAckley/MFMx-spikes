"""machine_util.py -- small helpers for machine.py: integer-range text, the
mock feed, layout parsing (eth-seams, tile tags) and option parsers."""


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
    """Leximited decimal: names sort in numeric order as plain text.  Up to
    8 digits it is <LEN><DIGITS>: 0..9 -> 10..19, 10..99 -> 210..299,
    100..999 -> 3100..3999.  Longer than that it is 9<LEX LEN><DIGITS>,
    the length itself leximited: 9 digits -> 919<DIGITS>, 10 digits ->
    9210<DIGITS>, 100 digits -> 93100<DIGITS>."""
    s = str(int(n))
    if len(s) <= 8:
        return f"{len(s)}{s}"
    return "9" + leximit(len(s)) + s


B36 = "0123456789abcdefghijklmnopqrstuvwxyz"


def b36(n):
    return B36[n]


def parse_tile_tag(text):
    """A log-style tile TAG: 'a' (address), a tile type letter ('t' is
    Tensix; 'e' ERISC may come later), the chip digit, then NoC0 x and y
    as base 36 digits, e.g. at023.  Returns (chip, x, y).  The prefix is
    required: tag format is tag format."""
    t = text.strip().lower()
    want = f"bad tile tag {text!r}: want atCXY (a, tile type t, chip digit, x and y base 36), e.g. at023"
    if len(t) != 5 or t[0] != "a" or t[2] not in B36[:10] or t[3] not in B36 or t[4] not in B36:
        raise ValueError(want)
    if t[1] != "t":
        raise ValueError(f"bad tile tag {text!r}: tile type {t[1]!r} is not supported (only t, Tensix)")
    return int(t[2]), B36.index(t[3]), B36.index(t[4])


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
    """'supercell' | 'fourcorners' (any unique prefix, nothing else) -> name.
    Raises ValueError."""
    t = str(text).strip().lower()
    hits = [p for p in CACHE_POLICIES if t and p.startswith(t)]
    if len(hits) != 1:
        raise ValueError(f"cache policy '{text}': want one of "
                         f"{', '.join(CACHE_POLICIES)}")
    return hits[0]


EVENT_MODES = ("shortcut", "honest")

# The overlay's name for the event counting method: AER + a suffix that
# says what happens to EMPTY draws (not "honest"/"cheating"): "se" =
# Shortcut Empties (credited as free events), "re" = Run Empties (shipped
# and run like any other event).
AER_LABELS = {"shortcut": "AERse", "honest": "AERre"}


def parse_events(text):
    """'shortcut' | 'honest' (any unique prefix, nothing else) -> name.
    shortcut: the hub credits every empty draw as a free event (cheating
    AER).  honest: every drawn center is shipped, empty or not."""
    t = str(text).strip().lower()
    hits = [e for e in EVENT_MODES if t and e.startswith(t)]
    if len(hits) != 1:
        raise ValueError(f"events mode '{text}': want one of "
                         f"{', '.join(EVENT_MODES)}")
    return hits[0]
