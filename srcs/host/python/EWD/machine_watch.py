"""machine_watch.py -- Machine methods that watch the running machine: slowscans,
L1 dumps, painter lookup (who), speck refresh, report-stream health (zhd)."""
import os
import time

from machine_util import b36, leximit, parse_tile_range, parse_tile_tag


class WatchMixin:
    """Machine methods: monitoring."""

    # ---- monitoring ---------------------------------------------------------------
    def slowscan(self, count=1):
        """old slowScan(): per chip, pace then runSlowScans(count).
        This is the hostblock-log path that still works when an 'hn' hart is hung."""
        self.need_booted()
        rets = []
        for bh in self.bhs:
            if self.slowscan_gap:
                time.sleep(self.slowscan_gap)
            rets.append(bh.runSlowScans(count))
        self.scan_count += 1
        self.last_scan = rets
        return rets

    def released(self):
        """True once every chip has been taken to CODE_RUNNING or beyond (the T6s
        have been released and have set up their hostblocks)."""
        return bool(self.bhs) and all(bh.getStep() >= self.M.Step.HAS_T6_CODE_RUNNING
                                      for bh in self.bhs)

    def dumpl1(self, items):
        """Write the whole L1 of every Tensix tile named by 'items' into a
        new directory <run>/dumps/N/ (N leximited, one per call), one
        file per tile named TAG.l1 (TAG as in the logs, e.g. at023).  Each
        item is a TAG or TAGLO-TAGHI (see parse_tile_range).  Reads only;
        safe while the tiles run.  Returns a summary line."""
        self.need_booted()
        ranges = [parse_tile_range(i) for i in items]
        todo = []
        for (c0, c1), (x0, x1), (y0, y1) in ranges:
            for c in range(c0, c1 + 1):
                for x in range(x0, x1 + 1):
                    for y in range(y0, y1 + 1):
                        if (c, x, y) not in todo:
                            todo.append((c, x, y))
        bybh = {bh.getChipNumber(): bh for bh in self.bhs}
        outdir, wrote, nottile, nochip, failed = None, 0, 0, set(), 0
        for c, x, y in todo:
            bh = bybh.get(c)
            if bh is None:
                nochip.add(c)
                continue
            if outdir is None:
                outdir = os.path.join(self.simDir, "dumps",
                                      leximit(self.dump_count))
                os.makedirs(outdir, exist_ok=True)
                self.dump_count += 1
            path = os.path.join(outdir, f"at{c}{b36(x)}{b36(y)}.l1")
            r = bh.dumpTileL1(x, y, path)
            if r == 1:
                wrote += 1
            elif r == 0:
                nottile += 1
            else:
                failed += 1
        text = f"dumpl1: {wrote} tile L1s written to {outdir}" if wrote else "dumpl1: nothing written"
        if nottile:
            text += f"; {nottile} not Tensix tiles"
        if failed:
            text += f"; {failed} FAILED (code not deployed yet, or file error)"
        if nochip:
            text += f"; no such chip: {sorted(nochip)}"
        self.last_dump_dir = outdir
        return text

    def hub_map_path(self):
        """hub.map of the build that made this run: $MFMX_HUB_MAP, else the
        repo's build_cross (as scripts/l1dump/l1grid.py does)."""
        env = os.environ.get("MFMX_HUB_MAP")
        if env:
            return env
        here = os.path.dirname(os.path.abspath(__file__))
        return os.path.join(here, "..", "..", "..", "..", "build_cross",
                            "srcs", "cross", "hub.map")

    def hist_text(self, pt):
        """' HIST: ...' the last reports that reached pixel pt, newest first:
        S/R (SELF/RING) type@eEPOCH+phase: the epoch and phase of the site's
        CHANGE (v = events, s = SAVE/LOAD), a leading '-' = dropped as stale.  ''
        if the module lacks pixelHistory or nothing arrived."""
        f = getattr(self.M, "pixelHistory", None)
        try:
            h = f(pt[0], pt[1]) if f is not None else []
        except Exception:       # noqa: BLE001 - diagnostics only
            return ""
        if not h:
            return ""
        def one(x):
            a, r, t, e = x[:4]
            ph = ("" if len(x) < 5 else ("v" if x[4] else "s"))
            return f"{'' if a else '-'}{'R' if r else 'S'}{t}@e{e}{ph}"
        return " HIST: " + " < ".join(one(x) for x in h)

    def specks_refresh(self, points):
        """Ask the hubs behind each (x,y) to re-report it (host-directed
        refresh: the hub queues the site and reports its current atom, which
        repaints a turd from the truth).  Returns the number of requests."""
        f = getattr(self.M, "refreshPixel", None)
        if f is None:
            return 0                    # an older MFMx module
        n = 0
        for (x, y) in points:
            n += int(f(int(x), int(y)))
        return n

    def refresh_stats_text(self):
        f = getattr(self.M, "refreshStats", None)
        if f is None:
            return ""
        a, b, c = f()
        return f"refresh totals: {a} sites asked, {b} answered, {c} turds repainted empty"

    def specks_who(self, points):
        """Lines for zoom specks: who painted each (x,y) plus, automatically,
        what the painting hub's OWN grid holds there now (dumpl1 of those
        hubs, decoded by scripts/l1dump/l1grid.py).  HUB: EMPTY = the hub
        emptied the site and the host never heard (a turd); HUB: type N =
        the hub still holds an atom.  For a RING painter it is the
        borrower's cache copy, so a RING row also shows OWNER <hub>: the
        pixel's owning hub's SELF value.  A row is marked "=> TURD" only when
        every hub copy that could hold the truth is EMPTY (a SELF painter's
        own site; for RING both the borrower's ring and the owner), because
        with cache elision a kept band's owner SELF is stale by design.  Never raises: a failed dump or a missing
        hub.map just says so once."""
        rows = [self.who_row(x, y) for x, y in points]
        f = getattr(self.M, "whoPainted", None)
        who = [f(x, y) if f is not None else None for x, y in points]
        sites = []
        for r in who:
            if r is None or len(r) < 9:
                sites.append(None)
                continue
            tx, ty = self.tlbi_to_noc(r[1])
            sites.append((f"at{r[0]}{b36(tx)}{b36(ty)}", r[7], r[8]))
        # RING painters: the painter's value is the borrower's cache copy,
        # so also read the OWNER hub's SELF site (the pixel's real truth).
        fo = getattr(self.M, "pixelOwner", None)
        owners = []
        for (x, y), r in zip(points, who):
            o = fo(x, y) if (fo is not None and r is not None and len(r) >= 9
                             and r[2]) else None
            if o is None:
                owners.append(None)
                continue
            tx, ty = self.tlbi_to_noc(o[1])
            owners.append((f"at{o[0]}{b36(tx)}{b36(ty)}", o[2], o[3]))
        tags = sorted({st[0] for st in sites + owners if st})
        if not tags:
            return rows
        try:
            import sys
            sd = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "..", "..", "..", "..", "scripts", "l1dump")
            if sd not in sys.path:
                sys.path.insert(0, sd)
            import l1grid
            addr = l1grid.addr_from_map(self.hub_map_path())
            self.last_dump_dir = None
            self.dumpl1(tags)
            if not self.last_dump_dir:
                raise ValueError("no dump written")
            grids = {}
            for t in tags:
                try:
                    l1 = open(os.path.join(self.last_dump_dir, t + ".l1"), "rb").read()
                    grids[t] = l1grid.decode(l1, addr)[0]
                except (OSError, ValueError):
                    grids[t] = None
        except Exception as e:      # noqa: BLE001 - diagnostics never break the shell
            return rows + [f"(hub truth unavailable: {e})"]
        def look(st):
            g = grids.get(st[0]) if st else None
            if g is None:
                return "?"
            ty, ok = g[st[1]][st[2]]
            return ("EMPTY" if ty == 0 else f"type {ty}") + \
                ("" if ok else " (parity bad)")
        out = []
        for row, st, ow, pt in zip(rows, sites, owners, points):
            line = row + f" HUB: {look(st)}"
            if ow:
                line += f" OWNER {ow[0]}: {look(ow)}"
            # TURD only when every hub copy that could hold the truth is
            # empty: a SELF painter's own site, or for a RING painter both
            # the borrower's ring AND the owner.  (With cache elision a kept
            # band's owner SELF is stale by design, the ring holds the truth.)
            mine = look(st) if st else "?"
            theirs = look(ow) if ow else None
            if mine == "EMPTY" and theirs in (None, "EMPTY"):
                line += " => TURD"
            elif ow and mine != "?" and theirs == "EMPTY":
                line += " (ring holds it: kept band?)"
            line += self.hist_text(pt)
            out.append(line)
        return out

    def tick_slowscan(self):
        # Before the T6s are released their hostblocks hold no valid magic, so
        # every scan would report BAD MAGIC and dump L1 to disk.  Manual 'scan'
        # still works at any phase.
        if self.configured and self.inited and self.released():
            self.slowscan(1)

    def tick_zhd(self):
        # Its own tick: the seam configs turn the slowscan tick off.
        if self.configured and self.inited:
            self.check_zhd()

    # ---- report stream health (ZHostDecompressor.h) -------------------------------
    ZHD_KINDS = {0: "?", 1: "reports skipped", 2: "garbage"}

    def zhd_rows(self):
        """Per-hub report stream health: dicts, [] if the module lacks it."""
        snap = getattr(self.M, "zhdSnapshot", None)    # absent in old/mock MFMx
        if snap is None:
            return []
        keys = ("chip", "tlbi", "reports", "invalid", "bad_atoms", "consec",
                "desyncs", "desynced", "at_report", "at_cbytes", "at_packet",
                "kind", "restarts", "restart_pending", "stale")
        rows = [dict(zip(keys, r)) for r in snap()]
        for r in rows:
            x, y = self.tlbi_to_noc(r["tlbi"])
            r["tag"] = f"at{r['chip']}{b36(x)}{b36(y)}"
        return sorted(rows, key=lambda r: (r["chip"], r["tlbi"]))

    def reports_total(self):
        """Reports received by all hub streams so far (None: no counters)."""
        if getattr(self.M, "zhdSnapshot", None) is None:
            return None
        return sum(r["reports"] for r in self.zhd_rows())

    def zhd_text(self, verbose=False):
        rows = self.zhd_rows()
        if not rows:
            return "zhd: no report streams (module has no zhdSnapshot, or none up)"
        bad = [r for r in rows if r["desynced"]]
        ever = [r for r in rows if r["desyncs"] and not r["desynced"]]
        torn = sum(r["bad_atoms"] for r in rows)
        stale = sum(r.get("stale", 0) for r in rows)
        restarts = sum(r["restarts"] for r in rows)
        pend = sum(1 for r in rows if r["restart_pending"])
        on = getattr(self.M, "zhdRestartEnabled", None)
        out = [f"zhd: {len(rows)} report streams, {len(bad)} desynced"
               f"{f', {len(ever)} recovered' if ever else ''}; "
               f"{restarts} restarts done, {pend} requested and waiting"
               f"{'' if on is None else ('' if on() else ' (restart requests OFF)')}; "
               f"{torn} intact reports carried a bad atom (skipped); "
               f"{stale} stale reports ignored (older epoch than their pixel)"]
        for r in (rows if verbose else bad):
            state = "DESYNCED" if r["desynced"] else "ok"
            out.append(f"  {r['tag']} bh{r['chip']}:{r['tlbi']} {state}: "
                       f"{r['reports']} good reports, {r['invalid']} bad, "
                       f"{r['bad_atoms']} bad atoms, "
                       f"{r.get('stale', 0)} stale"
                       + (f"; broke at report {r['at_report']}, "
                          f"{r['at_cbytes']} compressed bytes, packet "
                          f"{r['at_packet']} ({self.ZHD_KINDS.get(r['kind'], '?')})"
                          if r["desyncs"] else "")
                       + (f"; {r['restarts']} restarts" if r["restarts"] else "")
                       + ("; restart requested, waiting for the hub"
                          if r["restart_pending"] else ""))
        return "\n".join(out)

    def who_row(self, x, y):
        """One compact line of who: (x,y) RING|SELF type T e<epoch> <lag>
        <hub tag> site (gx,gy).  For zoom specks, which asks for many."""
        f = getattr(self.M, "whoPainted", None)
        r = f(x, y) if f is not None else None
        if r is None:
            return f"({x},{y}) nobody has painted it"
        chip, tlbi, ring, known, epoch, typ, newest = r[:7]
        tx, ty = self.tlbi_to_noc(tlbi)
        lag = ""
        if known and newest >= 0:
            d = (newest - epoch) & 0xffff
            lag = f" lag {d - 0x10000 if d >= 0x8000 else d}"
        site = f" site ({r[7]},{r[8]})" if len(r) >= 9 else ""
        return (f"({x},{y}) {'RING' if ring else 'SELF'} type {typ} "
                f"e{epoch if known else '?'}{lag} at{chip}{b36(tx)}{b36(ty)}{site}")

    def who(self, x, y):
        """Who last painted pixel (x,y) of the picture (ghost-pixel hunting):
        hub, self sites or S/E cache ring, epoch and how far behind the
        newest epoch seen.  Text; never raises on a missing module."""
        f = getattr(self.M, "whoPainted", None)
        if f is None:
            return "who: this MFMx module has no whoPainted"
        r = f(x, y)
        if r is None:
            return f"who ({x},{y}): nobody has painted that pixel (never reported)"
        chip, tlbi, ring, known, epoch, typ, newest = r[:7]
        gtxt = f", hub grid site ({r[7]},{r[8]})" if len(r) >= 9 else ""
        tx, ty = self.tlbi_to_noc(tlbi)
        tag = f"at{chip}{b36(tx)}{b36(ty)}"
        if not known:
            when = "epoch unknown (no marker seen on that stream yet)"
        elif newest < 0:
            when = f"epoch {epoch}"
        else:
            lag = (newest - epoch) & 0xffff
            lag = lag - 0x10000 if lag >= 0x8000 else lag
            when = f"epoch {epoch}, {lag} behind the newest seen ({newest})"
        return (f"who ({x},{y}): last painted by {tag} bh{chip}:{tlbi} from its "
                f"{'S/E CACHE RING' if ring else 'own SELF sites'}, atom type "
                f"{typ}, {when}; hub-relative pixel ({x % 137},{y % 108}){gtxt}")

    @staticmethod
    def noc_to_tlbi(x, y):
        """(x,y) NoC0 tile coords -> tlbi, or None if not a Tensix tile."""
        if not (1 <= x <= 7 or 10 <= x <= 16) or not (2 <= y <= 11):
            return None
        return (y - 2) * 14 + (x - 1 if x <= 7 else x - 3)

    def zhd_break(self, tags):
        """TEST: lose one compressed byte of each named hub's report stream
        (the host-side decoder drops it, as a real loss would), to exercise
        desync detection and the restart.  tags: 'at0a7' style."""
        f = getattr(self.M, "zhdBreak", None)
        if f is None:
            return "zhd break: this MFMx has no zhdBreak"
        out = []
        for t in tags:
            try:
                chip, x, y = parse_tile_tag(t)
            except ValueError as e:
                out.append(f"{t}: {e}")
                continue
            tlbi = self.noc_to_tlbi(x, y)
            if tlbi is None:
                out.append(f"{t}: not a Tensix tile")
            elif f(chip, tlbi):
                out.append(f"{t}: one compressed byte will be dropped (bh{chip}:{tlbi})")
            else:
                out.append(f"{t}: no report stream there (chip not in the run, or not a hub?)")
        return "\n".join(out) or "usage: zhd break TAG[,TAG...]"

    def zhd_restart(self, on):
        """Turn restart requests to desynced hubs on or off (detection stays)."""
        f = getattr(self.M, "zhdRestartEnable", None)
        if f is None:
            return "zhd restart: this MFMx has no zhdRestartEnable"
        f(bool(on))
        return f"zhd restart: requests {'ON' if on else 'OFF'}"

    def check_zhd(self, every=10.0):
        """Say it (and log it) when the desynced set, restarts done or restarts
        pending change."""
        now = time.monotonic()
        if now - getattr(self, "zhd_checked", -1e9) < every:
            return
        self.zhd_checked = now
        try:
            rows = self.zhd_rows()
            bad = (tuple(r["tag"] for r in rows if r["desynced"]),
                   sum(r["restarts"] for r in rows),
                   sum(1 for r in rows if r["restart_pending"]))
        except Exception as e:
            self.zhd_checked = float("inf")      # broken: stop asking
            self.say(f"zhd check disabled: {e}")
            return
        if bad != getattr(self, "zhd_bad", ((), 0, 0)):
            self.zhd_bad = bad
            text = self.zhd_text()
            self.say(text)
            self.logkt("ZHD", f"{self._run_secs():.0f}s {text}")
