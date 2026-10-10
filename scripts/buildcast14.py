#!/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python3
"""buildcast.py - run a command and stream its stdout/stderr text as video to RTMP.

Built by generic AHAXAI (Wed Oct  7 12:33:21 2026)

  ./buildcast.py --cmd 'make -j8 && ./app' --rtmp-file ~/.rtmp-build
  ./buildcast.py --cmd 'make -j8' --rtmp-file key.txt --tee

Deps: Pillow (pip install pillow), ffmpeg on PATH.

Security Considerations:

The RTMP URL can be supplied via a file (--rtmp-file) so it will never
appear in buildcast's argv, logs, shell history, or an Emacs
*compilation* buffer.

Also, the generated ffmpeg command is never printed, but note that any
user on the local system will be able to extract the full ffmpeg
command, e.g. from /proc, if desired.

Hacks to eliminate that possibility were proposed but deemed overkill
for this application.

"""
from __future__ import annotations

import argparse
import codecs
import math
import os
import re
import select
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections import deque

from PIL import Image, ImageDraw, ImageFont

FONT_CANDIDATES = (
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
)

# Strip escape sequences rather than interpret them.
ANSI_RE = re.compile(r"\x1b(?:\[[0-9;?]*[A-Za-z]|\][^\x07\x1b]*(?:\x07|\x1b\\))")

BG, FG, DIM, BAR = (18, 18, 20), (216, 216, 216), (120, 120, 130), (32, 32, 38)
OK, BAD = (35, 209, 139), (241, 76, 76)

# Failure palette: the two endpoints of the pulse.
FAIL_BG, FAIL_BG2 = (70, 4, 4), (150, 12, 12)
FAIL_BAR, FAIL_BAR2 = (110, 0, 0), (210, 28, 28)
FAIL_DIM = (255, 190, 190)


def _mix(c1, c2, k: float):
    return tuple(int(a + (b - a) * k) for a, b in zip(c1, c2))


def load_font(size: int):
    for c in FONT_CANDIDATES:
        if os.path.exists(c):
            return ImageFont.truetype(c, size)
    print("warning: no monospace TTF found, using bitmap font", file=sys.stderr)
    return ImageFont.load_default()


def wrap(text: str, cols: int):
    """Fixed-width chunking (terminal-faithful for a monospace grid)."""
    text = text.expandtabs(8)
    return [text[i:i + cols] for i in range(0, len(text), cols)] or [""]


class Tail:
    """Rolling tail of lines with an exponentially-decaying scroll offset."""

    def __init__(self, cols: int, speed: float, max_scroll: int,
                 max_lines: int = 6000):
        self.cols, self.speed, self.max_scroll = cols, speed, max_scroll
        self.lines: deque = deque(maxlen=max_lines)
        self.pending = ""
        self.s = 0.0
        self.lock = threading.Lock()

    def feed(self, text: str) -> None:
        text = ANSI_RE.sub("", text).replace("\r", "")
        if not text:
            return
        with self.lock:
            self.pending += text
            *done, self.pending = self.pending.split("\n")
            added = 0
            for line in done:
                vis = wrap(line, self.cols)
                self.lines.extend(vis)
                added += len(vis)
            self.s = min(self.s + added, float(self.max_scroll))

    def snapshot(self, rows: int, dt: float):
        with self.lock:
            self.s += -self.s * min(1.0, self.speed * dt)
            if self.s < 0.005:
                self.s = 0.0
            lines, pending, s = list(self.lines), self.pending, self.s
        if pending:
            lines += wrap(pending, self.cols)
        return lines, s


