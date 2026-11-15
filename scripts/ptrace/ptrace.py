#!/usr/bin/env python3
"""ptrace.py -- first-cut packet-trace analyzer for MFMx per-tile T6 logs.

7-bit ASCII only.  Python 3 stdlib only.  Read-only; never touches hardware.

Input : a run directory containing tiles/*.mfmk (per-tile expanded-mark logs).
Marks : {fid:line tick chip x,y hH TAG:payload}
        .ps  send       OUR+SIZE[#NN]->DEST(x,y)
        .pcs hub send   same layout
        .pr  recv       OUR+SIZE[#NN]<-DIR
        .pcr hub recv   same layout
        .pw  waypoint   OUR+SIZE#NN[ msg]   (fid:line = code waypoint)
        (#NN = optional 2-hex nonce; absent in logs predating the nonce
        change.  When present on both ends it is used as an exact key.)

What it does (first cut):
  1. ingest all tiles, keep only .p* marks (counting the rest)
  2. match each send to a receive at the destination endpoint (tile,addr4)
     by ORDER (+ nonce if present), tolerating loss; sizes and ticks are
     consistency checks, not keys
  3. link hub relays (.pcr -> .pcs at the same tile+slot)
  4. chain hops into traces
  5. report: summary, anomalies, traces, per-lane balance

Usage: ptrace.py RUNDIR [--traces] [--lanes] [--eps N] [--maxlat N]
"""
import argparse
import glob
import os
import re
import sys
from collections import defaultdict, Counter

MARK_RE = re.compile(
    r'^\{(\d+):(\d+) (\d+) (\d+) (\d+),(\d+) h(.) (.*)\}$')
SEP_RE = re.compile(r'^---([0-9.]+)---$')
SEND_RE = re.compile(
    r'^([0-9a-f]{4})\+(\d+)(?:#([0-9a-f]{2}))?->([0-9a-f]{4})\((\d+),(\d+)\)$')
RECV_RE = re.compile(
    r'^([0-9a-f]{4})\+(\d+)(?:#([0-9a-f]{2}))?<-(\w+)$')


# ---- slot layout.  All addresses are in "ptrTag4" units: (addr>>4)&0xffff.
# Defaults were derived from BLOCKCODE_LIST (INTERHUB 8 x 16640B storages,
# CORNERHUB 4 x 16640B, car = 4160B => 4 cars/storage) plus the base
# addresses observed in the 20261114-010305 run.  A misc/PKTS.txt, when
# present, overrides them.
DEFAULT_LAYOUT = {
    # name: (base4, storage_count, storage_size4, car_size4, car_count)
    'CHUB': (0x26d6, 4, 0x410, 0x104, 4),
    'IHUB': (0x3722, 8, 0x410, 0x104, 4),
}
LAYOUT = dict(DEFAULT_LAYOUT)
LAYOUT_SRC = 'built-in defaults (BLOCKCODE_LIST + run 20261114-010305)'
D8_NAMES = ['?0', '?1', '?2', '?3', '?4', '?5', '?6', '?7']  # not asserted


