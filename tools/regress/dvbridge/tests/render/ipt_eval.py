#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY: the TV-led tunnel in YCbCr (what Hades sends for profiles 7 and 8) and in
IPT-PQ-c2 (what licensed players send), both against an ideal 12-bit IPT-PQ-c2 reference, in the way
the DMDreview comparison does it (12-bit I, P, T codes; luma-only and colour synthetic pictures; merged steps).

Inputs are composer output pictures h (CCM 001 5.4.3.3: 12-bit YCbCr 4:2:0, chroma at the top left, the RPU's
YCbCr matrices: BT.2020 narrow range, rgb_to_lms of BT.2020): synthetic ones made here, and the composed planes of
the profile 7 FEL stream (ccmC_*, which Hades' composer gives sample for sample).

  YCbCr transport: Hades' packer, emulated exactly (Y = h; the chroma of even rows as it is, of odd rows Lanczos 6 at
      half phase in integers, as dvbridge_source.c does); checked against the bytes of the bridge (mode check).
  IPT transport: from the same h, chroma to 4:4:4 with the same Lanczos 6 (columns too), YCbCr -> R'G'B' (RPU) ->
      linear (PQ) -> LMS (RPU rgb_to_lms) -> crosstalk undone (the RGBtoLMS of the tunnel) -> PQ -> IPT (the
      inverse of the YCCtoRGB of the tunnel) + (0, 1/2, 1/2), 12-bit full range; the matrices of the tunnel are those
      that a licensed core sends (Hades' dvbridge_ipt.h, read on an Amlogic player with Dolby's IDK 2.6); P and T of each pair from its
      even pixel; in float64, and in float32 as a GPU shader would compute it.
  Ideal: the same chain in float64 from h with the chroma interpolated in float64 (no rounding), rounded once.
  A TV decodes the YCbCr transport to IPT itself: emulated in float64 (its best case), with linear chroma between
  the pairs.
Usage: ipt_eval.py synth <out dir> | real <data> <pic>... | check <data> <direct dir> <tag> <pic>...
"""
import os
import sys

import numpy as np

H, W = 2160, 3840
M1, M2 = 2610 / 16384, 2523 / 4096 * 128
C1, C2, C3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32
RPU_YCC = np.array([[9574, 0, 13802], [9574, -1540, -5348], [9574, 17610, 0]], float) / 8192
RPU_OFF = np.array([16777216, 134217728, 134217728], float) / 268435456
RPU_LMS = np.array([[7222, 8771, 390], [2654, 12430, 1300], [0, 422, 15962]], float) / 16384
# the matrices of the IPT tunnel: those that a licensed core sends (until 2026-11: those of the RPUs of profile 5,
# 8192 and 17081/-349, which differ in the last code of four coefficients)
P5_YCC = np.array([[8194, 799, 1681], [8194, -933, 1091], [8194, 267, -5545]], float) / 8192
P5_LMS = np.array([[17080, -348, -348], [-348, 17080, -348], [-348, -348, 17080]], float) / 16384
IPT_OFF = np.array([0.0, 0.5, 0.5])


def eotf(e, dt=np.float64):
    e = np.clip(e, 0, 1).astype(dt)
    p = e ** dt(1 / M2)
    return (np.maximum(p - dt(C1), dt(0)) / (dt(C2) - dt(C3) * p)) ** dt(1 / M1)


def oetf(y, dt=np.float64):
    y = np.maximum(y, 0).astype(dt)
    p = y ** dt(M1)
    return ((dt(C1) + dt(C2) * p) / (dt(1) + dt(C3) * p)) ** dt(M2)


def mm(M, v, dt=np.float64):
    return np.einsum('ij,...j->...i', M.astype(dt), v.astype(dt))


def lanczos_weights():
    a, one = 6, 1 << 14
    d = np.arange(12) - a + 1 - 0.5
    w = np.sinc(d) * np.sinc(d / a)
    w /= w.sum()
    iw = np.rint(w * one).astype(np.int64)
    iw[np.argmax(w)] += one - iw.sum()
    return w, iw


WF, WI = lanczos_weights()


def interp_rows(c, integer):
    """4:2:0 chroma (h/2, w/2) -> its odd rows of 4:2:2: Lanczos 6 at phase 1/2 between rows i and i + 1"""
    n = c.shape[0]
    idx = np.clip(np.arange(n)[:, None] + np.arange(12)[None, :] - 5, 0, n - 1)
    if integer:
        s = np.einsum('k,ikw->iw', WI, c[idx].astype(np.int64))
        return np.clip((s + 8192) >> 14, 0, 4095)
    return np.einsum('k,ikw->iw', WF, c[idx].astype(np.float64))


def to422(c, integer):
    """4:2:0 (h/2, w/2) -> 4:2:2 (h, w/2), chroma at the top left: even rows as they are"""
    out = np.empty((c.shape[0] * 2, c.shape[1]), np.int64 if integer else np.float64)
    out[0::2] = c
    out[1::2] = interp_rows(c, integer)
    return out


def interp_cols(c, integer):
    """(h, w/2) -> (h, w): even columns as they are (co-sited), odd ones Lanczos 6 at half phase"""
    n = c.shape[1]
    idx = np.clip(np.arange(n)[:, None] + np.arange(12)[None, :] - 5, 0, n - 1)
    out = np.empty((c.shape[0], n * 2), np.float64)
    out[:, 0::2] = c
    if integer:
        s = np.einsum('k,hik->hi', WI, c[:, idx].astype(np.int64))
        out[:, 1::2] = np.clip((s + 8192) >> 14, 0, 4095)
    else:
        out[:, 1::2] = np.einsum('k,hik->hi', WF, c[:, idx].astype(np.float64))
    return out


def ycc_to_ipt(y, cb, cr, dt=np.float64):
    """codes (float) -> IPT-PQ-c2 values (I, P, T in units of 1, P/T offset 1/2), the chain in precision dt"""
    v = np.stack([y / 4096.0 - RPU_OFF[0], cb / 4096.0 - RPU_OFF[1], cr / 4096.0 - RPU_OFF[2]], -1).astype(dt)
    rgb = np.clip(mm(RPU_YCC, v, dt), 0, 1)
    lms = mm(RPU_LMS, eotf(rgb, dt), dt)
    x = mm(np.linalg.inv(P5_LMS), lms, dt)
    return mm(np.linalg.inv(P5_YCC), oetf(x, dt), dt) + IPT_OFF.astype(dt)


def codes(v):
    return np.clip(np.floor(v.astype(np.float64) * 4096 + 0.5), 0, 4095).astype(np.int64)


def ideal(h):
    """float64 IPT codes at every pixel from h with float chroma (the reference)"""
    cb = interp_cols(to422(h[1], False), False)
    cr = interp_cols(to422(h[2], False), False)
    return codes(ycc_to_ipt(h[0].astype(np.float64), cb, cr))


def ipt_transport(h, dt):
    """the IPT tunnel: I of every pixel, P and T of the even one of each pair (integer chroma interpolation)"""
    cb = interp_cols(to422(h[1], True), True)
    cr = interp_cols(to422(h[2], True), True)
    c = codes(ycc_to_ipt(h[0].astype(np.float64), cb, cr, dt))
    return c[..., 0], c[:, 0::2, 1], c[:, 0::2, 2]


def ycc_transport(h):
    """Hades' tunnel: Y, Cb and Cr of the pairs (as the packer writes them)"""
    return h[0].astype(np.int64), to422(h[1], True), to422(h[2], True)


