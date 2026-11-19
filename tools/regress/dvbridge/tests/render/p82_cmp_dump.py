#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY: the composed planes that libplacebo wrote (test-only hook of lp-dump-hook.py:
<prefix>_y.f32, one float h / 2^hdr a texel; <prefix>_c.f32, two) against the samples h of the exact
reference (ccm_ref.c: <ref>_y.u16, _cb.u16, _cr.u16).
Usage: p82_cmp_dump.py <dump prefix> <ref prefix> <picture dir> <label>"""
import os
import sys

import numpy as np


def meta(pdir):
    m = {}
    with open(os.path.join(pdir, 'meta.txt')) as f:
        for line in f:
            k, _, v = line.rstrip('\n').partition(' ')
            m[k] = v
    return m


def main():
    dump, ref, pdir, label = sys.argv[1:5]
    m = meta(pdir)
    w, h = int(m['bl.width']), int(m['bl.height'])
    wc, hc = (w + 1) // 2, (h + 1) // 2
    scale = float(1 << int(m['hdr.vdr_bit_depth']))
    gy = np.fromfile(dump + '_y.f32', dtype='<f4').reshape(h, w).astype(np.float64) * scale
    gc = np.fromfile(dump + '_c.f32', dtype='<f4').reshape(hc, wc, 2).astype(np.float64) * scale
    planes = [('Y', gy, np.fromfile(ref + '_y.u16', dtype='<u2').reshape(h, w)),
              ('Cb', gc[..., 0], np.fromfile(ref + '_cb.u16', dtype='<u2').reshape(hc, wc)),
              ('Cr', gc[..., 1], np.fromfile(ref + '_cr.u16', dtype='<u2').reshape(hc, wc))]
    tot = 0
    parts = []
    for name, g, r in planes:
        frac = np.abs(g - np.round(g))
        d = np.abs(np.round(g).astype(np.int64) - r.astype(np.int64))
        n = int((d > 0).sum())
        tot += n
        where = np.unravel_index(int(np.argmax(d)), d.shape)
        parts.append('%s %d of %d differ (1: %d, 2-15: %d, 16+: %d; largest %d at x %d y %d; non-integer %d)'
                     % (name, n, d.size, int((d == 1).sum()), int(((d >= 2) & (d < 16)).sum()),
                        int((d >= 16).sum()), int(d.max()), int(where[1]), int(where[0]), int((frac > 1e-6).sum())))
    print('%s: %s; %s' % (label, 'BIT-EXACT' if tot == 0 else 'DIFFER', '; '.join(parts)))
    sys.exit(0 if tot == 0 else 1)


if __name__ == '__main__':
    main()
