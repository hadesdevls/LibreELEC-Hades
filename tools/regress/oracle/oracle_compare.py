#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Compare the CCM 001 oracle with what the software DV bridge (fidbench) composed.

  oracle_compare.py picture <data dir pN> <fidbench out dir> <tag> <name> [--noel] [--reading k=v]...
      [--mutate m]... [--expect same|differ]
      runs the oracle on the layers and RPU that ref_dump wrote (meta.txt) and compares its h planes
      (clause 5.4.3.3) with fidbench's <tag>-pN-ccm_{y,cb,cr}.u16; prints one ORACLE-ITEM line.
  oracle_compare.py metaset <data dir pN>      prints the parameter set read from meta.txt (JSON)
  oracle_compare.py scanmatch <data dir pN> <sets.jsonl> <frame>
      checks that the RPU of the dumped picture is the one the scan found for that picture number.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ccm001_oracle as O  # noqa: E402
import rpu_groups as G  # noqa: E402


def read_meta(d):
    kv = {}
    for line in open(os.path.join(d, 'meta.txt')):
        line = line.rstrip('\n')
        if ' ' in line:
            k, v = line.split(' ', 1)
            kv[k] = v
    return kv


def meta_set(d):
    kv = read_meta(d)

    def i(k):
        return int(kv[k].split()[0])

    hdr = {k: i('hdr.' + k) for k in ('rpu_type', 'rpu_format', 'vdr_rpu_profile', 'vdr_rpu_level',
                                      'coef_data_type', 'coef_log2_denom', 'bl_video_full_range_flag',
                                      'bl_bit_depth', 'el_bit_depth', 'vdr_bit_depth',
                                      'spatial_resampling_filter_flag',
                                      'el_spatial_resampling_filter_flag', 'disable_residual_flag')}
    curves = []
    for c in range(3):
        n = i('curve%d.num_pivots' % c)
        cv = {'num_pivots': n, 'pivots': [int(x) for x in kv['curve%d.pivots' % c].split()],
              'mapping_idc': [], 'poly_order': [], 'poly_coef': [], 'mmr_order': [], 'mmr_constant': [],
              'mmr_coef': []}
        for k in range(n - 1):
            p = 'curve%d.piece%d.' % (c, k)
            mi = i(p + 'mapping_idc')
            cv['mapping_idc'].append(mi)
            if mi == 0:
                f = [int(x) for x in kv[p + 'poly'].split()]
                cv['poly_order'].append(f[0])
                cv['poly_coef'].append(f[1:4])
                cv['mmr_order'].append(0)
                cv['mmr_constant'].append(0)
                cv['mmr_coef'].append([[0] * 7] * 3)
            else:
                mo = i(p + 'mmr_order')
                cv['poly_order'].append(0)
                cv['poly_coef'].append([0, 0, 0])
                cv['mmr_order'].append(mo)
                cv['mmr_constant'].append(i(p + 'mmr_constant'))
                rows = [[int(x) for x in kv[p + 'mmr%d' % (j + 1)].split()] for j in range(mo)]
                cv['mmr_coef'].append(rows + [[0] * 7] * (3 - mo))
        curves.append(cv)
    nlq = []
    for c in range(3):
        f = [int(x) for x in kv['nlq%d' % c].split()]
        nlq.append({'nlq_offset': f[0], 'vdr_in_max': f[1], 'linear_deadzone_slope': f[2],
                    'linear_deadzone_threshold': f[3]})
    mp = {'mapping_color_space': i('map.mapping_color_space'),
          'mapping_chroma_format_idc': i('map.mapping_chroma_format_idc'),
          'nlq_method_idc': i('map.nlq_method_idc'),
          'num_x_partitions': i('map.num_x_partitions'), 'num_y_partitions': i('map.num_y_partitions'),
          'curves': curves, 'nlq': nlq}
    return {'hdr': hdr, 'map': mp}, kv


def load_planes(d, kv, layer):
    w = int(kv[layer + '.width'].split()[0])
    h = int(kv[layer + '.height'].split()[0])
    cw, ch = (w + 1) // 2, (h + 1) // 2
    y = np.fromfile(os.path.join(d, layer + '_y.u16'), '<u2').reshape(h, w).astype(np.int64)
    u = np.fromfile(os.path.join(d, layer + '_u.u16'), '<u2').reshape(ch, cw).astype(np.int64)
    v = np.fromfile(os.path.join(d, layer + '_v.u16'), '<u2').reshape(ch, cw).astype(np.int64)
    return [y, u, v]


