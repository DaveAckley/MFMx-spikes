#!/usr/bin/env python3
"""runsum.py -- boil one MFMx run down to a small text file, and bundle it.

mfmxctl calls write_bundle() at shutdown: the run directory then holds
send-<RUNDIR>.tgz (about 0.2-2 MB: runsum.txt, the console transcript and the
small misc/ logs), so only that need be sent, not the 100 MB - 3 GB run
directory.  The same code runs by hand on an old run (ON the QB):

    scripts/runsum/runsum.py NAME.out MFMX-YYYYMMDD-HHMMSS/ [NAME.sum]
    scripts/runsum/runsum.py NAME.out MFMX-.../ --grep 'FROZEN|\\.sc' --max 80
    scripts/runsum/runsum.py NAME.out MFMX-.../ --bundle

It reads the console (NAME.out, or mfmxctl's own transcript) and the run
directory's misc/ logs (BOLT, FEEDSTAT, LIVEAER, NUKE, SEED), never the big
files, and writes: the build and command line, elapsed time, consensus
times per bolt kind and every timeout, feed pacing, the AER trend, zhd
health, nukes and seeds, anything that looks like trouble, and the tail
of the console.

--grep REGEX adds the matching lines (at most --max, default 60, each cut
to 200 columns) of the console and of all.mfmk, for a closer look at one
thing without sending the whole file.  Standard library only.
"""
import argparse
import os
import re
import sys
import tarfile
import time

TROUBLE = re.compile(
    r"FATAL|Aborted|Traceback|FROZEN|TIMEOUT|desync|BORKEN|"
    r"Error|failed|NO DATA", re.I)


def read_lines(path):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read().split("\n")
    except OSError:
        return []


def cut(s, n=200):
    return s if len(s) <= n else s[:n - 3] + "..."


def stats(vals):
    """(n, mean, max) of a list of floats."""
    if not vals:
        return 0, 0.0, 0.0
    return len(vals), sum(vals) / len(vals), max(vals)


def section(title):
    return ["", "== " + title]


def head_info(out, cmdline=None, elapsed=None):
    r = section("run")
    if cmdline:
        r.append(cut(cmdline.strip()))
    else:
        for l in out:
            if "python mfmxctl.py" in l or l.startswith("mfmxctl.py"):
                r.append(cut(l.strip()))
                break
    for l in out:
        if l.startswith("mfmxctl HEAD:"):
            r.append(cut(l.strip(), 160))
            break
    if elapsed is None:
        for l in reversed(out):
            m = re.search(r"Elapsed ([0-9.]+) sec", l)
            if m:
                elapsed = float(m.group(1))
                break
    if elapsed is not None:
        r.append(f"elapsed {elapsed:.0f} s ({elapsed / 3600:.2f} h)")
    for l in out:
        if re.match(r"mfmx> (events|decay|policy|elide) ", l):
            r.append(l.strip())
    for l in out:
        if l.startswith("mfmx> #"):     # the script's first comment: its name
            r.append("script: " + cut(l[len("mfmx> #"):].strip(), 160))
            break
    if not any(re.match(r"mfmx> decay ", l) for l in out):
        r.append("decay: NOT SET (the default autoseed type applies, not 4)")
    return r


def bolts(misc):
    r = section("bolts (consensus seconds)")
    by = {}
    touts = []
    for l in read_lines(os.path.join(misc, "mfmxctl-BOLT.txt")):
        m = re.match(r"BOLT:DONE #(\d+) (\w+)\(([^)]*)\) CONSENSUS ([0-9.]+)s",
                     l)
        if m:
            by.setdefault(m.group(2), []).append(float(m.group(4)))
        elif "TIMEOUT" in l and l.startswith("BOLT:DONE"):
            touts.append(cut(l, 240))
    for k in sorted(by):
        n, mean, mx = stats(by[k])
        over = sum(1 for v in by[k] if v > 0.3)
        r.append(f"{k:14s} n={n} mean={mean:.3f} max={mx:.3f} "
                 f"over0.3s={over}")
    if not by:
        r.append("(no BOLT log)")
    r.append(f"timeouts: {len(touts)}")
    r.extend(touts[:5])
    return r


