#!/usr/bin/env python3
"""Scenario tests for mfmxctl against the mock MFMx.  No hardware needed.

    python3 mock/selftest.py          # from srcs/host/python/EWD

Each scenario pipes commands into 'mfmxctl.py --mock' (so the real select loop,
tick scheduler, signal handling and teardown are exercised) and checks the
transcript.  Prints PASS/FAIL per check; exit status is nonzero on any FAIL.
"""
import os
import re
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
EWD = os.path.dirname(HERE)
CTL = os.path.join(EWD, "mfmxctl.py")
fails = []


def check(name, cond, detail=""):
    print(("PASS  " if cond else "FAIL  ") + name + (f"   [{detail}]" if (detail and not cond) else ""))
    if not cond:
        fails.append(name)


def run(script, api="async", extra_args=(), timeout=90, env_extra=None):
    env = dict(os.environ, MFMX_MOCK_API=api, MFMX_MOCK_SIMDIR=tempfile.mkdtemp(prefix="selftest-"),
               **(env_extra or {}))
    p = subprocess.run([sys.executable, CTL, "--mock", *extra_args], input=script,
                       capture_output=True, text=True, timeout=timeout, env=env)
    return p.returncode, p.stdout + p.stderr


FAST = ("mock delay HOLD_AT_BIRTH 0.05 0.2\nmock delay NEW_LEADER 0.05 0.3\n"
        "mock delay RUN_EVENTS 0.01 0.05\n")


def run_script(cmds, **kw):
    """Run cmds from a command file (-s), stdin empty: commands typed on stdin
    are executed DURING 'cycle waitpause' (so quit works), which would reorder
    a piped script."""
    d = tempfile.mkdtemp(prefix="selftest-cmd-")
    f = os.path.join(d, "t.cmd")
    with open(f, "w") as fh:
        fh.write("start\n" + cmds)
    return run("", extra_args=("--boot", "-s", f, "--script-exit"), **kw)


def scenario_normal_cycle():
    rc, out = run("boot\nstart\n" + FAST +
                  "shoot HOLD_AT_BIRTH 0\nwait 0.8\n"
                  "shoot NEW_LEADER 2 1\nwait 0.8\n"
                  "shoot RUN_EVENTS 3\nwait 0.5\nstatus\nbolt hist\nquit\n")
    check("normal: exit 0", rc == 0, f"rc={rc}")
    for cmd in ("HOLD_AT_BIRTH(0)", "NEW_LEADER(2,1,0,0,0)", "RUN_EVENTS(3)"):
        check(f"normal: {cmd} reaches CONSENSUS", f"{cmd} CONSENSUS" in out, out[-600:])
    check("normal: orderly teardown", "stop events: bh0 -> HAS_T6_CODE_RUNNING" in out
          and "quietbox shutdown: ok" in out)
    check("normal: async API detected", "async bolt API" in out)


def scenario_lamers_timeout():
    rc, out = run("boot\nstart\n" + FAST + "mock lamers 0 12 77 78 79\n"
                  "bolt timeout RUN_EVENTS 1\nshoot RUN_EVENTS 0\nwait 1.6\nstatus\nquit\n")
    check("lamers: bolt TIMEOUT reported", "RUN_EVENTS(0) TIMEOUT" in out, out[-800:])
    check("lamers: lamer TLBIs listed compactly", "lamers 12,77-79" in out, out[-800:])
    check("lamers: auto-diagnosis ran a slowscan", "diagnosing: slowscan" in out)
    check("lamers: exit 0", rc == 0)
    check("lamers: slowscan already on, tick not re-announced",
          "slowscan tick turned ON" not in out, out[-800:])
    # the freeze run (seam9) has `tick slowscan off`: a timeout must turn it on
    rc, out = run("boot\nstart\n" + FAST + "tick slowscan off\nmock lamers 0 12 77\n"
                  "bolt timeout RUN_EVENTS 1\nshoot RUN_EVENTS 0\nwait 1.6\ntick slowscan\nquit\n")
    check("lamers: timeout turns the slowscan tick ON",
          "slowscan tick turned ON" in out and "slowscan: on every" in out, out[-800:])


def scenario_ticks_during_pending_bolt():
    rc, out = run("boot\nstart\nmock delay NEW_LEADER 1.0 1.2\ntick slowscan period 0.3\n"
                  "shoot NEW_LEADER 1 1\nwait 1.0\nstatus\nwait 0.6\nquit\n",
                  extra_args=("--slowscan-gap", "0"))
    runs = [int(l.split("run:")[1].split()[0]) for l in out.splitlines() if l.startswith("slowscans run:")]
    check("ticks: slowscan ticks keep running while a bolt is pending",
          bool(runs) and runs[0] >= 2, f"runs={runs}")
    check("ticks: bolt still completes afterwards", "NEW_LEADER(1,1,0,0,0) CONSENSUS" in out, out[-500:])


def scenario_superseded():
    rc, out = run("boot\nstart\nmock delay NEW_LEADER 1.0 1.2\nshoot NEW_LEADER 1 1\n"
                  "shoot RUN_EVENTS 0\nwait 1.5\nquit\n")
    check("superseded: first bolt marked superseded", "NEW_LEADER superseded" in out, out[-500:])
    check("superseded: second bolt reaches consensus", "RUN_EVENTS(0) CONSENSUS" in out)


def scenario_legacy_blocking():
    rc, out = run("boot\nstart\nshoot RUN_EVENTS 0\nmock lamers 0 5\nshoot RUN_EVENTS 0\nstatus\nquit\n",
                  api="legacy")
    check("legacy: banner warns about blocking", "LEGACY blocking shootPHASER" in out, out[:600])
    check("legacy: clean bolt -> CONSENSUS", "RUN_EVENTS(0) CONSENSUS" in out, out[-600:])
    check("legacy: lamer bolt -> TIMEOUT", "RUN_EVENTS(0) TIMEOUT" in out, out[-600:])
    check("legacy: timeout also triggers the auto-diagnosis slowscan", "diagnosing: slowscan" in out, out[-600:])
    check("legacy: exit 0, no QuietBox.shutdown needed", rc == 0 and "quietbox shutdown" not in out)


def scenario_guards_and_errors():
    rc, out = run("shoot HOLD_AT_BIRTH 0\nboot\nstop\nstep\nshoot NOPE 1\nbogus\nshoot RUN_EVENTS x\n"
                  "decay 3\nquit\n")
    check("guards: shoot before boot refused", "not booted: run 'boot' first" in out)
    check("guards: stop before start leaves chip alone",
          "left alone" in out and "bh0=HAS_ALLOCATED_TLBS" in out, out[-700:])
    check("guards: unknown PHASER command explained", "unknown PHASER command 'NOPE'" in out)
    check("guards: unknown shell command explained", "unknown command 'bogus'" in out)
    check("guards: bad int arg reported, shell survives", "error: ValueError" in out and rc == 0)


def seam_config(ethseams):
    """A copy of nu1012.dtoml with the seamtest layout active and its
    ethseams replaced by 'ethseams' (a TOML array text)."""
    src = open(os.path.join(EWD, "config", "nu1012.dtoml")).read()
    src = src.replace("activeLayout=ewpandhub", "activeLayout=seamtest")
    src = re.sub(r'ethseams = \[.*\]', "ethseams = " + ethseams, src)
    f = os.path.join(tempfile.mkdtemp(prefix="seamcfg-"), "seams.dtoml")
    with open(f, "w") as fh:
        fh.write(src)
    return f


