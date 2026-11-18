#!/usr/bin/env python3
"""Self-test for aer.py: builds a small synthetic run directory with known
event counts and times and checks the rates it reports.  Run:
    python3 scripts/aer/aer_test.py"""
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import aer

fails = 0


def check(name, cond, detail=''):
    global fails
    print(('PASS  ' if cond else 'FAIL  ') + name
          + ('' if cond else '   [%s]' % detail))
    if not cond:
        fails += 1


def mark(kind, ms, c, r, o):
    return '{32:178 %d 0 2,3 hb .%s:%x:%x:%x}\n' % (ms, kind, c, r, o)


def make_run(hubs, cycles, rate, run_s, over_s, start_ms=1000,
             obsolete=0.25, skew=0, short=()):
    """Every hub commits `rate` events/s while running.  A cycle runs
    events for run_s seconds, then spends over_s seconds in bolts."""
    d = tempfile.mkdtemp(prefix='aertest-')
    os.mkdir(d + '/misc')
    os.mkdir(d + '/tiles')
    with open(d + '/misc/PKTS.txt', 'w') as f:
        for i in range(hubs):
            f.write('TILE chip=0 tlbi=%d noc=2,3 tag=at0%d img=HUB\n'
                    % (15 + 2 * i, i))
        f.write('TILE chip=0 tlbi=0 noc=1,2 tag=at012 img=EWP\n')
    for i in range(hubs):
        t = start_ms + i * skew       # unsynchronized hub clocks
        c = 77 * (i + 1)              # arbitrary starting counters
        lines = []
        for k in range(cycles - (1 if i in short else 0)):
            r_start = c
            lines.append(mark('es', t, c, int(c / (1 - obsolete)), c))
            n = int(rate * run_s)
            c += n
            t += int(run_s * 1000)
            lines.append(mark('ev', t, c, int(c / (1 - obsolete)), c))
            t += int(over_s * 1000)
        with open(d + '/tiles/at0%d.mfmk' % i, 'w') as f:
            f.write('---1.0---\n' + ''.join(lines))
    return d


def analyze_dir(d):
    per = {}
    for tag in aer.hub_tiles(d):
        c = aer.cycles_of(aer.load_marks('%s/tiles/%s.mfmk' % (d, tag)))
        if c:
            per[tag] = c
    return aer.analyze(per)


# 4 hubs, 10 cycles, 100 events/s/hub, 6 s running + 4 s overhead per cycle
sites = 4 * aer.SITES_PER_HUB
d = make_run(4, 10, 100, 6, 4, skew=777)
res = analyze_dir(d)
check('hubs and cycles', res['hubs'] == 4 and len(res['cycles']) == 10,
      str((res['hubs'], len(res['cycles']))))
w, r, duty, obs = aer.rates(res['cycles'], sites)
# first cycle has no overhead before it: 9 cycles of 10 s + one of 6 s
wall = 9 * 10 + 6
ev = 4 * 100 * 6 * 10
check('wall mAER', abs(w - 1000.0 * ev / (sites * wall)) < 1e-6, str(w))
check('running mAER', abs(r - 1000.0 * ev / (sites * 60)) < 1e-6, str(r))
check('duty', abs(duty - 60.0 / wall) < 1e-9, str(duty))
check('obsolete fraction', abs(obs - 0.25) < 0.01, str(obs))
check('per-hub spread', len(res['per_hub']) == 4 and
      all(v == 100 * 6 * 10 for v in res['per_hub'].values()),
      str(res['per_hub']))
tr = aer.trailing(res['cycles'], 30)
check('trailing window is the newest cycles', len(tr) == 3, str(len(tr)))
w30 = aer.rates(tr, sites)[0]
check('trailing wall mAER', abs(w30 - 1000.0 * 4 * 600 * 3 / (sites * 30))
      < 1e-6, str(w30))

# counter wrap (u32) in the middle of a run
d = make_run(2, 4, 100, 6, 4, start_ms=1000)
for i in range(2):
    p = '%s/tiles/at0%d.mfmk' % (d, i)
    txt = open(p).read().replace('.es:', '.es:').split('\n')
    out = []
    for line in txt:
        m = aer.MARK.search(line)
        if m:
            c, rr, o = (int(m.group(g), 16) for g in (3, 4, 5))
            c = (c + 0xFFFFFF00) % (1 << 32)
            line = mark(m.group(2), int(m.group(1)), c, rr, o).rstrip()
        out.append(line)
    open(p, 'w').write('\n'.join(out) + '\n')
res = analyze_dir(d)
check('u32 wrap handled', sum(c['committed'] for c in res['cycles'])
      == 2 * 600 * 4, str([c['committed'] for c in res['cycles']]))

# a hub with a missing cycle is reported and left out
d = make_run(4, 6, 100, 6, 4, short=(2,))
res = analyze_dir(d)
check('hub with fewer marks is dropped',
      res['dropped'] == ['at02'] and res['hubs'] == 3, str(res['dropped']))

# no marks at all: a clear error
d = make_run(2, 3, 100, 6, 4)
for i in range(2):
    open('%s/tiles/at0%d.mfmk' % (d, i), 'w').write('nothing here\n')
try:
    analyze_dir(d)
    check('no marks -> error', False)
except ValueError as e:
    check('no marks -> error', 'older hub image' in str(e))

# the report prints without error and mentions the pieces
d = make_run(4, 10, 100, 6, 4)
out = io.StringIO()
aer.report(analyze_dir(d), sites, [60, 300], 30, True,
           say=lambda s: out.write(s + '\n'))
txt = out.getvalue()
check('report text', 'mAER' in txt and 'duty' in txt and 'series every 30'
      in txt and 'events per hub' in txt, txt)

print('all ok' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
