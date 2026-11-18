#!/usr/bin/env python3
"""aer -- average event rate (AER) of a run, from the hub '.es' / '.ev' marks.

See also /CLAUDE.md, scripts/cachecensus/cachecensus.py.

usage: aer.py RUNDIR [--series SECS] [--window SECS ...] [--sites N]
              [--from-start] [-v]

AER is the average number of events per grid site per second, averaged over
the whole simulation; it is usually quoted in milli-AER (mAER).

Each hub logs two marks per supercycle (hub image, PhaseSteppers.cpp):
  .es:C:R:O   events START running   (hex, cumulative since boot)
  .ev:C:R:O   events STOP  running
where C = events committed (an event happened), R = events returned from
the event windows and tried (C + obsoleted), O = non-empty events offered.
The header of every mark carries the hub's own clock in ms, so all times
here are DIFFERENCES within one hub (hub clocks are not synchronized).  The
k-th cycle of every hub is lined up by mark number; a hub with a different
number of marks than most is reported and left out.

Per cycle: events = sum over hubs of C at this stop minus C at the previous
stop (nothing commits between a stop and the next start; the first cycle
starts at its own .es), wall time = median over hubs of
t(ev_k) - t(ev_k-1) (the first cycle: its running time), events-running time = median of t(ev_k) - t(es_k).

The averaging window starts at the AUTOSEED, when the sim first has an atom
to run events on: the hub that seeds logs '.as:TYPE' (its start symbol type)
with its own clock.  Cycles before the one containing that mark are left
out, and that cycle counts only from the mark on.  (Before it nothing can
commit.)  With no '.as' mark, or with --from-start, the window starts at the
first '.es', i.e. when events first start running.

Reported:
  wall AER        events / (sites * wall seconds)       what you get overall
  running AER     events / (sites * events-running s)   the physics speed
  duty            running / wall   (the rest is bolts, cache load and save)
  obsolete        fraction of returned events that were dropped
  trailing        the same over the last N seconds, at the end of the run
  --series S      total-so-far and trailing-S figures every S seconds
  --window W      trailing windows to print (default 60 and 300 s)
  --sites N       override the site count (default: hubs * 138 * 108, the
                  sites of the hubs' own regions)
  --from-start    start the window at the first '.es', not at the autoseed
"""
import re
import statistics
import sys

SITES_PER_HUB = 138 * 108   # T6GRID_BASE_WIDTH * T6GRID_BASE_HEIGHT
U32 = 1 << 32

MARK = re.compile(r'\{[^ ]+ (\d+) \d+ [0-9,]+ \w+ \.(es|ev):'
                  r'([0-9a-f]+):([0-9a-f]+):([0-9a-f]+)\}')


SEED = re.compile(r'\{[^ ]+ (\d+) \d+ [0-9,]+ \w+ \.as:([0-9a-f]+)\}')


def hub_tiles(rundir):
    """tag -> (chip, tlbi) of the HUB tiles, from misc/PKTS.txt."""
    tiles = {}
    for line in open(rundir + '/misc/PKTS.txt', errors='replace'):
        m = re.match(r'TILE chip=(\d+) tlbi=(\d+) noc=\d+,\d+ tag=(\w+) '
                     r'img=(\w+)', line)
        if m and m.group(4) == 'HUB':
            tiles[m.group(3)] = (int(m.group(1)), int(m.group(2)))
    return tiles


def load_marks(path):
    """The list of (kind, ms, committed, returned, offered) in file order;
    kind is 'es', 'ev', or 'as' (then committed holds the seed type)."""
    out = []
    for line in open(path, errors='replace'):
        m = MARK.search(line)
        if m:
            out.append((m.group(2), int(m.group(1)), int(m.group(3), 16),
                        int(m.group(4), 16), int(m.group(5), 16)))
            continue
        m = SEED.search(line)
        if m:
            out.append(('as', int(m.group(1)), int(m.group(2), 16), 0, 0))
    return out


def cycles_of(marks):
    """[(es_ms, ev_ms, es_counts, ev_counts)] for each es followed by ev."""
    out = []
    es = None
    for kind, ms, c, r, o in marks:
        if kind == 'es':
            es = (ms, (c, r, o))
        elif kind == 'ev' and es is not None:
            out.append((es[0], ms, es[1], (c, r, o)))
            es = None
    return out


def seed_of(marks):
    """(cycle index, seconds into that cycle's events, seed type) of the
    first '.as' mark, or None."""
    k = -1
    es_ms = None
    for kind, ms, c, r, o in marks:
        if kind == 'es':
            k += 1
            es_ms = ms
        elif kind == 'as':
            if es_ms is None:
                return (0, 0.0, c)
            return (max(k, 0), (ms - es_ms) % U32 / 1000.0, c)
    return None


def delta(a, b):
    return (b - a) % U32


