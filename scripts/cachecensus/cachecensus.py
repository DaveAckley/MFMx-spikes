#!/usr/bin/env python3
"""cachecensus -- per-neighbor cache occupancy of the hubs in a run.

usage: cachecensus.py RUNDIR [--seam-x CT6X0 CT6X1]

Reads the '.ac' marks (hub image, end of events: cache atoms per neighbor
NT:NW:WT:SW:ST:SE:ET:NE, hex) from RUNDIR/tiles/*.mfmk, maps tiles to chip
and CT6 through misc/PKTS.txt, and prints for each hub the LAST nonzero
sample.  A cache that stays 0 while its neighbors are alive is stale: for a
seam hub that means the cross-seam loader (Dir4 or diagonal) is not filling
it.  Hubs on the card edges facing a seam are marked (default: chip 0 east
edge ct6 x=13, chip 1 west edge x=1; CT6 grid is 14 x 10).
"""
import re, sys, collections

NAMES = "NT NW WT SW ST SE ET NE".split()

def main(argv):
    d = argv[1]
    tiles = {}
    for l in open(d + '/misc/PKTS.txt'):
        m = re.match(r'TILE chip=(\d+) tlbi=(\d+) noc=\d+,\d+ tag=(\w+) '
                     r'img=(\w+)', l)
        if m and m.group(4) == 'HUB':
            tiles[m.group(3)] = (int(m.group(1)), int(m.group(2)))
    rows = {}
    for tag, (chip, tlbi) in tiles.items():
        try:
            f = open('%s/tiles/%s.mfmk' % (d, tag))
        except OSError:
            continue
        last = None
        n = 0
        for l in f:
            m = re.search(r'\.ac:((?:[0-9a-f]+:){7}[0-9a-f]+)\}', l)
            if m:
                v = [int(x, 16) for x in m.group(1).split(':')]
                n += 1
                if sum(v):
                    last = v
        if n:
            rows[(chip, tlbi % 14, tlbi // 14)] = (n, last)
    print('hub tiles with .ac marks: %d' % len(rows))
    print('%-5s %-9s %3s  %s' % ('chip', 'ct6', 'n',
                                 ' '.join('%5s' % x for x in NAMES)))
    for (chip, x, y) in sorted(rows):
        n, v = rows[(chip, x, y)]
        seam = (chip == 0 and x == 13) or (chip == 1 and x == 1)
        edge = x in (0, 13) or y in (0, 9)
        vs = ' '.join('%5d' % q for q in v) if v else '   (all zero)'
        print('%-5d (%2d,%d)    %3d  %s%s' % (chip, x, y, n, vs,
              '  SEAM' if seam else ('  edge' if edge else '')))

if __name__ == '__main__':
    main(sys.argv)
