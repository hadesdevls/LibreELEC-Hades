# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY (profile 8.4): a picture directory with a base layer in HLG
(Recommendation ITU-R BT.2100, YCbCr of BT.2020 in the narrow range, 10 bits, 4:2:0, 1920x1080) for the composer
tests: a chart of flat 8x8 patches (so that 4:2:0 and the down-sampling of the luma of clause 5.4.2.3.3 are exact
inside a patch): every grey code from 64 to 940, then R'G'B' of HLG on a 21x21x21 grid, the rest mid grey.
The RPU is the meta.txt given (from p84_rpuinfo); patches.npy keeps the codes of each patch.
Usage: p84_mkdata.py <meta.txt of the RPU> <out dir> [eotf]    (eotf "a b c d": signal_eotf and params written instead)"""
import os
import sys

import numpy as np

meta, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
W, H, P = 1920, 1080, 8
cols, rows = W // P, H // P
n = cols * rows
codes = np.zeros((n, 3), dtype=np.int64)  # Y, Cb, Cr (10-bit)
codes[:] = (502, 512, 512)
k = 0
for y in range(64, 941):
    codes[k] = (y, 512, 512)
    k += 1
g = np.linspace(0.0, 1.0, 21)
for r in g:
    for gg in g:
        for b in g:
            yy = 0.2627 * r + 0.6780 * gg + 0.0593 * b
            cb = (b - yy) / 1.8814
            cr = (r - yy) / 1.4746
            codes[k] = (round(64 + 876 * yy), round(512 + 896 * cb), round(512 + 896 * cr))
            k += 1
codes = np.clip(codes, 0, 1023)
grey_count = 941 - 64
yp = np.zeros((H, W), dtype='<u2')
up = np.zeros((H // 2, W // 2), dtype='<u2')
vp = np.zeros((H // 2, W // 2), dtype='<u2')
for i in range(n):
    r, c = divmod(i, cols)
    yp[r * P:(r + 1) * P, c * P:(c + 1) * P] = codes[i, 0]
    up[r * P // 2:(r + 1) * P // 2, c * P // 2:(c + 1) * P // 2] = codes[i, 1]
    vp[r * P // 2:(r + 1) * P // 2, c * P // 2:(c + 1) * P // 2] = codes[i, 2]
yp.tofile(os.path.join(out, 'bl_y.u16'))
up.tofile(os.path.join(out, 'bl_u.u16'))
vp.tofile(os.path.join(out, 'bl_v.u16'))
# a layer that is never read (disable_residual_flag 1): two tiny planes for the readers that open them
for name, s in (('el_y', (2, 2)), ('el_u', (1, 1)), ('el_v', (1, 1))):
    np.zeros(s, dtype='<u2').tofile(os.path.join(out, name + '.u16'))
lines = open(meta).read().split('\n')
txt = []
for line in lines:
    key = line.split(' ', 1)[0]
    if key == 'bl.width':
        line = 'bl.width %d' % W
    elif key == 'bl.height':
        line = 'bl.height %d' % H
    elif key == 'el.width':
        line = 'el.width 2'
    elif key == 'el.height':
        line = 'el.height 2'
    elif key == 'color.signal_eotf' and len(sys.argv) > 3:
        line = 'color.signal_eotf ' + sys.argv[3]
    txt.append(line)
txt += ['bl.color_range 1 tv', 'el.chroma_location 2 top', 'ext0.level 5', 'ext0.l5 0 0 0 0']
open(os.path.join(out, 'meta.txt'), 'w').write('\n'.join(t for t in txt if t) + '\n')
np.save(os.path.join(out, 'patches.npy'), codes[:k])
print('mk84: %d patches (%d greys, %d colours) in %s' % (k, grey_count, k - grey_count, out))