def tv_decode_ycc(y, cb2, cr2):
    """a TV that converts the YCbCr tunnel to IPT exactly (float64), chroma between the pairs linear"""
    def up(c):
        o = np.repeat(c, 2, axis=1).astype(np.float64)
        nxt = np.concatenate([c[:, 1:], c[:, -1:]], axis=1)
        o[:, 1::2] = (c + nxt) / 2.0
        return o
    return codes(ycc_to_ipt(y.astype(np.float64), up(cb2), up(cr2)))


def stats(name, got, ref, mask):
    out = []
    for k, ch in enumerate('IPT'):
        d = (got[..., k] - ref[..., k])[mask]
        out.append('%s %6.2f %% off, max |d| %3d (min %+d, max %+d)' % (ch, 100.0 * (d != 0).mean(), np.abs(d).max(),
                                                                        d.min(), d.max()))
    print('  %-34s %s' % (name, ' | '.join(out)), flush=True)


def merged(name, inp, out):
    """along each row, steps of the input code that the output code does not follow (equal output for different
    inputs, adjacent), over the whole ramp"""
    a, b = inp[:, :-1] != inp[:, 1:], out[:, :-1] == out[:, 1:]
    print('  %-34s merged steps: %d of %d input steps' % (name, int((a & b).sum()), int(a.sum())), flush=True)


def evaluate(label, h, luma_only=False):
    print('== %s' % label, flush=True)
    ref = ideal(h)
    even = np.zeros((H, W), bool)
    even[:, 0::2] = True
    cosited = even.copy()
    cosited[1::2] = False
    res = {}
    for dt, dn in ((np.float64, 'IPT tunnel (float64)'), (np.float32, 'IPT tunnel (float32, as a shader)')):
        i, p, t = ipt_transport(h, dt)
        got = np.zeros_like(ref)
        got[..., 0] = i
        got[:, 0::2, 1] = p
        got[:, 0::2, 2] = t
        got[:, 1::2, 1] = p
        got[:, 1::2, 2] = t
        res[dn] = got
    y, cb2, cr2 = ycc_transport(h)
    res['YCbCr tunnel, TV converts exactly'] = tv_decode_ycc(y, cb2, cr2)
    for where, m in (('co-sited samples (even row, even column)', cosited), ('even columns (all rows)', even),
                     ('every pixel (I), P/T of the pairs', None)):
        print(' at %s:' % where)
        for name, got in res.items():
            if m is None:
                print('  %-34s I %6.2f %% off, max |d| %d' % (name, 100.0 * (got[..., 0] != ref[..., 0]).mean(),
                                                                np.abs(got[..., 0] - ref[..., 0]).max()))
            else:
                stats(name, got, ref, m)
    if luma_only:
        for name, got in res.items():
            print('  %-34s P in [%d, %d], T in [%d, %d] (neutral 2048)' % (name, got[..., 1].min(), got[..., 1].max(),
                                                                             got[..., 2].min(), got[..., 2].max()))
        for name, got in res.items():
            merged(name, h[0], got[..., 0])
        merged('ideal', h[0], ref[..., 0])


