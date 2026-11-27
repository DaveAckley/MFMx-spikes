"""Picture and report-stream commands: who, zoom, notch, feed, zhd.

Each command is a module-level function taking (ctx, args); register(ctx)
adds them to the shell.  ctx is the cmdutil.Ctx built by mfmxctl.main."""
import time
from functools import partial

from machine import NullFeed
from cmdutil import kw


def c_who(ctx, a):
    m = ctx.m
    if len(a) != 2:
        return "usage: who X Y   (a pixel of the picture, e.g. from zoom specks)"
    try:
        x, y = int(a[0]), int(a[1])
    except ValueError:
        return f"who: X and Y must be integers, not {a[0]!r} {a[1]!r}"
    if not (0 <= x < 1920 and 0 <= y < 1080):
        return f"who: ({x},{y}) is outside the 1920x1080 picture"
    return m.who(x, y)


def c_notch(ctx, a):
    """notch [on|off|status|list|hold on|off]: the corner notch detector
    (notchwatch.py, OPEN_ISSUES 'CORNER NOTCHES')."""
    m = ctx.m
    w = m.notch
    if not a:
        return w.status()
    sub = ctx.sh.resolve(a[0].lower(), ("on", "off", "status", "list",
                                        "hold"), "notch subcommand")
    if sub == "status":
        return w.status()
    if sub == "on":
        w.reset()
        w.on = True
        return w.status() + "\n(grid must be off: crosses count as occupied)"
    if sub == "off":
        w.on = False
        return w.status()
    if sub == "list":
        from notchwatch import describe
        return "\n".join(describe(n) for n in w.log) or "no notches yet"
    if sub == "hold":
        if len(a) < 2:
            return f"notch hold {'on' if w.hold else 'off'}"
        w.hold = kw(a[1], ("on", "off"), "notch hold") == "on"
        return (f"notch hold {'on' if w.hold else 'off'}"
                + (": the first notch arms cycle holdat RUN_EVENTS "
                   "(hubs out of events, the picture holds still)"
                   if w.hold else ""))
    return "usage: notch [on|off|status|list|hold on|off]"


def c_zoom(ctx, a):
    m = ctx.m
    z = m.zoom
    if not a or a[0].lower().startswith("st"):
        return z.status_long()
    w = a[0].lower()
    if w == "reset":
        z.reset_box()
        return "zoom: seen box forgotten; " + z.status()
    if w == "map":
        return z.occupancy_map(m.ewc.renderGraphicsGridWindowToImage().asNP()) + \
            f"\ngrid drawing: {'ON (its crosses and labels count as occupied)' if m.qb.mDrawGrid else 'off'}"
    if w == "region":
        try:
            x0, y0, x1, y1 = (int(v) for v in a[1:5])
            if len(a) != 5:
                raise ValueError
        except ValueError:
            return "usage: zoom region X0 Y0 X1 Y1   (pixels; a hub's whole grid is hx*137-20..hx*137+157, hy*108-20..hy*108+128)"
        return z.region_map(m.ewc.renderGraphicsGridWindowToImage().asNP(), x0, y0, x1, y1)
    if w == "specks":
        r = int(a[1]) if len(a) > 1 and a[1].isdigit() else 5
        doref = "refresh" in [t.lower() for t in a[1:]]
        rep = z.specks_report(m.ewc.renderGraphicsGridWindowToImage().asNP(), r,
                               annotate=m.specks_who,
                               later=lambda: m.ewc.renderGraphicsGridWindowToImage().asNP(),
                               refresh=m.specks_refresh if doref else None)
        return rep + ("\n" + m.refresh_stats_text() if doref else "")
    if w == "shrink":
        on = (a[1].lower() if len(a) > 1 else "on")
        if on not in ("on", "off"):
            return "usage: zoom shrink [on|off]"
        return z.set_shrink(on == "on")
    if w in ("off", "auto"):
        return z.set_mode(w)
    try:
        return z.set_mode("fixed", float(w))
    except ValueError as e:
        return f"usage: zoom [off|auto|reset|shrink on/off|SCALE|status]  ({e})"


def c_feed(ctx, a):
    m, sh, args = ctx.m, ctx.sh, ctx.args
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


def c_zhd(ctx, a):
    m = ctx.m
    sub = kw(a[0], ("all", "restart", "break"), "zhd subcommand") if a else None
    if sub == "restart":
        if len(a) != 2:
            return "usage: zhd restart on|off"
        return m.zhd_restart(kw(a[1], ("on", "off"), "zhd restart") == "on")
    if sub == "break":
        tags = [t for arg in a[1:] for t in arg.split(",") if t]
        return m.zhd_break(tags)
    return m.zhd_text(verbose=sub == "all")


def register(ctx):
    sh = ctx.sh
    sh.command("who", partial(c_who, ctx),
        "who X Y: which hub last painted picture pixel (x,y), from its self sites"
        " or its S/E cache ring, and in which epoch (ghost-pixel hunting; freeze "
        "with cycle hold first)")
    sh.command("notch", partial(c_notch, ctx),
        "notch [on|off|status|list|hold on|off]: watch every frame for "
        "CORNER NOTCHES (a hub-corner square, up to the cache ring size, "
        "that empties while the other three squares at that corner stay "
        "full); each is logged as [notch] (misc/...-NOTCH.txt) with its "
        "size and a pixel for who; hold on: the first one pauses the "
        "cycle with holdat RUN_EVENTS; grid must be off")
    sh.command("zoom", partial(c_zoom, ctx),
        "zoom [off|auto|reset|map|specks [R] [refresh] (also who for each; "
        "refresh asks the hubs to re-report them)|region X0 Y0 X1 Y1|shrink "
        "on/off|SCALE|status]: crop and scale the video (status shows per-stage "
        "cost); auto fits the box of every non-empty site ever seen (shrink on: "
        "of the last ~5 s instead, so it zooms back in), SCALE (1..64) is fixed; "
        "off by default; grid must be off")
    sh.command("feed", partial(c_feed, ctx),
        "feed [on|off|status]: start/stop sending graphics frames (RTMP feed, "
        "built on first 'on')")
    sh.command("zhd", partial(c_zhd, ctx),
               "zhd [all | restart on|off | break TAG[,TAG..]]: report-stream health (desynced hubs; "
               "all: every hub); restart: ask desynced hubs to restart their stream (default on); "
               "break: TEST, lose a byte of that hub's stream")