def scenario_ethseams():
    cfg = seam_config('[ "0:E:1:6,7,10,11" ]')
    rc, out = run("boot\nstart\nseams\nstatus\nstop\nseams\nquit\n",
                  api="fire", extra_args=("--config", cfg))
    check("seams: exit 0", rc == 0, f"rc={rc}\n{out[-800:]}")
    check("seams: declared from the layout",
          "eth-seams declared: bh0-E->bh1 eth6,7,10,11" in out, out[-800:])
    check("seams: all four links up",
          all(f"link bh0 <-> bh1 eth{x} UP" in out for x in (6, 7, 10, 11)), out[-800:])
    # order: declare, then both chips deployed, then links up, then T6 release
    i = [out.find(t) for t in ("eth-seams declared", "bh1 -> HAS_T6_CODE_DEPLOYED",
                               "eth-seams up:", "-> HAS_T6_EVENT_WINDOWS")]
    check("seams: declare < deploy < bring-up < release",
          all(x >= 0 for x in i) and i == sorted(i), str(i))
    check("seams: 'seams' command reports health",
          "bh1 eth11: port=1" in out and "fec corrected=12345" in out, out[-800:])
    check("seams: status line lists the seams", "eth-seams: bh0-E->bh1" in out)
    check("seams: E1 image found through the bindirs",
          "e1image: " in out and out.split("e1image: ")[1].split("\n")[0].endswith("e1fwd16.bin"), out[-600:])
    check("seams: stop tears the links down", "eth-seams down" in out)
    check("seams: no link left after stop", "bh0 eth6: no link" in out, out[-500:])

    cfg = seam_config('[ "0:E:1:6" ]')
    rc, out = run("boot\nstart\nstatus\nstop\nquit\n",
                  api="fire", extra_args=("--config", cfg))
    check("seams-fail: bring-up failure is reported",
          "eth-seam bring-up failed" in out and "NCH_MAX" in out, out[-800:])
    check("seams-fail: T6s not released", "HAS_T6_EVENT_WINDOWS" not in out
          and "bh0=HAS_T6_CODE_DEPLOYED" in out, out[-800:])
    check("seams-fail: stop still leaves everything down", "eth-seams down" in out, out[-500:])

    cfg = seam_config('[ "0:E:1:6,7,10,11" ]')
    rc, out = run("boot\nstart\nquit\n", api="async", extra_args=("--config", cfg))
    check("seams-nofire: refused without firePHASER", "need the firePHASER start path" in out, out[-500:])

    # a layout without ethseams is untouched: no seam output at all
    rc, out = run("boot\nstart\nseams\nquit\n", api="fire")
    check("seams-none: layout without ethseams unchanged",
          "no ethseams in the active layout" in out
          and not any(t in out for t in ("eth-seams declared:", "eth-seams up:",
                                         "\neth-seams down", "link bh", "\neth-seams: bh")),
          out[-500:])


def scenario_link_preflight():
    cfg = seam_config('[ "0:E:1:6,7" ]')
    base = ("--config", cfg, "--link-wait", "0.3")

    rc, out = run("boot\nquit\n", api="fire", extra_args=base)
    check("preflight: all Up passes quietly",
          "link preflight: all 4 link ends Up" in out
          and "chip reset 1" not in out, out[-600:])

    # fake tt-smi so the mock really "resets"; 8 down reads = 2 rounds
    # of 4 ends, so the third round (after 2 resets) succeeds
    env = dict(os.environ, MFMX_MOCK_LINK_DOWN_POLLS="8")
    os.environ.update(env)
    try:
        rc, out = run("boot\nquit\n", api="fire",
                      extra_args=base + ("--tt-smi", "/bin/true"))
    finally:
        os.environ.pop("MFMX_MOCK_LINK_DOWN_POLLS")
    check("preflight: resets until the links come up",
          "chip reset 1 of 10" in out and "all 4 link ends Up" in out
          and "extra reset(s)" in out, out[-900:])
    check("preflight: names the links that were down",
          "bh0:eth6(port 2)" in out, out[-900:])

    os.environ["MFMX_MOCK_LINK_DOWN_POLLS"] = "100000"
    try:
        rc, out = run("boot\nquit\n", api="fire",
                      extra_args=base + ("--tt-smi", "/bin/true",
                                         "--link-resets", "2"))
    finally:
        os.environ.pop("MFMX_MOCK_LINK_DOWN_POLLS")
    check("preflight: gives up after link-resets, naming the links",
          "chip reset 2 of 2" in out and "chip reset 3" not in out
          and "still not Up after 2 extra chip reset(s)" in out
          and "bh1:eth7(port 2)" in out, out[-900:])

    os.environ["MFMX_MOCK_LINK_DOWN_POLLS"] = "100000"
    try:
        rc, out = run("boot\nquit\n", api="fire", extra_args=base)
    finally:
        os.environ.pop("MFMX_MOCK_LINK_DOWN_POLLS")
    check("preflight: no reset available -> waits once then fails",
          "chip reset 1" not in out and "still not Up after 0 extra" in out,
          out[-900:])

    rc, out = run("boot\nquit\n", api="fire")
    check("preflight: layouts without seams do not preflight",
          "link preflight:" not in out, out[-400:])


def scenario_abort_on_error():
    def go(extra, lines):
        f = os.path.join(tempfile.mkdtemp(prefix="abort-"), "s.cmd")
        with open(f, "w") as fh:
            fh.write(lines)
        return run("", extra_args=("-s", f, "--script-exit") + extra)

    script = "echo before\nbogus_command\necho after\nquit\n"
    rc, out = go((), script)
    check("abort: default keeps going past a bad line",
          "after" in out.split("bogus_command")[-1] and rc == 0, out[-500:])
    rc, out = go(("--abort-on-error",), script)
    check("abort: flag stops at the bad line, exit 1, orderly shutdown",
          "script aborted (--abort-on-error) at: bogus_command" in out
          and "\nafter\n" not in out.split("script aborted")[-1]
          and "shutting down..." in out and rc == 1, f"rc={rc}\n{out[-600:]}")
    rc, out = go(("--abort-on-error",), "echo ok\nquit\n")
    check("abort: a clean script is unaffected", rc == 0 and "ok" in out, out[-400:])
    rc, out = go(("--abort-on-error",), "start\necho after\nquit\n")
    check("abort: start before boot (a command error) aborts",
          "script aborted" in out and rc == 1, out[-500:])


def run_pty(steps, timeout=60):
    """Run mfmxctl --mock on a pseudo-terminal (stdin is a tty, as when a
    person types).  steps: [(delay_secs, text_to_type), ...].  Returns
    (rc, output, elapsed)."""
    import pty
    env = dict(os.environ, MFMX_MOCK_API="async",
               MFMX_MOCK_SIMDIR=tempfile.mkdtemp(prefix="selftest-"))
    mfd, sfd = pty.openpty()
    t0 = time.monotonic()
    p = subprocess.Popen([sys.executable, CTL, "--mock"], stdin=sfd,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         env=env)
    os.close(sfd)
    for delay, text in steps:
        time.sleep(delay)
        os.write(mfd, text.encode())
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
    os.close(mfd)
    return p.returncode, out.decode("utf-8", "replace"), time.monotonic() - t0