def decode_slot(addr4):
    """addr4 -> (blockname, storage_idx, car_idx) or None"""
    for name, (base, scnt, ssz, csz, ccnt) in LAYOUT.items():
        off = addr4 - base
        if 0 <= off < scnt * ssz:
            sidx, rem = divmod(off, ssz)
            if rem % csz == 0 and rem // csz < ccnt:
                return (name, sidx, rem // csz)
    return None


def slot_name(addr4):
    d = decode_slot(addr4)
    if d is None:
        return '%04x' % addr4
    return '%s[%d].%d' % d


def tlbi_to_noc(t):
    y, x = divmod(t, 14)
    x += 1
    if x > 7:
        x += 2
    return (x, y + 2)


def parse_bolt(root):
    """misc/mfmxctl-BOLT.txt -> set of lamer NoC0 coords (or None)"""
    path = os.path.join(root, 'misc', 'mfmxctl-BOLT.txt')
    if not os.path.exists(path):
        return None
    lam = set()
    for ln in open(path, errors='replace'):
        m = re.search(r'lamers ([0-9,]+)', ln)
        if m:
            lam |= set(tlbi_to_noc(int(t)) for t in m.group(1).split(','))
    return lam


def tile_from_filename(path):
    """tiles/atXYZ.mfmk: 'at0' + x + y, digits 0-9a-g (base 17)."""
    b = os.path.basename(path)
    m = re.match(r'^at0([0-9a-g])([0-9a-g])\.mfmk$', b)
    if not m:
        return None
    return (int(m.group(1), 17), int(m.group(2), 17))


def load_pkts(root):
    """misc/PKTS.txt (see PKTS format notes); overrides LAYOUT."""
    global LAYOUT, LAYOUT_SRC
    path = os.path.join(root, 'misc', 'PKTS.txt')
    if not os.path.exists(path):
        return False
    # lines:  BC name=INTERHUB storcnt=8 storsz=16640 carsz=4160 carcnt=4
    #         BLK bc=INTERHUB addr=0x37220      (first BLK per bc wins;
    #                                            tiles of a type share it)
    bcs, bases = {}, {}
    for ln in open(path, errors='replace'):
        f = ln.split()
        if not f:
            continue
        kv = dict(x.split('=', 1) for x in f[1:] if '=' in x)
        if f[0] == 'BC':
            bcs[kv['name']] = kv
        elif f[0] == 'BLK':
            bases.setdefault(kv['bc'], int(kv['addr'], 0))
    new = {}
    short = {'INTERHUB': 'IHUB', 'CORNERHUB': 'CHUB'}
    for name, kv in bcs.items():
        if name not in bases:
            continue
        nm = short.get(name, name)
        new[nm] = (((bases[name] >> 4) & 0xffff), int(kv['storcnt']),
                   int(kv['storsz']) >> 4, int(kv['carsz']) >> 4,
                   int(kv['carcnt']))
    if new:
        LAYOUT = new
        LAYOUT_SRC = 'misc/PKTS.txt'
    return True


def autofit_layout(marks):
    """Builds shift base addresses between images; if the defaults leave
    addresses undecodable, try a uniform shift (ptrTag4 units) of all
    bases and keep the best.  Returns shift applied."""
    global LAYOUT, LAYOUT_SRC
    addrs = set()
    for m in marks:
        addrs.add(m.our)
        if m.dest:
            addrs.add(m.dest[1])
    def bad():
        return sum(1 for ad in addrs if decode_slot(ad) is None)
    base_layout = dict(LAYOUT)
    best = (bad(), 0)
    if best[0] == 0:
        return 0
    for sh in range(-0x80, 0x81):
        LAYOUT = dict((k, (v[0] + sh,) + v[1:]) for k, v in base_layout.items())
        b = bad()
        if b < best[0]:
            best = (b, sh)
    sh = best[1]
    LAYOUT = dict((k, (v[0] + sh,) + v[1:]) for k, v in base_layout.items())
    if sh:
        LAYOUT_SRC += ' + auto-fit shift %+d (%d addrs still undecodable)' % (
            sh, best[0])
    return sh


WAY_RE = re.compile(
    r'^([0-9a-f]{4})\+(\d+)#([0-9a-f]{2})(?: (.*))?$')


class Mark(object):
    __slots__ = ('tile', 'chip', 'tick', 'seq', 'blk', 'tag', 'our', 'size',
                 'nonce', 'dest', 'dir', 'fid', 'line', 'raw', 'peer', 'msg')

    def kind(self):
        return self.tag  # 'ps' 'pcs' 'pr' 'pcr'

    def is_send(self):
        return self.tag in ('ps', 'pcs')

    def ep(self):
        return (self.tile, self.our)

    def label(self):
        n = '' if self.nonce is None else '#%02x' % self.nonce
        return '%s@%d,%d %s+%d%s' % (self.tag, self.tile[0], self.tile[1],
                                      slot_name(self.our), self.size, n)


def parse_tile_file(path, stats):
    marks = []
    blk = 0
    seq = 0
    with open(path, 'r', errors='replace') as f:
        for ln in f:
            ln = ln.rstrip('\n')
            if SEP_RE.match(ln):
                blk += 1
                continue
            m = MARK_RE.match(ln)
            if not m:
                stats['nonmark'] += 1
                continue
            fid, line, tick, chip, x, y, hart, msg = m.groups()
            if not msg.startswith('.p'):
                stats['other:' + hart] += 1
                continue
            tag, _, payload = msg.partition(':')
            tag = tag[1:]  # drop '.'
            mk = Mark()
            mk.tile = (int(x), int(y))
            mk.chip = int(chip)
            mk.tick = int(tick)
            mk.seq = seq
            seq += 1
            mk.blk = blk
            mk.tag = tag
            mk.fid, mk.line = int(fid), int(line)
            mk.raw = msg
            mk.nonce = None
            mk.dest = None
            mk.dir = None
            mk.peer = None
            mk.msg = None
            if tag in ('ps', 'pcs'):
                s = SEND_RE.match(payload)
                if not s:
                    stats['badpayload'] += 1
                    continue
                mk.our = int(s.group(1), 16)
                mk.size = int(s.group(2))
                mk.nonce = None if s.group(3) is None else int(s.group(3), 16)
                mk.dest = ((int(s.group(5)), int(s.group(6))),
                           int(s.group(4), 16))
            elif tag == 'pw':
                w = WAY_RE.match(payload)
                if not w:
                    stats['badpayload'] += 1
                    continue
                mk.our = int(w.group(1), 16)
                mk.size = int(w.group(2))
                mk.nonce = int(w.group(3), 16)
                mk.msg = w.group(4) or ''
            elif tag in ('pr', 'pcr'):
                r = RECV_RE.match(payload)
                if not r:
                    stats['badpayload'] += 1
                    continue
                mk.our = int(r.group(1), 16)
                mk.size = int(r.group(2))
                mk.nonce = None if r.group(3) is None else int(r.group(3), 16)
                mk.dir = r.group(4)
            else:
                stats['unknown_ptag:' + tag] += 1
                continue
            stats['p:' + tag] += 1
            marks.append(mk)
    return marks


def match_endpoint(sends, recvs, eps, maxlat, compat=None, use_nonce=True):
    """Order-match sends (to one endpoint) against recvs (at that endpoint).
    Slots are 1-car, point-to-point, so sends to an endpoint and recvs at it
    are 1:1 in order.  compat(s,r) may veto a pairing (direction check).
    Returns (pairs, orphan_sends, orphan_recvs)."""
    pairs, os_, or_ = [], [], []
    i = j = 0
    sends = sorted(sends, key=lambda m: (m.tick, m.seq))
    recvs = sorted(recvs, key=lambda m: (m.tick, m.seq))
    while i < len(sends) and j < len(recvs):
        s, r = sends[i], recvs[j]
        dt = r.tick - s.tick
        ok = compat is None or compat(s, r)
        if ok and use_nonce and s.nonce is not None and r.nonce is not None:
            ok = (s.nonce == r.nonce)
        if ok and -eps <= dt <= maxlat:
            pairs.append((s, r)); i += 1; j += 1
        elif dt < -eps:
            or_.append(r); j += 1      # recv with no send before it
        elif dt > maxlat:
            os_.append(s); i += 1      # send with no recv soon after
        else:
            # in the time window but vetoed: peek for a better partner
            if j + 1 < len(recvs) and (compat is None or
                                       compat(s, recvs[j + 1])):
                or_.append(r); j += 1
            elif i + 1 < len(sends) and (compat is None or
                                         compat(sends[i + 1], r)):
                os_.append(s); i += 1
            else:
                or_.append(r); os_.append(s); i += 1; j += 1
    os_.extend(sends[i:])
    or_.extend(recvs[j:])
    return pairs, os_, or_


def sgn(v):
    return (v > 0) - (v < 0)


def learn_dir_signs(pairs):
    """From clean .ps->.pr pairs learn dir -> sign(sender - receiver)."""
    votes = defaultdict(Counter)
    for s, r in pairs:
        if r.tag != 'pr' or s.tag != 'ps':
            continue
        d = (sgn(s.tile[0] - r.tile[0]), sgn(s.tile[1] - r.tile[1]))
        votes[r.dir][d] += 1
    table = {}
    for d, c in votes.items():
        (best, n), = c.most_common(1)
        table[d] = (best, n, sum(c.values()))
    return table


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rundir')
    ap.add_argument('--traces', action='store_true')
    ap.add_argument('--lanes', action='store_true')
    ap.add_argument('--eps', type=int, default=40,
                    help='allowed recv-before-send tick skew (default 40; run 20261114-230036 showed up to ~25 between tiles)')
    ap.add_argument('--maxlat', type=int, default=700,
                    help='max send->recv tick latency (default 700)')
    a = ap.parse_args()

    stats = Counter()
    marks = []
    have_pkts = load_pkts(a.rundir)
    lamers = parse_bolt(a.rundir)
    files = sorted(glob.glob(os.path.join(a.rundir, 'tiles', '*.mfmk')))
    if not files:
        sys.exit('no tiles/*.mfmk under %s' % a.rundir)
    for p in files:
        marks.extend(parse_tile_file(p, stats))
    wps = [m for m in marks if m.tag == 'pw']     # waypoint marks
    marks = [m for m in marks if m.tag != 'pw']   # packet-motion marks
    if not have_pkts:
        autofit_layout(marks)

    sends_by_dest = defaultdict(list)   # (tile,addr) -> sends targeting it
    recvs_by_ep = defaultdict(list)     # (tile,addr) -> recvs at it
    for m in marks:
        if m.is_send():
            sends_by_dest[m.dest].append(m)
        else:
            recvs_by_ep[m.ep()].append(m)

    # ---- step 2: send->recv matching.  Pass 1 ignores direction; pass 2
    # re-matches vetoing pairs whose geometry contradicts the learned
    # direction table (dir -> sign of sender-minus-receiver).
    def run_match(compat, use_nonce=True):
        prs, osn, orc = [], [], []
        for ep in set(sends_by_dest) | set(recvs_by_ep):
            p, os_, or_ = match_endpoint(sends_by_dest.get(ep, []),
                                         recvs_by_ep.get(ep, []),
                                         a.eps, a.maxlat, compat,
                                         use_nonce)
            prs.extend(p); osn.extend(os_); orc.extend(or_)
        return prs, osn, orc

    pairs1, _, _ = run_match(None, False)   # nonce-blind pass
    dirtab = learn_dir_signs(pairs1)

    def compat(s, r):
        if s.size != r.size:
            return False       # recv size is decoded from the header
        if r.tag != 'pr' or s.tag != 'ps':
            # .pcr dir names the hub block's midway axis, not the origin
            # direction, so no geometric veto there.
            return True
        t = dirtab.get(r.dir)
        if t is None:
            return True
        d = (sgn(s.tile[0] - r.tile[0]), sgn(s.tile[1] - r.tile[1]))
        return d == t[0]

    pairs, orphan_sends, orphan_recvs = run_match(compat)
    nxt = {}      # send mark -> recv mark
    prv = {}      # recv mark -> send mark
    for s, r in pairs:
        nxt[s] = r
        prv[r] = s
        r.peer = s
        s.peer = r

    # nonce agreement on ORDER-matched pairs (nonce-blind pass with the same
    # size/direction vetoes): does the sender's nonce reach the receiver?
    blind_pairs, _, _ = run_match(compat, False)
    agree = Counter()
    for sm, rm in blind_pairs:
        if sm.nonce is None or rm.nonce is None:
            continue
        agree[(sm.tag + '->' + rm.tag, sm.nonce == rm.nonce)] += 1

    # classify the unmatched by whether the far tile could have logged
    p_tiles = set(m.tile for m in marks)
    logged_tiles = p_tiles
    unobs_sends = [s for s in orphan_sends if s.dest[0] not in p_tiles]
    true_orph_sends = [s for s in orphan_sends if s.dest[0] in p_tiles]
    # implied sender tile for orphan .pr recvs: tile - sign-table is only a
    # direction, so we report "sender side logged?" via the direction only
    # when a unique neighbour step is known (EWP neighbours are 2 apart).
    def implied_sender(r):
        t = dirtab.get(r.dir)
        if r.tag != 'pr' or t is None:
            return None
        dx, dy = t[0]
        return (r.tile[0] + 2 * dx, r.tile[1] + 2 * dy)
    orph_recv_unobs = []
    orph_recv_real = []
    orph_recv_unknown = []
    for r in orphan_recvs:
        st = implied_sender(r)
        if st is None:
            orph_recv_unknown.append(r)
        elif st not in p_tiles:
            orph_recv_unobs.append(r)
        else:
            orph_recv_real.append(r)

    # Implied receipts: an unmatched send to endpoint E that is later
    # "answered" by a send FROM endpoint E (the returned car) must have
    # arrived even though no recv mark exists (uninstrumented EP recv or
    # lost mark).
    sends_from = defaultdict(list)
    for m in marks:
        if m.is_send():
            sends_from[m.ep()].append(m)
    for v in sends_from.values():
        v.sort(key=lambda m: (m.tick, m.seq))
    implied = []
    used_ans = set()
    for sd in sorted(true_orph_sends, key=lambda m: m.tick):
        for cand in sends_from.get(sd.dest, []):
            if cand.tick >= sd.tick and id(cand) not in used_ans and \
               cand.tick - sd.tick <= 5000:
                used_ans.add(id(cand))
                implied.append((sd, cand))
                break
    implied_set = set(id(x[0]) for x in implied)
    really_open = [x for x in true_orph_sends if id(x) not in implied_set]

    # ---- step 3: hub relays  (.pcr -> .pcs at same tile+slot, in order)
    relay = {}    # pcr mark -> pcs mark
    relay_back = {}
    per_slot = defaultdict(lambda: ([], []))
    for m in marks:
        if m.tag == 'pcr':
            per_slot[m.ep()][0].append(m)
        elif m.tag == 'pcs':
            per_slot[m.ep()][1].append(m)
    relay_orph_r, relay_orph_s = [], []
    for ep, (rs, ss) in per_slot.items():
        rs.sort(key=lambda m: (m.tick, m.seq))
        ss.sort(key=lambda m: (m.tick, m.seq))
        i = j = 0
        while i < len(rs) and j < len(ss):
            r, s = rs[i], ss[j]
            if s.tick < r.tick - a.eps:
                relay_orph_s.append(s); j += 1
            elif s.tick - r.tick > a.maxlat:
                relay_orph_r.append(r); i += 1
            else:
                relay[r] = s; relay_back[s] = r; i += 1; j += 1
        relay_orph_r.extend(rs[i:]); relay_orph_s.extend(ss[j:])

    relay_nonce = Counter((r.nonce == x.nonce) for r, x in relay.items()
                          if r.nonce is not None and x.nonce is not None)

    # ---- step 4: chains.  A chain starts at a send with no relay_back,
    # i.e. not a hub forward (.ps, or .pcs with no .pcr).
    chains = []
    for m in marks:
        if not m.is_send() or m in relay_back:
            continue
        chain = [m]
        cur = m
        guard = 0
        while guard < 16:
            guard += 1
            r = nxt.get(cur)
            if r is None:
                break
            chain.append(r)
            s2 = relay.get(r)
            if s2 is None:
                break
            chain.append(s2)
            cur = s2
        chains.append(chain)

    # ---- consistency checks
    size_mismatch = [(s, r) for s, r in pairs if s.size != r.size]
    neg_lat = [(s, r) for s, r in pairs if r.tick < s.tick]
    lat = sorted(r.tick - s.tick for s, r in pairs)
    relay_size_mm = [(r, s) for r, s in relay.items() if r.size != s.size]

    out = sys.stdout.write
    out('== ptrace first cut: %s\n' % a.rundir)
    out('files: %d tiles; .p marks: %d\n' % (len(files), len(marks)))
    out('other lines: %s\n' % ', '.join(
        '%s=%d' % kv for kv in sorted(stats.items())
        if not kv[0].startswith('p:')))
    out('p marks: %s\n' % ', '.join(
        '%s=%d' % kv for kv in sorted(stats.items())
        if kv[0].startswith('p:')))
    tiles_with_p = sorted(set(m.tile for m in marks))
    out('tiles with .p marks: %s\n' % ' '.join(
        '(%d,%d)' % t for t in tiles_with_p))
    has_nonce = sum(1 for m in marks if m.nonce is not None)
    out('marks carrying nonce: %d of %d\n' % (has_nonce, len(marks)))
    out('\n-- send->recv matching (by order%s)\n' %
        (' + nonce where present' if has_nonce else ''))
    nsend = sum(1 for m in marks if m.is_send())
    nrecv = len(marks) - nsend
    out('sends=%d recvs=%d matched=%d\n' % (nsend, nrecv, len(pairs)))
    out('direction table learned from clean .ps->.pr pairs '
        '(dir: sign(sender-receiver) votes/total):\n')
    for d in sorted(dirtab):
        t = dirtab[d]
        out('    %s: %s  %d/%d\n' % (d, t[0], t[1], t[2]))
    out('unmatched sends, dest tile has no .p marks (unobservable): %d\n'
        % len(unobs_sends))
    out('unmatched sends, dest tile logged .p marks (possible loss/stuck): '
        '%d\n' % len(true_orph_sends))
    out('  of those, receipt implied by a later send from the dest slot: %d'
        ' (recv mark absent); truly unexplained: %d\n'
        % (len(implied), len(really_open)))
    out('unmatched .pr recvs, implied sender tile has no .p marks '
        '(unobservable): %d\n' % len(orph_recv_unobs))
    out('unmatched .pr recvs, implied sender tile logged .p marks '
        '(possible loss): %d\n' % len(orph_recv_real))
    out('unmatched recvs, sender not derivable (.pcr or unknown dir): %d\n'
        % len(orph_recv_unknown))
    if lat:
        out('latency ticks (recv-send) histogram: %s\n' %
            dict(sorted(Counter(lat).items())))
    out('nonce agreement on order-matched pairs (equal/total by kind):\n')
    kinds = sorted(set(k for k, _ in agree))
    for k in kinds:
        out('    %-9s %d/%d\n' % (k, agree[(k, True)],
                                 agree[(k, True)] + agree[(k, False)]))
    if not kinds:
        out('    (no nonce-bearing pairs)\n')
    out('relay .pcr->.pcs nonce equal/total: %d/%d\n' % (
        relay_nonce[True], relay_nonce[True] + relay_nonce[False]))
    out('size mismatches on matched pairs: %d\n' % len(size_mismatch))
    out('recv-before-send (negative latency) pairs: %d\n' % len(neg_lat))
    out('\n-- hub relays: pcr->pcs linked=%d, pcr orphan=%d, pcs orphan=%d,'
        ' size mismatches=%d\n' % (len(relay), len(relay_orph_r),
                                   len(relay_orph_s), len(relay_size_mm)))
    out('\n-- chains: %d (hops per chain: %s)\n' % (
        len(chains), dict(Counter(
            sum(1 for x in c if x.is_send()) for c in chains))))
    complete = sum(1 for c in chains if not c[-1].is_send())
    out('chains ending in a recv: %d; ending at a send (open): %d\n' % (
        complete, len(chains) - complete))

    out('\n-- slot layout source: %s%s\n' % (
        LAYOUT_SRC, '' if have_pkts else '  (no misc/PKTS.txt found)'))
    undec = Counter()
    for m in marks:
        for ad in (m.our, m.dest[1] if m.dest else None):
            if ad is not None and decode_slot(ad) is None:
                undec[ad] += 1
    out('addresses not decodable with this layout: %s\n' %
        (dict((('%04x' % k), v) for k, v in undec.items()) or 'none'))

    # per-storage view: where do sends originate and recvs land?
    by_tag_stor = Counter()
    for m in marks:
        d = decode_slot(m.our)
        if d:
            by_tag_stor[(m.tag, d[0], d[1])] += 1
    out('\nmarks by (tag, block, storage idx):\n')
    for blk in sorted(LAYOUT):
        for sidx in range(LAYOUT[blk][1]):
            row = ['%s=%d' % (t, by_tag_stor.get((t, blk, sidx), 0))
                   for t in ('ps', 'pr', 'pcs', 'pcr')]
            out('  %s[%d]: %s\n' % (blk, sidx, '  '.join(row)))
    gaps = [(blk, sidx) for blk in LAYOUT
            for sidx in range(LAYOUT[blk][1])
            if blk == 'IHUB' and by_tag_stor.get(('ps', blk, sidx), 0) > 0
            and by_tag_stor.get(('pr', blk, sidx), 0) == 0]
    if gaps:
        out('IHUB storages that send (.ps) but never log a recv (.pr): %s\n'
            % ', '.join('%s[%d]' % g for g in gaps))
    miss = Counter()
    for sd in true_orph_sends:
        d = decode_slot(sd.dest[1])
        miss[(d[0], d[1]) if d else None] += 1
    out('unmatched sends to logged tiles, by destination storage: %s\n' %
        dict(('%s[%d]' % k if k else '?', v) for k, v in miss.items()))

    # per-tile status
    out('\n-- hub-grid tile status (from tiles/ + BOLT lamers)\n')
    tfiles = {}
    for pth in files:
        t = tile_from_filename(pth)
        if t:
            tfiles[t] = pth
    npm = Counter(m.tile for m in marks + wps)
    rows = Counter()
    for t in sorted(tfiles):
        lam = (None if lamers is None else (t in lamers))
        key = ('lamer' if lam else 'acked' if lam is False else 'unknown-lamer',
               'has .p marks' if npm.get(t) else 'no .p marks')
        rows[key] += 1
    for k, v in sorted(rows.items()):
        out('  %-14s %-13s %d tiles\n' % (k[0], k[1], v))
    if lamers is not None:
        out('  silent lamers: %s\n' % ' '.join(
            '(%d,%d)' % t for t in sorted(
                t for t in tfiles if t in lamers and not npm.get(t))))
        out('  silent non-lamers: %s\n' % ' '.join(
            '(%d,%d)' % t for t in sorted(
                t for t in tfiles if t not in lamers and not npm.get(t))))

    # ---- waypoints (.pw): where in the code a packet was seen
    if wps:
        out('\n-- waypoints (.pw): %d marks\n' % len(wps))
        by_site = Counter((w.fid, w.line, w.msg) for w in wps)
        for (fid, line, msg), n in sorted(by_site.items()):
            out('  %d:%d %-12s x%d\n' % (fid, line, msg, n))
        way_at = defaultdict(list)   # (tile,slot) -> waypoints, tick order
        for w in sorted(wps, key=lambda w: (w.tick, w.seq)):
            way_at[w.ep()].append(w)

        def ways_for(ep, nonce, tick):
            # a car arriving with nonce n shows n on arrival, n+1 once
            # re-closed; accept both, at or after the send (minus skew)
            return [w for w in way_at.get(ep, [])
                    if w.nonce in (nonce, (nonce + 1) & 0xff)
                    and w.tick >= tick - a.eps]

        # last waypoint reached by packets that never produced a recv
        stuck = []
        for sd in sorted(orphan_sends, key=lambda m: m.tick):
            if sd.dest[0] not in p_tiles and not any(
                    w.tile == sd.dest[0] for w in wps):
                continue            # dest tile not observed at all
            ws = ways_for(sd.dest, sd.nonce, sd.tick) if sd.nonce is not None else []
            stuck.append((sd, ws[-1] if ws else None))
        if stuck:
            out('\n-- packets with no recv: last waypoint at destination '
                '(%d)\n' % len(stuck))
            lastsites = Counter()
            for sd, w in stuck:
                lastsites[(w.fid, w.line, w.msg) if w else None] += 1
            for k, n in sorted(lastsites.items(), key=lambda kv: -kv[1]):
                out('  %s x%d\n' % (
                    ('%d:%d %s' % k) if k else 'no waypoint seen', n))
            for sd, w in stuck[:12]:
                out('  %s t=%d -> (%d,%d)%s : %s\n' % (
                    sd.label(), sd.tick, sd.dest[0][0], sd.dest[0][1],
                    slot_name(sd.dest[1]),
                    ('%d:%d %s t=%d' % (w.fid, w.line, w.msg, w.tick))
                    if w else 'no waypoint seen'))

    def show(title, items, fmt, limit=12):
        if not items:
            return
        out('\n-- %s (%d, showing up to %d)\n' % (title, len(items), limit))
        for it in items[:limit]:
            out('  ' + fmt(it) + '\n')

    show('size mismatches', size_mismatch,
         lambda p: '%s t=%d  ->  %s t=%d' % (p[0].label(), p[0].tick,
                                              p[1].label(), p[1].tick))
    cnt = Counter((x[0].tag, x[0].size) for x in implied)
    out('  implied-receipt sends by (tag,size): %s\n' % dict(cnt))
    show('sends with no recv, no implied receipt', sorted(really_open,
                                      key=lambda m: m.tick),
         lambda m: '%s t=%d dest=(%d,%d)%04x' % (m.label(), m.tick,
                                                  m.dest[0][0],
                                                  m.dest[0][1], m.dest[1]))
    show('recvs with no send (sender tile logged)',
         sorted(orph_recv_real, key=lambda m: m.tick),
         lambda m: '%s t=%d <-%s' % (m.label(), m.tick, m.dir))

    if a.traces:
        out('\n-- traces (tick  tile slot  size  ->  ...)\n')
        for c in sorted(chains, key=lambda c: c[0].tick):
            parts = []
            for x in c:
                parts.append('%s[t%d]' % (x.label(), x.tick))
            line_ = '  ' + '  >  '.join(parts)
            if wps:
                last = c[-1]
                tgt = last.dest if last.is_send() else last.ep()
                nn = last.nonce
                if nn is not None:
                    ws = [w for w in wps if w.ep() == tgt and
                          w.nonce in (nn, (nn + 1) & 0xff) and
                          w.tick >= last.tick - a.eps]
                    for w in ws[:3]:
                        line_ += '  @{%d:%d %s}' % (w.fid, w.line, w.msg)
            out(line_ + '\n')

    if a.lanes:
        out('\n-- lane balance: sends (A:x->B:y), matched, and reverse\n')
        lane = defaultdict(lambda: [0, 0])
        for s in (m for m in marks if m.tag == 'ps'):
            key = (s.ep(), s.dest)
            lane[key][0] += 1
            if s in nxt:
                lane[key][1] += 1
        for key in sorted(lane):
            (st, sa), (dt_, da) = key
            out('  (%d,%d)%04x -> (%d,%d)%04x : sent=%d matched=%d\n' % (
                st[0], st[1], sa, dt_[0], dt_[1], da,
                lane[key][0], lane[key][1]))


if __name__ == '__main__':
    main()
