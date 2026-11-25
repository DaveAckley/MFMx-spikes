#!/usr/bin/env python3
"""mfmxctl -- command-line controller for the MFMx Blackhole machine.

Replaces the old Textual app (removed; see git history).  One Python thread: a select() loop
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
    ap.add_argument("--events", default="shortcut",
                    help="AER mode: shortcut (default; empty draws are free events) or "
                         "honest (every drawn center is shipped)")
    ap.add_argument("--elide", action="store_true",
                    help="keep the cache bands shared by consecutive corners instead of "
                         "saving and reloading them (four corners only)")
    ap.add_argument("--policy", default="fourcorners",
                    help="cache policy: fourcorners (default) or supercell")
    ap.add_argument("--link-wait", type=float, default=6.0,
                    help="boot preflight: seconds to wait for every declared "
                         "eth-seam link to report Up after a chip reset")
    ap.add_argument("--link-resets", type=int, default=10,
                    help="boot preflight: extra 'tt-smi -r' tries if links "
                         "are still not Up (0 = wait only)")
    ap.add_argument("--link-linger", type=float, default=0.0,
                    help="boot preflight experiment: extra seconds every round keeps "
                         "polling (a round that comes Up late is accepted and "
                         "reported as LATE); 0 = off")
    ap.add_argument("--t6s-per-chip", type=int, default=140)
    ap.add_argument("--slowscan-period", type=float, default=2.0,
                    help="seconds between slowscans; 0 turns the tick off")
    ap.add_argument("--no-slowscan", action="store_true",
                    help="no periodic slowscan (it reads every hub's hostblock "
                         "and starves the frame tick); 'scan' and the "
                         "after-timeout diagnosis still work")
    ap.add_argument("--slowscan-gap", type=float, default=0.2,
                    help="sleep before each chip's scan (the old Textual app used 0.2)")
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


def add_ticks(sh, m, cyc, args):
    """The periodic ticks that run between commands."""
    # 5 ms (was 0.2 and 0.1): the consensus time is only as fine as the tick
    # that notices it, so 0.1 s ticks made every fast bolt read "0.1 s".  Both
    # ticks return at once when nothing is pending.
    sh.add_tick("bolts", 0.005, m.tick_bolts, True,
                "poll the pending bolt's acks")
    sh.add_tick("cycle", 0.005, cyc.tick, True,
                "advance the SuperCycle state machine")
    noscan = args.no_slowscan or args.slowscan_period <= 0
    scan_tick = sh.add_tick("slowscan",
                            args.slowscan_period
                            if args.slowscan_period > 0 else 2.0,
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
    sh.add_tick("zhd", 1.0, m.tick_zhd, True,
                "say/log when the set of desynced report streams changes")
    sh.add_tick("frame", 1 / 5.1, m.tick_frame, m.feed is not None,
                "graphics frame to the feed")


def write_run_summary(sh, m, t_start):
    """Leave runsum.txt and send.tgz (a few hundred KB) in the run directory,
    so only that need be copied off the machine.  Never fails the shutdown."""
    try:
        import runsum
        cmd = " ".join(["python", "mfmxctl.py"] + sys.argv[1:])
        sh.say(runsum.write_bundle(m.simDir, list(sh.transcript), cmdline=cmd,
                                   elapsed=time.monotonic() - t_start))
    except Exception as e:
        sh.say(f"run summary failed: {type(e).__name__}: {e}")


def main(argv=None):
    t_start = time.monotonic()
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
    from machine import Machine, NullFeed
    from shell import Shell
    from sequencer import SuperCycle
    from cmdutil import Ctx
    import cmd_machine, cmd_bolts, cmd_cycle, cmd_view, cmd_mock

    sh = Shell(prompt="mfmx> ")
    _ver = _git_version()
    if _ver:
        sh.say(f"mfmxctl HEAD: {_ver}")
    cfg = Config.Config(os.path.join(HERE, "mfmxctl.py"), args.config)
    m = Machine(MFMx, cfg, sh.say, script_dir=HERE, logprefix=args.log_prefix,
                t6s_per_chip=args.t6s_per_chip, verbose=args.verbose,
                decay=args.decay, tt_smi=args.tt_smi,
                policy=args.policy, events=args.events, elide=args.elide,
                # --mock skips the real reset unless a fake --tt-smi is given
                # (the selftests use /bin/true to exercise the preflight)
                reset_chips=not (args.no_chip_reset or
                                 (args.mock and args.tt_smi == TT_SMI_DEFAULT)),
                extra_bindirs=extra_bindirs, slowscan_gap=args.slowscan_gap,
                layout=args.layout, link_wait=args.link_wait,
                link_resets=args.link_resets,
                link_linger=args.link_linger)
    dw = args.dwell if len(args.dwell) > 1 else args.dwell * 2
    cyc = SuperCycle(m, sh.say, dwell=tuple(dw[:2]), first_leader=args.first_leader)
    m.scy_fn = lambda: cyc.scy          # the stream's LIVE line shows it
    state = {"close": False}
    mock = getattr(MFMx, "_mock", None)

    if args.rtmp:
        from RTMPFeed13 import RTMPFeed     # needs cv2/PIL/ffmpeg; real mode only
        m.feed = RTMPFeed(m, "ewd")
    elif args.mock:
        m.feed = NullFeed()

    # ---- commands (cmd_*.py: functions taking (ctx, args)) -----------------------
    ctx = Ctx(sh=sh, m=m, cyc=cyc, args=args, MFMx=MFMx, mock=mock, state=state)
    for mod in (cmd_machine, cmd_bolts, cmd_cycle, cmd_view, cmd_mock):
        mod.register(ctx)
    add_ticks(sh, m, cyc, args)

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
        write_run_summary(sh, m, t_start)
    return 1 if sh.aborted else 0


if __name__ == "__main__":
    sys.exit(main())