def scenario_wait_typed_input():
    rc, out, dt = run_pty([(1.0, "wait 40\n"), (2.0, "quit\n")])
    check("wait-input: a typed quit ends a long wait early (tty)",
          rc == 0 and dt < 20, f"rc={rc} dt={dt:.1f}\n{out[-300:]}")
    rc, out, dt = run_pty([(1.0, "wait 40\n"), (1.5, "wait 5\n"),
                           (1.0, "echo hi\n"), (1.0, "quit\n")])
    check("wait-input: a typed wait inside a wait is refused (tty)",
          "already waiting" in out and "hi" in out and rc == 0 and dt < 20,
          f"dt={dt:.1f}\n{out[-400:]}")
    rc, out, dt = run_pty([(1.0, "wait 3\n"), (1.0, "echo hi\n")])
    check("wait-input: the wait prompt shows during a wait (tty)",
          out.count("mfmx-waiting> ") >= 2 and "hi" in out, out[-400:])
    # piped input is all buffered at once: wait must NOT run the rest early
    t0 = time.monotonic()
    rc, out = run("wait 3\necho AFTER\nquit\n")
    dt = time.monotonic() - t0
    check("wait-input: on a pipe the rest of the input waits its turn",
          rc == 0 and dt >= 2.5 and "AFTER" in out, f"dt={dt:.1f}")


def scenario_ethseams_layout_option():
    rc, out = run("quit\n", api="fire",
                  extra_args=("--layout", "seamtest", "--boot", "-s",
                              os.path.join(EWD, "config", "seamtest.cmd"), "--script-exit"),
                  timeout=150)
    check("layout opt: seamtest script runs to exit 0", rc == 0, f"rc={rc}\n{out[-600:]}")
    check("layout opt: --layout selects seamtest", "layout 'seamtest', chips [0, 1]" in out, out[:600])
    check("layout opt: links up after start", "eth-seams up:" in out, out[-800:])
    check("layout opt: stop tears down", "eth-seams down" in out and "bh0 eth6: no link" in out, out[-600:])
    rc, out = run("quit\n", api="fire", extra_args=("--layout", "nope", "--boot"))
    check("layout opt: unknown layout is a clear error",
          "unknown layout 'nope'" in out and "seamtest" in out, out[-500:])


def scenario_ethseams_ns():
    d = tempfile.mkdtemp(prefix="seamns-")
    f = os.path.join(d, "ns.cmd")
    with open(f, "w") as fh:
        fh.write(FAST + "dwell 0.2 0.3\nstart\nseams\ncycle start\nwait 4\nseams\n"
                 "cycle stop\nwait 2\nstop\nseams\nquit close\n")
    rc, out = run("quit\n", api="fire",
                  extra_args=("--layout", "seamns", "--boot", "-s", f, "--script-exit"),
                  timeout=150)
    check("ns: script exits 0", rc == 0, f"rc={rc}\n{out[-600:]}")
    check("ns: layout seamns uses chips 0 and 2", "layout 'seamns', chips [0, 2]" in out, out[:600])
    check("ns: declares the N/S cabling", "declared: bh0-S->bh2 eth3,4,12,13" in out, out[-900:])
    check("ns: all four links come up", all(f"link bh0 <-> bh2 eth{x} UP" in out for x in (3, 4, 12, 13)), out[-900:])
    check("ns: stop tears down", "eth-seams down" in out and "bh2 eth13: no link" in out, out[-600:])


def scenario_ethseams_four():
    d = tempfile.mkdtemp(prefix="seam4-")
    f = os.path.join(d, "four.cmd")
    with open(f, "w") as fh:
        fh.write(FAST + "dwell 0.2 0.3\nstart\nseams\ncycle start\nwait 4\nseams\n"
                 "cycle stop\nwait 2\nstop\nseams\nquit close\n")
    rc, out = run("quit\n", api="fire",
                  extra_args=("--layout", "seam4", "--boot", "-s", f, "--script-exit"),
                  timeout=200)
    check("four: script exits 0", rc == 0, f"rc={rc}\n{out[-600:]}")
    check("four: layout seam4 uses all four chips", "layout 'seam4', chips [0, 1, 2, 3]" in out, out[:700])
    check("four: declares all four seams",
          all(t in out for t in ("bh0-E->bh1 eth6,7,10,11", "bh2-E->bh3 eth6,7,10,11",
                                 "bh0-S->bh2 eth3,4,12,13", "bh1-S->bh3 eth3,4,12,13")), out[-900:])
    check("four: 16 links come up",
          all(f"link bh{a} <-> bh{b} eth{x} UP" in out
              for a, b, xs in ((0, 1, (6, 7, 10, 11)), (2, 3, (6, 7, 10, 11)),
                               (0, 2, (3, 4, 12, 13)), (1, 3, (3, 4, 12, 13))) for x in xs), out[-1200:])
    check("four: stop tears down", "eth-seams down" in out and "bh3 eth13: no link" in out, out[-600:])


def scenario_ethseams_counters():
    d = tempfile.mkdtemp(prefix="seamcyc-")
    f = os.path.join(d, "cyc.cmd")
    with open(f, "w") as fh:
        fh.write(FAST + "dwell 0.2 0.3\nstart\nseams\ncycle start\nwait 4\nseams\n"
                 "cycle stop\nwait 2\nstop\nquit close\n")
    rc, out = run("quit\n", api="fire",
                  extra_args=("--layout", "seamtest", "--boot", "-s", f, "--script-exit"),
                  timeout=150)
    check("counters: script exits 0", rc == 0, f"rc={rc}\n{out[-600:]}")
    check("counters: 'seams' prints the counter table",
          "channel counters" in out and "total: out=" in out, out[-900:])
    tot = [int(l.split("out=")[1].split()[0]) for l in out.splitlines()
           if l.strip().startswith("total: out=")]
    check("counters: first dump has two totals recorded", len(tot) >= 2, str(tot))
    check("counters: totals move between dumps", len(tot) >= 2 and tot[-1] > tot[0], str(tot))
    check("counters: per-channel line names chip, tile and channel",
          "bh0 eth6 ch0: out=" in out and "bh1 eth11 ch2: out=" in out, out[-900:])


def scenario_ethseams_parse():
    sys.path.insert(0, EWD)
    import machine
    ok = machine.parse_ethseams(["0:E:1:6,7", "0:s:2:3,4", "1:1:0:6"], [0, 1, 2])
    check("seam parse: valid specs", ok == [(0, 3, 1, [6, 7]), (0, 2, 2, [3, 4]), (1, 1, 0, [6])], str(ok))
    check("seam parse: none given", machine.parse_ethseams(None, [0]) == [])
    for bad, why in (("0:E:1", "too few fields"), ("0:X:1:6", "bad direction letter"),
                     ("0:E:1:", "no links"), ("0:E:9:6", "chip not in layout"),
                     ("a:E:1:6", "bad chip number"), ("0:7:1:6", "direction out of range"),
                     ("0:E:1:6,x", "bad link number")):
        try:
            machine.parse_ethseams([bad], [0, 1])
            check(f"seam parse: rejects {why}", False)
        except ValueError:
            check(f"seam parse: rejects {why}", True)


