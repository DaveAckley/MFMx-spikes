"""Machine lifecycle, configuration and inspection commands.

Each command is a module-level function taking (ctx, args); register(ctx)
adds them to the shell.  ctx is the cmdutil.Ctx built by mfmxctl.main."""
from functools import partial

from cmdutil import kw
from machine_util import EW_TILE_RADIUS


def c_step(ctx, a):
    m, MFMx = ctx.m, ctx.MFMx
    m.need_booted()
    if not a:
        cur = "  ".join(f"bh{c}={m.pname(p)}" for c, p in m.steps())
        return (cur + "\nstep names, in order (setStep goes up to HAS_T6_CODE_RUNNING):\n  "
                + "\n  ".join(n for n in MFMx.Step.__members__ if n != "STEP_COUNT"))
    want = a[0]
    P = MFMx.Step
    names = [n for n in P.__members__ if n != "STEP_COUNT"]
    if want.isdigit():
        ph = P(int(want))
    elif want.upper() in names:     # exact on purpose: setStep is not a place for guessing
        ph = getattr(P, want.upper())
    else:
        raise ValueError(f"unknown step {want!r}; exact names are:\n  " + "\n  ".join(names))
    return "\n".join(f"bh{bh.getChipNumber()}: setStep -> {bh.setStep(ph)}"
                     for bh in m.bhs)


def c_decay(ctx, a):
    m = ctx.m
    if a:
        m.decay = int(a[0])
    return f"start decay type: {m.decay} (applied at next 'start')"


def c_autoseed(ctx, a):
    m = ctx.m
    if a:
        if m.inited and m.steps() and any(p >= m.M.Step.HAS_T6_CODE_DEPLOYED
                                          for _c, p in m.steps()):
            return ("autoseeding is set when the code is deployed "
                    "(at 'start'): set it before 'start'")
        m.autoseed = kw(a[0], ("on", "off", "layout"), "autoseed option")
    return (f"autoseed: {m.autoseed_report()} (applied at next 'start'; "
            "the autoseed type is 'decay')")


def c_events(ctx, a):
    m = ctx.m
    from machine import EVENT_MODES, parse_events
    if a:
        if m.inited and m.steps() and any(p >= m.M.Step.HAS_T6_CODE_DEPLOYED
                                          for _c, p in m.steps()):
            return ("the events mode is applied when the code is deployed "
                    "(at 'start'): set it before 'start'")
        m.events = parse_events(a[0])
    return (f"events mode: {m.events} (applied at next 'start'; "
            f"one of {', '.join(EVENT_MODES)})")


def c_elide(ctx, a):
    m = ctx.m
    if a:
        if m.inited and m.steps() and any(p >= m.M.Step.HAS_T6_CODE_DEPLOYED
                                          for _c, p in m.steps()):
            return ("cache elision is applied when the code is deployed "
                    "(at 'start'): set it before 'start'")
        m.elide = kw(a[0], ("on", "off"), "elide option") == "on"
    return (f"cache elision: {'on' if m.elide else 'off'} (applied at next 'start'; "
            "four corners only)")


def c_policy(ctx, a):
    m = ctx.m
    from machine import CACHE_POLICIES, parse_policy
    if a:
        if m.inited and m.steps() and any(p >= m.M.Step.HAS_T6_CODE_DEPLOYED
                                          for _c, p in m.steps()):
            return ("cache policy is applied when the code is deployed "
                    "(at 'start'): set it before 'start'")
        m.policy = parse_policy(a[0])
    return (f"cache policy: {m.policy} (applied at next 'start'; "
            f"one of {', '.join(CACHE_POLICIES)})")


def c_where(ctx, a):
    m = ctx.m
    return f"sim dir: {m.simDir}\nlog files: {m.simDir}misc/{m.logprefix}-<KEY>.txt"


def c_quit(ctx, a):
    sh, state = ctx.sh, ctx.state
    state["close"] = bool(a) and kw(a[0], ("close",), "quit option") == "close"
    sh.stop()


