#!/usr/bin/env python3
"""Self-test for host/python/EWD/LiveAER.py.  Run:
    python3 scripts/aer/liveaer_test.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'srcs', 'host', 'python',
                                'EWD'))
import LiveAER as L

fails = 0


def check(name, cond, detail=''):
    global fails
    print(('PASS  ' if cond else 'FAIL  ') + name
          + ('' if cond else '   [%s]' % detail))
    if not cond:
        fails += 1


S = L.SITES_PER_HUB
state = {'seeded': False, 'hubs': []}
t = [0.0]
aer = L.LiveAER(lambda: (state['seeded'], list(state['hubs'])),
                clock=lambda: t[0], gap=0.0)

check('no hubs: empty text', (aer.update(), aer.text())[1] == '')

# two hubs, each doing 1000 mAER (= S events per second per hub), hub clocks offset from each other; a mark every 6 s of hub time
def run(secs, rate_hz, hubs=((1, 5000), (2, 900000)), step=6):
    for _ in range(secs // step):
        t[0] += step
        state['hubs'] = [(k, ev(k, rate_hz), ms(k)) for k, _ in hubs]
        aer.update()

clk = {1: 5000, 2: 900000}
evs = {1: 0, 2: 0}
def ms(k):
    clk[k] += 6000
    return clk[k]
def ev(k, rate_hz):
    evs[k] += int(rate_hz * 6)
    return evs[k]

per_s = float(S)                       # AER 1.0 = 1000 mAER per hub
run(30, per_s)
run(90, per_s)
m1 = aer.window_maer(60)
check('1 minute figure ~1000 mAER', m1 and abs(m1 - 1000) < 15, m1)
check('5 minute window still filling -> None', aer.window_maer(300) is None,
      aer.window_maer(300))
check('before the seed: no seed figure', aer.seed_maer() is None)
state['seeded'] = True
run(60, per_s * 2)                     # rate doubles after the seed
check('seed figure ~2000 mAER', abs(aer.seed_maer() - 2000) < 30,
      aer.seed_maer())
check('1 minute now ~2000', abs(aer.window_maer(60) - 2000) < 30,
      aer.window_maer(60))
run(300, per_s * 2)
check('5 minute figure ~2000', abs(aer.window_maer(300) - 2000) < 30,
      aer.window_maer(300))
txt = aer.text()
check('text has the figures', txt == '[2] est. AER: 2.00, 2.00, 2.00', txt)
f = L.LiveAER.fmt_aer
check('three significant digits',
      [f(x) for x in (123456, 12345, 1234, 123.4, None)]
      == ['123', '12.3', '1.23', '0.123', '--'], [f(x) for x in (123456, 12345, 1234, 123.4)])

# a hub whose clock goes back (restart) is not poisoned
clk[1] = 100
evs[1] = 0
run(30, per_s * 2)
check('hub restart: still ~2000', abs(aer.window_maer(60) - 2000) < 100,
      aer.window_maer(60))

# a silent hub drops out of the mean and the hub count
state['hubs'] = []
t[0] += 200
check('silent hubs dropped', aer.hubs() == 0 and aer.text() == '', aer.text())

# torn marks among many hubs: one low, one absurdly high; figure stays put
state = {'seeded': True, 'hubs': []}
t = [0.0]
aer = L.LiveAER(lambda: (state['seeded'], list(state['hubs'])),
                clock=lambda: t[0], gap=0.0)
NH = 40
evs = {k: 0 for k in range(NH)}
clk = {k: 1000 * k for k in range(NH)}
for step in range(80):                    # 80 marks x 6 s = 480 s
    t[0] += 6
    snap = []
    for k in range(NH):
        evs[k] += int(S * 6)              # 1000 mAER each
        clk[k] += 6000
        v = evs[k]
        if step in (40, 41) and k == 3:
            v = 7                         # torn low
        if step == 50 and k == 5:
            v = evs[k] * 1000             # torn high
        snap.append((k, v, clk[k]))
    state['hubs'] = snap
    aer.update()
w1 = aer.window_maer(60)
check('torn marks among 40 hubs: 1m figure unmoved',
      w1 and abs(w1 - 1000) < 30, w1)
check('torn marks among 40 hubs: all figure unmoved',
      abs(aer.seed_maer() - 1000) < 30, aer.seed_maer())
print('FAILURES: %d' % fails)
sys.exit(1 if fails else 0)