def scenario_sigterm():
    env = dict(os.environ, MFMX_MOCK_API="async", MFMX_MOCK_SIMDIR=tempfile.mkdtemp(prefix="selftest-"))
    p = subprocess.Popen([sys.executable, CTL, "--mock"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    p.stdin.write("boot\nstart\n")
    p.stdin.flush()
    time.sleep(1.5)
    p.send_signal(signal.SIGTERM)
    try:
        out, _ = p.communicate(timeout=20)
    except subprocess.TimeoutExpired:
        p.kill()
        out, _ = p.communicate()
        out += "\n(KILLED: did not exit on SIGTERM)"
    check("sigterm: exits with 128+15", p.returncode == 143, f"rc={p.returncode}")
    check("sigterm: orderly teardown ran",
          "stop events: bh0 -> HAS_T6_CODE_RUNNING" in out and "quietbox shutdown: ok" in out, out[-400:])


def scenario_quit_close():
    rc, out = run("boot\nstart\nquit close\n")
    check("quit close: chips fully retreated", "close chips: bh0 -> HAS_CHIP_NUM" in out, out[-400:])


def scenario_shell_tick_errors():
    sys.path.insert(0, EWD)
    from shell import Shell
    r, w = os.pipe()
    sh = Shell(fd=r, out=open(os.devnull, "w"))
    calls = []

    def bad():
        calls.append(1)
        raise RuntimeError("boom")
    t = sh.add_tick("bad", 0.01, bad)
    sh.run_for(0.5)
    check("shell: failing tick is disabled after 3 consecutive errors",
          len(calls) == 3 and not t.enabled, f"calls={len(calls)} enabled={t.enabled}")
    os.close(w)


def hist(text):
    """Parse 'bolt hist' lines: '#3 RUN_EVENTS(1) CONSENSUS 0.1s' -> (cmd, [args], state)."""
    return [(c, [int(x) for x in a.split(",") if x], s)
            for c, a, s in re.findall(r"^#\d+ (\w+)\(([\d,]*)\) (\w+)", text, re.M)]


def sections(out):
    """Split a transcript on '# NAME' marker lines the script echoed."""
    parts = re.split(r"mfmx> # (\w+)\n", out)
    return {parts[i]: parts[i + 1] for i in range(1, len(parts) - 1, 2)}


CYC = FAST + "dwell 0.2\n"


def scenario_cycle_run():
    rc, out = run("boot\nstart\n" + CYC.replace("dwell 0.2", "dwell 0.3") +
                  "cycle start\nwait 3.0\ncycle stop\nbolt hist\nquit\n")
    h = hist(out)
    check("cycle: HOLD_AT_BIRTH fires first", bool(h) and h[0][0] == "HOLD_AT_BIRTH", str(h[:2]))
    nl = [a for c, a, s in h if c == "NEW_LEADER"]
    re_ = [a for c, a, s in h if c == "RUN_EVENTS"]
    check("cycle: at least 2 full cycles", len(nl) >= 2 and len(re_) >= 2, f"nl={nl} re={re_}")
    check("cycle: first leader is 2, scy starts at 1", nl[:1] == [[2, 1, 0, 0, 0]], str(nl))
    check("cycle: scy increments by one", [a[1] for a in nl] == list(range(1, len(nl) + 1)), str(nl))
    check("cycle: consecutive leaders differ",
          all(nl[i][0] != nl[i + 1][0] for i in range(len(nl) - 1)), str(nl))
    check("cycle: RUN_EVENTS carries its cycle's scy", all(re_[i] == [nl[i][1]] for i in range(len(re_))),
          f"nl={nl} re={re_}")
    check("cycle: order is NL,RE,NL,RE...", [c for c, a, s in h][1:5] == ["NEW_LEADER", "RUN_EVENTS"] * 2,
          str([c for c, a, s in h]))


def scenario_cycle_holdat():
    rc, out = run_script(CYC.replace("dwell 0.2", "dwell 0.1") +
                  "cycle holdat NEW_LEADER 3\ncycle status\ncycle start\n"
                  "cycle waitpause 20\ncycle status\ncycle holdat\n")
    check("holdat: armed message", "will pause when next is NEW_LEADER and scy >= 3" in out, out[-300:])
    check("holdat: paused with NEW_LEADER next, scy 3",
          "paused after DWELL (scy 3); next: NEW_LEADER" in out, out[-400:])
    check("holdat: waitpause reports paused", "cycle paused:" in out, out[-300:])
    check("holdat: disarmed after firing", "holdat not armed" in out, out[-200:])
    rc2, out2 = run("boot\nstart\n" + CYC + "cycle holdat bogus\ncycle holdat off\nquit\n")
    check("holdat: bad step rejected", "holdat" in out2 and "must be one of" in out2.replace("\n", " ") or "unknown" in out2.lower(), out2[-300:])


def scenario_cycle_freeze_retry():
    rc, out = run("boot\nstart\n" + CYC + "mock lamers 0 5 6 cmd RUN_EVENTS\nbolt timeout RUN_EVENTS 0.5\n"
                  "cycle start\nwait 2.0\n# A\nbolt hist\nwait 1.5\n# B\nbolt hist\ncycle status\n"
                  "mock clear\ncycle retry\nwait 1.0\n# C\nbolt hist\ncycle status\n"
                  "cycle start\nwait 1.2\ncycle stop\n# D\nbolt hist\nquit\n")
    s = sections(out)
    check("freeze: cycle FROZEN at the failing step, lamers named",
          "FROZEN at scy 1, step RUN_EVENTS" in out and "lamers 5-6" in out, out[-900:])
    check("freeze: stops firing bolts while frozen",
          len(hist(s.get("A", ""))) == 3 and len(hist(s.get("B", ""))) == 3,
          f"A={hist(s.get('A',''))} B={hist(s.get('B',''))}")
    check("freeze: status shows FROZEN", "cycle: frozen" in s.get("B", ""))
    check("freeze: retry re-fires with the SAME args", "re-firing RUN_EVENTS(1)" in out)
    check("freeze: retry then holds", "cycle: paused" in s.get("C", "") and len(hist(s.get("C", ""))) == 4,
          s.get("C", ""))
    check("freeze: 'cycle start' continues with the next cycle (scy 2)",
          any(c == "NEW_LEADER" and a[1] == 2 for c, a, st in hist(s.get("D", ""))), s.get("D", ""))


def scenario_cycle_step_and_hold():
    script = "boot\nstart\n" + CYC
    for i in range(1, 6):
        script += f"cycle step\nwait {0.8 if i != 4 else 0.6}\n# S{i}\nbolt hist\n"
    script += "cycle start\nwait 0.5\ncycle hold\nwait 1.0\n# A\nbolt hist\ncycle status\nwait 1.0\n# B\nbolt hist\nquit\n"
    rc, out = run(script)
    s = sections(out)
    counts = [len(hist(s.get(f"S{i}", ""))) for i in range(1, 6)]
    check("step: each 'cycle step' runs exactly one step (HOLD, NL, RE, DWELL, NL)",
          counts == [1, 2, 3, 3, 4], str(counts))
    check("hold: stops after the current step and stays stopped",
          len(hist(s.get("A", ""))) == len(hist(s.get("B", ""))) and "cycle: paused" in s.get("A", ""),
          f"A={len(hist(s.get('A','')))} B={len(hist(s.get('B','')))}")


def scenario_cycle_late_consensus():
    rc, out = run("boot\nstart\n" + CYC + "mock slow 0 2.0 5\nbolt timeout NEW_LEADER 0.6\n"
                  "cycle start\nwait 6.0\ncycle status\nquit\n", timeout=120)
    check("late: cycle freezes on the slow NEW_LEADER", "FROZEN at scy 1, step NEW_LEADER" in out, out[-700:])
    check("late: reports the frozen bolt reached consensus LATE", "reached consensus LATE" in out, out[-700:])
    check("late: stays frozen (does not resume by itself)", "cycle: frozen" in out)


def scenario_cycle_legacy_freeze():
    rc, out = run("boot\nstart\n" + CYC + "mock lamers 0 5 cmd NEW_LEADER\ncycle start\nwait 5.0\nstatus\nquit\n",
                  api="legacy", timeout=120)
    check("legacy cycle: freezes when the blocking bolt times out",
          "FROZEN at scy 1, step NEW_LEADER" in out and "cycle: frozen" in out, out[-700:])


def scenario_cycle_guards():
    rc, out = run("cycle start\nboot\nstart\nmock delay HOLD_AT_BIRTH 1.0 1.0\ncycle step\nwait 0.3\ncycle step\ncycle retry\ncycle leader 9\n"
                  "cycle leader 1\ncycle bogus\nquit\n")
    check("cycle guards: start before boot refused", "not booted" in out)
    check("cycle guards: step while a step is active refused", "in progress" in out)
    check("cycle guards: retry when not frozen refused", "not frozen" in out)
    check("cycle guards: bad leader rejected, good leader accepted",
          "leader must be 0..3" in out and "next NEW_LEADER will use leader 1" in out)
    check("cycle guards: unknown subcommand explained", "unknown cycle subcommand" in out and rc == 0)


def scenario_abbrev():
    rc, out = run("boot\nsta\nst\nbol hi\ncy sta\ncy st\ncy sto\nfr\nex\n")
    check("abbrev: unique prefix 'sta' is ambiguous (start/status)", "ambiguous command 'sta'" in out, out[-600:])
    check("abbrev: 'st' reported ambiguous with candidates", "ambiguous command 'st'" in out and "stop" in out)
    check("abbrev: 'bol hi' -> bolt hist", "none" in out or "#" in out)
    check("abbrev: ambiguous cycle subcommand 'st'", "ambiguous cycle subcommand 'st'" in out)
    check("abbrev: 'cy sto' -> cycle stop", "cycle stopped" in out)
    check("abbrev: 'fr' -> frame, 'ex' -> exit", "frame" in out and rc == 0)
    rc, out = run("boot\nzzz\nquit\n")
    check("abbrev: unknown still unknown", "unknown command 'zzz'" in out)


def scenario_abbrev_args():
    FASTB = FAST
    rc, out = run("boot\nstart\nq c\n")
    check("args: 'q c' is quit close", "close chips: bh0 -> HAS_CHIP_NUM" in out, out[-400:])
    rc, out = run("boot\nstart\nq x\nquit\n")
    check("args: unknown quit option is refused, not a plain quit",
          "unknown quit option 'x'" in out and "close chips" not in out, out[-400:])
    rc, out = run("boot\ngrid of\ngrid o\ngrid on\ngrid\nquit\n")
    check("args: grid of -> off, o ambiguous, on -> on",
          "mDrawGrid False" in out and "ambiguous grid 'o'" in out and "mDrawGrid True" in out, out[-600:])
    rc, out = run("boot\nbolt di of\nbolt di ON\nbolt di x\nquit\n")
    check("args: bolt diagnose on|off abbreviated, case blind, junk refused",
          "diagnose-on-timeout False" in out and "diagnose-on-timeout True" in out
          and "unknown bolt diagnose 'x'" in out, out[-600:])
    rc, out = run("boot\ncy l ra\ncy b d\ncy b p\ncy b q\ncy l 3\nquit\n")
    check("args: cycle leader random/N, birth done|pending",
          "next leader: random" in out and "treated as done" in out and "will be sent first" in out
          and "unknown cycle birth 'q'" in out and "leader 3" in out, out[-700:])
    rc, out = run("boot\nstart\nzhd r of\nzhd re on\nzhd r maybe\nzhd a\nzhd zz\nquit\n")
    check("args: zhd sub/value abbreviations, junk refused",
          "requests OFF" in out and "requests ON" in out and "unknown zhd restart 'maybe'" in out
          and "unknown zhd subcommand 'zz'" in out, out[-700:])
    rc, out = run("boot\nstart\n" + FASTB + "shoot hold 0\nwait 0.8\nshoot new_l 2 1\nwait 0.8\nshoot ru 3\nquit\n")
    check("args: bolt names abbreviated (hold, new_l; 'ru' ambiguous or unique, never silent junk)",
          "HOLD_AT_BIRTH(0) CONSENSUS" in out and "NEW_LEADER(2,1,0,0,0) CONSENSUS" in out, out[-700:])
    rc, out = run("boot\ndecay 7\nstart\nseed 3\nseed 25 2\nseed\nseed 0\nseed 5000\nseed al\nseed 1 2 3\nquit\n")
    check("args: seed COUNT [TYPE] (default decay type), bounded, junk refused",
          "seeded 3/3 type 7" in out and "seeded 25/25 type 2" in out
          and out.count("seed count must be 1..1000") == 2
          and out.count("usage: seed [COUNT [TYPE]]") == 2, out[-900:])
    rc, out = run("boot\nstart\nnuke b\nnuke zz\nquit\n")
    check("args: nuke options abbreviated, junk refused",
          "unknown nuke option 'zz'" in out, out[-700:])


def scenario_tick_abbrev():
    rc, out = run("tick sl of\ntick\ntick sl on\ntick zz on\ntick sl x\ntick fr\nquit\n")
    check("tick: name and on/off abbreviated, junk refused",
          "slowscan: off every" in out and "slowscan: on every" in out
          and "no such tick 'zz'" in out and "usage: tick NAME on|off|period SECS|now" in out,
          out[-700:])


def scenario_feed_placeholder():
    rc, out = run("feed on\nwait 1.0\n# PRE\nstatus\nboot\nstart\nwait 1.0\n# POST\nstatus\nquit\n")
    s = sections(out)
    pre, post = s.get("PRE", ""), s.get("POST", "")
    m1 = re.search(r"frames sent: (\d+) sim, (\d+) placeholder", pre)
    m2 = re.search(r"frames sent: (\d+) sim, (\d+) placeholder", post)
    check("placeholder: frames flow before boot, none are sim frames",
          bool(m1) and int(m1.group(1)) == 0 and int(m1.group(2)) > 0, pre[-300:])
    check("placeholder: sim frames take over after boot",
          bool(m2) and int(m2.group(1)) > 0, post[-300:])


def scenario_slowscan_gated():
    rc, out = run("boot\nstep HAS_TRANSPORT_THREAD\nwait 0.8\n# PRE\nstatus\nscan\n# FORCED\nstatus\n"
                  "step HAS_T6_CODE_RUNNING\nwait 0.8\n# POST\nstatus\nquit\n",
                  extra_args=("--slowscan-period", "0.1", "--slowscan-gap", "0"))
    s = sections(out)
    n = lambda k: int(re.search(r"slowscans run: (\d+)", s.get(k, "")).group(1))
    check("slowscan gate: no automatic scans before the T6s are released", n("PRE") == 0, s.get("PRE", "")[-300:])
    check("slowscan gate: manual 'scan' still works", n("FORCED") == 1, s.get("FORCED", "")[-300:])
    check("slowscan gate: automatic scans resume once released", n("POST") > 1, s.get("POST", "")[-300:])


def scenario_step_names():
    rc, out = run("boot\nstep\nstep HAS_TRANSPORT_THREAD\nstep\nstep has_t6_code_running\nstep\n"
                  "step CODE_RUNNING\nquit\n")
    check("step: bare 'step' lists every step name", out.count("HAS_T6_EVENT_WINDOWS") >= 1
          and "step names, in order" in out and "UNINITTED" in out, out[:900])
    check("step: exact names work (any case)",
          "bh0=HAS_TRANSPORT_THREAD" in out and "bh0=HAS_T6_CODE_RUNNING" in out, out[-900:])
    check("step: short names are NOT accepted; error lists exact names",
          "unknown step 'CODE_RUNNING'" in out and "exact names are" in out, out[-900:])


def scenario_feed_command():
    rc, out = run("feed\nboot\nstart\nfeed on\nwait 1.0\nfeed status\nfeed off\nfeed st\nquit\n")
    check("feed: starts with no feed", "feed: none" in out or "frame tick off" in out, out[:400])
    check("feed: 'feed on' enables frames", "feed on:" in out and "frame tick on" in out, out[-500:])
    # live mAER: the mock MFMx has a fake snapshot; status shows the line
    _rc, lout = run("boot\nstart\nfeed on\nwait 2.5\nstatus\nquit\n")
    check("live: status shows the AER line", "live: [2] est. AER: " in lout, lout[-600:])
    check("live: hubs counted", "live: [2] " in lout, lout[-600:])
    check("live: final figures logged at shutdown",
          "live AER final: [2] est. AER: " in lout, lout[-600:])
    check("feed: 'feed off' stops the tick", "feed frames stopped" in out and rc == 0, out[-500:])


def scenario_events_mode():
    rc, out = run("events\nevents honest\nevents s\nevents hon\nboot\nstart\n"
                  "mock\nstatus\nevents shortcut\nquit\n")
    check("events: default is shortcut", "events mode: shortcut (applied at next" in out, out[:400])
    check("events: honest accepted (and prefix 'hon')",
          "events mode: honest (applied" in out, out[:600])
    check("events: chips told at start (mock saw every_event=True)",
          "every_event=True" in out, out[-900:])
    check("events: status shows it", "events mode: honest\n" in out, out[-900:])
    check("events: refused after the code is deployed",
          "events mode is applied when the code is deployed" in out, out[-500:])
    rc, out = run("events bogus\nevents\nquit\n")
    check("events: bad name explained, shell survives",
          "want one of shortcut, honest" in out and rc == 0, out[-500:])
    rc, out = run("boot\nstart\nmock\nquit\n")
    check("events: default run tells chips every_event=False",
          "every_event=False" in out, out[-500:])
    rc, out = run("boot\nstart\nmock\nquit\n", extra_args=("--events", "honest"))
    check("events: --events honest tells chips every_event=True",
          "every_event=True" in out, out[-500:])


def scenario_elide_and_repeat():
    rc, out = run("elide\nelide on\nelide of\nelide on\nboot\nstart\nmock\n"
                  "status\nelide off\nquit\n")
    check("elide: default is off", "cache elision: off (applied at next" in out, out[:400])
    check("elide: on accepted", "cache elision: on (applied" in out, out[:600])
    check("elide: chips told at start (mock saw elide=True)", "elide=True" in out, out[-900:])
    check("elide: status shows it", "cache elision: on\n" in out, out[-900:])
    check("elide: refused after the code is deployed",
          "cache elision is applied when the code is deployed" in out, out[-500:])
    rc, out = run("boot\nstart\nmock\nquit\n")
    check("elide: default run tells chips elide=False", "elide=False" in out, out[-500:])
    rc, out = run("boot\nstart\nmock\nquit\n", extra_args=("--elide",))
    check("elide: --elide tells chips elide=True", "elide=True" in out, out[-500:])
    rc, out = run("cycle repeat\ncycle repeat on\ncycle rep of\ncycle repeat bogus\nquit\n")
    check("cycle repeat: default off, on accepted, bad value refused",
          "cycle repeat: off" in out and "cycle repeat: on" in out and rc == 0, out[-600:])


def scenario_cache_policy():
    rc, out = run("policy\npolicy fourcorners\npolicy supercell\npolicy four\n"
                  "boot\nstart\nmock\nstatus\npolicy supercell\nquit\n")
    check("policy: default is fourcorners", "cache policy: fourcorners (applied at next" in out, out[:400])
    check("policy: fourcorners accepted (prefix 'four' too)",
          "cache policy: fourcorners (applied" in out, out[:600])
    check("policy: chips told at start (mock saw policy=1)", "policy=1" in out, out[-900:])
    check("policy: status shows it", "cache policy: fourcorners\n" in out, out[-900:])
    check("policy: refused after the code is deployed",
          "set it before 'start'" in out, out[-500:])
    rc, out = run("policy corners\npolicy fourcornersish\npolicy\nquit\n")
    check("policy: only unique prefixes of the real names (no 'corners' alias)",
          out.count("want one of supercell, fourcorners") == 2
          and "cache policy: fourcorners (applied" in out, out[-500:])
    rc, out = run("policy fourcorners\npolicy bogus\npolicy\nquit\n")
    check("policy: bad name explained, shell survives",
          "want one of supercell, fourcorners" in out and rc == 0, out[-500:])
    rc, out = run("boot\nstart\nmock\nquit\n")
    check("policy: default run tells chips policy=1", "policy=1" in out, out[-500:])
    rc, out = run("policy supercell\nboot\nstart\nmock\nquit\n")
    check("policy: supercell opt-out tells chips policy=0", "policy=0" in out, out[-500:])


def scenario_dumpl1():
    sys.path.insert(0, EWD)
    import machine as MA
    seq = [MA.leximit(n) for n in (0, 1, 9, 10, 11, 99, 100, 999)]
    check("dumpl1: leximited counter", seq == ["10", "11", "19", "210", "211", "299", "3100", "3999"], str(seq))
    big = [MA.leximit(n) for n in (10**7, 10**8 - 1, 10**8, 10**9, 10**99)]
    check("dumpl1: leximited beyond 8 digits",
          big == ["81" + "0" * 7, "8" + "9" * 8, "919" + "1" + "0" * 8,
                  "9210" + "1" + "0" * 9, "93100" + "1" + "0" * 99], str(big[:4]))
    check("dumpl1: tags", MA.parse_tile_range("at023") == ((0, 0), (2, 2), (3, 3))
          and MA.parse_tile_range("at1ag-at2bz") == ((1, 2), (10, 11), (16, 35)),
          str(MA.parse_tile_range("at023")))
    rc, out = run("dumpl1 at023\nboot\nstart\ndumpl1 at023,at033\ndumpl1 at023-at047\n"
                  "dumpl1 at000-at099\ndumpl1 at023 at023\ndumpl1 at1zz\ndumpl1 at047-at023\n"
                  "dumpl1 junk\ndumpl1 023\ndumpl1 ae011\ndumpl1\nquit\n")
    check("dumpl1: two tiles in the first dir", "2 tile L1s written to " in out and "/dumps/10\n" in out, out[-1500:])
    check("dumpl1: range, next dir", "15 tile L1s written to " in out and "/dumps/11\n" in out, out[-1500:])
    check("dumpl1: range with non-tiles", "56 tile L1s written" in out and "44 not Tensix tiles" in out
          and "/dumps/12; 44" in out, out[-1500:])
    check("dumpl1: duplicates dumped once", "1 tile L1s written to " in out and "/dumps/13\n" in out, out[-1500:])
    check("dumpl1: chip not in the run", "no such chip: [1]" in out, out[-1500:])
    check("dumpl1: backwards range and junk refused", "runs backwards" in out and "bad tile tag" in out and rc == 0,
          out[-1500:])
    check("dumpl1: prefix required, other tile types refused",
          "bad tile tag '023'" in out and "tile type 'e' is not supported" in out, out[-1500:])
    check("dumpl1: usage", "usage: dumpl1" in out, out[-1500:])


def scenario_who():
    import json
    who = {"100,200": [0, 81, True, True, 118, 4, 142],      # ring painter, 24 epochs old
           "7,9": [2, 15, False, False, 0, 4, -1]}          # epoch unknown
    rc, out = run("boot\nstart\nwho 100 200\nwho 7 9\nwho 5 5\nwho 5000 1\nwho\nwho a b\nquit\n",
                  env_extra={"MFMX_MOCK_WHO": json.dumps(who)})
    check("who: ring painter, hub tag, epoch lag",
          "who (100,200): last painted by at0e7 bh0:81 from its S/E CACHE RING" in out
          and "epoch 118, 24 behind the newest seen (142)" in out
          and "hub-relative pixel (100,92)" in out, out[-900:])
    check("who: unknown epoch, self sites",
          "who (7,9): last painted by at223 bh2:15 from its own SELF sites" in out
          and "epoch unknown" in out, out[-900:])
    check("who: never painted, out of range, usage, junk",
          "nobody has painted that pixel" in out and "outside the 1920x1080" in out
          and "usage: who X Y" in out and "must be integers" in out, out[-900:])


def scenario_specks_who():
    """zoom specks' hub-truth: dump + decode + HUB: EMPTY / type N (fake dump)."""
    import struct, tempfile, types, json
    sys.path.insert(0, os.path.join(HERE, ".."))
    sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "..", "scripts", "l1dump"))
    os.environ["MFMX_MOCK_WHO"] = json.dumps({
        "10,10": [0, 15, False, True, 40, 4, 51, 30, 40],     # SELF, hub holds type 4
        "20,20": [0, 15, False, True, 40, 4, 51, 31, 41]})    # SELF, hub says empty
    import l1grid, machine
    tmp = tempfile.mkdtemp()
    addr = 0x1000
    l1 = bytearray(addr + l1grid.W * l1grid.H * l1grid.ATOM + 8)
    def put(x, y, ty):      # a parity-valid atom word of that type
        w = next(w for w in range(ty, 1 << 16, 1 << 9) if l1grid.valid_parity(w))
        struct.pack_into("<H", l1, addr + (x * l1grid.H + y) * l1grid.ATOM, w)
    put(30, 40, 4)
    mp = os.path.join(tmp, "hub.map")
    open(mp, "w").write("  0x%x  MFM::theT6Grid\n" % addr)
    os.environ["MFMX_HUB_MAP"] = mp
    sys.path.insert(0, os.path.join(HERE, "mfmx"))
    fake = types.SimpleNamespace(M=__import__("MFMx"))
    fake.tlbi_to_noc = machine.Machine.tlbi_to_noc
    fake.who_row = lambda x, y: machine.Machine.who_row(fake, x, y)
    fake.hist_text = lambda pt: machine.Machine.hist_text(fake, pt)
    fake.hub_map_path = lambda: machine.Machine.hub_map_path(fake)
    def dumpl1(tags):
        d = os.path.join(tmp, "dumps"); os.makedirs(d, exist_ok=True)
        for t in tags:
            open(os.path.join(d, t + ".l1"), "wb").write(bytes(l1))
        fake.last_dump_dir = d
        return "ok"
    fake.dumpl1 = dumpl1
    rows = machine.Machine.specks_who(fake, [(10, 10), (20, 20)])
    check("specks_who: hub value read from the dump",
          len(rows) == 2 and "HUB: type 4" in rows[0] and "HUB: EMPTY" in rows[1]
          and "site (30,40)" in rows[0], str(rows))
    # a RING painter: its own cache copy is empty, the OWNER (tlbi 16) holds 4
    os.environ["MFMX_MOCK_WHO"] = json.dumps({
        "10,10": [0, 15, False, True, 40, 4, 51, 30, 40],
        "30,30": [0, 15, True, True, 40, 4, 51, 5, 6]})
    os.environ["MFMX_MOCK_OWNER"] = json.dumps({"30,30": [0, 16, 30, 40]})
    os.environ["MFMX_MOCK_HIST"] = json.dumps({"30,30": [
        [True, True, 4, 150], [True, False, 0, 150], [False, False, 4, 148]]})
    rows = machine.Machine.specks_who(fake, [(30, 30)])
    check("specks_who: RING row also reads the OWNER hub's SELF",
          len(rows) == 1 and "HUB: EMPTY" in rows[0] and
          "OWNER at0" in rows[0] and "type 4" in rows[0].split("OWNER")[1],
          str(rows))
    check("specks_who: HIST newest first, dropped marked",
          "HIST: R4@e150 < S0@e150 < -S4@e148" in rows[0], str(rows))
    os.environ["MFMX_HUB_MAP"] = os.path.join(tmp, "nope.map")
    rows = machine.Machine.specks_who(fake, [(10, 10)])
    check("specks_who: missing hub.map is said, not raised",
          "hub truth unavailable" in rows[-1], str(rows))


