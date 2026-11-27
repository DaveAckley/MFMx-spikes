"""netctl.py -- mfmxctl's command API over HTTP (`--listen PORT`).

For Stream Deck buttons and other programs that want to drive a running
mfmxctl:

  curl -s -d 'nuke 959 540 200' http://127.0.0.1:PORT/cmd
  curl -s http://127.0.0.1:PORT/state

  POST /cmd      body: one or more command lines; each runs exactly as if
                 typed (abbreviations too) and the reply is their output,
                 text/plain.  403 if a command is not allowed (below).
  GET /cmd?c=... the same, one URL-encoded line (for clients that can only
                 do GET)
  GET /state     JSON: run time, cycle, leader, AER line, modes

No threads: the Shell's select loop (shell.py) watches the listening
socket and every open connection alongside stdin, so a request runs
between ticks like a typed line, including during a script's `wait`.
Each command is echoed on the console as `[net] LINE`.

Safety:
  * binds 127.0.0.1 unless --listen-host says otherwise; a non-loopback
    host needs --listen-token (or MFMX_LISTEN_TOKEN), sent by the client
    as `Authorization: Bearer TOKEN` or `X-Token: TOKEN`
  * only commands in the allow list run (ALLOW_DEFAULT, or
    --listen-allow NAME,NAME... / all): nothing that boots, stops,
    quits, waits or sources files by default
  * requests are small (MAX_REQUEST bytes) and a connection that has not
    sent a whole request in CONN_TIMEOUT seconds is dropped, so a slow or
    stuck client cannot stall the loop
"""
import json
import socket
import time
import urllib.parse

# commands a client may run unless --listen-allow says otherwise: reading
# state, and the interventions (seed, nuke, cycle and feed controls)
ALLOW_DEFAULT = ("help", "status", "zhd", "who", "where", "echo", "seed",
                 "nuke", "autonuke", "dwell", "cycle", "feed", "zoom",
                 "grid", "bolt")

MAX_REQUEST = 16 << 10
CONN_TIMEOUT = 5.0
SEND_TIMEOUT = 2.0

REASONS = {200: "OK", 400: "Bad Request", 401: "Unauthorized",
           403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed",
           413: "Payload Too Large"}


def is_loopback(host):
    return host in ("127.0.0.1", "localhost", "::1") or \
        host.startswith("127.")