def active_area(kv, w, h):
    """Rows and columns of the picture inside the active area of the RPU's level 5 block (all of it
    when there is none): [top, bottom) x [left, right)."""
    for k, v in kv.items():
        if k.startswith('ext') and k.endswith('.l5'):
            left, right, top, bottom = (int(x) for x in v.split())
            return top, h - bottom, left, w - right
    return 0, h, 0, w


def not_composed(got, ref, c, area):
    """Rows (and columns) that the bench left at 0 where the oracle has something: the bridge composes
    only the rows its later filters read. Returns (row mask, column mask, inside) where `inside`
    counts such rows/columns inside the active area (those are real differences)."""
    hh, ww = got.shape
    top, bottom, left, right = area
    if c:
        top, bottom, left, right = top // 2, (bottom + 1) // 2, left // 2, (right + 1) // 2
    rows = (got == 0).all(axis=1) & ~(ref == 0).all(axis=1)
    cols = (got == 0).all(axis=0) & ~(ref == 0).all(axis=0)
    r_in = int(np.count_nonzero(rows[top:bottom]))
    c_in = int(np.count_nonzero(cols[left:right]))
    return rows, cols, r_in + c_in


def describe_diffs(o, bl, el, got, c, idx, limit=6):
    out = []
    for y, x in idx[:limit]:
        e = o['E'][c][y, x] if o['E'][c] is not None else '-'
        out.append('(%s %d,%d: s=%d piece=%d %s v=%d e=%s r=%d h=%d bench=%d)' % (
            ('y', 'cb', 'cr')[c], x, y, bl[c][y, x], o['piece'][c][y, x],
            'mmr' if o['kind'][c][y, x] else 'poly', o['V'][c][y, x], e, o['R'][c][y, x],
            o['H'][c][y, x], got[y, x]))
    return ' '.join(out)


