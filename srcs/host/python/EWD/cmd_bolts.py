"""Bolt commands: shoot, bolt, waitbolt.

Each command is a module-level function taking (ctx, args); register(ctx)
adds them to the shell.  ctx is the cmdutil.Ctx built by mfmxctl.main."""
import time
from functools import partial

from shell import AbbrevError
from cmdutil import kw, need


def c_shoot(ctx, a):
    m = ctx.m
    if not a:
        return ("usage: shoot CMD [int args...]\ncommands: "
                + ", ".join(m.cmd_names() if m.inited else ["(boot first)"]))
    bolt = a[0].upper()
    if m.inited:
        try:
            bolt = kw(bolt, [n.lower() for n in m.cmd_names()], "bolt command").upper()
        except AbbrevError as e:
            if not str(e).startswith("ambiguous"):
                pass            # unknown: m.fire explains and lists the real names
            else:
                raise
    bargs = [int(x, 0) for x in a[1:]]
    if bolt == "NEW_LEADER":      # always 5 words: words persist from bolt to
        bargs += [0] * (5 - len(bargs))   # bolt on the device, a stale blast must not fire
    b = m.fire(bolt, bargs)
    return (f"fired bolt #{b.seq} {b.cmd} nonce={b.nonce}; "
            f"timeout {b.timeout:g}s (result is reported when it completes)"
            if m.async_api else None)


def c_bolt(ctx, a):
    m, sh = ctx.m, ctx.sh
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
        m.diagnose = kw(a[1], ("on", "off"), "bolt diagnose") == "on"
        return f"diagnose-on-timeout {m.diagnose}"
    raise ValueError("usage: bolt [hist | timeout CMD SECS | diagnose on|off]")


def c_waitbolt(ctx, a):
    """Run ticks until the pending bolt settles (for command files)."""
    m, sh = ctx.m, ctx.sh
    b = m.bolt
    if b is None:
        return "no bolt yet"
    cap = float(a[0]) if a else b.timeout + 5.0
    end = time.monotonic() + cap
    while b.state == "pending" and time.monotonic() < end:
        sh.run_for(0.1)
    return b.describe(time.monotonic())


def register(ctx):
    sh = ctx.sh
    sh.command("shoot", partial(c_shoot, ctx),
        "shoot CMD [ARGS]: fire a PHASER bolt (async; completion is reported)")
    sh.command("bolt", partial(c_bolt, ctx),
        "bolt [hist | timeout CMD SECS | diagnose on|off]: bolt state and policy")
    sh.command("waitbolt", partial(c_waitbolt, ctx),
               "waitbolt [SECS]: let ticks run until the pending bolt settles (default: its timeout + 5s)")
