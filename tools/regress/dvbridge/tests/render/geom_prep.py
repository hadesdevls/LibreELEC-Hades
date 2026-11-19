#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY: a 1920x1080 base layer made from a 3840x2160 one as a
streaming encoder makes its 1080p rendition of a 4K master (the base layer's own codes, with the same RPU): each
plane brought to half its size by Lanczos 3 widened to the new sample spacing (12 taps: the anti-aliasing
filter), the chroma kept at its location. Rounded to 10-bit codes.

Usage: geom_prep.py <out .yuv> <chroma location> (<4K yuv420p10le> | <dir with bl_y.u16 bl_u.u16 bl_v.u16>)
                     [lanczos3 | bicubic (B 0, C 0.6, as FFmpeg's swscale)]
"""
import os
import sys

import numpy as np

CHROMA_POS = {1: (0.0, 0.5), 2: (0.5, 0.5), 3: (0.0, 0.0), 4: (0.5, 0.0), 5: (0.0, 1.0), 6: (0.5, 1.0)}


def lanczos(x, a):
    out = np.sinc(x) * np.sinc(x / a)
    out[np.abs(x) >= a] = 0.0
    return out


KERNEL = 'lanczos3'


def bicubic(x, b=0.0, c=0.6):
    x = np.abs(x)
    out = np.where(x < 1, ((12 - 9 * b - 6 * c) * x ** 3 + (-18 + 12 * b + 6 * c) * x ** 2 + (6 - 2 * b)) / 6,
                   np.where(x < 2, ((-b - 6 * c) * x ** 3 + (6 * b + 30 * c) * x ** 2 + (-12 * b - 48 * c) * x +
                                    (8 * b + 24 * c)) / 6, 0.0))
    return out


def down(arr, axis, positions, a=3, scale=2.0):
    n_in = arr.shape[axis]
    if KERNEL == 'bicubic':
        a = 2
    base = np.floor(positions).astype(np.int64)
    k = np.arange(-int(a * scale) + 1, int(a * scale) + 1)
    idx = base[:, None] + k[None, :]
    w = (bicubic((positions[:, None] - idx) / scale) if KERNEL == 'bicubic'
         else lanczos((positions[:, None] - idx) / scale, a))
    w /= w.sum(axis=1, keepdims=True)
    idx = np.clip(idx, 0, n_in - 1)
    out = np.zeros((len(positions), arr.shape[1]) if axis == 0 else (arr.shape[0], len(positions)))
    for j in range(idx.shape[1]):
        if axis == 0:
            out += w[:, j, None] * arr[idx[:, j], :]
        else:
            out += w[None, :, j] * arr[:, idx[:, j]]
    return out


def main():
    global KERNEL
    out, loc, src = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    if len(sys.argv) > 4:
        KERNEL = sys.argv[4]
    W, H = 3840, 2160
    if os.path.isdir(src):
        y = np.fromfile(src + '/bl_y.u16', dtype='<u2').reshape(H, W)
        u = np.fromfile(src + '/bl_u.u16', dtype='<u2').reshape(H // 2, W // 2)
        v = np.fromfile(src + '/bl_v.u16', dtype='<u2').reshape(H // 2, W // 2)
    else:
        raw = np.fromfile(src, dtype='<u2')
        y = raw[:W * H].reshape(H, W)
        u = raw[W * H:W * H + W * H // 4].reshape(H // 2, W // 2)
        v = raw[W * H + W * H // 4:].reshape(H // 2, W // 2)
    y, u, v = (p.astype(np.float64) for p in (y, u, v))
    ox, oy = CHROMA_POS[loc]
    # luma: sample o of the half size is at 2 o + 0.5 of the full one
    ly = 2.0 * np.arange(H // 2) + 0.5
    lx = 2.0 * np.arange(W // 2) + 0.5
    y2 = down(down(y, 0, ly), 1, lx)
    # chroma sample j of the half size sits at luma 2 j + o of the half size, that is at 2 (2 j + o) + 0.5 of the
    # full size, which is chroma (that - o) / 2 of the full size
    cy = (2.0 * (2.0 * np.arange(H // 4) + oy) + 0.5 - oy) / 2.0
    cx = (2.0 * (2.0 * np.arange(W // 4) + ox) + 0.5 - ox) / 2.0
    u2 = down(down(u, 0, cy), 1, cx)
    v2 = down(down(v, 0, cy), 1, cx)
    planes = [np.clip(np.round(p), 0, 1023).astype('<u2') for p in (y2, u2, v2)]
    with open(out, 'wb') as f:
        for p in planes:
            p.tofile(f)
    print('%s: 1920x1080 from %s, luma %d..%d, chroma %d..%d' % (out, src, planes[0].min(), planes[0].max(),
                                                                min(planes[1].min(), planes[2].min()),
                                                                max(planes[1].max(), planes[2].max())))


if __name__ == '__main__':
    main()
