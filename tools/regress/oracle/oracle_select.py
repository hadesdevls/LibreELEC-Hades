#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Choose the pictures of a clip to draw (from rpu_groups.py's files <prefix>-frames.txt and
<prefix>-sets.jsonl):

  pics  the first picture of every distinct composer set, and <spread> pictures spread evenly over
        the clip (first and last included);
  sub   the first picture of every mapping shape (pivots and the mapping of each piece), the
        pictures of the smallest and largest NLQ parameters of each component, and the spread ones:
        drawn again without the enhancement layer and with the MMR table.

usage: oracle_select.py <prefix> <spread>
writes <prefix>-pics.txt, <prefix>-sub.txt and <prefix>-shapes.jsonl (the set of the first picture of
each mapping shape)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpu_groups as G  # noqa: E402


def main():
    prefix, spread = sys.argv[1], int(sys.argv[2])
    first = {}
    frames = []
    for line in open(prefix + '-frames.txt'):
        f, k = line.split()
        f = int(f)
        frames.append(f)
        if k != '-' and k not in first:
            first[k] = f
    total = len(frames)
    sp = sorted({frames[round(i * (total - 1) / (spread - 1))] for i in range(spread)}) if spread > 1 else [0]
    shapes = {}
    shape_set = {}
    nlq = {}
    for line in open(prefix + '-sets.jsonl'):
        s = json.loads(line)
        f = first[s['key']]
        shp = G.describe(s)
        if shp not in shapes or f < shapes[shp]:
            shapes[shp] = f
            shape_set[shp] = line
        for c, q in enumerate(s['map'].get('nlq') or []):
            for name in ('nlq_offset', 'vdr_in_max', 'linear_deadzone_slope', 'linear_deadzone_threshold'):
                v = q[name]
                lo = nlq.get((c, name, 'min'))
                hi = nlq.get((c, name, 'max'))
                if lo is None or v < lo[0] or (v == lo[0] and f < lo[1]):
                    nlq[(c, name, 'min')] = (v, f)
                if hi is None or v > hi[0] or (v == hi[0] and f < hi[1]):
                    nlq[(c, name, 'max')] = (v, f)
    pics = sorted(set(first.values()) | set(sp))
    sub = sorted(set(shapes.values()) | {f for _, f in nlq.values()} | set(sp))
    open(prefix + '-pics.txt', 'w').write(' '.join(map(str, pics)) + '\n')
    open(prefix + '-sub.txt', 'w').write(' '.join(map(str, sub)) + '\n')
    with open(prefix + '-shapes.jsonl', 'w') as f:
        for shp in sorted(shapes, key=lambda x: shapes[x]):
            f.write(shape_set[shp])
    print('%s: %d pictures, %d distinct sets, %d spread, %d mapping shapes; sub-list %d pictures' % (
        prefix, len(pics), len(first), len(sp), len(shapes), len(sub)))


if __name__ == '__main__':
    main()