def scenario_zhd():
    import json
    rows = [(0, 81, 3270179, 12, 0, 40, 1, True, 3270179, 123456, 125, 2, 0, True),   # at0e7, asked, waiting
            (0, 15, 19900001, 0, 3, 0, 0, False, 0, 0, 0, 0, 0, False),                 # at023, healthy
            (2, 111, 500, 5, 0, 0, 1, False, 400, 9000, 9, 1, 1, False)]                # restarted
    rc, out = run("boot\nstart\nzhd\nzhd all\nzhd restart off\nzhd restart on\nzhd restart maybe\nquit\n",
                  env_extra={"MFMX_MOCK_ZHD": json.dumps(rows)})
    check("zhd: summary counts", "3 report streams, 1 desynced, 1 recovered" in out
          and "3 intact reports carried a bad atom" in out, out[-900:])
    check("zhd: restarts done and pending", "1 restarts done, 1 requested and waiting" in out
          and "restart requested, waiting for the hub" in out, out[-900:])
    check("zhd: restart switch", "requests OFF" in out and "zhd restart: requests ON" in out
          and "unknown zhd restart 'maybe'" in out, out[-900:])
    check("zhd: names the desynced hub with the break point",
          "at0e7 bh0:81 DESYNCED" in out and "broke at report 3270179" in out
          and "garbage" in out, out[-900:])
    check("zhd: 'all' lists healthy hubs too", "at023 bh0:15 ok" in out, out[-900:])
    rc, out = run("boot\nstart\nzhd break at0a7,at323 at023 ae011 at0zz junk\nzhd break\nquit\n")
    check("zhd break: tags mapped, chip/tile errors named",
          "at0a7: one compressed byte will be dropped (bh0:" in out
          and "at323: no report stream there" in out and "at023: one compressed byte" in out
          and "ae011:" in out and "at0zz: not a Tensix tile" in out and "junk:" in out
          and "usage: zhd break" in out, out[-900:])
    sys.path.insert(0, EWD)
    import machine as MA
    check("zhd break: noc -> tlbi is the inverse of tlbi -> noc for all 140 tiles",
          all(MA.Machine.noc_to_tlbi(*MA.Machine.tlbi_to_noc(t)) == t for t in range(140))
          and MA.Machine.noc_to_tlbi(10, 7) == 77 and MA.Machine.noc_to_tlbi(14, 7) == 81
          and MA.Machine.noc_to_tlbi(8, 7) is None and MA.Machine.noc_to_tlbi(2, 12) is None)
    rc, out = run("boot\nstart\nzhd\nquit\n")
    check("zhd: no rows is not an error", rc == 0 and "0 report streams" not in out and "no report streams" in out, out[-500:])


