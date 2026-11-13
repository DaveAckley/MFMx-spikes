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


def run(script, api="async", extra_args=(), timeout=90):
    env = dict(os.environ, MFMX_MOCK_API=api, MFMX_MOCK_SIMDIR=tempfile.mkdtemp(prefix="selftest-"))
    p = subprocess.run([sys.executable, CTL, "--mock", *extra_args], input=script,
                       capture_output=True, text=True, timeout=timeout, env=env)
    return p.returncode, p.stdout + p.stderr


FAST = ("mock delay HOLD_AT_BIRTH 0.05 0.2\nmock delay NEW_LEADER 0.05 0.3\n"
        "mock delay RUN_EVENTS 0.01 0.05\n")


def scenario_normal_cycle():
    rc, out = run("boot\nstart\n" + FAST +
                  "shoot HOLD_AT_BIRTH 0\nwait 0.8\n"
                  "shoot NEW_LEADER 2 1\nwait 0.8\n"
                  "shoot RUN_EVENTS 3\nwait 0.5\nstatus\nbolt hist\nquit\n")
    check("normal: exit 0", rc == 0, f"rc={rc}")
    for cmd in ("HOLD_AT_BIRTH(0)", "NEW_LEADER(2,1)", "RUN_EVENTS(3)"):
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


def scenario_ticks_during_pending_bolt():
    rc, out = run("boot\nstart\nmock delay NEW_LEADER 1.0 1.2\ntick slowscan period 0.3\n"
                  "shoot NEW_LEADER 1 1\nwait 1.0\nstatus\nwait 0.6\nquit\n",
                  extra_args=("--slowscan-gap", "0"))
    runs = [int(l.split("run:")[1].split()[0]) for l in out.splitlines() if l.startswith("slowscans run:")]
    check("ticks: slowscan ticks keep running while a bolt is pending",
          bool(runs) and runs[0] >= 2, f"runs={runs}")
    check("ticks: bolt still completes afterwards", "NEW_LEADER(1,1) CONSENSUS" in out, out[-500:])


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
    check("legacy: exit 0, no QuietBox.shutdown needed", rc == 0 and "quietbox shutdown" not in out)


def scenario_guards_and_errors():
    rc, out = run("shoot HOLD_AT_BIRTH 0\nboot\nstop\nphase\nshoot NOPE 1\nbogus\nshoot RUN_EVENTS x\n"
                  "decay 3\nquit\n")
    check("guards: shoot before boot refused", "not booted: run 'boot' first" in out)
    check("guards: stop before start leaves chip alone",
          "left alone" in out and "bh0=HAS_ALLOCATED_TLBS" in out, out[-700:])
    check("guards: unknown PHASER command explained", "unknown PHASER command 'NOPE'" in out)
    check("guards: unknown shell command explained", "unknown command 'bogus'" in out)
    check("guards: bad int arg reported, shell survives", "error: ValueError" in out and rc == 0)


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
    check("cycle: first leader is 2, scy starts at 1", nl[:1] == [[2, 1]], str(nl))
    check("cycle: scy increments by one", [a[1] for a in nl] == list(range(1, len(nl) + 1)), str(nl))
    check("cycle: consecutive leaders differ",
          all(nl[i][0] != nl[i + 1][0] for i in range(len(nl) - 1)), str(nl))
    check("cycle: RUN_EVENTS carries its cycle's scy", all(re_[i] == [nl[i][1]] for i in range(len(re_))),
          f"nl={nl} re={re_}")
    check("cycle: order is NL,RE,NL,RE...", [c for c, a, s in h][1:5] == ["NEW_LEADER", "RUN_EVENTS"] * 2,
          str([c for c, a, s in h]))


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


def scenario_feed_command():
    rc, out = run("feed\nboot\nstart\nfeed on\nwait 1.0\nfeed status\nfeed off\nfeed st\nquit\n")
    check("feed: starts with no feed", "feed: none" in out or "frame tick off" in out, out[:400])
    check("feed: 'feed on' enables frames", "feed on:" in out and "frame tick on" in out, out[-500:])
    check("feed: 'feed off' stops the tick", "feed frames stopped" in out and rc == 0, out[-500:])


if __name__ == "__main__":
    only = sys.argv[1:]
    for name, fn in list(globals().items()):
        if name.startswith("scenario_") and (not only or name[9:] in only):
            print(f"--- {name[9:]}")
            fn()
    print()
    print("ALL PASS" if not fails else f"{len(fails)} FAILED: {fails}")
    sys.exit(1 if fails else 0)
