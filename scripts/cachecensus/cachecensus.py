#!/usr/bin/env python3
"""cachecensus -- per-neighbor cache occupancy of the hubs in a run.

See also /CLAUDE.md, srcs/host/tools/ERISC_SEAM_DESIGN.md, scripts/ptrace/README.txt.

usage: cachecensus.py RUNDIR [--seam CHIPA:DIR:CHIPB ...]

Reads the '.ac' marks (hub image, end of events: cache atoms per neighbor
NT:NW:WT:SW:ST:SE:ET:NE, hex) from RUNDIR/tiles/*.mfmk, maps tiles to chip
and CT6 through misc/PKTS.txt, and prints for each hub the LAST nonzero
sample.  A cache that stays 0 while its neighbors are alive is stale.

--seam marks the hubs on both sides of a seam (CHIPB lies DIR of CHIPA, DIR
one of N W S E; default 0:E:1, repeatable) and checks that their three
caches FACING the seam are nonzero (the centre corner cache of a hub on two
seam edges is shown but not judged): east edge NE/ET/SE, west edge NW/WT/SW,
south edge SW/ST/SE, north edge NW/NT/NE.  The CT6 grid is 14 x 10.
"""
import re, sys

NAMES = "NT NW WT SW ST SE ET NE".split()
W, H = 14, 10          # hubs sit at odd CT6 (1, 3 .. 13 x 1, 3 .. 9)
FACING = {'E': ('NE', 'ET', 'SE'), 'W': ('NW', 'WT', 'SW'),
          'S': ('SW', 'ST', 'SE'), 'N': ('NW', 'NT', 'NE')}
OPP = {'E': 'W', 'W': 'E', 'S': 'N', 'N': 'S'}


def on_edge(d, x, y):
    return {'E': x == W - 1, 'W': x == 1, 'S': y == H - 1, 'N': y == 1}[d]


def main(argv):
    d = argv[1]
    seams = []
    i = 2
    while i < len(argv):
        if argv[i] == '--seam' and i + 1 < len(argv):
            a, dr, b = argv[i + 1].split(':')
            seams.append((int(a), dr.upper(), int(b)))
            i += 2
        else:
            sys.exit(__doc__)
    if not seams:
        seams = [(0, 'E', 1)]
    # (chip, direction its seam faces)
    faces = {}
    for a, dr, b in seams:
        faces.setdefault(a, set()).add(dr)
        faces.setdefault(b, set()).add(OPP[dr])

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
            rows[(chip, tlbi % W, tlbi // W)] = (n, last)
    print('hub tiles with .ac marks: %d' % len(rows))
    print('%-5s %-9s %3s  %s' % ('chip', 'ct6', 'n',
                                 ' '.join('%5s' % x for x in NAMES)))
    good = bad = unseen = 0
    for (chip, x, y) in sorted(rows):
        n, v = rows[(chip, x, y)]
        seamdirs = [dr for dr in faces.get(chip, ()) if on_edge(dr, x, y)]
        note = ''
        if seamdirs:
            # a hub on two seam edges (a card corner of the 2x2 array) has
            # one CENTRE corner cache (e.g. SE for E+S): the diagonal across
            # the array centre, not wired until slice 3c, so not judged
            centre = set()
            if len(seamdirs) == 2:
                centre = set(FACING[seamdirs[0]]) & set(FACING[seamdirs[1]])
            if v is None:
                note = '  SEAM (no atoms yet)'
                unseen += 1
            else:
                missing = [c for dr in seamdirs for c in FACING[dr]
                           if v[NAMES.index(c)] == 0 and c not in centre]
                if missing:
                    note = '  SEAM STALE: ' + ','.join(missing)
                    bad += 1
                else:
                    note = '  SEAM ok'
                    good += 1
                for c in sorted(centre):
                    note += ' (centre %s=%d)' % (c, v[NAMES.index(c)])
        vs = ' '.join('%5d' % q for q in v) if v else '   (all zero)'
        print('%-5d (%2d,%d)    %3d  %s%s' % (chip, x, y, n, vs, note))
    print('seam hubs: %d ok, %d stale, %d without atoms yet'
          % (good, bad, unseen))


if __name__ == '__main__':
    main(sys.argv)