def synth():
    # 1. grey wedge, luma only: every 12-bit code of narrow range (256..3760), two pixels each, in two halves;
    #    a "12-bit marker": in the band of rows 960..1199 the codes step by 1 with the low 4 bits drawing a pattern
    y = np.full((H, W), 3760, np.int64)
    k = np.arange(W) // 2
    y[:1080] = np.clip(256 + k, 256, 3760)[None, :]
    y[1080:] = np.clip(256 + 1920 + k, 256, 3760)[None, :]
    rr, cc = np.mgrid[960:1200, 0:W]
    y[960:1200] = (y[960:1200] & ~15) | (((rr // 30) ^ (cc // 60)) & 15)
    c = np.full((H // 2, W // 2), 2048, np.int64)
    evaluate('grey wedge, luma only (with a 4-bit marker band)', [y, c, c.copy()], luma_only=True)
    # 2. colour gradients: four luma bands, Cb across, Cr down, over the narrow chroma range
    y = np.repeat(np.array([500, 1200, 2000, 2800], np.int64), H // 4)[:, None] * np.ones((1, W), np.int64)
    cx = np.linspace(448, 3648, W // 2)
    cy = np.linspace(448, 3648, H // 2)
    cb = np.rint(np.ones((H // 2, 1)) * cx[None, :]).astype(np.int64)
    cr = np.rint(cy[:, None] * np.ones((1, W // 2))).astype(np.int64)
    evaluate('colour gradients (four luma bands, Cb across, Cr down)', [y, cb, cr])
    # 3. a hard colour edge every 32 columns (saturated red / cyan pairs), mid luma
    y = np.full((H, W), 1900, np.int64)
    cb = np.where((np.arange(W // 2) // 16) % 2 == 0, 1200, 2900)[None, :] * np.ones((H // 2, 1), np.int64)
    cr = np.where((np.arange(W // 2) // 16) % 2 == 0, 3300, 900)[None, :] * np.ones((H // 2, 1), np.int64)
    evaluate('colour edges (bars of 32 pixels)', [y, cb.astype(np.int64), cr.astype(np.int64)])


def real(data, pics):
    for n in pics:
        h = []
        for name, shape in (('y', (H, W)), ('cb', (H // 2, W // 2)), ('cr', (H // 2, W // 2))):
            h.append(np.fromfile(os.path.join(data, 'p%d' % n, 'ccmC_%s.u16' % name), '<u2').reshape(shape).astype(np.int64))
        evaluate('picture %d of the profile 7 FEL stream (composed planes)' % n, h)


def check(data, ddir, tag, pics):
    """the packer emulation against the bytes the bridge wrote (direct way, source transport)"""
    for n in pics:
        h = []
        for name, shape in (('y', (H, W)), ('cb', (H // 2, W // 2)), ('cr', (H // 2, W // 2))):
            h.append(np.fromfile(os.path.join(data, 'p%d' % n, 'ccmC_%s.u16' % name), '<u2').reshape(shape).astype(np.int64))
        y, cb2, cr2 = ycc_transport(h)
        c = np.empty((H, W), np.int64)
        c[:, 0::2], c[:, 1::2] = cb2, cr2
        a = np.fromfile(os.path.join(ddir, '%s-p%d-direct.rgba' % (tag, n)), np.uint8).reshape(H, W, 4).astype(np.int64)
        ty, tc = (a[..., 1] << 4) | (a[..., 2] & 15), (a[..., 0] << 4) | (a[..., 2] >> 4)
        for flip in (False, True):
            yy, cc2 = (ty[::-1], tc[::-1]) if flip else (ty, tc)
            act = (yy != 256).any(axis=1) | (cc2 != 2048).any(axis=1)
            act[:8] = False
            dy = (yy[act] != y[act]).mean()
            # the low bit of the chroma of the first rows carries metadata; the rows checked start at 8
            dc = (cc2[act] != c[act]).mean()
            print('p%d flip %d: %d rows with a picture; Y %.6f %% off, C %.6f %% off' % (n, flip, act.sum(), 100 * dy, 100 * dc))


if __name__ == '__main__':
    m = sys.argv[1]
    if m == 'synth':
        synth()
    elif m == 'real':
        real(sys.argv[2], [int(x) for x in sys.argv[3:]])
    elif m == 'check':
        check(sys.argv[2], sys.argv[3], sys.argv[4], [int(x) for x in sys.argv[5:]])