def c_grid(ctx, a):
    m = ctx.m
    if a and kw(a[0], ("on", "off", "edges"), "grid") == "edges":
        # display overlay only (ewoverlay.py): no chips needed
        if len(a) > 1:
            m.edges.on = kw(a[1], ("on", "off"), "grid edges") == "on"
        pol = getattr(m, "policy", None)
        kind = (f"zig-zag T={EW_TILE_RADIUS[pol]}" if pol in EW_TILE_RADIUS
                else "rectangle")
        return (f"grid edges {'on' if m.edges.on else 'off'} "
                f"({kind} hub boundaries for policy {pol}, white, display "
                f"only)")
    m.need_booted()
    if a:
        m.qb.mDrawGrid = kw(a[0], ("on", "off"), "grid") == "on"
    return f"mDrawGrid {m.qb.mDrawGrid}"


def c_scan(ctx, a):
    m = ctx.m
    return f"slowscan -> {m.slowscan(int(a[0]) if a else 1)}"


def c_dumpl1(ctx, a):
    m = ctx.m
    items = [t for w in a for t in w.split(",") if t]
    if not items:
        return ("usage: dumpl1 TAG[,TAG..] | TAGLO-TAGHI ...   (TAG like at023: "
                "a, t for Tensix, chip digit, NoC0 x and y base 36)")
    try:
        return m.dumpl1(items)
    except ValueError as e:
        return f"dumpl1: {e}"


def c_tlbi(ctx, a):
    """Show tag and image name for one or more TLBIs."""
    m = ctx.m
    m.need_booted()
    if not a:
        return ("usage: tlbi [CHIP:]TLBI[,TLBI...] | lamers\n"
                "  tlbi 15          -- all chips, TLBI 15\n"
                "  tlbi 0:15,19     -- chip 0, TLBIs 15 and 19\n"
                "  tlbi lamers      -- TLBIs from last bolt's lamer set")
    bh_by_chip = {bh.getChipNumber(): bh for bh in m.bhs}
    pairs = []  # list of (chip, tlbi)
    if a[0][:1].isalpha() and kw(a[0], ("lamers",), "tlbi option") == "lamers":
        b = m.bolt
        if b is None:
            return "no bolt yet"
        for (c, _nonce, _n, lamers) in b.rows:
            if lamers:
                for tlbi in lamers:
                    if isinstance(tlbi, int):
                        pairs.append((c, tlbi))
        if not pairs:
            return "last bolt had no lamers (or lamers were '?')"
    else:
        spec = a[0]
        chip_filter = None
        if ':' in spec:
            chip_str, tlbi_str = spec.split(':', 1)
            chip_filter = int(chip_str)
        else:
            tlbi_str = spec
        tlbis = [int(t) for t in tlbi_str.split(',')]
        if chip_filter is not None:
            pairs = [(chip_filter, tlbi) for tlbi in tlbis]
        else:
            for bh in m.bhs:
                for tlbi in tlbis:
                    pairs.append((bh.getChipNumber(), tlbi))
    lines = []
    for chip, tlbi in pairs:
        bh = bh_by_chip.get(chip)
        if bh is None:
            lines.append(f"bh{chip}:{tlbi}  (chip not present)")
            continue
        tag = bh.getTLBITag(tlbi)
        img = bh.getTLBIImageName(tlbi) or "(no image)"
        lines.append(f"bh{chip}:{tlbi}  {tag}  {img}")
    return "\n".join(lines)


def c_seed(ctx, a):
    m = ctx.m
    m.need_booted()
    if len(a) > 2 or not all(w.isdigit() for w in a):
        return "usage: seed [COUNT [TYPE]]"
    n = int(a[0]) if a else 1
    if not 1 <= n <= 1000:
        return "seed count must be 1..1000"
    # TYPE defaults to the 'decay' start type (FB1 = 4 if never set)
    t = int(a[1]) if len(a) > 1 else (4 if m.decay is None else int(m.decay))
    res = []
    for _ in range(n):
        r = m.ewc.doSeed(t)
        m.logkt("SEED", r)
        res.append(r)
    if n == 1:
        return res[0]
    bad = [r for r in res if r.startswith("Bad")]
    return f"seeded {n - len(bad)}/{n} type {t}" + (f"; failed: {bad[:3]}" if bad else "")


