#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Group the per-picture RPU composer parameters printed by rpu_sets into distinct sets.

Only what clause 5.4 of ETSI GS CCM 001 uses goes into a set: bit depths, coefficient_log2_denom,
disable_residual_flag, the curves (pivots and the coefficients of the mapping each piece really
uses) and the NLQ parameters when the RPU carries them. Fields of a piece that its mapping_idc does
not use (FFmpeg keeps old values there) are left out, so two pictures are in the same set exactly
when the composer must treat them the same way.

usage: rpu_groups.py <rpu.jsonl[.gz]> <out-prefix>
writes <out-prefix>-sets.jsonl (one set per line, with "key", "frames", "count") and
<out-prefix>-summary.txt, and prints the summary.
"""
import collections
import gzip
import hashlib
import json
import sys


def composer_view(d):
    h = d['hdr']
    m = d['map']
    hv = {k: h[k] for k in ('bl_bit_depth', 'el_bit_depth', 'vdr_bit_depth', 'coef_log2_denom',
                            'coef_data_type', 'disable_residual_flag',
                            'spatial_resampling_filter_flag', 'el_spatial_resampling_filter_flag',
                            'rpu_format', 'vdr_rpu_profile')}
    curves = []
    for c in m['curves']:
        n = c['num_pivots']
        cv = {'num_pivots': n, 'pivots': c['pivots'][:n], 'mapping_idc': c['mapping_idc'][:n - 1],
              'poly_order': [], 'poly_coef': [], 'mmr_order': [], 'mmr_constant': [], 'mmr_coef': []}
        for k in range(n - 1):
            if c['mapping_idc'][k] == 0:
                po = c['poly_order'][k]
                cv['poly_order'].append(po)
                cv['poly_coef'].append(c['poly_coef'][k][:po + 1] + [0] * (2 - po))
                cv['mmr_order'].append(0)
                cv['mmr_constant'].append(0)
                cv['mmr_coef'].append([[0] * 7] * 3)
            else:
                mo = c['mmr_order'][k]
                cv['poly_order'].append(0)
                cv['poly_coef'].append([0, 0, 0])
                cv['mmr_order'].append(mo)
                cv['mmr_constant'].append(c['mmr_constant'][k])
                cv['mmr_coef'].append(c['mmr_coef'][k][:mo] + [[0] * 7] * (3 - mo))
        curves.append(cv)
    mv = {'mapping_color_space': m['mapping_color_space'],
          'mapping_chroma_format_idc': m['mapping_chroma_format_idc'],
          'nlq_method_idc': m['nlq_method_idc'],
          'num_x_partitions': m['num_x_partitions'], 'num_y_partitions': m['num_y_partitions'],
          'curves': curves}
    if m['nlq_method_idc'] == 0:
        mv['nlq'] = m['nlq']
    return {'hdr': hv, 'map': mv}


def key_of(view):
    return hashlib.sha256(json.dumps(view, sort_keys=True).encode()).hexdigest()[:16]


def describe(view):
    parts = []
    for c, cv in enumerate(view['map']['curves']):
        pieces = []
        for k in range(cv['num_pivots'] - 1):
            if cv['mapping_idc'][k] == 0:
                pieces.append('poly%d' % cv['poly_order'][k])
            else:
                pieces.append('mmr%d' % cv['mmr_order'][k])
        parts.append('c%d[%s|%s]' % (c, ' '.join(str(p) for p in cv['pivots']), ','.join(pieces)))
    return ' '.join(parts)


def main():
    src, prefix = sys.argv[1], sys.argv[2]
    op = gzip.open if src.endswith('.gz') else open
    sets = collections.OrderedDict()
    frames = 0
    no_rpu = 0
    shape = collections.Counter()
    nlq_sets = collections.Counter()
    curve_sets = [collections.Counter() for _ in range(3)]
    headers = collections.Counter()
    dup_pivots = collections.Counter()
    index = open(prefix + '-frames.txt', 'w')
    for line in op(src, 'rt'):
        d = json.loads(line)
        frames += 1
        if d.get('no_rpu'):
            no_rpu += 1
            index.write('%d -\n' % d['frame'])
            continue
        v = composer_view(d)
        k = key_of(v)
        index.write('%d %s\n' % (d['frame'], k))
        if k not in sets:
            sets[k] = {'key': k, 'frames': [], 'count': 0, 'set': v}
        s = sets[k]
        s['count'] += 1
        if len(s['frames']) < 50:
            s['frames'].append(d['frame'])
        shape[describe(v)] += 1
        headers[json.dumps(v['hdr'], sort_keys=True)] += 1
        nlq_sets[json.dumps(v['map'].get('nlq'), sort_keys=True)] += 1
        for c in range(3):
            curve_sets[c][json.dumps(v['map']['curves'][c], sort_keys=True)] += 1
            pv = v['map']['curves'][c]['pivots']
            if any(pv[i] == pv[i + 1] for i in range(len(pv) - 1)):
                dup_pivots['c%d %s' % (c, ' '.join(map(str, pv)))] += 1
    index.close()
    with open(prefix + '-sets.jsonl', 'w') as f:
        for s in sets.values():
            out = dict(s['set'])
            out['key'] = s['key']
            out['frames'] = s['frames']
            out['count'] = s['count']
            f.write(json.dumps(out, sort_keys=True) + '\n')
    lines = ['pictures %d, without RPU %d, distinct composer sets %d' % (frames, no_rpu, len(sets)),
             'distinct headers %d' % len(headers)]
    for h, n in headers.items():
        lines.append('  header %s: %d' % (h, n))
    lines.append('distinct curves per component: %s' % ' '.join(str(len(c)) for c in curve_sets))
    lines.append('distinct NLQ parameter sets %d' % len(nlq_sets))
    for q, n in sorted(nlq_sets.items(), key=lambda x: -x[1])[:5]:
        lines.append('  nlq %s: %d pictures' % (q, n))
    lines.append('pivots with two equal values: %s' % (dict(dup_pivots) or 'none'))
    lines.append('mapping shapes (component[pivots|piece mappings]): %d' % len(shape))
    for s, n in sorted(shape.items(), key=lambda x: -x[1]):
        lines.append('  %6d  %s' % (n, s))
    txt = '\n'.join(lines) + '\n'
    open(prefix + '-summary.txt', 'w').write(txt)
    sys.stdout.write(txt)


if __name__ == '__main__':
    main()