def feed(misc):
    r = section("feed (per 30 s window)")
    tot, real, dup, drop, owed, gap, wr = [], [], [], [], [], [], []
    resync = 0
    bad = []
    for l in read_lines(os.path.join(misc, "mfmxctl-FEEDSTAT.txt")):
        m = re.search(r"real (\d+) dup (\d+) maxgap ([0-9.]+)s "
                      r"maxwrite ([0-9.]+)s(?: drop (\d+) resync (\d+) "
                      r"owed ([0-9.]+))?", l)
        if not m:
            continue
        a, b = int(m.group(1)), int(m.group(2))
        real.append(a)
        dup.append(b)
        tot.append(a + b)
        gap.append(float(m.group(3)))
        wr.append(float(m.group(4)))
        if m.group(5) is not None:
            drop.append(int(m.group(5)))
            resync += int(m.group(6))
            owed.append(float(m.group(7)))
        if float(m.group(3)) > 1.0:
            bad.append(len(tot))
    if not tot:
        return r + ["(no FEEDSTAT log)"]
    r.append(f"windows {len(tot)}; real+dup mean {sum(tot)/len(tot):.1f} "
             f"min {min(tot)} max {max(tot)} (5 Hz = 150)")
    r.append(f"real mean {sum(real)/len(real):.1f}, "
             f"dup mean {sum(dup)/len(dup):.1f}")
    if drop:
        r.append(f"drops total {sum(drop)}, resyncs {resync}, "
                 f"owed max {max(owed):.1f}")
    r.append(f"maxgap {max(gap):.2f} s, maxwrite {max(wr):.2f} s; "
             f"windows with a gap over 1 s: {len(bad)}"
             + (f" (first at #{bad[0]}, last at #{bad[-1]})" if bad else ""))
    return r


