# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY: picture directories for the composer with a base layer in
BT.1886 (no 8.2 sample exists: made-up from the one real picture directory of the fidelity bench, whose RPU is
that of the profile 7 FEL test stream: luma 8 polynomial pieces, chroma one MMR piece of order 3, YCCtoRGB of
BT.2020 in the narrow range).
  p1200    the real picture (3840x2160, its enhancement layer): as it is (only linked)
  rand     1920x1080 (the size of profile 8.2 streaming), BL codes uniformly random over 0..1023 (every code,
           every jump between neighbours: the filters of annex C at their extremes), the RPU of p1200; the EL
           random too (only read with the residual)
  ramp     1920x1080: grey and colour ramps over the whole code range, the RPU of p1200
  rgb709   p1200 with the default YCCtoRGB of CCM 001 clause 6.2.2 (that of BT.709): the conversion of 5.5 must
           refuse it
  nomaster p1200 with source_min_PQ = source_max_PQ: the conversion must refuse it
Usage: p82_mkdata.py <p1200 dir> <out root>"""
import os
import shutil
import sys

import numpy as np

src, root = sys.argv[1], sys.argv[2]
os.makedirs(root, exist_ok=True)


def meta_lines(path):
    return open(os.path.join(path, 'meta.txt')).read().split('\n')


def write_meta(d, lines, changes):
    out = []
    for line in lines:
        k = line.split(' ', 1)[0]
        out.append(k + ' ' + changes[k] if k in changes else line)
    open(os.path.join(d, 'meta.txt'), 'w').write('\n'.join(out))


def fresh(name):
    d = os.path.join(root, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


lines = meta_lines(src)
# p1200: links to the real planes
d = fresh('p1200')
for f in os.listdir(src):
    if f == 'meta.txt' or (f.endswith('.u16') and (f.startswith('bl_') or f.startswith('el_'))):
        os.symlink(os.path.join(src, f), os.path.join(d, f))

rng = np.random.default_rng(1886)
for name in ('rand', 'ramp'):
    d = fresh(name)
    w, h, ew, eh = 1920, 1080, 960, 540
    if name == 'rand':
        y = rng.integers(0, 1024, size=(h, w))
        u = rng.integers(0, 1024, size=(h // 2, w // 2))
        v = rng.integers(0, 1024, size=(h // 2, w // 2))
    else:
        x = np.arange(w)[None, :]
        r = np.arange(h)[:, None]
        y = np.broadcast_to((x * 1023) // (w - 1), (h, w)).copy()
        y[h // 2:] = ((r[h // 2:] - h // 2) * 1023) // (h // 2 - 1)
        xc = np.arange(w // 2)[None, :]
        rc = np.arange(h // 2)[:, None]
        u = np.broadcast_to(64 + (xc * 896) // (w // 2 - 1), (h // 2, w // 2)).copy()
        v = np.broadcast_to(64 + (rc * 896) // (h // 2 - 1), (h // 2, w // 2)).copy()
        u[:h // 8] = 512
        v[:h // 8] = 512  # the grey ramp
    for n, a in (('bl_y', y), ('bl_u', u), ('bl_v', v)):
        a.astype('<u2').tofile(os.path.join(d, n + '.u16'))
    for n, s in (('el_y', (eh, ew)), ('el_u', (eh // 2, ew // 2)), ('el_v', (eh // 2, ew // 2))):
        rng.integers(0, 1024, size=s).astype('<u2').tofile(os.path.join(d, n + '.u16'))
    write_meta(d, lines, {'bl.width': str(w), 'bl.height': str(h), 'el.width': str(ew), 'el.height': str(eh)})

d = fresh('rgb709')
for f in os.listdir(src):
    if f.endswith('.u16') and (f.startswith('bl_') or f.startswith('el_')):
        os.symlink(os.path.join(src, f), os.path.join(d, f))
write_meta(d, lines, {'color.ycc_to_rgb_matrix': '9575/8192 0/8192 14742/8192 9575/8192 -1754/8192 -4383/8192 '
                                                 '9575/8192 17372/8192 0/8192'})
d = fresh('nomaster')
for f in os.listdir(src):
    if f.endswith('.u16') and (f.startswith('bl_') or f.startswith('el_')):
        os.symlink(os.path.join(src, f), os.path.join(d, f))
write_meta(d, lines, {'color.source_min_pq': '3696'})
print('mkdata: p1200 rand ramp rgb709 nomaster in', root)