class Renderer:
    def __init__(self, w, h, font, char_w, line_h, pad, status_h):
        self.w, self.h, self.font = w, h, font
        self.char_w, self.line_h, self.pad, self.status_h = \
            char_w, line_h, pad, status_h
        self.rows = max(1, (h - status_h) // line_h)
        self.img = Image.new("RGB", (w, h), BG)
        self.draw = ImageDraw.Draw(self.img)

    def render(self, lines, s, left, right, color,
               failed: bool = False, t: float = 0.0) -> bytes:
        d = self.draw

        if failed:
            k = 0.5 + 0.5 * math.sin(t * 4.0)        # ~1.6 s pulse
            bg, bar = _mix(FAIL_BG, FAIL_BG2, k), _mix(FAIL_BAR, FAIL_BAR2, k)
            left_col, right_col = FAIL_DIM, (255, 255, 255)
        else:
            bg, bar, left_col, right_col = BG, BAR, DIM, color

        d.rectangle([0, 0, self.w, self.h], fill=bg)

        n = len(lines)
        top = max(0.0, (n - self.rows) - s)
        start, frac = int(top), top - int(top)

        # Body first, so it slides under the bar drawn last (opaque bar).
        y0 = self.status_h
        for i in range(self.rows + 1):
            idx = start + i
            if idx >= n:
                break
            y = y0 + i * self.line_h - frac * self.line_h
            if y > self.h:
                break
            d.text((self.pad, y), lines[idx], font=self.font, fill=FG)

        if self.status_h:
            d.rectangle([0, 0, self.w, self.status_h], fill=bar)
            d.text((self.pad, 0), left, font=self.font, fill=left_col)
            if right:
                tw = self.font.getlength(right)
                d.text((self.w - self.pad - tw, 0), right, font=self.font,
                       fill=right_col)
        return self.img.tobytes()


def ffmpeg_cmd(rtmp: str, w: int, h: int, fps: int) -> list[str]:
    return [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pixel_format", "rgb24",
        "-video_size", f"{w}x{h}", "-framerate", str(fps), "-i", "-",
        "-vf", "format=yuv420p",
        "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency",
        "-b:v", "1500k", "-maxrate", "1500k", "-bufsize", "3000k",
        "-g", str(fps * 2), "-keyint_min", str(fps * 2), "-sc_threshold", "0",
        "-f", "flv", rtmp,
    ]


class FrameSink:
    def __init__(self, cmd: list[str]):
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, bufsize=0)

    def write(self, frame: bytes) -> bool:
        try:
            self.proc.stdin.write(frame)
            return True
        except (BrokenPipeError, ValueError, OSError):
            return False

    def close(self) -> None:
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def reader_thread(argv, tail, stop, info, tee):
    """Read the child's merged stdout/stderr as raw bytes."""
    try:
        proc = subprocess.Popen(
            argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0,
            start_new_session=True)               # own pgrp, for killpg
    except OSError as e:
        info["error"], info["done"] = str(e), True
        return

    info["pid"] = proc.pid
    fd = proc.stdout.fileno()
    dec = codecs.getincrementaldecoder("utf-8")("replace")

    try:
        while not stop.is_set():
            try:
                r, _, _ = select.select([fd], [], [], 0.25)
            except (OSError, ValueError):
                break
            if not r:
                if proc.poll() is not None:
                    break
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                break
            if not data:
                break
            if tee is not None:
                try:
                    tee.write(data)
                    tee.flush()
                except (BrokenPipeError, ValueError, OSError):
                    tee = None                    # reader gone; keep streaming
            tail.feed(dec.decode(data))
    finally:
        try:
            tail.feed(dec.decode(b"", True))
        except Exception:
            pass
        if tee is not None:
            try:
                tee.flush()
            except Exception:
                pass
        try:
            info["rc"] = proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError:
                pass
            info["rc"] = proc.wait()
        info["done"] = True
        try:
            proc.stdout.close()
        except Exception:
            pass


def read_rtmp_file(path: str) -> str:
    try:
        with open(path) as f:
            url = f.read().strip()
    except OSError as e:
        sys.exit(f"error: cannot read --rtmp-file {path}: {e}")
    if not url:
        sys.exit(f"error: --rtmp-file {path} is empty")
    return url


def fmt_hms(sec: float) -> str:
    sec = int(sec)
    return f"{sec // 3600:02d}:{(sec % 3600) // 60:02d}:{sec % 60:02d}"


def parse_args():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cmd", required=True,
                   help="command to run (shell string unless --no-shell)")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--rtmp-file", metavar="FILE",
                     help="file containing the RTMP URL (preferred; keeps the "
                          "secret out of argv/logs)")
    src.add_argument("--rtmp", metavar="URL",
                     help="RTMP URL directly (visible in ps; use only for "
                          "non-secret targets like a local .flv)")
    p.add_argument("--no-shell", action="store_true",
                   help="exec --cmd directly instead of via bash -lc")
    p.add_argument("--tee", action="store_true",
                   help="mirror the child's raw output to stdout "
                        "(e.g. into an Emacs compilation buffer)")
    p.add_argument("--width", type=int, default=1280)
    p.add_argument("--height", type=int, default=720)
    p.add_argument("--fps", type=int, default=15)
    p.add_argument("--font-size", type=int, default=16)
    p.add_argument("--title", default=None, help="status-bar left text")
    p.add_argument("--no-status", action="store_true")
    p.add_argument("--linger", type=float, default=8.0,
                   help="seconds to keep streaming after a SUCCESSFUL command")
    p.add_argument("--hold", action="store_true",
                   help="keep streaming after success until Ctrl-C")
    p.add_argument("--no-fail-hold", action="store_true",
                   help="do NOT hold the stream open on failure (exit after "
                        "--linger like a success)")
    return p.parse_args()


