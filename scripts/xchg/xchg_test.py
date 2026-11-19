#!/usr/bin/env python3
"""Self-test for xchg.py with a synthetic run.  Run:
    python3 scripts/xchg/xchg_test.py"""
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xchg

fails = 0


def check(name, cond, detail=''):
    global fails
    print(('PASS  ' if cond else 'FAIL  ') + name
          + ('' if cond else '   [%s]' % detail))
    if not cond:
        fails += 1


def hubfile(d, tag, i, phases, junk=True):
    """phases: list of (kind, start_ms, end_ms, packets)."""
    with open(d + '/tiles/%s.mfmk' % tag, 'w') as f:
        if junk:
            f.write('{32:1 5 0 2,3 hb .es:1:1:0}\n')
        for kind, b, e, p in phases:
            f.write('{32:178 %d 0 2,3 hb .xb:%x}\n' % (b, kind))
            f.write('{32:179 %d 0 2,3 hb .xe:%x:%x}\n' % (e, kind, p))


d = tempfile.mkdtemp(prefix='xchgtest-')
os.mkdir(d + '/misc')
os.mkdir(d + '/tiles')
with open(d + '/misc/PKTS.txt', 'w') as f:
    for i, tag in enumerate(('at0a', 'at0b', 'at0c')):
        f.write('TILE chip=0 tlbi=%d noc=2,3 tag=%s img=HUB\n' % (15 + i, tag))
    f.write('TILE chip=0 tlbi=99 noc=2,3 tag=ewp1 img=EWP\n')
# hub a: slow, 26 packets at 70 ms; b: 4 packets; c: lost one cycle too many
for tag, secs, pk in (('at0a', 1.82, 26), ('at0b', 0.28, 4)):
    ph = []
    for c in range(5):
        t0 = 1000 + 20000 * c
        ph.append((0, t0, t0 + int(secs * 1000), pk))
        ph.append((1, t0 + 5000, t0 + 5000 + int(secs * 1500), pk))
    hubfile(d, tag, 0, ph)
hubfile(d, 'at0c', 0, [(0, 1000, 1100, 0), (1, 6000, 6100, 0)])

per = {t: xchg.load_phases('%s/tiles/%s.mfmk' % (d, t))
       for t in xchg.hub_tags(d)}
check('three hubs, EWP ignored', sorted(per) == ['at0a', 'at0b', 'at0c'])
check('five SAVE cycles read', len(per['at0a'][0]) == 5)
check('SAVE time and packets', per['at0a'][0][0] == (1.82, 26), per['at0a'][0])
check('LOAD time', abs(per['at0a'][1][0][0] - 2.73) < 1e-9, per['at0a'][1][0])
res, dropped = xchg.analyze(per)
check('short hub dropped', dropped == ['at0c'], dropped)
s, ld = res
check('SAVE slowest is a', s['slow_by'][0] == ('at0a', 5), s['slow_by'])
check('SAVE slowest mean', abs(s['slow_mean'] - 1.82) < 1e-9, s['slow_mean'])
check('packets min/max', (s['pk_min'], s['pk_max']) == (4, 26),
      (s['pk_min'], s['pk_max']))
check('per packet median', abs(s['per_packet'] - (1.82 / 26 + 0.28 / 4) / 2)
      < 1e-6, s['per_packet'])
check('LOAD slowest mean', abs(ld['slow_mean'] - 2.73) < 1e-9)

out = []
bf = d + '/run.out'
with open(bf, 'w') as f:
    f.write('bolt #1 NEW_LEADER(2,1) CONSENSUS 0.0s\n'
            'bolt #2 RUN_EVENTS(1) CONSENSUS 3.0s\n'
            'bolt #3 NEW_LEADER(1,2) CONSENSUS 2.0s\n'
            'bolt #4 RUN_EVENTS(2) CONSENSUS 4.0s\n')
b = xchg.bolt_means(bf)
check('bolt means skip the 0.0 s one', b['NEW_LEADER'] == (2.0, 1), b)
xchg.report(res, dropped, b, out.append)
text = '\n'.join(out)
check('report names the phases', 'SAVE: 2 hubs, 5 cycles' in text, text)
check('report has the overhead line',
      'NEW_LEADER bolt (host) mean 2.00 s over 1' in text
      and '0.18 s is not the exchange' in text, text)
check('report says dropped', 'dropped (lost marks): at0c' in text, text)
none = []
xchg.report([None, None], [], None, none.append)
check('no marks is said', 'no .xb/.xe marks' in '\n'.join(none))

# --- heartbeat (.xw) parsing
hb = d + '/hb.mfmk'
with open(hb, 'w') as f:
    f.write('{32:150 5000 1 2,3 hb .xw:1:3e8:a:4:11111111:00020000}\n')
    f.write('{32:150 6000 1 2,3 hb .xw:1:7d0:a:4:11111111:00020000}\n')
last, n = xchg.load_stalls(hb)
check('xw: count', n == 2, n)
check('xw: last beat', last and last['ms'] == 2000 and last['kind'] == 1
      and last['need'] == 0xa and last['ldr'] == 4, last)
out = []
xchg.say_stalls({'at1c9': (last, n)}, out.append)
check('xw: report names waited dirs (0xa = NW,SW)', 'NW,SW' in ' '.join(out), out)
out = []
xchg.say_stalls({}, out.append)
check('xw: none', 'none' in out[0], out)
print('FAILURES: %d' % fails)
sys.exit(1 if fails else 0)
