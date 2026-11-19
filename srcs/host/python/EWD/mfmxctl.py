#!/usr/bin/env python3
"""mfmxctl -- command-line controller for the MFMx Blackhole machine.

Replaces the Textual app (nu1011/nu1012).  One Python thread: a select() loop
reads commands from stdin and runs periodic ticks (slowscan monitoring, bolt
completion polling, optional RTMP frames) in between.  Works in a terminal,
under emacs (M-x shell / M-x pdb), or with piped input.

  mfmxctl.py --mock --boot        # no hardware: use the mock MFMx module
  mfmxctl.py --boot --rtmp        # real machine, with the RTMP video feed
  mfmxctl.py --boot -s run.cmd    # then run the commands in run.cmd as if typed

Type 'help' at the prompt.  Typical session:
  boot ; start ; shoot HOLD_AT_BIRTH 0 ; status ; shoot NEW_LEADER 2 1 ; ...
"""
import argparse
import atexit
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TT_SMI_DEFAULT = "/opt/tenstorrent/pipx/bin/tt-smi"


def _git_version():
    """Return a brief git status string for the startup banner, or '' on failure."""
    try:
        log = subprocess.check_output(
            ["git", "-C", HERE, "log", "-1", "--format=%h %s"],
            stderr=subprocess.DEVNULL, text=True).strip()
        dirty = subprocess.call(
            ["git", "-C", HERE, "diff", "--quiet", "HEAD"],
            stderr=subprocess.DEVNULL)
        return log + (" [dirty]" if dirty else "")
    except Exception:
        return ""


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--config", default="config/nu1012.dtoml",
                    help="dtoml config, searched in . then next to this script")
    ap.add_argument("--layout", default=None, metavar="NAME",
                    help="use this [layout.NAME] instead of the config's activeLayout")
    ap.add_argument("--mock", action="store_true",
                    help="use mock/mfmx instead of the real MFMx module (no hardware)")
    ap.add_argument("--boot", action="store_true",
                    help="configure, reset chips, and init QuietBox at startup")
    ap.add_argument("--rtmp", action="store_true", help="enable the RTMP video feed")
    ap.add_argument("--no-chip-reset", action="store_true", help="skip 'tt-smi -r' during boot")
    ap.add_argument("--tt-smi", default=TT_SMI_DEFAULT)
    ap.add_argument("--decay", type=int, default=None, help="start decay type (was argv[1])")
    ap.add_argument("--policy", default="fourcorners",
                    help="cache policy: fourcorners (default) or supercell")
    ap.add_argument("--alt-decay", type=int, default=None, help="alt seed type (was argv[2])")
    ap.add_argument("--link-wait", type=float, default=12.0,
                    help="boot preflight: seconds to wait for every declared "
                         "eth-seam link to report Up after a chip reset")
    ap.add_argument("--link-resets", type=int, default=5,
                    help="boot preflight: extra 'tt-smi -r' tries if links "
                         "are still not Up (0 = wait only)")
    ap.add_argument("--t6s-per-chip", type=int, default=140)
    ap.add_argument("--slowscan-period", type=float, default=2.0,
                    help="seconds between slowscans; 0 turns the tick off")
    ap.add_argument("--no-slowscan", action="store_true",
                    help="no periodic slowscan (it reads every hub's hostblock "
                         "and starves the frame tick); 'scan' and the "
                         "after-timeout diagnosis still work")
    ap.add_argument("--slowscan-gap", type=float, default=0.2,
                    help="sleep before each chip's scan (nu1011 used 0.2)")
    ap.add_argument("--dwell", type=float, nargs="+", default=[3.0], metavar="SECS",
                    help="SuperCycle dwell after RUN_EVENTS: SECS, or LO HI for a random range")
    ap.add_argument("--first-leader", type=int, default=2,
                    help="leader for the first NEW_LEADER (old C++ debug default: 2)")
    ap.add_argument("-s", "--script", action="append", default=[], metavar="FILE",
                    help="run the commands in FILE as if typed at the prompt (after --boot); "
                         "may be repeated; then continue with stdin")
    ap.add_argument("--abort-on-error", action="store_true",
                    help="a command-file line that fails (error, unknown "
                         "command) ends the run with exit status 1 after an "
                         "orderly shutdown, and a failed --boot skips the "
                         "script (typed lines never abort)")
    ap.add_argument("--script-exit", action="store_true",
                    help="quit (orderly, like 'quit') when the --script files are used up, "
                         "instead of continuing with stdin")
    ap.add_argument("--log-prefix", default="mfmxctl")
    ap.add_argument("--verbose", action="store_true")
    return ap.parse_args(argv)