def main() -> int:
    a = parse_args()
    if shutil.which("ffmpeg") is None:
        sys.exit("error: ffmpeg not found on PATH")

    rtmp = a.rtmp if a.rtmp else read_rtmp_file(a.rtmp_file)

    w, h = a.width - (a.width % 2), a.height - (a.height % 2)
    font = load_font(a.font_size)
    ascent, descent = font.getmetrics()
    line_h = ascent + descent + 2
    char_w = font.getlength("M" * 64) / 64.0
    pad = 12
    status_h = line_h if not a.no_status else 0
    rows = max(1, (h - status_h) // line_h)
    cols = max(20, int((w - 2 * pad) / char_w))

    tail = Tail(cols, speed=9.0, max_scroll=rows)
    renderer = Renderer(w, h, font, char_w, line_h, pad, status_h)

    title = a.title if a.title is not None else a.cmd
    max_title = int((w - 2 * pad) / char_w) - 24
    if len(title) > max_title > 8:
        title = "…" + title[-(max_title - 1):]

    argv = ["bash", "-lc", a.cmd] if not a.no_shell else shlex.split(a.cmd)
    tee = getattr(sys.stdout, "buffer", None) if a.tee else None

    info: dict = {"done": False, "rc": None, "pid": None}
    stop = threading.Event()
    threading.Thread(target=reader_thread,
                     args=(argv, tail, stop, info, tee), daemon=True).start()

    out = FrameSink(ffmpeg_cmd(rtmp, w, h, a.fps))
    where = a.rtmp_file if a.rtmp_file else "--rtmp"
    print(f"buildcast: streaming to rtmp (source: {where})", file=sys.stderr)

    # Ctrl-C and Emacs' kill-compilation (SIGTERM) tear down the process group.
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))

    period = 1.0 / a.fps
    t0 = last = next_t = time.monotonic()
    done_at, rc, failed = None, 0, False

    try:
        while True:
            now = time.monotonic()
            dt, last = now - last, now
            elapsed = now - t0

            lines, s = tail.snapshot(rows, dt)

            if info["done"] and done_at is None:
                done_at = now
                if info.get("error"):
                    print(f"error: {info['error']}", file=sys.stderr)

            if info["done"]:
                rc = info["rc"] if info["rc"] is not None else 1
                failed = rc != 0
                if failed:
                    right = f"BUILD FAILED rc={rc}   {fmt_hms(elapsed)}"
                else:
                    right = f"EXITED {rc}   {fmt_hms(elapsed)}"
                color = OK if rc == 0 else BAD
            else:
                right, color = f"RUNNING   {fmt_hms(elapsed)}", OK

            frame = renderer.render(lines, s, title, right, color,
                                    failed=failed, t=elapsed)
            if not out.write(frame):
                print("error: ffmpeg closed the pipe", file=sys.stderr)
                rc = 1
                break

            # On failure we keep streaming indefinitely (until Ctrl-C) so the
            # red screen stays up; on success honour --linger / --hold.
            hold_on_fail = failed and not a.no_fail_hold
            if done_at is not None and not hold_on_fail and not a.hold \
                    and (now - done_at) >= a.linger:
                break

            next_t += period
            slack = next_t - time.monotonic()
            if slack > 0:
                time.sleep(slack)
            else:
                next_t = time.monotonic()       # fell behind; resync

    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        if rc == 0:
            rc = 130                            # keep a real failure code
    finally:
        stop.set()
        signal.signal(signal.SIGTERM, signal.SIG_IGN)  # don't abort on re-SIGTERM
        pid = info.get("pid")
        if pid and not info["done"]:
            try:
                os.killpg(pid, signal.SIGTERM)
            except OSError:
                pass
            else:
                time.sleep(1.0)                 # TERM -> KILL gap
                if not info["done"]:
                    try:
                        os.killpg(pid, signal.SIGKILL)
                    except OSError:
                        pass
        out.close()

    return rc


if __name__ == "__main__":
    raise SystemExit(main())