class Listener:
    """sh: the Shell.  state_fn() -> dict for GET /state."""

    def __init__(self, sh, port, host="127.0.0.1", token=None,
                 allow=ALLOW_DEFAULT, state_fn=None, clock=time.monotonic):
        if not is_loopback(host) and not token:
            raise ValueError(f"--listen-host {host} is not loopback: "
                             "give --listen-token (or MFMX_LISTEN_TOKEN)")
        self.sh = sh
        self.token = token
        self.allow = None if allow is None else set(allow)  # None: all
        self.state_fn = state_fn or (lambda: {})
        self.clock = clock
        self.lsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.lsock.bind((host, port))
        self.lsock.listen(8)
        self.lsock.setblocking(False)
        self.port = self.lsock.getsockname()[1]
        self.host = host
        self.conns = {}         # socket -> [bytearray, opened at]
        self.served = 0

    def describe(self):
        allow = "all" if self.allow is None else ",".join(sorted(self.allow))
        return (f"listening on http://{self.host}:{self.port} "
                f"(POST /cmd, GET /state; allowed: {allow}"
                f"{'; token required' if self.token else ''})")

    # ---- the Shell's select loop calls these -------------------------------
    def fds(self):
        self._reap()
        return [self.lsock] + list(self.conns)

    def service(self, readable):
        for s in readable:
            if s is self.lsock:
                self._accept()
            elif s in self.conns:
                self._read(s)

    def close(self):
        for s in list(self.conns):
            self._drop(s)
        self.lsock.close()

    # ---- connections ---------------------------------------------------------
    def _accept(self):
        while True:
            try:
                c, _ = self.lsock.accept()
            except (BlockingIOError, InterruptedError):
                return
            c.setblocking(False)
            self.conns[c] = [bytearray(), self.clock()]

    def _drop(self, s):
        self.conns.pop(s, None)
        try:
            s.close()
        except OSError:
            pass

    def _reap(self):
        now = self.clock()
        for s, (_, t0) in list(self.conns.items()):
            if now - t0 > CONN_TIMEOUT:
                self._drop(s)

    def _read(self, s):
        try:
            data = s.recv(65536)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            self._drop(s)
            return
        if not data:
            self._drop(s)
            return
        buf = self.conns[s][0]
        buf += data
        if len(buf) > MAX_REQUEST:
            self._reply(s, 413, "request too large\n")
            return
        req = self._parse(bytes(buf))
        if req is None:
            return              # not all here yet
        self._reply(s, *self._handle(*req))

    def _reply(self, s, code, body, ctype="text/plain; charset=utf-8"):
        b = body.encode("utf-8")
        head = (f"HTTP/1.0 {code} {REASONS.get(code, '')}\r\n"
                f"Content-Type: {ctype}\r\nContent-Length: {len(b)}\r\n"
                "Connection: close\r\n\r\n").encode("ascii")
        try:
            s.setblocking(True)
            s.settimeout(SEND_TIMEOUT)
            s.sendall(head + b)
        except OSError:
            pass
        self._drop(s)
        self.served += 1

    # ---- HTTP ----------------------------------------------------------------
    @staticmethod
    def _parse(buf):
        """(method, target, headers, body), or None if incomplete."""
        end = buf.find(b"\r\n\r\n")
        if end < 0:
            return None
        lines = buf[:end].decode("latin-1").split("\r\n")
        parts = lines[0].split()
        method, target = (parts[0].upper(), parts[1]) if len(parts) >= 2 \
            else ("", "")
        headers = {}
        for l in lines[1:]:
            k, _, v = l.partition(":")
            headers[k.strip().lower()] = v.strip()
        try:
            n = int(headers.get("content-length", "0"))
        except ValueError:
            n = 0
        body = buf[end + 4:]
        if len(body) < n:
            return None
        return method, target, headers, body[:n].decode("utf-8", "replace")

    def _authorized(self, headers):
        if not self.token:
            return True
        auth = headers.get("authorization", "")
        return headers.get("x-token") == self.token or \
            auth == f"Bearer {self.token}"

    def _handle(self, method, target, headers, body):
        if not self._authorized(headers):
            return 401, "token required\n"
        url = urllib.parse.urlsplit(target)
        if url.path == "/state":
            if method != "GET":
                return 405, "GET /state\n"
            try:
                st = self.state_fn()
            except Exception as e:
                st = {"error": f"{type(e).__name__}: {e}"}
            return 200, json.dumps(st, sort_keys=True) + "\n", \
                "application/json"
        if url.path == "/cmd":
            if method == "POST":
                text = body
            elif method == "GET":
                text = "\n".join(urllib.parse.parse_qs(url.query)
                                 .get("c", []))
            else:
                return 405, "POST /cmd (or GET /cmd?c=...)\n"
            return self.run_lines(text)
        return 404, "try POST /cmd or GET /state\n"

    def run_lines(self, text):
        """Check every line first (all or nothing), then run them."""
        lines = [l.strip() for l in text.replace("\r", "").split("\n")]
        lines = [l for l in lines if l and not l.startswith("#")]
        if not lines:
            return 400, "no command\n"
        for l in lines:
            why = self.refuse(l)
            if why:
                self.sh.say(f"[net] refused: {l}  ({why[1]})")
                return why[0], why[1] + "\n"
        out = []
        ok = True
        for l in lines:
            self.sh.say(f"[net] {l}")
            ok_l, said = self.sh.dispatch_capture(l)
            ok = ok and ok_l
            out += said
        return 200, "\n".join(out) + ("\n" if out else "") + \
            ("" if ok else "(a command failed)\n")

    def refuse(self, line):
        """None if line may run, else (HTTP code, why not)."""
        import shlex
        try:
            argv = shlex.split(line)
        except ValueError as e:
            return 400, f"parse error: {e}"
        try:
            name = self.sh.resolve(argv[0].lower(), self.sh.cmds, "command")
        except ValueError as e:
            return 400, str(e)
        if self.allow is not None and name not in self.allow:
            return 403, f"'{name}' is not allowed over the network " \
                "(mfmxctl --listen-allow)"
        return None
