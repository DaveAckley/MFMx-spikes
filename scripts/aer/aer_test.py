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
             obsolete=0.25, skew=0, short=(), seed_cycle=None, seed_at=0.0,
             live=None):
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
            if seed_cycle is not None and k < seed_cycle:
                n = 0                 # nothing runs before the seed
            elif seed_cycle is not None and k == seed_cycle:
                n = int(rate * (run_s - seed_at))
                if i == 0:            # hub 0 seeds, logging its own clock
                    lines.append('{27:80 %d 0 2,3 hb .as:4}\n'
                                 % (t + int(seed_at * 1000)))
            else:
                n = int(rate * run_s)
            c += n
            t += int(run_s * 1000)
            if live is not None:      # '.ae:TOTAL:INTERIOR:BAND:CACHE'
                lines.append('{32:176 %d 0 2,3 hb .ae:%x:%x:%x:0}\n'
                             % (t, live + 50, live - 100, 100))
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

# the run ends at different moments on different hubs: one cycle short is
# fine (everybody is cut to the common cycles); three short is a lost-marks
# hub and is dropped
d = make_run(4, 6, 100, 6, 4, short=(2,))
res = analyze_dir(d)
check('hub one cycle short is kept, cycles cut to the common ones',
      res['dropped'] == [] and res['hubs'] == 4 and len(res['cycles']) == 5,
      str((res['dropped'], res['hubs'], len(res['cycles']))))
d = make_run(4, 8, 100, 6, 4)
p3 = '%s/tiles/at02.mfmk' % d
lines = open(p3).read().split('\n')
keep = [l for l in lines if not l.startswith('{') or
        any(f' {t} ' in l for t in range(0, 1)) or True]
body = [l for l in lines if l.startswith('{')]
open(p3, 'w').write('---1.0---\n' + '\n'.join(body[:-6]) + '\n')   # 3 cycles
res = analyze_dir(d)
check('hub three cycles short is dropped',
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

# the window starts at the autoseed: 10 cycles, seed 2 s into cycle 3's 6 s
d = make_run(4, 10, 100, 6, 4, seed_cycle=3, seed_at=2.0)
per, seed = {}, None
for tag in aer.hub_tiles(d):
    m = aer.load_marks('%s/tiles/%s.mfmk' % (d, tag))
    per[tag] = aer.cycles_of(m)
    seed = seed or aer.seed_of(m)
check('seed found', seed == (3, 2.0, 4), str(seed))
res = aer.analyze(per, seed)
cy = res['cycles']
check('seed window drops earlier cycles', len(cy) == 7, str(len(cy)))
check('first window cycle counts only after the seed',
      abs(cy[0]['wall'] - 4.0) < 1e-9 and abs(cy[0]['run'] - 4.0) < 1e-9
      and cy[0]['committed'] == 4 * 100 * 4, str(cy[0]))
w, r, duty, obs = aer.rates(cy, sites)
ev = 4 * 100 * (4 + 6 * 6)
wall = 4 + 6 * 10
check('seeded wall mAER', abs(w - 1000.0 * ev / (sites * wall)) < 1e-6, str(w))
check('seeded running mAER', abs(r - 1000.0 * ev / (sites * 40)) < 1e-6,
      str(r))
res0 = aer.analyze(per, None)
check('--from-start keeps every cycle', len(res0['cycles']) == 10,
      str(len(res0['cycles'])))
check('no seed mark -> None', aer.seed_of(aer.load_marks(
    '%s/tiles/at01.mfmk' % d)) is None)

# per live atom: 4 hubs with 1000 live self atoms each (interior 900 + band
# 100 + cache 50 in the TOTAL), 6 s of events at 100 events/s/hub
d = make_run(4, 5, 100, 6, 4, live=1000)
res = analyze_dir(d)
pa = aer.per_atom(res['cycles'], sites)
check('fill', pa is not None and abs(pa[0] - 4 * 1000 / sites) < 1e-9,
      str(pa))
check('per live atom mAER', abs(pa[1] - 1000.0 * 100 / 1000) < 1e-6, str(pa))
check('no census -> no per-atom figure', aer.per_atom(
    analyze_dir(make_run(4, 5, 100, 6, 4))['cycles'], sites) is None)

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