def scenario_hubgeom():
    repo = os.path.abspath(os.path.join(EWD, "..", "..", "..", ".."))
    p = subprocess.run(["scripts/hubgeom/hubgeom_check.sh"], cwd=repo,
                       capture_output=True, text=True, timeout=90)
    check("hubgeom: Python and C++ geometry agree", p.returncode == 0 and "PASS hubgeom" in p.stdout,
          p.stdout[-400:] + p.stderr[-400:])


def scenario_command_file():
    d = tempfile.mkdtemp(prefix="selftest-cmd-")
    main_f, sub_f, self_f = (os.path.join(d, n) for n in ("main.cmd", "sub.cmd", "self.cmd"))
    with open(sub_f, "w") as f:
        f.write("echo SUBFILE-ONE\necho SUBFILE-TWO\n")
    with open(self_f, "w") as f:
        f.write(f"source {self_f}\n")
    with open(main_f, "w") as f:
        f.write("# comment\n\n" + FAST + "start\necho BEFORE\nshoot HOLD_AT_BIRTH 0\nwaitbolt\n"
                f"shoot NEW_LEADER 2 1\nwaitbolt\nsource {sub_f}\necho AFTER\n"
                f"source {self_f}\necho STILL-ALIVE\n")
    rc, out = run("echo FROM-STDIN\nquit\n", extra_args=("--boot", "-s", main_f))
    check("script: exit 0", rc == 0, f"rc={rc}")
    check("script: bolts settle in order",
          "HOLD_AT_BIRTH(0) CONSENSUS" in out and "NEW_LEADER(2,1,0,0,0) CONSENSUS" in out
          and "superseded" not in out.lower(), out[-900:])
    i = [out.find(t) for t in ("BEFORE", "SUBFILE-ONE", "SUBFILE-TWO", "AFTER", "STILL-ALIVE", "FROM-STDIN")]
    check("script: nested source runs in place, then stdin continues",
          all(x >= 0 for x in i) and i == sorted(i), str(i))
    check("script: self-sourcing file is cut off, script continues",
          "nested more than" in out and "STILL-ALIVE" in out)
    rc, out = run("echo NEVER\n", extra_args=("--boot", "-s", main_f, "--script-exit"))
    check("script-exit: quits when files are used up", rc == 0 and "NEVER" not in out
          and "STILL-ALIVE" in out and "shutting down" in out, out[-500:])
    rc, out = run("quit\n", extra_args=("-s", os.path.join(d, "no-such.cmd")))
    check("script: missing file is a clear startup error", rc != 0 and "command file not found" in out, out[:300])