def cmd_picture(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('data')
    ap.add_argument('outdir')
    ap.add_argument('tag')
    ap.add_argument('name')
    ap.add_argument('--noel', action='store_true')
    ap.add_argument('--reading', action='append')
    ap.add_argument('--mutate', action='append')
    ap.add_argument('--expect', default='same', choices=('same', 'differ'))
    ap.add_argument('--save', help='write the oracle planes here')
    a = ap.parse_args(argv)
    rd = dict(O.DEFAULT_READINGS)
    for kv_ in a.reading or []:
        k, v = kv_.split('=', 1)
        rd[k] = v
    mut = tuple(a.mutate or [])
    num = os.path.basename(os.path.normpath(a.data)).lstrip('p')
    d, kv = meta_set(a.data)
    P = O.Params(d)
    bl = load_planes(a.data, kv, 'bl')
    el = None if a.noel else load_planes(a.data, kv, 'el')
    try:
        o = O.compose(P, bl, el, rd, mut)
    except Exception as ex:  # an oracle that cannot run is a failed item, never a pass
        print('ORACLE-ITEM %s: FAILED | the oracle did not run: %r' % (a.name, ex))
        return 1
    if a.save:
        os.makedirs(a.save, exist_ok=True)
        for c, n in enumerate(('y', 'cb', 'cr')):
            o['H'][c].astype('<u2').tofile(os.path.join(a.save, 'h_%s.u16' % n))
            o['V'][c].astype('<u2').tofile(os.path.join(a.save, 'v_%s.u16' % n))
    total_diff = 0
    worst = 0
    parts = []
    missing = []
    area = active_area(kv, bl[0].shape[1], bl[0].shape[0])
    for c, n in enumerate(('y', 'cb', 'cr')):
        path = os.path.join(a.outdir, '%s-p%s-ccm_%s.u16' % (a.tag, num, n))
        if not os.path.exists(path):
            missing.append(n)
            continue
        got = np.fromfile(path, '<u2').astype(np.int64)
        if got.size != o['H'][c].size:
            missing.append('%s(size %d, oracle %d)' % (n, got.size, o['H'][c].size))
            continue
        got = got.reshape(o['H'][c].shape)
        rows, cols, inside = not_composed(got, o['H'][c], c, area)
        cmpmask = ~rows[:, None] & ~cols[None, :]
        if inside:
            # a row or column the bridge did not compose inside the active area is compared as it is
            cmpmask = np.ones_like(cmpmask)
        diff = np.where(cmpmask, np.abs(got - o['H'][c]), 0)
        nd = int(np.count_nonzero(diff))
        mx = int(diff.max())
        total_diff += nd
        worst = max(worst, mx)
        txt = '%s: max %d, %d of %d compared differ' % (n, mx, nd, int(np.count_nonzero(cmpmask)))
        if rows.any() or cols.any():
            txt += ' (not composed: %d rows, %d columns outside the active area%s)' % (
                int(rows.sum()), int(cols.sum()), ', %d INSIDE it' % inside if inside else '')
        if nd:
            idx = np.argwhere(diff > 0)
            # where: by piece and mapping
            pk = o['piece'][c][diff > 0]
            kd = o['kind'][c][diff > 0]
            byp = {}
            for p_, k_ in zip(pk.tolist(), kd.tolist()):
                key = 'piece%d-%s' % (p_, 'mmr' if k_ else 'poly')
                byp[key] = byp.get(key, 0) + 1
            txt += ' %s first %s' % (json.dumps(byp, sort_keys=True), describe_diffs(o, bl, el, got, c, idx.tolist()))
        parts.append(txt)
    if missing:
        print('ORACLE-ITEM %s: FAILED | fidbench planes missing: %s' % (a.name, ' '.join(missing)))
        return 1
    same = total_diff == 0
    ok = same if a.expect == 'same' else not same
    shapes = []
    for c in range(3):
        cv = P.curves[c]
        shapes.append('c%d[%s]' % (c, ','.join(('mmr%d' % cv['MMR_order'][k]) if cv['mapping_idc'][k]
                                                else ('poly%d' % (cv['poly_order_minus1'][k] + 1))
                                                for k in range(cv['num_pivots_minus2'] + 1))))
    detail = '%s; residual %s; %s; expected %s%s%s' % (
        '; '.join(parts), 'yes' if o['residual'] else 'no', ' '.join(shapes), a.expect,
        ('; readings ' + ','.join('%s=%s' % kv_ for kv_ in sorted(rd.items()))) if a.reading else '',
        ('; mutations ' + ','.join(mut)) if mut else '')
    print('ORACLE-ITEM %s: %s | %s' % (a.name, 'PASSED' if ok else 'FAILED', detail))
    return 0 if ok else 1


def cmd_formula(argv):
    """<data dir> <name>: the negative control. Its RPU must be the identity polynomial (c0 0, c1
    2^denom) on [0, 2^BL - 1] in every component and a residual that is not added; then 5.4.2.3.2 gives
    v = s << (16 - BL) and 5.4.3.3 h = (v + 2^(15 - out)) >> (16 - out) = s << (out - BL), whatever the
    EL holds. The oracle must give exactly that (the bridge is compared with the oracle by `picture`)."""
    d, kv = meta_set(argv[0])
    name = argv[1]
    P = O.Params(d)
    B = P.BL_bit_depth
    for cv in P.curves:
        if cv['pivot_value'] != [0, (1 << B) - 1] or cv['mapping_idc'] != [0] or \
                cv['fp_poly_coef'][0][:2] != [0, 1 << P.coefficient_log2_denom] or \
                (cv['poly_order_minus1'][0] == 1 and cv['fp_poly_coef'][0][2] != 0):
            print('ORACLE-ITEM %s: FAILED | the RPU is not the identity' % name)
            return 1
    bl = load_planes(argv[0], kv, 'bl')
    el = load_planes(argv[0], kv, 'el')
    o = O.compose(P, bl, el)
    ob = o['out_bit_depth']
    bad = [c for c in range(3) if not np.array_equal(o['H'][c], bl[c] << (ob - B))]
    ok = not bad and not o['residual']
    print('ORACLE-ITEM %s: %s | identity, residual %s: oracle h = s << %d in %s of 3 components' % (
        name, 'PASSED' if ok else 'FAILED', 'added' if o['residual'] else 'not added', ob - B, 3 - len(bad)))
    return 0 if ok else 1


def cmd_metaset(argv):
    d, _ = meta_set(argv[0])
    print(json.dumps(d, sort_keys=True))
    return 0


def cmd_scanmatch(argv):
    """<data dir pN> <scan frames index (frame key per line)> <frame>"""
    d, _ = meta_set(argv[0])
    frame = int(argv[2])
    want = None
    for line in open(argv[1]):
        f, k = line.split()
        if int(f) == frame:
            want = k
            break
    a = G.key_of(G.composer_view(d))
    print('dumped picture %d: composer set %s, scan of the clip %s: %s' % (
        frame, a, want, 'same' if a == want else 'DIFFERENT'))
    return 0 if a == want else 1


def main():
    cmd = sys.argv[1]
    if cmd == 'picture':
        return cmd_picture(sys.argv[2:])
    if cmd == 'metaset':
        return cmd_metaset(sys.argv[2:])
    if cmd == 'formula':
        return cmd_formula(sys.argv[2:])
    if cmd == 'scanmatch':
        return cmd_scanmatch(sys.argv[2:])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