def setup_paths(args):
    """Mock mode puts mock/ FIRST (so 'from mfmx import MFMx' finds the mock)
    and the tomlikey shim LAST (a real tomlikey wins)."""
    extra_bindirs = []
    if args.mock:
        sys.path.insert(0, os.path.join(HERE, "mock"))
        sys.path.append(os.path.join(HERE, "mock", "shims"))
        d = tempfile.mkdtemp(prefix="mfmxctl-mockbin-")
        atexit.register(shutil.rmtree, d, True)
        for n in ("ewp.bin", "hub.bin", "e1fwd16.bin"):
            with open(os.path.join(d, n), "wb") as f:
                f.write(bytes(range(64)))
        extra_bindirs.append(d)
    sys.path.insert(0, HERE)
    return extra_bindirs


def main(argv=None):
    args = parse_args(argv)
    bad = [f for f in args.script if not os.path.isfile(f)]
    if bad:
        sys.exit(f"mfmxctl: command file not found: {', '.join(bad)}")
    extra_bindirs = setup_paths(args)

    # These are existing repo files, NOT part of the mfmxctl overlay.
    missing = [p for p in ("Config.py", args.config)
               if not (os.path.isfile(os.path.join(HERE, p)) or os.path.isfile(p))]
    if missing:
        sys.exit(f"mfmxctl: missing {', '.join(missing)}.\n"
                 f"  This directory ({HERE}) must be srcs/host/python/EWD of the MFMx-spikes\n"
                 "  checkout (Config.py, config/*.dtoml and RTMPFeed13.py live there);\n"
                 "  copy the mfmxctl files over a checkout, or copy those files in here.")

    from mfmx import MFMx          # real module, or the mock (see setup_paths)
    import Config
    from machine import Machine, NullFeed, fmt_ints
    from shell import Shell
    from sequencer import SuperCycle

    sh = Shell(prompt="mfmx> ")
    _ver = _git_version()
    if _ver:
        sh.say(f"mfmxctl HEAD: {_ver}")
    cfg = Config.Config(os.path.join(HERE, "mfmxctl.py"), args.config)
    m = Machine(MFMx, cfg, sh.say, script_dir=HERE, logprefix=args.log_prefix,
                t6s_per_chip=args.t6s_per_chip, verbose=args.verbose,
                decay=args.decay, alt_decay=args.alt_decay, tt_smi=args.tt_smi,
                policy=args.policy,
                # --mock skips the real reset unless a fake --tt-smi is given
                # (the selftests use /bin/true to exercise the preflight)
                reset_chips=not (args.no_chip_reset or
                                 (args.mock and args.tt_smi == TT_SMI_DEFAULT)),
                extra_bindirs=extra_bindirs, slowscan_gap=args.slowscan_gap,
                layout=args.layout, link_wait=args.link_wait,
                link_resets=args.link_resets)
    dw = args.dwell if len(args.dwell) > 1 else args.dwell * 2
    cyc = SuperCycle(m, sh.say, dwell=tuple(dw[:2]), first_leader=args.first_leader)
    state = {"close": False}
    mock = getattr(MFMx, "_mock", None)

    if args.rtmp:
        from RTMPFeed13 import RTMPFeed     # needs cv2/PIL/ffmpeg; real mode only
        m.feed = RTMPFeed(m, "ewd")
    elif args.mock:
        m.feed = NullFeed()

    # ---- commands ------------------------------------------------------------------
    def need(args_, n, usage):
        if len(args_) < n:
            raise ValueError(f"usage: {usage}")

    def c_shoot(a):
        if not a:
            return ("usage: shoot CMD [int args...]\ncommands: "
                    + ", ".join(m.cmd_names() if m.inited else ["(boot first)"]))
        b = m.fire(a[0].upper(), [int(x, 0) for x in a[1:]])
        return (f"fired bolt #{b.seq} {b.cmd} nonce={b.nonce}; "
                f"timeout {b.timeout:g}s (result is reported when it completes)"
                if m.async_api else None)

    def c_bolt(a):
        if not a:
            return m.bolt.describe(time.monotonic()) if m.bolt else "no bolt yet"
        a = [sh.resolve(a[0], ("hist", "timeout", "diagnose"), "bolt subcommand")] + a[1:]
        if a[0] == "hist":
            now = time.monotonic()
            return "\n".join(b.describe(now) for b in m.bolt_history) or "none"
        if a[0] == "timeout":
            need(a, 3, "bolt timeout CMD SECS")
            m.timeouts[a[1].upper()] = float(a[2])
            return f"{a[1].upper()} timeout {float(a[2]):g}s"
        if a[0] == "diagnose":
            need(a, 2, "bolt diagnose on|off")
            m.diagnose = a[1] == "on"
            return f"diagnose-on-timeout {m.diagnose}"
        raise ValueError("usage: bolt [hist | timeout CMD SECS | diagnose on|off]")

    def c_step(a):
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
        elif want.upper() in names:
            ph = getattr(P, want.upper())
        else:
            raise ValueError(f"unknown step {want!r}; exact names are:\n  " + "\n  ".join(names))
        return "\n".join(f"bh{bh.getChipNumber()}: setStep -> {bh.setStep(ph)}"
                         for bh in m.bhs)

    def c_decay(a):
        if a:
            m.decay = int(a[0])
        return f"start decay type: {m.decay} (applied at next 'start')"

    def c_policy(a):
        from machine import CACHE_POLICIES, parse_policy
        if a:
            if m.inited and m.steps() and any(p >= m.M.Step.HAS_T6_CODE_DEPLOYED
                                              for _c, p in m.steps()):
                return ("cache policy is applied when the code is deployed "
                        "(at 'start'): set it before 'start'")
            m.policy = parse_policy(a[0])
        return (f"cache policy: {m.policy} (applied at next 'start'; "
                f"one of {', '.join(CACHE_POLICIES)})")

    def c_scan(a):
        return f"slowscan -> {m.slowscan(int(a[0]) if a else 1)}"

    def c_dumpl1(a):
        items = [t for w in a for t in w.split(",") if t]
        if not items:
            return ("usage: dumpl1 TAG[,TAG..] | TAGLO-TAGHI ...   (TAG like at023: "
                    "a, t for Tensix, chip digit, NoC0 x and y base 36)")
        try:
            return m.dumpl1(items)
        except ValueError as e:
            return f"dumpl1: {e}"

    def c_grid(a):
        m.need_booted()
        if a:
            m.qb.mDrawGrid = a[0] == "on"
        return f"mDrawGrid {m.qb.mDrawGrid}"

    def c_seed(a):
        m.need_booted()
        t = 1 if not (a and a[0] == "alt") or m.alt_decay is None else int(m.alt_decay)
        r = m.ewc.doSeed(t)
        m.logkt("SEED", r)
        return r

    def c_nuke(a):
        m.need_booted()
        r = m.ewc.doNuke(bool(a and a[0] == "big"))
        m.logkt("NUKE" if a and a[0] == "big" else "nuke", r)
        return r

    def c_cycle(a):
        sub = sh.resolve(a[0], ("status", "start", "hold", "step", "stop", "retry", "reset",
                                "leader", "first", "birth"), "cycle subcommand") if a else "status"
        if sub == "status":
            return cyc.status()
        if sub in ("start", "hold", "step", "stop", "retry"):
            return getattr(cyc, sub)()
        if sub == "reset":
            cyc.reset()
            return "cycle reset (counters, leader memory, birth-hold flag)"
        if sub == "leader":
            return cyc.set_leader(None if len(a) < 2 or a[1] == "random" else a[1])
        if sub == "first":
            need(a, 2, "cycle first N")
            cyc.first_leader = int(a[1])
            return f"first leader {cyc.first_leader}"
        if sub == "birth":
            need(a, 2, "cycle birth done|pending")
            cyc.held = a[1] == "done"
            return f"HOLD_AT_BIRTH {'treated as done' if cyc.held else 'will be sent first'}"
        raise ValueError("usage: cycle [status|start|hold|step|stop|retry|reset|"
                         "leader N|random|first N|birth done|pending]")

    def c_feed(a):
        sub = sh.resolve(a[0], ("on", "off", "status"), "feed subcommand") if a else "status"
        ft = sh.ticks["frame"]
        if sub == "on":
            if m.feed is None or isinstance(m.feed, NullFeed) and not args.mock:
                if args.mock:
                    m.feed = NullFeed()
                else:
                    from RTMPFeed13 import RTMPFeed     # needs cv2/PIL/ffmpeg
                    m.feed = RTMPFeed(m, "ewd")
            ft.enabled, ft.errors = True, 0
            ft.due = time.monotonic() + ft.period
            return f"feed on: {type(m.feed).__name__}; frames go out at {1 / ft.period:.1f}/s once booted"
        if sub == "off":
            ft.enabled = False
            return ("feed frames stopped (the feed object, and any ffmpeg it started, stays "
                    "until exit; 'feed on' resumes)")
        return (f"feed: {type(m.feed).__name__ if m.feed is not None else 'none'}, "
                f"frame tick {'on' if ft.enabled else 'off'}")

    def c_dwell(a):
        if a:
            return cyc.set_dwell(*a[:2])
        return f"dwell {cyc.dwell[0]:g}-{cyc.dwell[1]:g}s"

    def c_where(a):
        return f"sim dir: {m.simDir}\nlog files: {m.simDir}misc/{m.logprefix}-<KEY>.txt"

    def c_waitbolt(a):
        """Run ticks until the pending bolt settles (for command files)."""
        b = m.bolt
        if b is None:
            return "no bolt yet"
        cap = float(a[0]) if a else b.timeout + 5.0
        end = time.monotonic() + cap
        while b.state == "pending" and time.monotonic() < end:
            sh.run_for(0.1)
        return b.describe(time.monotonic())

    def c_quit(a):
        state["close"] = bool(a and a[0] == "close")
        sh.stop()

    def c_mock(a):
        if not a:
            return mock.summary()
        a = [sh.resolve(a[0], ("lamers", "slow", "delay", "clear"), "mock subcommand")] + a[1:]
        if a[0] == "lamers":
            need(a, 2, "mock lamers CHIP [TLBI...] [cmd NAME]   (no TLBIs = clear that chip)")
            rest, cmd = a[2:], None
            if "cmd" in rest:
                i = rest.index("cmd")
                need(rest, i + 2, "mock lamers CHIP [TLBI...] cmd NAME")
                cmd, rest = rest[i + 1].upper(), rest[:i]
            mock.set_lamers(a[1], rest, cmd)
            return mock.summary()
        if a[0] == "slow":
            need(a, 4, "mock slow CHIP SECS TLBI...   (those tiles ack SECS late)")
            mock.set_slow(a[1], a[3:], a[2])
            return mock.summary()
        if a[0] == "delay":
            need(a, 4, "mock delay CMD LO HI")
            mock.set_delay(a[1].upper(), a[2], a[3])
            return mock.summary()
        if a[0] == "clear":
            mock.clear_lamers()
            return "lamers cleared"
        raise ValueError("usage: mock [lamers CHIP [TLBI..] [cmd NAME] | slow CHIP SECS TLBI.. | "
                         "delay CMD LO HI | clear]")

    def c_tlbi(a):
        """Show tag and image name for one or more TLBIs."""
        m.need_booted()
        if not a:
            return ("usage: tlbi [CHIP:]TLBI[,TLBI...] | lamers\n"
                    "  tlbi 15          -- all chips, TLBI 15\n"
                    "  tlbi 0:15,19     -- chip 0, TLBIs 15 and 19\n"
                    "  tlbi lamers      -- TLBIs from last bolt's lamer set")
        bh_by_chip = {bh.getChipNumber(): bh for bh in m.bhs}
        pairs = []  # list of (chip, tlbi)
        if a[0] == "lamers":
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

    sh.command("zhd", lambda a: m.zhd_text(verbose=bool(a and a[0] == "all")),
               "zhd [all]: report-stream health; lists desynced hubs (all: every hub)")
    sh.command("boot", lambda a: m.boot(),
               "boot: configure + chip reset + QuietBox init (as nu1011 did at startup)")
    sh.command("configure", lambda a: m.configure(), "configure: build layout/images/Blackholes from the dtoml")
    sh.command("chipreset", lambda a: m.reset_chips(), "chipreset: run 'tt-smi -r'")
    sh.command("init", lambda a: m.init_box(), "init: QuietBox.init() (starts the IHH/SuperCycle thread suspended)")
    sh.command("start", lambda a: m.start(), "start: startEWProcessing on every chip (walks all phases; can be slow)")
    sh.command("seams", lambda a: m.seams_report(),
               "seams: eth-seam declarations, link status and health counters")
    sh.command("stop", lambda a: m.stop(), "stop: stopEWProcessing (only on chips at event windows)")
    sh.command("close", lambda a: m.close(), "close: full retreat of every chip (careful)")
    sh.command("step", c_step, "step [NAME|N]: show chip init steps and all step names, or setStep to NAME (exact, e.g. HAS_T6_CODE_RUNNING; settable up to that one)")
    sh.command("decay", c_decay, "decay [N]: show/set start decay type")
    sh.command("policy", c_policy, "policy [fourcorners|supercell]: show/set the cache policy (before 'start')")
    sh.command("status", lambda a: m.status() + "\n" + cyc.status(),
               "status: phases, quietbox, bolt, timeouts, scans, cycle")
    sh.command("cycle", c_cycle,
               "cycle [status|start|hold|step|stop|retry|reset|leader N|first N|birth done|pending]: SuperCycle")
    sh.command("dwell", c_dwell, "dwell [SECS | LO HI]: SuperCycle dwell after RUN_EVENTS")
    sh.command("shoot", c_shoot, "shoot CMD [ARGS]: fire a PHASER bolt (async; completion is reported)")
    sh.command("waitbolt", c_waitbolt,
               "waitbolt [SECS]: let ticks run until the pending bolt settles (default: its timeout + 5s)")
    sh.command("bolt", c_bolt, "bolt [hist | timeout CMD SECS | diagnose on|off]: bolt state and policy")
    sh.command("scan", c_scan, "scan [N]: run slowscans now")
    sh.command("dumpl1", c_dumpl1,
               "dumpl1 TAG[,TAG..] | TAGLO-TAGHI: whole L1 (1.5MB) of each Tensix tile to dumps/N/TAG.l1")
    sh.command("frame", lambda a: m.frame() or "frame sent", "frame: render+send one graphics frame now")
    sh.command("grid", c_grid, "grid [on|off]: QuietBox.mDrawGrid")
    sh.command("seed", c_seed, "seed [alt]: EWControl.doSeed")
    sh.command("nuke", c_nuke, "nuke [big]: EWControl.doNuke")
    sh.command("tlbi", c_tlbi, "tlbi [CHIP:]N[,N...] | lamers: show tag+image for TLBIs (e.g. tlbi lamers, tlbi 15, tlbi 0:15,19)")
    sh.command("where", c_where, "where: sim dir and log file locations")
    sh.command("quit", c_quit, "quit [close]: stop events, shut down, exit ('close' retreats chips fully)",
               aliases=("exit",))
    if mock is not None:
        sh.command("mock", c_mock, "mock [lamers CHIP TLBI.. | delay CMD LO HI | clear]: mock-only knobs")

    sh.add_tick("bolts", 0.2, m.tick_bolts, True, "poll the pending bolt's acks")
    sh.add_tick("cycle", 0.1, cyc.tick, True, "advance the SuperCycle state machine")
    noscan = args.no_slowscan or args.slowscan_period <= 0
    scan_tick = sh.add_tick("slowscan",
                            args.slowscan_period if args.slowscan_period > 0 else 2.0,
                            m.tick_slowscan, not noscan,
                            "hostblock log monitoring (no-op until booted)")

    def start_scan_ticks():
        """After a bolt timeout: turn the slowscan tick on if it was off.
        True only when this call changed it."""
        if scan_tick.enabled:
            return False
        scan_tick.enabled = True
        scan_tick.due = sh.clock()
        return True
    m.start_scan_ticks = start_scan_ticks
    sh.command("feed", c_feed, "feed [on|off|status]: start/stop sending graphics frames (RTMP feed, built on first 'on')")
    sh.add_tick("zhd", 1.0, m.tick_zhd, True, "say/log when the set of desynced report streams changes")
    sh.add_tick("frame", 1 / 5.1, m.tick_frame, m.feed is not None, "graphics frame to the feed")

    # SIGTERM/SIGHUP take the same orderly exit path as 'quit'
    for s in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(s, lambda sig, frm: sys.exit(128 + sig))

    try:
        if args.boot:
            try:
                sh.say(m.boot())
            except Exception as e:
                sh.say(f"boot failed: {type(e).__name__}: {e}")
                if args.abort_on_error:
                    sh.say("script skipped (--abort-on-error)")
                    sh.aborted = True
        sh.script_exit = args.script_exit
        sh.abort_on_error = args.abort_on_error
        if not sh.aborted:
            for f in args.script:
                sh.load_script(f)
            sh.run()
    finally:
        sh.say("shutting down...")
        sh.say(m.shutdown(close=state["close"]) or "nothing to shut down")
    return 1 if sh.aborted else 0


if __name__ == "__main__":
    sys.exit(main())