def analyze(per_hub, seed=None):
    """per_hub: {name: cycles_of(...)}.  seed: seed_of() of the seeding hub
    (the window then starts there) or None.  Returns a dict of results."""
    counts = {n: len(c) for n, c in per_hub.items()}
    if not counts:
        raise ValueError('no hub has any .es/.ev marks (an older hub image?)')
    n = statistics.mode(counts.values())
    kept = {h: c[:n] for h, c in per_hub.items() if len(c) == n}
    dropped = sorted(h for h in per_hub if h not in kept)
    nh = len(kept)
    cyc = []
    k0 = min(seed[0], n - 1) if seed else 0
    for k in range(k0, n):
        ev = [0, 0, 0]
        wall, run = [], []
        for c in kept.values():
            cur = c[k]
            # events since the previous stop (nothing commits between a
            # stop and the next start); the first cycle starts at its .es
            first = k == k0
            base = cur[2] if first else c[k - 1][3]
            for i in range(3):
                ev[i] += delta(base[i], cur[3][i])
            wall.append(delta(cur[0] if first else c[k - 1][1], cur[1])
                        / 1000.0)
            run.append(delta(cur[0], cur[1]) / 1000.0)
        if k == k0 and seed:    # the window starts at the seed, not the .es
            off = seed[1]
            wall = [max(w - off, 0.001) for w in wall]
            run = [max(r - off, 0.001) for r in run]
        cyc.append({'committed': ev[0], 'returned': ev[1], 'offered': ev[2],
                    'wall': statistics.median(wall),
                    'run': statistics.median(run)})
    per_hub_total = {h: delta(c[k0][2][0], c[n - 1][3][0])
                     for h, c in kept.items()}
    return {'hubs': nh, 'dropped': dropped, 'cycles': cyc, 'seed': seed,
            'per_hub': per_hub_total}


def rates(cyc, sites):
    """(wall mAER, running mAER, duty, obsolete fraction) of a cycle list."""
    ev = sum(c['committed'] for c in cyc)
    ret = sum(c['returned'] for c in cyc)
    wall = sum(c['wall'] for c in cyc)
    run = sum(c['run'] for c in cyc)
    if wall <= 0 or run <= 0:
        return (0.0, 0.0, 0.0, 0.0)
    return (1000.0 * ev / (sites * wall), 1000.0 * ev / (sites * run),
            run / wall, (1.0 - ev / ret) if ret else 0.0)


def trailing(cyc, secs):
    """The newest cycles that add up to at least secs of wall time."""
    out, t = [], 0.0
    for c in reversed(cyc):
        out.append(c)
        t += c['wall']
        if t >= secs:
            break
    return list(reversed(out))


def fmt(r):
    return ('wall %8.2f mAER   running %8.2f mAER   duty %3.0f%%   '
            'obsolete %4.1f%%' % (r[0], r[1], 100 * r[2], 100 * r[3]))


def report(res, sites, windows, series, verbose, say=print):
    cyc = res['cycles']
    say('hubs %d (sites %d)  cycles %d  wall %.0f s  running %.0f s'
        % (res['hubs'], sites, len(cyc), sum(c['wall'] for c in cyc),
           sum(c['run'] for c in cyc)))
    sd = res.get('seed')
    if sd:
        say('window starts at the autoseed (type %d, cycle %d, %.1f s into '
            'its events)' % (sd[2], sd[0] + 1, sd[1]))
    else:
        say('window starts at the first events start (no autoseed mark)')
    if res['dropped']:
        say('left out (different number of marks): %s'
            % ' '.join(res['dropped']))
    if not cyc:
        say('fewer than two cycles: no rates')
        return
    say('total          ' + fmt(rates(cyc, sites)))
    for w in windows:
        say('last %4d s    ' % w + fmt(rates(trailing(cyc, w), sites)))
    if series:
        say('series every %d s of wall time (total so far | trailing):'
            % series)
        t, mark, upto = 0.0, series, 0
        for i, c in enumerate(cyc):
            t += c['wall']
            if t >= mark or i == len(cyc) - 1:
                so_far = cyc[:i + 1]
                say('  t=%6.0f  total %8.2f / running %8.2f  |  trailing %8.2f mAER'
                    % (t, rates(so_far, sites)[0], rates(so_far, sites)[1],
                       rates(trailing(so_far, series), sites)[0]))
                mark += series
    ph = sorted(res['per_hub'].values())
    mean = statistics.mean(ph)
    say('events per hub: min %d  mean %.0f  max %d  (spread %.1f%%)'
        % (ph[0], mean, ph[-1],
           100 * statistics.pstdev(ph) / mean if mean else 0.0))
    if verbose:
        for i, c in enumerate(cyc, 1):
            say('  cycle %3d: committed %9d  returned %9d  wall %5.1f s  '
                'running %5.1f s' % (i, c['committed'], c['returned'],
                                     c['wall'], c['run']))


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        sys.exit(__doc__)
    rundir = argv[1]
    series, windows, sites, verbose, from_start = 0, [], None, False, False
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == '--series' and i + 1 < len(argv):
            series = int(argv[i + 1])
            i += 2
        elif a == '--window' and i + 1 < len(argv):
            windows.append(int(argv[i + 1]))
            i += 2
        elif a == '--sites' and i + 1 < len(argv):
            sites = int(argv[i + 1])
            i += 2
        elif a == '-v':
            verbose = True
            i += 1
        elif a == '--from-start':
            from_start = True
            i += 1
        else:
            sys.exit(__doc__)
    windows = windows or [60, 300]
    per_hub = {}
    seed = None
    for tag in hub_tiles(rundir):
        try:
            marks = load_marks('%s/tiles/%s.mfmk' % (rundir, tag))
        except OSError:
            continue
        c = cycles_of(marks)
        if c:
            per_hub[tag] = c
        sd = seed_of(marks)
        if sd and (seed is None or sd[0] < seed[0]):
            seed = sd
    res = analyze(per_hub, None if from_start else seed)
    report(res, sites or res['hubs'] * SITES_PER_HUB, windows, series,
           verbose)


if __name__ == '__main__':
    main(sys.argv)
