"""Mock-only knobs.

Each command is a module-level function taking (ctx, args); register(ctx)
adds them to the shell.  ctx is the cmdutil.Ctx built by mfmxctl.main."""
from functools import partial

from cmdutil import need


def c_mock(ctx, a):
    sh, mock = ctx.sh, ctx.mock
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


def register(ctx):
    sh, mock = ctx.sh, ctx.mock
    if mock is not None:
        sh.command("mock", partial(c_mock, ctx),
            "mock [lamers CHIP TLBI.. | delay CMD LO HI | clear]: mock-only knobs")
