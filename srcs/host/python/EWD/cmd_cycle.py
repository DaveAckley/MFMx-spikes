"""SuperCycle commands: cycle, dwell, nuke, autonuke.

Each command is a module-level function taking (ctx, args); register(ctx)
adds them to the shell.  ctx is the cmdutil.Ctx built by mfmxctl.main."""
import time
from functools import partial

from sequencer import wait_quiet
from cmdutil import kw, need


def c_cycle(ctx, a):
    m, sh, cyc = ctx.m, ctx.sh, ctx.cyc
    sub = sh.resolve(a[0], ("status", "start", "hold", "step", "stop", "retry", "reset",
                            "leader", "first", "birth", "repeat", "holdat",
                            "waitpause"), "cycle subcommand") if a else "status"
    if sub == "status":
        return cyc.status()
    if sub in ("start", "hold", "step", "stop", "retry"):
        return getattr(cyc, sub)()
    if sub == "reset":
        cyc.reset()
        return "cycle reset (counters, leader memory, birth-hold flag)"
    if sub == "holdat":
        if len(a) < 2:
            return cyc.status().split("\n  holdat", 1)[1].strip() \
                if cyc.hold_at else "holdat not armed"
        if a[1].lower() == "off":
            return cyc.set_hold_at(None)
        step = sh.resolve(a[1], ("NEW_LEADER", "RUN_EVENTS", "DWELL"), "cycle holdat STEP")
        return cyc.set_hold_at(step, int(a[2]) if len(a) > 2 else 0)
    if sub == "waitpause":
        rest = [x for x in a[1:]]
        quiet = None                    # seconds of report silence wanted
        if "quiet" in [x.lower() for x in rest]:
            i = [x.lower() for x in rest].index("quiet")
            rest.pop(i)
            quiet = float(rest.pop(i)) if i < len(rest) else 1.0
        cap = float(rest[0]) if rest else 3600.0
        end = time.monotonic() + cap
        # poll_input: a typed quit / cycle stop / cycle holdat off works
        # while we wait (a stop ends the wait; so does a stopped cycle)
        while (not cyc.is_paused() and not cyc.frozen and cyc.state() != "idle"
               and not sh._stop and time.monotonic() < end):
            sh.run_for(0.1, poll_input=True)
        if not cyc.is_paused():
            why = "interrupted" if sh._stop else f"not paused after {cap:g}s"
            return f"{why}: " + cyc.state()
        msg = "cycle paused: " + cyc.status().split("\n", 1)[1].strip()
        if quiet is not None:
            ok, waited, grew = wait_quiet(
                m.reports_total, lambda x: sh.run_for(x, poll_input=True),
                time.monotonic, quiet_s=quiet, cap=60.0,
                stop=lambda: sh._stop)
            msg += (f"\nreports quiet for {quiet:g}s after {waited:.1f}s"
                    if ok else f"\nreports still arriving after {waited:.0f}s")
            if grew is not None:
                msg += f" ({grew} reports arrived while waiting)"
        return msg
    if sub == "repeat":
        if len(a) > 1:
            cyc.repeat = kw(a[1], ("on", "off"), "cycle repeat") == "on"
        what = ("the next corner is any of the 4, so it may repeat"
                if cyc.repeat else "the next corner always differs")
        return (f"cycle repeat: {'on' if cyc.repeat else 'off'} ({what}; "
                "corner policies only)")
    if sub == "leader":
        n = None if len(a) < 2 or (not a[1].isdigit() and
                                   kw(a[1], ("random",), "cycle leader") == "random") else a[1]
        return cyc.set_leader(n)
    if sub == "first":
        need(a, 2, "cycle first N")
        cyc.first_leader = int(a[1])
        return f"first leader {cyc.first_leader}"
    if sub == "birth":
        need(a, 2, "cycle birth done|pending")
        cyc.held = kw(a[1], ("done", "pending"), "cycle birth") == "done"
        return f"HOLD_AT_BIRTH {'treated as done' if cyc.held else 'will be sent first'}"
    raise ValueError("usage: cycle [status|start|hold|step|stop|retry|reset|"
                     "leader N|random|first N|birth done|pending|holdat STEP "
                     "[SCY]|off|waitpause [SECS] [quiet [SECS]]]")


def c_dwell(ctx, a):
    cyc = ctx.cyc
    if a:
        return cyc.set_dwell(*a[:2])
    return f"dwell {cyc.dwell[0]:g}-{cyc.dwell[1]:g}s"


def c_nuke(ctx, a):
    m, cyc = ctx.m, ctx.cyc
    m.need_booted()
    usage = "usage: nuke [big] [X Y R]"
    big = bool(a) and not a[0].lstrip("-").isdigit() and \
        kw(a[0], ("big",), "nuke option") == "big"
    nums = a[1:] if big else a
    if nums and (len(nums) != 3 or not all(w.isdigit() for w in nums)):
        return usage
    x, y, r = (int(w) for w in nums) if nums else (None, None, None)
    res = cyc.queue_nuke(big, x, y, r)
    m.logkt("NUKE", f"QUEUED {res}")
    return res + ("" if cyc.state() == "running" else
                  "  (the cycle is not running: it lands at its next NEW_LEADER)")


def c_autonuke(ctx, a):
    m, cyc = ctx.m, ctx.cyc
    m.need_booted()
    usage = "usage: autonuke [on|off|status] [DELAY_LO DELAY_HI [R_LO R_HI]]  (seconds, sites)"
    if not a or a[0].lower().startswith("st"):
        return cyc.status().split("\n  autonuke", 1)[1].strip() if cyc.auto else "autonuke off"
    if a[0].lower() == "off":
        return cyc.clear_autonuke()
    nums = a[1:] if a[0].lower() == "on" else a
    try:
        v = [float(w) for w in nums]
    except ValueError:
        return usage
    if len(v) not in (0, 2, 4):
        return usage
    dlo, dhi = (v[0], v[1]) if len(v) >= 2 else (5.0, 300.0)
    rlo, rhi = (int(v[2]), int(v[3])) if len(v) == 4 else (40, 400)
    res = cyc.set_autonuke(dlo, dhi, rlo, rhi)
    m.logkt("NUKE", res)
    return res


def register(ctx):
    sh = ctx.sh
    sh.command("cycle", partial(c_cycle, ctx),
               "cycle [status|start|hold|step|stop|retry|reset|leader N|first N|birth "
               "done|pending|holdat STEP [SCY]|waitpause [SECS] [quiet [SECS]]]: "
               "SuperCycle (holdat NEW_LEADER 150 pauses when NEW_LEADER is next once "
               "scy >= 150; waitpause blocks a command file until paused; with 'quiet' "
               "it then waits until no report has arrived for 1s, i.e. DLs and ACBs "
               "drained)")
    sh.command("dwell", partial(c_dwell, ctx), "dwell [SECS | LO HI]: SuperCycle dwell after RUN_EVENTS")
    sh.command("nuke", partial(c_nuke, ctx),
        "nuke [big] [X Y R]: erase a disc of sites (global coords) on every hub "
        "at the next NEW_LEADER (random radius 10-100, big 100-1000)")
    sh.command("autonuke", partial(c_autonuke, ctx),
        "autonuke [on|off|status] [DELAY_LO DELAY_HI [R_LO R_HI]]: repeatedly "
        "nuke a random place after a random delay (defaults 5-300 s, radius "
        "40-400) while the cycle runs")
