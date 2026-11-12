#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Pictures made on purpose from a real one (its decoded layers, with composing metadata changed by
meta_edit), for the readings of the clause that the real RPUs do not exercise.

usage: oracle_synth.py <meta_edit binary> <base data dir pN> <out dir>
writes <out dir>/<case>/pN (planes linked to the base picture, meta.bin and meta.txt changed) and
prints, per case, the mutations that its comparison must detect.

Cases (D = 2^19, a step of 1/16 of the mapped range):
  pivots    luma: pivots 0 300 600 1023, three pieces from the real polynomial of the base picture
            with c0 - 4D, c0 + D, c0 + 2D (a sample on 300 or 600 tells which piece; the first piece
            goes below 0 for dark samples, which 5.4.2.3.2 clamps); Cr: pivots 0 512 1023, the
            identity and the identity + D (a sample of 512 tells the piece)
  range     luma: pivots lo, (lo + hi) / 2, hi (lo one above the 10th percentile of the luma of the
            active area, hi its 90th: samples below the first pivot and at or above the last), two
            pieces of the real polynomial (c0, c0 + D); Cb and Cr: their real MMR mapping with pivots
            100 900 (s0 is clamped with the luma pivots, s1 and s2 with their own)
  nlq       the real NLQ parameters with hdr_in_max / 6 and a threshold of 2^16 (2^-7)
  mmr1      Cb and Cr: their real MMR mapping cut to order 1
  identity  the identity polynomial on the three components, disable_residual_flag 1 (the EL
            picture is there and must not be added): h = s << (12 - 10) (negative control)
"""
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_compare as C  # noqa: E402

D = 1 << 19
EXPECT = {
    'pivots': ['pivot-le', 'poly-noneg'],
    'range': ['top-first', 'poly-noclamp', 'mmr-ownclamp'],
    'nlq': ['nlq-thr', 'nlq-noclamp'],
    'mmr1': ['mmr-tt7', 'mmr-shift'],
    'identity': [],
}


def luma_range(base, kv):
    """The 10th and 90th percentiles of the BL luma inside the active area: pivots that leave samples
    below the first one and at or above the last one."""
    y = C.load_planes(base, kv, 'bl')[0]
    top, bottom, left, right = C.active_area(kv, y.shape[1], y.shape[0])
    a = y[top:bottom, left:right]
    lo = int(np.percentile(a, 10)) + 1
    hi = int(np.percentile(a, 90))
    if not 0 < lo < hi - 2 <= 1020:
        raise SystemExit('range: the luma of the base picture spans too little (%d..%d)' % (lo, hi))
    return lo, hi


def edits(case, d, base=None, kv=None):
    m = d['map']
    cv = m['curves']
    lp = cv[0]['num_pivots'] - 2          # the last (real) luma piece
    po = cv[0]['poly_order'][lp]
    c = cv[0]['poly_coef'][lp]
    poly = lambda dc: '%d,%d,%d,%d' % (po, c[0] + dc, c[1], c[2])  # noqa: E731
    ident = lambda dc: '1,%d,%d,0' % (dc, 1 << 23)                  # noqa: E731

    def mmr(ci, order=None):
        k = 0
        o = order or cv[ci]['mmr_order'][k]
        coefs = [x for row in cv[ci]['mmr_coef'][k][:o] for x in row]
        return '%d,%d,%s' % (o, cv[ci]['mmr_constant'][k], ','.join(str(x) for x in coefs))

    if case == 'pivots':
        return ['c0.pivots=0,300,600,1023', 'c0.p0.poly=' + poly(-4 * D), 'c0.p1.poly=' + poly(D),
                'c0.p2.poly=' + poly(2 * D), 'c2.pivots=0,512,1023', 'c2.p0.poly=' + ident(0),
                'c2.p1.poly=' + ident(D)]
    if case == 'range':
        lo, hi = luma_range(base, kv)
        out = ['c0.pivots=%d,%d,%d' % (lo, (lo + hi) // 2, hi), 'c0.p0.poly=' + poly(0), 'c0.p1.poly=' + poly(D)]
        for ci in (1, 2):
            if cv[ci]['num_pivots'] != 2 or cv[ci]['mapping_idc'][0] != 1:
                raise SystemExit('range: the base picture has no single MMR piece in component %d' % ci)
            out += ['c%d.pivots=100,900' % ci, 'c%d.p0.mmr=%s' % (ci, mmr(ci))]
        return out
    if case == 'nlq':
        return ['nlq%d=%d,%d,%d,%d' % (k, q['nlq_offset'], q['vdr_in_max'] // 6, q['linear_deadzone_slope'],
                                        1 << 16) for k, q in enumerate(m['nlq'])]
    if case == 'mmr1':
        return ['c%d.p0.mmr=%s' % (ci, mmr(ci, 1)) for ci in (1, 2)]
    if case == 'identity':
        return ['c%d.pivots=0,1023' % ci for ci in range(3)] + \
               ['c%d.p0.poly=%s' % (ci, ident(0)) for ci in range(3)] + ['disable_residual=1', 'nlq_method=-1']
    raise SystemExit('unknown case ' + case)


def main():
    me, base, out = sys.argv[1:4]
    base = os.path.abspath(os.path.normpath(base))
    pic = os.path.basename(base)
    d, kv = C.meta_set(base)
    for case in EXPECT:
        dst = os.path.join(out, case, pic)
        os.makedirs(dst, exist_ok=True)
        for f in os.listdir(base):
            if f.endswith('.u16'):
                link = os.path.join(dst, f)
                if os.path.lexists(link):
                    os.remove(link)
                os.symlink(os.path.join(base, f), link)
        e = edits(case, d, base, kv)
        r = subprocess.run([me, os.path.join(base, 'meta.bin'), os.path.join(dst, 'meta.bin')] + e,
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('meta_edit %s: %s' % (case, r.stderr))
        new = r.stdout.splitlines()
        keep = [line for line in open(os.path.join(base, 'meta.txt')).read().splitlines()
                if not (line.startswith('curve') or line.startswith('nlq') or line.startswith('map.nlq_method_idc')
                        or line.startswith('hdr.disable_residual_flag'))]
        with open(os.path.join(dst, 'meta.txt'), 'w') as f:
            f.write('\n'.join(keep + new) + '\n')
        print('%s %s %s' % (case, dst, ','.join(EXPECT[case]) or '-'))


if __name__ == '__main__':
    main()