def aer(misc):
    r = section("AER (LIVEAER, about 12 samples)")
    ls = [l for l in read_lines(os.path.join(misc, "mfmxctl-LIVEAER.txt"))
          if l.startswith("LIVEAER")]
    if not ls:
        return r + ["(no LIVEAER log)"]
    step = max(1, len(ls) // 12)
    pick = ls[::step]
    if ls[-1] not in pick:
        pick.append(ls[-1])
    return r + [cut(l, 100) for l in pick]


def misc_counts(misc):
    r = section("nukes and seeds")
    nuke = read_lines(os.path.join(misc, "mfmxctl-NUKE.txt"))
    seed = read_lines(os.path.join(misc, "mfmxctl-SEED.txt"))
    r.append("nukes shot: %d; seeds logged: %d" % (
        sum(1 for l in nuke if l.startswith("NUKE:SHOOT")),
        sum(1 for l in seed if l.startswith("SEED:"))))
    return r


def console(out):
    r = section("console highlights")
    for l in reversed(out):
        if l.startswith("zhd:"):
            r.append("last zhd: " + cut(l, 240))
            break
    else:
        r.append("no zhd output in this run")
    for l in out:
        if l.startswith("seeded") or l.startswith("live AER final"):
            r.append(cut(l))
    bad = [l for l in out if TROUBLE.search(l) and not l.startswith("{")
           and "CONSENSUS" not in l and "abort-on-error" not in l]
    r.append(f"trouble lines: {len(bad)} (first 25, last 10)")
    r.extend(cut(l.strip()) for l in bad[:25])
    if len(bad) > 35:
        r.append("...")
    r.extend(cut(l.strip()) for l in bad[max(25, len(bad) - 10):])
    return r


def tail(out, n=40):
    keep = [l for l in out if l.strip() and not l.startswith("{")
            and not l.startswith("(100)")]
    return section(f"console tail ({n} lines, mark lines removed)") + \
        [cut(l) for l in keep[-n:]]


def grep(outpath, rundir, pat, mx):
    rx = re.compile(pat)
    r = section(f"grep /{pat}/ (max {mx} each)")
    for label, path in (("console", outpath),
                        ("all.mfmk", os.path.join(rundir, "all.mfmk"))):
        hits = []
        try:
            with open(path, "r", errors="replace") as f:
                for l in f:
                    if rx.search(l):
                        hits.append(cut(l.rstrip("\n")))
                        if len(hits) >= mx:
                            break
        except OSError:
            r.append(f"[{label}: not readable]")
            continue
        r.append(f"[{label}: {len(hits)} lines]")
        r.extend(hits)
    return r


# Hub init marks worth seeing in every summary (tiles/*.mfmk): per tag,
# how many hubs logged it and its most common values; for colon-separated
# hex fields also each field's min..max.  .cp = cache policy id, .ct =
# ewsnap live centers per center table (no corner, SE, SW, NW, NE).
HUB_MARK_TAGS = (".cp:", ".ct:")
HUB_MARK_VALUES = 6


def hub_marks(rundir):
    import glob
    r = section("hub marks (first per hub, tiles/*.mfmk)")
    files = sorted(glob.glob(os.path.join(rundir, "tiles", "*.mfmk")))
    if not files:
        return r + ["[no tile logs]"]
    for tag in HUB_MARK_TAGS:
        vals = []
        for path in files:
            try:
                with open(path, "r", errors="replace") as f:
                    for l in f:
                        i = l.find(" " + tag)
                        if i >= 0:
                            v = l[i + 1 + len(tag):].strip().rstrip("}")
                            vals.append(v.rstrip("."))
                            break
            except OSError:
                pass
        if not vals:
            r.append(f"{tag} none of {len(files)} tiles")
            continue
        common = {}
        for v in vals:
            common[v] = common.get(v, 0) + 1
        top = sorted(common.items(), key=lambda kv: -kv[1])
        shown = ", ".join(f"{v} x{n}" for v, n in top[:HUB_MARK_VALUES])
        more = "" if len(top) <= HUB_MARK_VALUES else \
            f", +{len(top) - HUB_MARK_VALUES} more"
        r.append(f"{tag} {len(vals)} of {len(files)} tiles: {shown}{more}")
        fields = [v.split(":") for v in vals]
        if len(top) > 1 and all(len(f) == len(fields[0]) for f in fields):
            try:
                nums = [[int(x, 16) for x in f] for f in fields]
                rng = [f"{min(c)}..{max(c)}" for c in zip(*nums)]
                r.append(f"{tag} fields (decimal) min..max: "
                         + " ".join(rng))
            except ValueError:
                pass
    return r


# Physics timing (EWP `.pt:TYPE:EVENTS:TICKS:INSNS.` marks, hex, from
# srcs/cross/images/ewp/src/Physics.cpp): summed over every tile log,
# mean AI clock cycles per event per physics type (4 FB1, 5 FB4 native;
# 7 FB1, 8 FB4 bytecode, the BytePhys spike) and bytecode instructions
# per event.  A pair (4,7) or (5,8) gives the interpreter's overhead.
PT_NAMES = {4: "FB1 native", 5: "FB4 native", 7: "FB1 bytecode",
            8: "FB4 bytecode"}


PT_WINDOW_SECS = 300            # the per-window table's time step


def physics_timing(rundir, window=PT_WINDOW_SECS):
    """The physics timing section, plus a table of events per type per
    `window` seconds of run time (from the `---SECS---` host time lines
    in each tile log), so a contest between types (e.g. 5 vs 8 in rounds
    after whole-grid nukes) can be read window by window."""
    import glob
    files = sorted(glob.glob(os.path.join(rundir, "tiles", "*.mfmk")))
    tot = {}
    tiles = {}
    per = {}                    # window index -> {type: events}
    for path in files:
        try:
            with open(path, "r", errors="replace") as f:
                now = None
                for l in f:
                    if l.startswith("---"):
                        try:
                            now = float(l.strip().strip("-"))
                        except ValueError:
                            pass
                        continue
                    i = l.find(" .pt:")
                    if i < 0:
                        continue
                    v = l[i + 5:].strip().rstrip("}").rstrip(".")
                    try:
                        t, n, ticks, ins = (int(x, 16) for x in v.split(":"))
                    except ValueError:
                        continue    # torn mark
                    e = tot.setdefault(t, [0, 0, 0])
                    e[0] += n
                    e[1] += ticks
                    e[2] += ins
                    tiles.setdefault(t, set()).add(path)
                    if now is not None:
                        w = per.setdefault(int(now // window), {})
                        w[t] = w.get(t, 0) + n
        except OSError:
            pass
    if not tot:
        return []
    r = section("physics timing (EWP .pt marks, AI clock cycles per event)")
    for t in sorted(tot):
        n, ticks, ins = tot[t]
        if n == 0:
            continue
        name = PT_NAMES.get(t, f"type {t}")
        extra = f", {ins / n:.1f} bytecode insns/event" if ins else ""
        r.append(f"type {t} ({name}): {n} events on {len(tiles[t])} tiles, "
                 f"{ticks / n:.0f} cycles/event{extra}")
    for nat, byt in ((4, 7), (5, 8)):
        if nat in tot and byt in tot and tot[nat][0] and tot[byt][0]:
            a = tot[nat][1] / tot[nat][0]
            b = tot[byt][1] / tot[byt][0]
            r.append(f"bytecode / native, {PT_NAMES[nat].split()[0]}: "
                     f"{b / a:.2f}x")
    if per:
        types = sorted({t for w in per.values() for t in w})
        r.append(f"events per {window} s window (millions; share = "
                 f"8 / (5+8), bytecode FB4's part of all FB4):")
        r.append("  window s     " + "".join(f"{t:>9d}" for t in types)
                 + "    share")
        for k in sorted(per):
            w = per[k]
            five, eight = w.get(5, 0), w.get(8, 0)
            share = f"{eight / (five + eight):9.2f}" if five + eight else ""
            r.append(f"  {k * window:5d}-{(k + 1) * window:<5d} "
                     + "".join(f"{w.get(t, 0) / 1e6:9.1f}" for t in types)
                     + share)
    return r


# BytePhys microbenchmark (EWP `.pb:NAME:V:TICKS:INSNS.` marks, TICKS and
# INSNS hex, from ewp/src/Physics.cpp runBench; programs from
# scripts/bytephys/benchgen.py).  V: L = program in L1 (as real physics
# runs), F = copied into fast RAM, N = a native load probe (INSNS = loads).
def bench_timing(rundir):
    import glob
    runs = {}                   # (name, v) -> [runs, ticks, insns]
    order = []
    for path in sorted(glob.glob(os.path.join(rundir, "tiles", "*.mfmk"))):
        try:
            with open(path, "r", errors="replace") as f:
                for l in f:
                    i = l.find(" .pb:")
                    if i < 0:
                        continue
                    v = l[i + 5:].strip().rstrip("}").rstrip(".")
                    try:
                        name, var, ticks, ins = v.split(":")
                        ticks, ins = int(ticks, 16), int(ins, 16)
                    except ValueError:
                        continue    # torn mark
                    if name not in order:
                        order.append(name)
                    e = runs.setdefault((name, var), [0, 0, 0])
                    e[0] += 1
                    e[1] += ticks
                    e[2] += ins
        except OSError:
            pass
    if not runs:
        return []
    r = section("bytecode microbenchmark (EWP .pb marks, AI clock cycles)")
    r.append("per pattern insn = (cycles - base cycles) / (insns - base "
             "insns); L = code in L1, F = in fast RAM")
    r.append(f"  {'bench':8s} {'runs':>5s} {'insns':>6s} {'L cyc':>8s} "
             f"{'F cyc':>8s} {'L/insn':>7s} {'F/insn':>7s}")

    def mean(name, var):
        e = runs.get((name, var))
        return (e[1] / e[0], e[2] / e[0], e[0]) if e else None

    bl, bf = mean("base", "L"), mean("base", "F")
    for name in order:
        lv, fv = mean(name, "L"), mean(name, "F")
        if lv is None:
            continue

        def per(x, b):
            if x is None or b is None:
                return "--"
            if name == "base":
                return f"{x[0] / x[1]:.1f}"
            return f"{(x[0] - b[0]) / (x[1] - b[1]):.1f}" \
                if x[1] > b[1] else "--"
        r.append(f"  {name:8s} {lv[2]:5d} {lv[1]:6.0f} {lv[0]:8.0f} "
                 f"{fv[0] if fv else 0:8.0f} {per(lv, bl):>7s} "
                 f"{per(fv, bf):>7s}")
    for name in order:
        nv = mean(name, "N")
        if nv:
            r.append(f"  probe {name}: {nv[0] / nv[1]:.1f} cycles per "
                     f"byte load (loop included), {nv[2]} runs")
    return r


# What a lamer's slowscan said about why it is stuck: STUCK? per hart,
# the BHANG! assert and its operands, the init sequencer's ded marks
# and hart masks, a FAIL, physics failure marks, and backtrace words
# that name a function.
LAMER_RX = re.compile(r"STUCK\?|BHANG|ded(When|What)|CALR0|FAIL|"
                      r"\(u32\) ?(memask|whomask|ps\.)|"
                      r"\.p[fw]:|!sp\[\d+\] = \S+ \{")
LAMER_MAX_LINES = 40            #< per lamer hub
LAMER_MAX_HUBS = 8


def parse_ints(s):
    """'1-3,7' -> [1, 2, 3, 7]; None if not that form (e.g. '?')."""
    xs = []
    for part in s.split(","):
        a, _, b = part.partition("-")
        if not a.isdigit() or (b and not b.isdigit()):
            return None
        xs.extend(range(int(a), int(b or a) + 1))
    return xs


def lamer_tags(misc):
    """[(chip, tlbi, tag)] of the hubs the timed-out bolts named, in
    order, from misc/mfmxctl-BOLT.txt and misc/PKTS.txt."""
    tags = {}
    for l in read_lines(os.path.join(misc, "PKTS.txt")):
        m = re.match(r"TILE chip=(\d+) tlbi=(\d+) .*tag=(\S+)", l)
        if m:
            tags[(int(m.group(1)), int(m.group(2)))] = m.group(3)
    seen = []
    for l in read_lines(os.path.join(misc, "mfmxctl-BOLT.txt")):
        if not (l.startswith("BOLT:DONE") and "TIMEOUT" in l):
            continue
        for m in re.finditer(r"bh(\d+):\d+ acked, lamers (\S+)", l):
            for t in parse_ints(m.group(2)) or []:
                k = (int(m.group(1)), t)
                if k not in [s[:2] for s in seen]:
                    seen.append(k + (tags.get(k, f"bh{k[0]}:tlbi{t}"),))
    return seen


def lamer_failures(rundir, misc):
    """Why the hubs a timed-out bolt named are stuck, from their slowscan
    lines in the full all.mfmk (the bundle keeps only its tail).  A
    slowscan's HostBlock dump sits between `TAG[...]<<<` and `>>>TAG[`
    and its lines do not carry the tag, so lines are taken by block."""
    lam = lamer_tags(misc)
    if not lam:
        return []
    r = section("lamers (hubs that missed a bolt: slowscan evidence)")
    path = os.path.join(rundir, "all.mfmk")
    want = {tag: [] for (_c, _t, tag) in lam[:LAMER_MAX_HUBS]}
    seen = {tag: set() for tag in want}
    inside = None
    try:
        with open(path, "r", errors="replace") as f:
            for l in f:
                l = l.rstrip("\n")
                for tag in want:
                    if tag + "[" in l and "<<<" in l:
                        inside = tag
                    elif ">>>" + tag + "[" in l:
                        inside = None
                hit = inside
                if hit is None:
                    for tag in want:
                        if tag in l:
                            hit = tag
                            break
                if hit is None or not LAMER_RX.search(l):
                    continue
                # one copy of each distinct line (scans repeat them)
                key = re.sub(r"^\S+:\d+: [0-9.]+ ", "", l)
                if key in seen[hit] or len(want[hit]) >= LAMER_MAX_LINES:
                    continue
                seen[hit].add(key)
                want[hit].append(cut(l))
    except OSError:
        return r + ["[all.mfmk not readable]"]
    for c, t, tag in lam[:LAMER_MAX_HUBS]:
        r.append(f"[bh{c} tlbi {t} = {tag}: {len(want[tag])} lines]")
        r.extend(want[tag] or ["(no slowscan evidence in all.mfmk)"])
    if len(lam) > LAMER_MAX_HUBS:
        r.append(f"(and {len(lam) - LAMER_MAX_HUBS} more lamers)")
    return r


NOTCH_SIDES = (4, 8, 12, 16, 20, 24, 28)


def notches(misc):
    """Corner notches logged by `notch on` (notchwatch.py), with the
    control points (hub edge midpoints and centers) side by side: per
    kind, notches per point by size, so an excess at corners stands out.
    Nothing if never turned on."""
    lines = read_lines(os.path.join(misc, "mfmxctl-NOTCH.txt"))
    npts = {"corner": 117}              # runs before the controls
    for l in lines:
        m = re.search(r"NOTCHPOINTS (.*)", l)
        if m:
            w = m.group(1).split()
            npts = {w[i]: int(w[i + 1]) for i in range(0, len(w) - 1, 2)}
    rx = re.compile(r"NOTCH (?:scy \d+ )?(corner|edge|center) \(\d+,\d+\) "
                    r"(NW|NE|SW|SE) (\d+)x\d+ ")
    rows = []
    for l in lines:
        m = rx.search(l)
        if m:
            rows.append((m.group(1), m.group(2), int(m.group(3)), l))
    if not rows and not any("NOTCHPOINTS" in l for l in lines):
        return []
    r = section("corner notches (notch on; OPEN_ISSUES 'CORNER NOTCHES')")
    r.append("notches per point by largest empty side (count in "
             "parentheses); corner = the suspect, edge and center = "
             "FB background")
    r.append(f"  {'kind':7s} {'points':>6s} "
             + " ".join(f"{s_:>11d}" for s_ in NOTCH_SIDES)
             + f" {'all':>11s}")
    for kd in ("corner", "edge", "center"):
        if kd not in npts:
            continue
        p = max(npts[kd], 1)
        cnt = {s_: 0 for s_ in NOTCH_SIDES}
        for (k, _q, side, _l) in rows:
            if k == kd and side in cnt:
                cnt[side] += 1
        tot = sum(cnt.values())
        r.append(f"  {kd:7s} {npts[kd]:6d} "
                 + " ".join(f"{cnt[s_] / p:5.2f}({cnt[s_]:4d})"
                            for s_ in NOTCH_SIDES)
                 + f" {tot / p:5.2f}({tot:4d})")
    corner = [x for x in rows if x[0] == "corner"]
    quads = {}
    for (_k, q, _s, _l) in corner:
        quads[q] = quads.get(q, 0) + 1
    r.append("corner notches by square: "
             + ", ".join(f"{k}: {v}" for k, v in sorted(quads.items())))
    big = [x[3] for x in corner if x[2] >= 20]
    r.append(f"corner notches of side 20 or more: {len(big)} (first 12)")
    r.extend(cut(l) for l in big[:12])
    return r


def summarize(out, rundir, cmdline=None, elapsed=None, pat=None, mx=60,
              outpath=None):
    """The summary as a list of lines.  out: console lines."""
    misc = os.path.join(rundir, "misc")
    lines = [f"runsum of {os.path.basename(rundir.rstrip('/'))}"]
    for part in (head_info(out, cmdline, elapsed), bolts(misc),
                 lamer_failures(rundir, misc), feed(misc),
                 aer(misc), misc_counts(misc), notches(misc),
                 hub_marks(rundir),
                 physics_timing(rundir), bench_timing(rundir),
                 console(out), tail(out)):
        lines.extend(part)
    if pat:
        lines.extend(grep(outpath, rundir, pat, mx))
    return lines


MARKS_TAIL_BYTES = 2 << 20      #< how much of the end of all.mfmk to bundle


def write_bundle(rundir, out, cmdline=None, elapsed=None, limit=8 << 20):
    """Write rundir/runsum.txt and rundir/send-<rundir name>.tgz (runsum.txt, console.txt
    without mark lines, the last 2 MB of all.mfmk, and every misc/*.txt up
    to `limit` bytes).  Returns
    a one-line report."""
    misc = os.path.join(rundir, "misc")
    if not os.path.isdir(misc):
        return "run summary skipped: no misc directory"
    sumpath = os.path.join(rundir, "runsum.txt")
    with open(sumpath, "w") as f:
        f.write("\n".join(summarize(out, rundir, cmdline, elapsed)) + "\n")
    conpath = os.path.join(rundir, "console.txt")
    with open(conpath, "w") as f:
        f.write("\n".join(l for l in out if not l.startswith("{")) + "\n")
    # named for the run, so bundles from many runs can sit in one directory
    tgz = os.path.join(rundir, "send-"
                       + os.path.basename(rundir.rstrip("/")) + ".tgz")
    with tarfile.open(tgz, "w:gz") as t:
        t.add(sumpath, arcname="runsum.txt")
        t.add(conpath, arcname="console.txt")
        marks = os.path.join(rundir, "all.mfmk")     # the END of the marks
        if os.path.isfile(marks):
            tailpath = os.path.join(rundir, "all.mfmk.tail")
            with open(marks, "rb") as f:
                f.seek(0, os.SEEK_END)
                f.seek(max(0, f.tell() - MARKS_TAIL_BYTES))
                data = f.read()
            data = data[data.find(b"\n") + 1:]      # whole lines only
            with open(tailpath, "wb") as f:
                f.write(data)
            t.add(tailpath, arcname="all.mfmk.tail")
        for n in sorted(os.listdir(misc)):
            p = os.path.join(misc, n)
            if n.endswith(".txt") and os.path.getsize(p) <= limit:
                t.add(p, arcname="misc/" + n)
    return (f"run summary: {sumpath}; send {tgz} "
            f"({os.path.getsize(tgz) / 1024:.0f} KB)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", help="NAME.out console capture")
    ap.add_argument("rundir", help="MFMX-YYYYMMDD-HHMMSS directory")
    ap.add_argument("dest", nargs="?", help="output file (default NAME.sum)")
    ap.add_argument("--grep", metavar="REGEX")
    ap.add_argument("--max", type=int, default=60)
    ap.add_argument("--bundle", action="store_true",
                    help="write runsum.txt and send-<RUNDIR>.tgz in the run directory")
    a = ap.parse_args(argv)
    if not os.path.isdir(os.path.join(a.rundir, "misc")):
        sys.exit(f"runsum: no {a.rundir}/misc")
    out = read_lines(a.out)
    if a.bundle:
        print(write_bundle(a.rundir, out))
        return 0
    lines = summarize(out, a.rundir, pat=a.grep, mx=a.max, outpath=a.out)
    dest = a.dest or re.sub(r"(\.out)?$", ".sum", a.out, count=1)
    with open(dest, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{dest}: {os.path.getsize(dest)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
