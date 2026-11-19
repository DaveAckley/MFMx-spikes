#!/usr/bin/env python3
"""Self-test of the RTMPFeed keepalive pacing (repeatBase).  Run:
    python3 scripts/feed/keepalive_test.py

Simulates the keepAlive loop on a fake clock (poll every 0.2 period, a
repeat takes 70 ms to render and write) and counts repeats per 30 s: the old
rule (next due = actual start + period) against the new one (repeatBase)."""
import os
import sys
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
EWD = os.path.join(HERE, '..', '..', 'srcs', 'host', 'python', 'EWD')
sys.path.insert(0, EWD)
for name in ('cv2', 'numpy', 'PIL', 'PIL.Image', 'PIL.ImageDraw',
             'PIL.ImageFont'):
    try:
        __import__(name)
    except ImportError:
        sys.modules[name] = mock.MagicMock()
import RTMPFeed13

FPS = 5.1
PERIOD = 1.0 / FPS
RENDER = 0.070
fails = 0


def check(name, cond, detail=''):
    global fails
    print(('PASS  ' if cond else 'FAIL  ') + name
          + ('' if cond else '   [%s]' % detail))
    if not cond:
        fails += 1


def simulate(new, secs=30.0):
    """Repeats in secs of idle, from the last real frame at t=0."""
    t, writeT = 0.0, 0.0
    base, dupEnd, n = 0.0, -1.0, 0
    while t < secs:
        t += 0.2 * PERIOD                    # the loop's sleep
        if writeT == dupEnd:
            due = base + PERIOD
        else:
            due = writeT + 1.3 * PERIOD
        if t >= due:
            base = RTMPFeed13.repeatBase(t, due, PERIOD) if new else t
            t += RENDER                      # render and write
            writeT = t
            dupEnd = writeT
            n += 1
    return n


old, new = simulate(False), simulate(True)
print('repeats per 30 s: old %d  new %d  (target %.0f)' % (old, new, 30 * FPS))
check('old rule is slow (about 134 per 30 s, as measured)',
      130 <= old <= 140, old)
check('new rule holds the frame rate (within 2 percent of 153)',
      abs(new - 30 * FPS) <= 0.02 * 30 * FPS, new)
check('repeatBase: slightly late counts from due',
      abs(RTMPFeed13.repeatBase(1.05, 1.0, PERIOD) - 1.0) < 1e-9)
check('repeatBase: more than a period behind resyncs to now',
      RTMPFeed13.repeatBase(5.0, 1.0, PERIOD) == 5.0)
print('FAILURES: %d' % fails)
sys.exit(1 if fails else 0)