def register(ctx):
    m, sh, cyc = ctx.m, ctx.sh, ctx.cyc
    sh.command("boot", lambda a: m.boot(),
               "boot: configure + chip reset + QuietBox init (as the old Textual app did at startup)")
    sh.command("configure", lambda a: m.configure(), "configure: build layout/images/Blackholes from the dtoml")
    sh.command("chipreset", lambda a: m.reset_chips(), "chipreset: run 'tt-smi -r'")
    sh.command("init", lambda a: m.init_box(), "init: QuietBox.init() (starts the IHH/SuperCycle thread suspended)")
    sh.command("start", lambda a: m.start(), "start: startEWProcessing on every chip (walks all phases; can be slow)")
    sh.command("seams", lambda a: m.seams_report(),
               "seams: eth-seam declarations, link status and health counters")
    sh.command("stop", lambda a: m.stop(), "stop: stopEWProcessing (only on chips at event windows)")
    sh.command("close", lambda a: m.close(), "close: full retreat of every chip (careful)")
    sh.command("step", partial(c_step, ctx),
        "step [NAME|N]: show chip init steps and all step names, or setStep to "
        "NAME (exact, e.g. HAS_T6_CODE_RUNNING; settable up to that one)")
    sh.command("decay", partial(c_decay, ctx), "decay [N]: show/set start decay type")
    sh.command("autoseed", partial(c_autoseed, ctx),
        "autoseed [off|on|layout]: whether hubs drop a 'decay' atom of their "
        "own a while after start (layout: its seedchips; before 'start')")
    sh.command("events", partial(c_events, ctx),
        "events [shortcut|honest]: show/set the AER mode (before 'start')")
    sh.command("elide", partial(c_elide, ctx),
        "elide [on|off]: keep bands shared by consecutive corners (before "
        "'start')")
    sh.command("policy", partial(c_policy, ctx),
        "policy [fourcorners|supercell|ewsnap|ewsnap10]: show/set the cache policy (before "
        "'start')")
    sh.command("status", lambda a: m.status() + "\n" + cyc.status(),
               "status: phases, quietbox, bolt, timeouts, scans, cycle")
    sh.command("where", partial(c_where, ctx), "where: sim dir and log file locations")
    sh.command("quit", partial(c_quit, ctx), "quit [close]: stop events, shut down, exit ('close' retreats chips fully)",
               aliases=("exit",))
    sh.command("grid", partial(c_grid, ctx),
               "grid [on|off]: QuietBox.mDrawGrid (crosses, labels); "
               "grid edges [on|off]: white single-pixel hub ownership "
               "edges over the picture (zig-zag under policy ewsnap/ewsnap10)")
    sh.command("scan", partial(c_scan, ctx), "scan [N]: run slowscans now")
    sh.command("dumpl1", partial(c_dumpl1, ctx),
               "dumpl1 TAG[,TAG..] | TAGLO-TAGHI: whole L1 (1.5MB) of each Tensix tile to dumps/N/TAG.l1")
    sh.command("frame", lambda a: m.frame() or "frame sent", "frame: render+send one graphics frame now")
    sh.command("tlbi", partial(c_tlbi, ctx),
        "tlbi [CHIP:]N[,N...] | lamers: show tag+image for TLBIs (e.g. tlbi "
        "lamers, tlbi 15, tlbi 0:15,19)")
    sh.command("seed", partial(c_seed, ctx),
        "seed [COUNT [TYPE]]: COUNT random seeds (1..1000) of TYPE (default: "
        "decay type)")
