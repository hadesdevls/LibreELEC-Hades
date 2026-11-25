#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY: the IPT tunnel as the software bridge writes it against the oracle (ipt_eval.py).

  bridge <data> <out dir> <tag> <pic>...   direct.rgba of FB_SOURCE=1 FB_IPT=1 against the oracle's IPT tunnel
      from the same composed planes (float32 as a shader, float64) and the ideal; the DM header of the frame
  same <dirA> <tagA> <dirB> <tagB> <kind> <pic>...   two outputs of the same kind byte for byte (and how many differ)
  gui <out dir> <tag> <pic>...    composed.rgba with the GUI against direct.rgba: pixels not drawn over the same
  scaled <dirY> <tagY> <dirI> <tagI> <pic>...  a scaled picture: the IPT tunnel decoded against the YCbCr one decoded
  p5 <dir0> <tag0> <dir1> <tag1> <pic>...  a profile 5 picture as composed (its RPU's matrices) and in the IPT tunnel:
      the second's header the licensed core's, and its codes those of the first converted in float64 (decoded with
      the RPU's matrices, encoded with the tunnel's, rounded once) within 1 code, on at most 1 % of the samples; and
      what the conversion costs, the two decoded as a display decodes them from their own headers to the PQ of LMS,
      reported (its 99th percentile within 2 codes of 4095: a code of I, P or T is up to 1 + 0.1 + 0.2 of L', M', S';
      no maximum, as a channel whose crosstalk brings it to 0 has the steepest part of PQ there: 80 codes of S from
      one code of T in a colour out of gamut, the same with the conversion in float64)
Exit status 1 when a target is not met (bridge: the header not IPT with the matrices of a licensed core; against the
float64 oracle a sample off by more than 1 code; against the float32 oracle a sample off by more than 1 code, or more
than 5 % of the samples of a component off; the totals over all the pictures are printed for the caller to hold to
its reference: the shares of codes off depend on the pictures). The ideal (the chain with the chroma
interpolated in float64, rounded once) is reported only: at a hard edge its unclamped Lanczos leaves the range of
the codes, which no transport can follow. The totals are made from counts, not from the arrays of every picture.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ipt_eval as E  # noqa: E402

H, W = 2160, 3840


def tunnel(path):
    a = np.fromfile(path, np.uint8).reshape(H, W, 4).astype(np.int64)[::-1]  # GL rows: bottom first
    y = (a[..., 1] << 4) | (a[..., 2] & 15)
    c = (a[..., 0] << 4) | (a[..., 2] >> 4)
    return y, c


def header(y, c):
    """the first DM metadata packet (CCM 001 6.4.1: bit i on the chroma LSB of pixel i, parity-scrambled)"""
    yf, cf = y.reshape(-1)[:1024], c.reshape(-1)[:1024]
    pop = np.vectorize(lambda v: bin(int(v)).count('1'))
    par = (pop(cf >> 1) + pop(yf)) & 1
    by = np.packbits(((cf & 1) ^ par).astype(np.uint8)).tobytes()
    p = by[5:]  # the packet header; then the payload: two bytes, YCCtoRGB, offsets, RGBtoLMS ...
    s16 = lambda i: int.from_bytes(p[i:i + 2], 'big', signed=True)
    u32 = lambda i: int.from_bytes(p[i:i + 4], 'big')
    ycc = [s16(2 + 2 * k) for k in range(9)]
    off = [u32(20 + 4 * k) for k in range(3)]
    lms = [s16(32 + 2 * k) for k in range(9)]
    return ycc, off, lms, p[60], p[61]


def licensed(ycc, off, lms):
    """the matrices of the IPT tunnel that a licensed core sends (2002 031f 0691 2002 fc5b 0443 2002 010b ea57,
    0 / 0.5 / 0.5, 42b8 fea4 fea4 fea4 42b8 ...: the user's Amlogic player, five films)"""
    return ycc == [8194, 799, 1681, 8194, -933, 1091, 8194, 267, -5545] and off == [0, 134217728, 134217728] \
        and lms == [17080, -348, -348, -348, 17080, -348, -348, -348, 17080]


def planes(data, n):
    h = []
    for name, shape in (('y', (H, W)), ('cb', (H // 2, W // 2)), ('cr', (H // 2, W // 2))):
        h.append(np.fromfile(os.path.join(data, 'p%d' % n, 'ccmC_%s.u16' % name), '<u2').reshape(shape).astype(np.int64))
    return h


def active(y):
    rows = (y != 0).any(axis=1)
    rows[:8] = False
    return rows


def bridge(data, out, tag, pics):
    bad = False
    tot = {}
    for n in pics:
        y, c = tunnel(os.path.join(out, '%s-p%d-direct.rgba' % (tag, n)))
        ycc, off, lms, depth, space = header(y, c)
        hdr_ok = licensed(ycc, off, lms) and depth == 12 and space == 2
        bad |= not hdr_ok
        h = planes(data, n)
        rows = active(y)
        res = {}
        for name, dt in (('float32 oracle', np.float32), ('float64 oracle', np.float64)):
            i, p, t = E.ipt_transport(h, dt)
            res[name] = (i, p, t)
        ref = E.ideal(h)
        res['ideal'] = (ref[..., 0], ref[:, 0::2, 1], ref[:, 0::2, 2])
        line = 'p%-5d header %s (space %d, depth %d)' % (n, 'IPT, the licensed core\'s matrices' if hdr_ok else 'WRONG',
                                                         space, depth)
        cos = np.zeros((H, W // 2), bool)
        cos[0::2] = True
        cos &= rows[:, None]
        for name, (i, p, t) in res.items():
            di = np.abs(y[rows] - i[rows])
            dp = np.abs(c[:, 0::2][rows] - p[rows])
            dtt = np.abs(c[:, 1::2][rows] - t[rows])
            # co-sited: even rows and even columns (the composer's own chroma)
            ci = np.abs(y[:, 0::2] - i[:, 0::2])[cos]
            cp = np.abs(c[:, 0::2] - p)[cos]
            ct = np.abs(c[:, 1::2] - t)[cos]
            line += ' | vs %s: I %.3f%% max %d, P %.3f%% max %d, T %.3f%% max %d (co-sited I/P/T off %.3f/%.3f/%.3f%%, max %d)' % (
                name, 100 * (di > 0).mean(), di.max(), 100 * (dp > 0).mean(), dp.max(), 100 * (dtt > 0).mean(), dtt.max(),
                100 * (ci > 0).mean(), 100 * (cp > 0).mean(), 100 * (ct > 0).mean(), max(ci.max(), cp.max(), ct.max()))
            acc = tot.setdefault(name, [[0, 0, 0], [0, 0, 0], [0, 0, 0]])
            for k, d in enumerate((di, dp, dtt)):
                acc[k][0] += int(np.count_nonzero(d))
                acc[k][1] += int(d.size)
                acc[k][2] = max(acc[k][2], int(d.max()))
            if name == 'float32 oracle':
                worst = max(di.max(), dp.max(), dtt.max())
                share = max((di > 0).mean(), (dp > 0).mean(), (dtt > 0).mean())
                bad |= worst > 1 or share > 0.05
            if name == 'float64 oracle':
                bad |= max(di.max(), dp.max(), dtt.max()) > 1
        print(line, flush=True)
    for name, v in tot.items():
        (ni, si, mi), (np_, sp, mp), (nt, st, mt) = v
        print('ALL vs %s: I %.3f%% off max %d | P %.3f%% max %d | T %.3f%% max %d'
              % (name, 100 * (ni / si), mi, 100 * (np_ / sp), mp, 100 * (nt / st), mt))
    print('BRIDGE-IPT %s' % ('FAILED' if bad else 'PASSED'))
    return 1 if bad else 0


def same(da, ta, db, tb, kind, pics):
    diff = 0
    for n in pics:
        a = np.fromfile(os.path.join(da, '%s-p%d-%s' % (ta, n, kind)), np.uint8)
        b = np.fromfile(os.path.join(db, '%s-p%d-%s' % (tb, n, kind)), np.uint8)
        d = int((a != b).sum()) if a.size == b.size else -1
        diff += d != 0
        print('p%-5d %s %s vs %s: %s' % (n, kind, ta, tb, 'identical' if d == 0 else '%d bytes differ' % d))
    print('SAME %s' % ('PASSED' if not diff else 'FAILED (%d pictures)' % diff))
    return 1 if diff else 0


def gui(out, tag, pics):
    bad = False
    for n in pics:
        yd, cd = tunnel(os.path.join(out, '%s-p%d-direct.rgba' % (tag, n)))
        yc, cc = tunnel(os.path.join(out, '%s-p%d-composed.rgba' % (tag, n)))
        g = np.fromfile(os.path.join(out, '%s-p0-gui.rgba8' % tag), np.uint8).reshape(H, W, 4)
        drawn = g[..., 3] > 0                       # the GUI's raster, rows as the texture holds them
        drawn = drawn | drawn[::-1]                 # either row order (only the pixels never under it are compared)
        pair = drawn.copy()
        pair[:, 0::2] |= drawn[:, 1::2]
        pair[:, 1::2] |= drawn[:, 0::2]
        keep = ~pair
        keep[:8] = False
        d = (yd != yc) | (cd != cc)
        print('p%-5d pixels not drawn over: %d, of them different from the direct way: %d; drawn over: %d, changed: %d'
              % (n, keep.sum(), int((d & keep).sum()), pair.sum(), int((d & pair).sum())))
        bad |= bool((d & keep).any())
    print('GUI %s' % ('FAILED' if bad else 'PASSED'))
    return 1 if bad else 0


def decode_rgb(y, c, ipt):
    cb = np.repeat(c[:, 0::2], 2, axis=1).astype(np.float64)
    cr = np.repeat(c[:, 1::2], 2, axis=1).astype(np.float64)
    if ipt:
        v = np.stack([y / 4096.0, cb / 4096.0 - 0.5, cr / 4096.0 - 0.5], -1)
        lms = E.mm(E.P5_LMS, E.eotf(E.mm(E.P5_YCC, v)))
        return E.oetf(E.mm(np.linalg.inv(E.RPU_LMS), lms))
    v = np.stack([y / 4096.0 - E.RPU_OFF[0], cb / 4096.0 - E.RPU_OFF[1], cr / 4096.0 - E.RPU_OFF[2]], -1)
    return np.clip(E.mm(E.RPU_YCC, v), 0, 1)


def scaled(dy, ty, di, ti, pics):
    bad = False
    for n in pics:
        y0, c0 = tunnel(os.path.join(dy, '%s-p%d-direct.rgba' % (ty, n)))
        y1, c1 = tunnel(os.path.join(di, '%s-p%d-direct.rgba' % (ti, n)))
        rows = active(y0)
        a = decode_rgb(y0, c0, False)[rows][:, 0::2] * 4095
        b = decode_rgb(y1, c1, True)[rows][:, 0::2] * 4095
        d = np.abs(a - b).max(axis=-1)
        print('p%-5d scaled, even pixels, R\'G\'B\' of the two tunnels decoded: mean %.3f, p99 %.2f, max %.1f codes of 4095'
              % (n, d.mean(), np.percentile(d, 99), d.max()))
        bad |= np.percentile(d, 99) > 2.0
    print('SCALED %s' % ('FAILED' if bad else 'PASSED'))
    return 1 if bad else 0


def decode_pq_lms(y, c, ycc, off, lms):
    """the even pixels of a tunnel decoded as a display decodes them from its header: LMS = RGBtoLMS EOTF(YCCtoRGB
    (codes - offsets)), given in PQ (x 4095)"""
    v = np.stack([y[:, 0::2] / 4096.0 - off[0] / 268435456.0, c[:, 0::2] / 4096.0 - off[1] / 268435456.0,
                  c[:, 1::2] / 4096.0 - off[2] / 268435456.0], -1)
    m = np.array(ycc, float).reshape(3, 3) / 8192
    x = np.array(lms, float).reshape(3, 3) / 16384
    return E.oetf(E.mm(x, E.eotf(E.mm(m, v)))) * 4095


def p5(d0, t0, d1, t1, pics):
    bad = False
    for n in pics:
        y0, c0 = tunnel(os.path.join(d0, '%s-p%d-direct.rgba' % (t0, n)))
        y1, c1 = tunnel(os.path.join(d1, '%s-p%d-direct.rgba' % (t1, n)))
        h0, h1 = header(y0, c0), header(y1, c1)
        lic = licensed(*h1[:3]) and h1[3] == 12 and h1[4] == 2
        rows = active(y0) & active(y1)
        # the oracle: the codes of the picture as composed, decoded with the header of its RPU and encoded with the
        # tunnel's, in float64, rounded once (the even pixel of each pair, where the composer's chroma is)
        v = np.stack([y0[:, 0::2] / 4096.0 - h0[1][0] / 268435456.0, c0[:, 0::2] / 4096.0 - h0[1][1] / 268435456.0,
                      c0[:, 1::2] / 4096.0 - h0[1][2] / 268435456.0], -1)
        m0, x0 = np.array(h0[0], float).reshape(3, 3) / 8192, np.array(h0[2], float).reshape(3, 3) / 16384
        m1, x1 = np.array(h1[0], float).reshape(3, 3) / 8192, np.array(h1[2], float).reshape(3, 3) / 16384
        t = E.mm(np.linalg.inv(x1) @ x0, E.eotf(E.mm(m0, v)))
        ipt = E.mm(np.linalg.inv(m1), E.oetf(t)) + np.array(h1[1], float) / 268435456.0
        want = np.clip(np.floor(ipt * 4096 + 0.5), 0, 4095)
        got = np.stack([y1[:, 0::2], c1[:, 0::2], c1[:, 1::2]], -1)
        dd = np.abs(want - got)[rows]
        # ... and what it costs: the two decoded as a display decodes them from their own headers, in PQ of LMS
        a = decode_pq_lms(y0, c0, *h0[:3])[rows]
        b = decode_pq_lms(y1, c1, *h1[:3])[rows]
        d = np.abs(a - b).max(axis=-1)
        print('p%-5d profile 5 in the tunnel (header %s): against the float64 conversion of its codes, I/P/T off '
              '%.3f/%.3f/%.3f%%, max %d %d %d; decoded against the picture as composed (PQ of LMS, codes of 4095): '
              'mean %.3f, p99 %.3f' % (n, 'the licensed core\'s' if lic else 'WRONG', 100 * (dd[..., 0] > 0).mean(),
                                        100 * (dd[..., 1] > 0).mean(), 100 * (dd[..., 2] > 0).mean(), dd[..., 0].max(),
                                        dd[..., 1].max(), dd[..., 2].max(), d.mean(), np.percentile(d, 99)))
        bad |= not lic or dd.max() > 1 or (dd > 0).mean() > 0.01 or np.percentile(d, 99) > 2.0
    print('P5-IPT %s' % ('FAILED' if bad else 'PASSED'))
    return 1 if bad else 0


if __name__ == '__main__':
    m, a = sys.argv[1], sys.argv[2:]
    if m == 'bridge':
        sys.exit(bridge(a[0], a[1], a[2], [int(x) for x in a[3:]]))
    if m == 'same':
        sys.exit(same(a[0], a[1], a[2], a[3], a[4], [int(x) for x in a[5:]]))
    if m == 'gui':
        sys.exit(gui(a[0], a[1], [int(x) for x in a[2:]]))
    if m == 'p5':
        sys.exit(p5(a[0], a[1], a[2], a[3], [int(x) for x in a[4:]]))
    if m == 'scaled':
        sys.exit(scaled(a[0], a[1], a[2], a[3], [int(x) for x in a[4:]]))
