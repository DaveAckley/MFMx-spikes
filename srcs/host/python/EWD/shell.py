"""shell.py -- single-threaded command shell with periodic ticks.

No MFMx dependency, so it can be tested on its own.

Design (see the design discussion):
  * ONE Python thread.  stdin is read with select() + os.read() on the raw fd
    (not sys.stdin: its Python-level buffer defeats select on piped input),
    so periodic ticks run between lines, and while idle at the prompt.
  * Line editing is left to the front end (emacs comint, a terminal, ...).
  * A command that blocks (e.g. a long C++ call) blocks the ticks too.  That is
    the price of having no worker thread; keep blocking calls short, or make
    them asynchronous (fire + poll), which is what machine.py does for bolts.
  * Works under pdb: nothing here spawns threads.
"""
import os
import select
import shlex
import sys
import time


class Tick:
    def __init__(self, name, period, fn, enabled, help):
        self.name, self.period, self.fn = name, float(period), fn
        self.enabled, self.help = enabled, help
        self.due = 0.0
        self.count = 0
        self.errors = 0       # consecutive


class AbbrevError(ValueError):
    pass


class Shell:
    MAX_TICK_ERRORS = 3

    def __init__(self, prompt="mfmx> ", fd=0, out=None, clock=time.monotonic):
        self.prompt = prompt
        self.fd = fd
        self.out = out if out is not None else sys.stdout
        self.clock = clock
        self.cmds = {}        # name -> (fn, help)
        self.ticks = {}       # name -> Tick
        self.buf = b""
        self.eof = False
        self._stop = False
        self._prompt_up = False
        self._need_prompt = True
        self._echo = not os.isatty(fd)   # make piped transcripts readable
        self.command("help", self._cmd_help, "help [CMD]: list commands, or show one")
        self.command("tick", self._cmd_tick,
                     "tick [NAME on|off|period SECS|now]: list or control periodic ticks")
        self.command("wait", self._cmd_wait,
                     "wait SECS: let ticks run for SECS (input is not read meanwhile)")

    # ---- registration -------------------------------------------------------
    def command(self, name, fn, help, aliases=()):
        """fn(args:list[str]) -> str | None"""
        self.cmds[name] = (fn, help)
        for a in aliases:
            self.cmds[a] = (fn, f"alias for {name}")

    def add_tick(self, name, period, fn, enabled=True, help=""):
        t = Tick(name, period, fn, enabled, help)
        t.due = self.clock() + t.period
        self.ticks[name] = t
        return t

    # ---- output -------------------------------------------------------------
    def say(self, msg):
        """Print an asynchronous message (from a tick or command)."""
        if self._prompt_up:
            self.out.write("\n")
            self._prompt_up = False
            self._need_prompt = True
        self.out.write(msg if msg.endswith("\n") else msg + "\n")
        self.out.flush()

    def stop(self):
        self._stop = True

    # ---- ticks --------------------------------------------------------------
    def _run_due(self):
        now = self.clock()
        for t in list(self.ticks.values()):
            if not t.enabled or now < t.due:
                continue
            t.due = now + t.period        # no catch-up after a long command
            try:
                t.fn()
                t.count += 1
                t.errors = 0
            except Exception as e:        # a bad tick must not kill the shell
                t.errors += 1
                self.say(f"[tick {t.name}] {type(e).__name__}: {e}")
                if t.errors >= self.MAX_TICK_ERRORS:
                    t.enabled = False
                    self.say(f"[tick {t.name}] disabled after {t.errors} consecutive errors")

    def _until_next_tick(self, cap=0.5):
        now = self.clock()
        dues = [t.due for t in self.ticks.values() if t.enabled]
        if not dues:
            return cap
        return max(0.0, min(min(dues) - now, cap))

    def run_for(self, secs):
        """Run ticks (only) for secs."""
        end = self.clock() + secs
        while True:
            self._run_due()
            left = end - self.clock()
            if left <= 0:
                return
            time.sleep(min(self._until_next_tick(), left))

    # ---- input --------------------------------------------------------------
    def _next_line(self):
        i = self.buf.find(b"\n")
        if i >= 0:
            line, self.buf = self.buf[:i], self.buf[i + 1:]
            return line.decode("utf-8", "replace").rstrip("\r")
        if self.eof and self.buf:
            line, self.buf = self.buf, b""
            return line.decode("utf-8", "replace")
        return None

    def _fill(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if r:
            data = os.read(self.fd, 65536)
            if data:
                self.buf += data
            else:
                self.eof = True

    # ---- main loop ----------------------------------------------------------
    @staticmethod
    def resolve(word, choices, what="command"):
        """Exact match, else the unique choice that `word` is a prefix of.
        Aliases of the same command don't make a prefix ambiguous.  Raises AbbrevError(msg)."""
        if word in choices:
            return word
        hits = [c for c in choices if c.startswith(word)]
        if isinstance(choices, dict):
            fns = {id(choices[c][0]) if isinstance(choices[c], tuple) else c for c in hits}
            uniq = len(fns) == 1
        else:
            uniq = len(hits) == 1
        if hits and uniq:
            return hits[0]
        if hits:
            raise AbbrevError(f"ambiguous {what} {word!r}: {', '.join(sorted(hits))}")
        raise AbbrevError(f"unknown {what} {word!r}; try 'help'")

    def dispatch(self, line):
        line = line.strip()
        if not line or line.startswith("#"):
            return
        try:
            argv = shlex.split(line)
        except ValueError as e:
            self.say(f"parse error: {e}")
            return
        name, args = argv[0].lower(), argv[1:]
        try:
            name = self.resolve(name, self.cmds, "command")
        except AbbrevError as e:
            self.say(str(e))
            return
        try:
            out = self.cmds[name][0](args)
        except KeyboardInterrupt:
            out = "interrupted"
        except AbbrevError as e:
            out = str(e)
        except Exception as e:             # keep the shell alive
            out = f"error: {type(e).__name__}: {e}"
        if out:
            self.say(str(out))

    def run(self):
        while not self._stop:
            try:
                if self._need_prompt:
                    self.out.write(self.prompt)
                    self.out.flush()
                    self._prompt_up, self._need_prompt = True, False
                line = self._next_line()
                if line is None:
                    if self.eof:
                        break
                    self._fill(self._until_next_tick())
                    self._run_due()
                    continue
                self._prompt_up = False
                if self._echo:
                    self.out.write(line + "\n")
                self.dispatch(line)
                self._need_prompt = True
            except KeyboardInterrupt:
                self.say("^C (type 'quit' to exit)")

    # ---- builtin commands -----------------------------------------------------
    def _cmd_help(self, args):
        if args:
            ent = self.cmds.get(args[0].lower())
            return ent[1] if ent else f"no such command {args[0]!r}"
        return "commands:\n" + "\n".join(
            f"  {name:10s} {h.split(':', 1)[-1].strip() if ':' in h else h}"
            for name, (_, h) in sorted(self.cmds.items()) if not h.startswith("alias"))

    def _cmd_wait(self, args):
        self.run_for(float(args[0]) if args else 1.0)

    def _cmd_tick(self, args):
        if not args:
            return "\n".join(
                f"  {t.name:10s} {'on ' if t.enabled else 'off'} every {t.period:g}s  "
                f"ran {t.count}x  {t.help}" for t in self.ticks.values()) or "no ticks"
        t = self.ticks.get(args[0])
        if t is None:
            return f"no such tick {args[0]!r} (have: {', '.join(self.ticks)})"
        if len(args) < 2:
            return f"{t.name}: {'on' if t.enabled else 'off'} every {t.period:g}s"
        what = args[1]
        if what in ("on", "off"):
            t.enabled = what == "on"
            t.errors = 0
            t.due = self.clock() + t.period
        elif what == "period" and len(args) > 2:
            t.period = float(args[2])
            t.due = self.clock() + t.period
        elif what == "now":
            t.fn()
            t.count += 1
        else:
            return "usage: tick NAME on|off|period SECS|now"
        return f"{t.name}: {'on' if t.enabled else 'off'} every {t.period:g}s"