def scenario_wait_quiet():
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    from sequencer import wait_quiet
    t = [0.0]
    def clock(): return t[0]
    def sleep(x): t[0] += x
    # counter grows for 2s (ticks every poll), then stops
    def reader():
        return int(min(t[0], 2.0) * 100)
    ok, waited, grew = wait_quiet(reader, sleep, clock, quiet_s=1.0, cap=30.0)
    check("quiet: waits out a draining counter", ok and 2.9 <= waited <= 3.6 and grew == 200,
          f"{ok} {waited} {grew}")
    t[0] = 0.0
    ok, waited, grew = wait_quiet(lambda: int(t[0] * 100), sleep, clock, quiet_s=1.0, cap=5.0)
    check("quiet: gives up at the cap if it never settles", (not ok) and 5.0 <= waited <= 5.5,
          f"{ok} {waited}")
    t[0] = 0.0
    ok, waited, grew = wait_quiet(lambda: None, sleep, clock)
    check("quiet: no counter means quiet at once", ok and waited == 0.0 and grew is None)
    rc, out = run_script(CYC.replace("dwell 0.2", "dwell 0.1") +
                  "cycle holdat NEW_LEADER 2\ncycle start\ncycle waitpause quiet 0.5\n")
    check("quiet: waitpause quiet end to end", "cycle paused:" in out
          and "reports quiet for 0.5s" in out, out[-400:])
    rc, out = run_script(CYC.replace("dwell 0.2", "dwell 0.1") +
                  "cycle holdat NEW_LEADER 2\ncycle start\ncycle waitpause 30\n")
    # a typed quit ends a waitpause that would otherwise wait for scy 99999
    rc3, out3 = run("boot\nstart\n" + CYC.replace("dwell 0.2", "dwell 0.1") +
                    "cycle holdat NEW_LEADER 99999\ncycle start\ncycle waitpause 60\nquit\n",
                    timeout=30)
    check("waitpause: a typed quit interrupts it", rc3 == 0 and "interrupted" in out3
          or "shutting down" in out3, f"rc={rc3} " + out3[-300:])
    check("quiet: plain waitpause unchanged", "cycle paused:" in out and "reports quiet" not in out,
          out[-300:])


if __name__ == "__main__":
    only = sys.argv[1:]
    for name, fn in list(globals().items()):
        if name.startswith("scenario_") and (not only or name[9:] in only):
            print(f"--- {name[9:]}")
            fn()
    print()
    print("ALL PASS" if not fails else f"{len(fails)} FAILED: {fails}")
    sys.exit(1 if fails else 0)
