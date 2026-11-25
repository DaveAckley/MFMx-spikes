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


def summarize(out, rundir, cmdline=None, elapsed=None, pat=None, mx=60,
              outpath=None):
    """The summary as a list of lines.  out: console lines."""
    misc = os.path.join(rundir, "misc")
    lines = [f"runsum of {os.path.basename(rundir.rstrip('/'))}"]
    for part in (head_info(out, cmdline, elapsed), bolts(misc), feed(misc),
                 aer(misc), misc_counts(misc), console(out), tail(out)):
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
