#!/usr/bin/env python3
"""xchg -- cache exchange timing of a run, from the hub '.xb' / '.xe' marks.

See also /CLAUDE.md, /OPEN_ISSUES.md (four-corners design, measurement step),
scripts/aer/aer.py.

usage: xchg.py RUNDIR [--bolts OUTFILE] [-v]

Each hub logs (hub image, PhaseSteppers.cpp, xchgStep) around each cache
exchange phase of every supercycle:
  .xb:K        first call of the phase     K = 0 SAVE (inside NEW_LEADER)
  .xe:K:P      the hub is ready to ack                 1 LOAD (inside RUN_EVENTS)
where P (hex) is the number of packets this hub loaded or unloaded in the
phase.  The marks carry the hub's own clock in ms, so a phase's time is
xe - xb within one hub (hub clocks are not synchronized).  Hubs finish at
different moments and a bolt only completes when the SLOWEST hub is ready.

Reported for each phase:
  hub time      over all hub-cycles: median, mean, max (s)
  slowest       per cycle the slowest hub's time: mean, min, max (s), and
                which hubs are slowest most often
  packets       per hub-cycle: min, mean, max
  per packet    hub time / packets, for hub-cycles that moved packets: median
                (s).  If every packet costs a fixed busy-wait this is it.
  --bolts FILE  the mfmxctl output (seam4-NN.out): mean host bolt time
                (NEW_LEADER, RUN_EVENTS) next to the mean slowest-hub time;
                the difference is what is not the exchange itself (host
                polling, ack propagation, census, configure).
"""
import re
import statistics
import sys

SLACK = 2                   # cycles a hub may be short before it is dropped
TILE = re.compile(r'TILE chip=(\d+) tlbi=(\d+) noc=\d+,\d+ tag=(\w+) '
                  r'img=(\w+)')
XB = re.compile(r'\{[^ ]+ (\d+) \d+ [0-9,]+ \w+ \.xb:([0-9a-f]+)\}')
XE = re.compile(r'\{[^ ]+ (\d+) \d+ [0-9,]+ \w+ \.xe:([0-9a-f]+):([0-9a-f]+)\}')
BOLT = re.compile(r'bolt #\d+ (NEW_LEADER|RUN_EVENTS)\(.*\) CONSENSUS '
                  r'([0-9.]+)s')
NAMES = ('SAVE', 'LOAD')


def hub_tags(rundir):
    tags = []
    for line in open(rundir + '/misc/PKTS.txt', errors='replace'):
        m = TILE.match(line)
        if m and m.group(4) == 'HUB':
            tags.append(m.group(3))
    return tags


def load_phases(path):
    """([(secs, packets)] for SAVE, same for LOAD), from one hub's marks."""
    out = ([], [])
    began = [None, None]
    for line in open(path, errors='replace'):
        m = XB.search(line)
        if m:
            began[int(m.group(2), 16) & 1] = int(m.group(1))
            continue
        m = XE.search(line)
        if m:
            k = int(m.group(2), 16) & 1
            if began[k] is not None:
                out[k].append(((int(m.group(1)) - began[k]) / 1000.0,
                               int(m.group(3), 16)))
                began[k] = None
    return out


def analyze(per_hub):
    """per_hub: tag -> (save, load) lists.  Returns, for each phase, a dict
    of the statistics (None for a phase without data), and the dropped."""
    res = []
    dropped = []
    for k in (0, 1):
        n = max((len(v[k]) for v in per_hub.values()), default=0)
        keep = {t: v[k] for t, v in per_hub.items() if len(v[k]) >= n - SLACK}
        if k == 0:
            dropped = sorted(set(per_hub) - set(keep))
        ncyc = min((len(v) for v in keep.values()), default=0)
        if ncyc == 0:
            res.append(None)
            continue
        times, packs, perp, slowest, slow_by = [], [], [], [], {}
        for c in range(ncyc):
            best = (-1.0, None)
            for t, v in keep.items():
                s, p = v[c]
                times.append(s)
                packs.append(p)
                if p > 0:
                    perp.append(s / p)
                if s > best[0]:
                    best = (s, t)
            slowest.append(best[0])
            slow_by[best[1]] = slow_by.get(best[1], 0) + 1
        res.append({
            'hubs': len(keep), 'cycles': ncyc,
            'med': statistics.median(times), 'mean': statistics.mean(times),
            'max': max(times),
            'slow_mean': statistics.mean(slowest), 'slow_min': min(slowest),
            'slow_max': max(slowest),
            'pk_min': min(packs), 'pk_mean': statistics.mean(packs),
            'pk_max': max(packs),
            'per_packet': statistics.median(perp) if perp else None,
            'slow_by': sorted(slow_by.items(), key=lambda x: -x[1])[:5],
        })
    return res, dropped


def bolt_means(path):
    d = {'NEW_LEADER': [], 'RUN_EVENTS': []}
    for line in open(path, errors='replace'):
        m = BOLT.match(line)
        if m and float(m.group(2)) > 0:
            d[m.group(1)].append(float(m.group(2)))
    return {k: (statistics.mean(v), len(v)) for k, v in d.items() if v}


def report(res, dropped, bolts=None, say=print):
    for k, r in enumerate(res):
        if r is None:
            say('%s: no .xb/.xe marks (hub image too old?)' % NAMES[k])
            continue
        say('%s: %d hubs, %d cycles' % (NAMES[k], r['hubs'], r['cycles']))
        say('  hub time   median %.3f  mean %.3f  max %.3f s'
            % (r['med'], r['mean'], r['max']))
        say('  slowest    mean %.3f  min %.3f  max %.3f s per cycle'
            % (r['slow_mean'], r['slow_min'], r['slow_max']))
        say('  packets    min %d  mean %.1f  max %d per hub-cycle'
            % (r['pk_min'], r['pk_mean'], r['pk_max']))
        if r['per_packet'] is not None:
            say('  per packet median %.1f ms' % (1000.0 * r['per_packet']))
        say('  slowest hub most often: '
            + ', '.join('%s x%d' % x for x in r['slow_by']))
    if dropped:
        say('dropped (lost marks): ' + ' '.join(dropped))
    if bolts:
        for k, bname in ((0, 'NEW_LEADER'), (1, 'RUN_EVENTS')):
            if bname in bolts and res[k]:
                mean, n = bolts[bname]
                say('%s bolt (host) mean %.2f s over %d  vs slowest hub %.2f'
                    ' s: %.2f s is not the exchange'
                    % (bname, mean, n, res[k]['slow_mean'],
                       mean - res[k]['slow_mean']))


def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        sys.exit(__doc__)
    rundir = argv[1]
    bolts = None
    i = 2
    while i < len(argv):
        if argv[i] == '--bolts' and i + 1 < len(argv):
            bolts = bolt_means(argv[i + 1])
            i += 2
        elif argv[i] == '-v':
            i += 1
        else:
            sys.exit(__doc__)
    per_hub = {}
    for tag in hub_tags(rundir):
        try:
            per_hub[tag] = load_phases('%s/tiles/%s.mfmk' % (rundir, tag))
        except OSError:
            continue
    res, dropped = analyze(per_hub)
    report(res, dropped, bolts)


if __name__ == '__main__':
    main(sys.argv)
