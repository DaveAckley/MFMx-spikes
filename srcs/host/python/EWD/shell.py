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
import collections
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

    def __init__(self, prompt="mfmx> ", fd=0, out=None, clock=time.monotonic,
                 wait_prompt="mfmx-waiting> "):
        self.prompt = prompt
        self.wait_prompt = wait_prompt     # shown while 'wait' polls input
        self.fd = fd
        self.out = out if out is not None else sys.stdout
        # everything said or echoed, kept for the summary written at shutdown
        self.transcript = collections.deque(maxlen=200_000)
        self.clock = clock
        self.cmds = {}        # name -> (fn, help)
        self.ticks = {}       # name -> Tick
        self.buf = b""
        self.eof = False
        self._stop = False
        self._prompt_up = False
        self._need_prompt = True
        self._echo = not os.isatty(fd)   # make piped transcripts readable
        self.script = collections.deque()  # pending command-file lines (text)
        self.script_exit = False           # quit once command files are used up
        self.abort_on_error = False        # a failing script line ends the run
        self.aborted = False               # set when abort_on_error fired
        self._waiting = False              # inside 'wait' (no nested waits)
        # 'wait' executes lines typed meanwhile, but ONLY on a terminal.  On
        # a pipe or file every line is already buffered, so polling would run
        # the rest of the input at the first 'wait' instead of after it.
        self.poll_wait = os.isatty(fd)
        self._script_ran = False
        self.max_source_depth = 8
        self._source_depth = 0
        self.command("help", self._cmd_help, "help [CMD]: list commands, or show one")
        self.command("tick", self._cmd_tick,
                     "tick [NAME on|off|period SECS|now]: list or control periodic ticks")
        self.command("wait", self._cmd_wait,
                     "wait SECS: let ticks run for SECS; on a terminal, lines "
                     "typed meanwhile run (so 'quit' ends the wait)")
        self.command("source", self._cmd_source,
                     "source FILE: run the commands in FILE as if typed (comments with #)")
        self.command("echo", lambda a: " ".join(a),
                     "echo TEXT...: print TEXT (handy for annotating command files)")

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
        self.transcript.extend(msg.rstrip("\n").split("\n"))

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

    def run_for(self, secs, poll_input=False):
        """Run ticks for secs.  With poll_input, lines typed meanwhile are
        executed as they arrive (so a typed 'quit' or 'stop' works during a
        long script 'wait'); a stop request ends the wait early.  At EOF
        nothing more is polled (select would return at once, forever)."""
        end = self.clock() + secs
        while True:
            self._run_due()
            if self._stop:
                return
            if poll_input:
                if not self._prompt_up:
                    # say() or a typed command took the prompt away
                    self.out.write(self.wait_prompt)
                    self.out.flush()
                    self._prompt_up = True
                self._run_typed()
                if self._stop:
                    return
            left = end - self.clock()
            if left <= 0:
                return
            nap = min(self._until_next_tick(), left)
            if poll_input and not self.eof:
                self._fill(nap)
            else:
                time.sleep(nap)

    def _run_typed(self):
        """Execute the complete lines already read from stdin."""
        while not self._stop:
            line = self._next_line()
            if line is None:
                return
            if self._echo:
                self._prompt_up = False
                self.out.write(line + "\n")
                self.out.flush()
                self.transcript.append(line)
            self._prompt_up = False     # the typed Enter moved to a new line
            self.dispatch(line)
            self._need_prompt = True

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

    # ---- command files ------------------------------------------------------
    def load_script(self, path, front=False, depth=1):
        """Queue the lines of a command file.  front=True runs them next
        (before any lines already queued) -- that is what 'source' wants, so
        nested files run in place.  depth bounds 'source' recursion.
        Raises OSError if the file is unreadable."""
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = [(depth, ln.rstrip("\r\n")) for ln in f]
        if front:
            self.script.extendleft(reversed(lines))
        else:
            self.script.extend(lines)
        self._script_ran = True
        return len(lines)

    def _next_script_line(self):
        if not self.script:
            self._source_depth = 0
            return None
        self._source_depth, text = self.script.popleft()
        return text

    def _cmd_source(self, args):
        if len(args) != 1:
            return "usage: source FILE"
        if self._source_depth + 1 > self.max_source_depth:
            return f"source: nested more than {self.max_source_depth} deep (a file sourcing itself?)"
        n = self.load_script(args[0], front=True, depth=self._source_depth + 1)
        return f"source {args[0]}: {n} lines"

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
        """Run one command line.  Returns False if it failed (parse error,
        unknown or ambiguous command, or an exception); True otherwise."""
        line = line.strip()
        if not line or line.startswith("#"):
            return True
        try:
            argv = shlex.split(line)
        except ValueError as e:
            self.say(f"parse error: {e}")
            return False
        name, args = argv[0].lower(), argv[1:]
        try:
            name = self.resolve(name, self.cmds, "command")
        except AbbrevError as e:
            self.say(str(e))
            return False
        ok = True
        try:
            out = self.cmds[name][0](args)
        except KeyboardInterrupt:
            out = "interrupted"
        except AbbrevError as e:
            out = str(e)
            ok = False
        except Exception as e:             # keep the shell alive
            out = f"error: {type(e).__name__}: {e}"
            ok = False
        if out:
            self.say(str(out))
        return ok

    def run(self):
        while not self._stop:
            try:
                if self._need_prompt:
                    self.out.write(self.prompt)
                    self.out.flush()
                    self._prompt_up, self._need_prompt = True, False
                # command-file lines take precedence over stdin; they are
                # echoed (stdin is not what typed them) and ticks keep
                # running between them, as they would between typed lines.
                line = self._next_script_line()
                from_script = line is not None
                if not from_script:
                    if self.script_exit and self._script_ran:
                        break               # command files used up: quit
                    line = self._next_line()
                if line is None:
                    if self.eof:
                        break
                    self._fill(self._until_next_tick())
                    self._run_due()
                    continue
                self._prompt_up = False
                if self._echo or from_script:
                    self.out.write(line + "\n")
                    self.out.flush()
                self.transcript.append(self.prompt + line)
                ok = self.dispatch(line)
                self._need_prompt = True
                if from_script and not ok and self.abort_on_error \
                   and not self._stop:
                    self.say(f"script aborted (--abort-on-error) at: {line.strip()}")
                    self.script.clear()
                    self.aborted = True
                    self._stop = True
                if from_script:
                    self._run_due()
            except KeyboardInterrupt:
                self.say("^C (type 'quit' to exit)")

    # ---- builtin commands -----------------------------------------------------
    def _cmd_help(self, args):
        if args:
            # abbreviated like any command (AbbrevError is reported by dispatch)
            name = self.resolve(args[0].lower(), self.cmds, "command")
            return self.cmds[name][1]
        return "commands:\n" + "\n".join(
            f"  {name:10s} {h.split(':', 1)[-1].strip() if ':' in h else h}"
            for name, (_, h) in sorted(self.cmds.items()) if not h.startswith("alias"))

    def _cmd_wait(self, args):
        if self._waiting:
            return "wait: already waiting (a typed wait inside a wait is refused)"
        self._waiting = True
        try:
            self.run_for(float(args[0]) if args else 1.0,
                         poll_input=self.poll_wait)
        finally:
            self._waiting = False
            if self.poll_wait and self._prompt_up:
                self.out.write("\n")     # leave the wait prompt behind
                self.out.flush()
                self._prompt_up = False

    def _cmd_tick(self, args):
        if not args:
            return "\n".join(
                f"  {t.name:10s} {'on ' if t.enabled else 'off'} every {t.period:g}s  "
                f"ran {t.count}x  {t.help}" for t in self.ticks.values()) or "no ticks"
        try:
            t = self.ticks[self.resolve(args[0], tuple(self.ticks), "tick")]
        except AbbrevError as e:
            if str(e).startswith("ambiguous"):
                return str(e)
            return f"no such tick {args[0]!r} (have: {', '.join(self.ticks)})"
        if len(args) < 2:
            return f"{t.name}: {'on' if t.enabled else 'off'} every {t.period:g}s"
        try:
            what = self.resolve(args[1].lower(), ("on", "off", "period", "now"), "tick option")
        except AbbrevError as e:
            return str(e) if str(e).startswith("ambiguous") else \
                "usage: tick NAME on|off|period SECS|now"
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
