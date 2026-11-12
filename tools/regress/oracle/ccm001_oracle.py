#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Independent oracle of the composer of ETSI GS CCM 001 V1.1.1 (2017-02), clause 5.4.

Written from the text of the specification only (clauses 3.2, 5.2, 5.3, 5.4 and annex B.3),
without reading any other implementation of it. Every step is a literal transcription of the
clause's pseudo-code in 64-bit integer arithmetic, in two forms:

  * a scalar form (functions s_*) with Python integers that follows the pseudo-code line by line
    and checks that every intermediate value fits in a signed 64-bit integer;
  * a vectorised form (functions v_*) with numpy int64 arrays, used on whole pictures, guarded by
    an upper bound of every product computed from the coefficients before it runs.

The self test checks that both forms agree on random inputs and on every real parameter set given
to it. Readings of the text that are not unique are listed in README.md (R1..R12) and the ones
with a real alternative are selectable with --reading so that a difference against another
implementation can be attributed. Mutations (--mutate) change one aspect at a time on purpose;
they exist only to prove that a comparison can see that kind of fault.

Parameter sets use the field names of FFmpeg's AVDOVIMetadata (libavutil/dovi_meta.h), whose
coefficients are already the fixed-point values fp_* of clause 5.3 ((int << denom) + frac).
"""

import argparse
import json
import os
import sys

import numpy as np

I64 = np.int64
INT64_MAX = (1 << 63) - 1
INT64_MIN = -(1 << 63)

READING_CHOICES = {
    # R2: which index of s_{i,j} in the luma down-sampling formula of 5.4.2.3.3 is the column.
    'ds-axes': ('i-is-column', 'i-is-row'),
    # R3: grouping of the down-sampling formula (each 1-2-1 row rounded, then the two averaged).
    'ds-round': ('per-row', 'joint'),
    # R7: up-sampling of a quarter-size enhancement layer (annex B.3, informative).
    'el-up': ('auto', 'annex-b3', 'none'),
}
DEFAULT_READINGS = {'ds-axes': 'i-is-column', 'ds-round': 'per-row', 'el-up': 'auto'}

MUTATIONS = {
    'pivot-le': '5.4.2.2: s <= pivot_value[idx+1] instead of <',
    'top-first': 'R1: a sample at or above the last pivot uses piece 0 instead of the last piece',
    'poly-noclamp': '5.4.2.3.2: no clamp of s to [pivot_value[0], pivot_value[last]]',
    'poly-shift': '5.4.2.3.2: v = vv >> (5 + coefficient_log2_denom)',
    'poly-noneg': '5.4.2.3.2: no clamp of a negative vv to 0',
    'mmr-transpose': '5.4.2.3.3: terms summed term-major instead of order-major',
    'mmr-tt7': '5.4.2.3.3: tt[7] = (tt[4]*tt[2]) >> 20',
    'mmr-ownclamp': '5.4.2.3.3: s0 clamped with the pivots of the chroma component',
    'mmr-shift': '5.4.2.3.3: v = rr >> (5 + coefficient_log2_denom)',
    'ds-noround': '5.4.2.3.3: down-sampling without its rounding offsets',
    'ds-axes': '5.4.2.3.3: down-sampling with i as the row index',
    'nlq-nosign': '5.4.3.2: no rr -= sign',
    'nlq-noclamp': '5.4.3.2: no clamp of dq to [-RR, RR]',
    'nlq-shift': '5.4.3.2: r = dq >> (coefficient_log2_denom - 4 - EL_bit_depth)',
    'nlq-thr': '5.4.3.2: TT without the sign',
    'recon-noround': '5.4.3.3: no h += 1 << (15 - out_bit_depth)',
    'recon-noresidual': '5.4.3.3: the residual r is not added',
    'el-nearest': 'B.3: enhancement layer up-sampled by sample repetition',
    'el-hfirst': 'B.3: horizontal up-sampling before the vertical one',
}


class Fit64Error(ArithmeticError):
    pass


def chk(x):
    """Scalar form: an intermediate value must fit a signed 64-bit integer."""
    if x > INT64_MAX or x < INT64_MIN:
        raise Fit64Error(x)
    return x


def clip3(x, y, z):
    """Clip3(x, y, z) of 3.2.5: x if z < x, y if z > y, z otherwise."""
    return x if z < x else (y if z > y else z)


# ---------------------------------------------------------------------------------------------
# Parameter set (FFmpeg names -> clause 5.3 names)
# ---------------------------------------------------------------------------------------------

class Params:
    def __init__(self, d):
        h = d['hdr']
        m = d['map']
        self.raw = d
        self.BL_bit_depth = int(h['bl_bit_depth'])
        self.EL_bit_depth = int(h['el_bit_depth'])
        self.hdr_bit_depth = int(h['vdr_bit_depth'])
        self.coefficient_log2_denom = int(h['coef_log2_denom'])
        self.disable_residual_flag = int(h['disable_residual_flag'])
        self.nlq_method_idc = int(m.get('nlq_method_idc', -1))
        if self.BL_bit_depth not in (8, 10):
            raise ValueError('BL_bit_depth %d outside 5.3.2 (8 or 10)' % self.BL_bit_depth)
        self.curves = []
        for c in m['curves']:
            n = int(c['num_pivots'])
            cv = {
                'pivot_value': [int(x) for x in c['pivots'][:n]],
                'num_pivots_minus2': n - 2,
                'mapping_idc': [int(x) for x in c['mapping_idc'][:n - 1]],
                'poly_order_minus1': [int(x) - 1 for x in c['poly_order'][:n - 1]],
                'fp_poly_coef': [[int(x) for x in row] for row in c['poly_coef'][:n - 1]],
                'MMR_order': [int(x) for x in c['mmr_order'][:n - 1]],
                'fp_mmr_constant': [int(x) for x in c['mmr_constant'][:n - 1]],
                # spec mmr_coef[i][j], i = 1..MMR_order -> FFmpeg mmr_coef[piece][i - 1][j]
                'fp_mmr_coef': [[None] + [[int(x) for x in row] for row in piece]
                                for piece in c['mmr_coef'][:n - 1]],
            }
            self.curves.append(cv)
        self.nlq = []
        for q in m.get('nlq', []):
            self.nlq.append({
                'nlq_offset': int(q['nlq_offset']),
                'fp_hdr_in_max': int(q['vdr_in_max']),
                'fp_linear_deadzone_slope': int(q['linear_deadzone_slope']),
                'fp_linear_deadzone_threshold': int(q['linear_deadzone_threshold']),
            })
        for c in (0,):
            if any(x != 0 for x in self.curves[c]['mapping_idc']):
                raise ValueError('5.3.3.1: mapping_idc of component 0 shall be 0')

    def out_bit_depth(self, bl_transfer='pq'):
        # 5.4.3.3: 14 for a BT.1886 base layer, hdr_bit_depth for an ST 2084 one.
        return 14 if bl_transfer == 'bt1886' else self.hdr_bit_depth

    def residual_used(self, el_present):
        # 5.3.2: without an EL picture disable_residual_flag is taken as 1. FFmpeg reports
        # nlq_method_idc = -1 (AV_DOVI_NLQ_NONE) when the RPU carries no NLQ parameters.
        return bool(el_present) and not self.disable_residual_flag and self.nlq_method_idc == 0


# ---------------------------------------------------------------------------------------------
# Scalar forms: literal transcription of the pseudo-code
# ---------------------------------------------------------------------------------------------

def s_pivot_idx(s, pivot_value, mut=()):
    """5.4.2.2."""
    num_pivots_minus2 = len(pivot_value) - 2
    pivot_idx = num_pivots_minus2 + 1
    for idx in range(num_pivots_minus2 + 1):
        below = (s <= pivot_value[idx + 1]) if 'pivot-le' in mut else (s < pivot_value[idx + 1])
        if below:
            pivot_idx = idx
            break
    return pivot_idx


def piece_of(pivot_idx, num_pivots_minus2, mut=()):
    """Reading R1: pivot_idx = num_pivots_minus2 + 1 (sample at or above the last pivot) has no
    coefficients; the mapping clamps the sample to the last pivot, so the last piece is used."""
    if pivot_idx == num_pivots_minus2 + 1:
        return 0 if 'top-first' in mut else num_pivots_minus2
    return pivot_idx


def s_poly(s, pivot_value, poly_order_minus1, poly_coef, BL_bit_depth, coefficient_log2_denom,
           mut=()):
    """5.4.2.3.2."""
    if 'poly-noclamp' not in mut:
        if s < pivot_value[0]:
            s = pivot_value[0]
        if s > pivot_value[len(pivot_value) - 1]:
            s = pivot_value[len(pivot_value) - 1]
    ss = 1
    shift = 20  # 2*(maximum BL_bit_depth)
    vv = 0
    for i in range(poly_order_minus1 + 2):
        vv = chk(vv + chk(poly_coef[i] * chk(ss << shift)))
        ss = chk(ss * s)
        shift -= BL_bit_depth
    if 'poly-noneg' not in mut:
        vv = 0 if vv < 0 else vv
    v = vv >> (4 + coefficient_log2_denom + (1 if 'poly-shift' in mut else 0))
    v = 0xffff if v > 0xffff else v
    return v


def s_mmr(s0, s1, s2, cmp, pivot_values, MMR_order, mmr_constant, mmr_coef, BL_bit_depth,
          coefficient_log2_denom, mut=()):
    """5.4.2.3.3 (after the luma down-sampling). pivot_values[k] = pivot_value[k][]."""
    s = [s0, s1, s2]
    for k in range(3):
        pv = pivot_values[cmp] if ('mmr-ownclamp' in mut and k == 0) else pivot_values[k]
        if s[k] < pv[0]:
            s[k] = pv[0]
        if s[k] > pv[len(pv) - 1]:
            s[k] = pv[len(pv) - 1]
    s0, s1, s2 = s
    B = BL_bit_depth
    tt = [0] * 22
    tt[0] = 1 << 20
    if MMR_order >= 1:
        tt[1] = chk(s0 << (20 - B))
        tt[2] = chk(s1 << (20 - B))
        tt[3] = chk(s2 << (20 - B))
        tt[4] = chk(chk(s0 * s1) << (20 - 2 * B))
        tt[5] = chk(chk(s0 * s2) << (20 - 2 * B))
        tt[6] = chk(chk(s1 * s2) << (20 - 2 * B))
        tt[7] = chk(tt[4] * (tt[2] if 'mmr-tt7' in mut else tt[3])) >> 20
    if MMR_order >= 2:
        tt[8] = chk(chk(s0 * s0) << (20 - 2 * B))
        tt[9] = chk(chk(s1 * s1) << (20 - 2 * B))
        tt[10] = chk(chk(s2 * s2) << (20 - 2 * B))
        tt[11] = chk(tt[4] * tt[4]) >> 20
        tt[12] = chk(tt[5] * tt[5]) >> 20
        tt[13] = chk(tt[6] * tt[6]) >> 20
        tt[14] = chk(tt[7] * tt[7]) >> 20
    if MMR_order >= 3:
        tt[15] = chk(tt[1] * tt[8]) >> 20
        tt[16] = chk(tt[2] * tt[9]) >> 20
        tt[17] = chk(tt[3] * tt[10]) >> 20
        tt[18] = chk(tt[4] * tt[11]) >> 20
        tt[19] = chk(tt[5] * tt[12]) >> 20
        tt[20] = chk(tt[6] * tt[13]) >> 20
        tt[21] = chk(tt[7] * tt[14]) >> 20
    rr = chk(mmr_constant * tt[0])
    cnt = 1
    for i in range(1, MMR_order + 1):
        for j in range(7):
            k = (1 + j * MMR_order + (i - 1)) if 'mmr-transpose' in mut else cnt
            rr = chk(rr + chk(mmr_coef[i][j] * tt[k]))
            cnt += 1
    rr = 0 if rr < 0 else rr
    v = rr >> (4 + coefficient_log2_denom + (1 if 'mmr-shift' in mut else 0))
    v = 0xffff if v > 0xffff else v
    return v


def s_downsample(L, cx, cy, readings, mut=()):
    """5.4.2.3.3, luma down-sampling for the chroma position (cx, cy); L[y][x] row-major."""
    H = len(L)
    W = len(L[0])
    axes = 'i-is-row' if 'ds-axes' in mut else readings['ds-axes']

    def s(i, j):
        x, y = (i, j) if axes == 'i-is-column' else (j, i)
        x = min(max(x, 0), W - 1)  # pixel replication at the picture boundary
        y = min(max(y, 0), H - 1)
        return int(L[y][x])

    i, j = (cx, cy) if axes == 'i-is-column' else (cy, cx)
    a = s(2 * i - 1, 2 * j) + 2 * s(2 * i, 2 * j) + s(2 * i + 1, 2 * j)
    b = s(2 * i - 1, 2 * j + 1) + 2 * s(2 * i, 2 * j + 1) + s(2 * i + 1, 2 * j + 1)
    if 'ds-noround' in mut:
        return ((a >> 2) + (b >> 2)) >> 1
    if readings['ds-round'] == 'joint':
        return (a + b + 4) >> 3
    return (((a + 2) >> 2) + ((b + 2) >> 2) + 1) >> 1


def s_nlq(e, nlq_offset, T, S, R, EL_bit_depth, coefficient_log2_denom, mut=()):
    """5.4.3.2 (NLQ_LINEAR_DZ). T, S, R = fp_linear_deadzone_threshold, _slope, fp_hdr_in_max."""
    rr = e - nlq_offset
    if rr == 0:
        return 0
    sign = -1 if rr < 0 else 1
    rr <<= 1
    if 'nlq-nosign' not in mut:
        rr -= sign
    rr <<= (10 - EL_bit_depth)
    dq = chk(rr * S)
    TT = chk((T << (10 - EL_bit_depth + 1)) * (1 if 'nlq-thr' in mut else sign))
    dq = chk(dq + TT)
    RR = chk(R << (10 - EL_bit_depth + 1))
    if 'nlq-noclamp' not in mut:
        if dq > RR:
            dq = RR
        elif dq < -RR:
            dq = -RR
    return dq >> (coefficient_log2_denom - 5 - EL_bit_depth + (1 if 'nlq-shift' in mut else 0))


def s_recon(v, r, disable_residual_flag, out_bit_depth, mut=()):
    """5.4.3.3."""
    MAXOUT = (1 << out_bit_depth) - 1
    h = v
    if not disable_residual_flag and 'recon-noresidual' not in mut:
        h += r
    if 'recon-noround' not in mut:
        h += (1 << (15 - out_bit_depth))
    h >>= (16 - out_bit_depth)
    h = 0 if h < 0 else h
    h = MAXOUT if h > MAXOUT else h
    return h


# Annex B.3 filters (informative), one line of samples y[] -> twice as many.
def _rep(y, n):
    return y[min(max(n, 0), len(y) - 1)]


def s_up_v_luma(y):
    out = []
    for n in range(len(y)):
        out.append(clip3(0, 0xFFFF, (-3 * _rep(y, n - 2) + 29 * _rep(y, n - 1) + 111 * _rep(y, n)
                                     - 9 * _rep(y, n + 1) + 64) >> 7))
        out.append(clip3(0, 0xFFFF, (-9 * _rep(y, n - 1) + 111 * _rep(y, n) + 29 * _rep(y, n + 1)
                                     - 3 * _rep(y, n + 2) + 64) >> 7))
    return out


def s_up_v_chroma(y):
    out = []
    for n in range(len(y)):
        out.append(clip3(0, 0xFFFF, (64 * _rep(y, n - 1) + 192 * _rep(y, n) + 128) >> 8))
        out.append(clip3(0, 0xFFFF, (192 * _rep(y, n) + 64 * _rep(y, n + 1) + 128) >> 8))
    return out


def s_up_h(y):
    out = []
    for n in range(len(y)):
        out.append(y[n])
        out.append(clip3(0, 0xFFFF, (22 * _rep(y, n - 3) + 94 * _rep(y, n - 2) - 524 * _rep(y, n - 1)
                                     + 2456 * _rep(y, n) + 2456 * _rep(y, n + 1)
                                     - 524 * _rep(y, n + 2) + 94 * _rep(y, n + 3)
                                     + 22 * _rep(y, n + 4) + 2048) >> 12))
    return out


# ---------------------------------------------------------------------------------------------
# Vectorised forms (numpy int64), same steps
# ---------------------------------------------------------------------------------------------

def _a(x):
    return np.asarray(x, dtype=I64)


def v_pivot_idx(s, pivot_value, mut=()):
    num_pivots_minus2 = len(pivot_value) - 2
    idx = np.full(s.shape, num_pivots_minus2 + 1, dtype=I64)
    found = np.zeros(s.shape, dtype=bool)
    for k in range(num_pivots_minus2 + 1):
        below = (s <= pivot_value[k + 1]) if 'pivot-le' in mut else (s < pivot_value[k + 1])
        m = below & ~found
        idx[m] = k
        found |= m
    return idx


def v_piece(pivot_idx, num_pivots_minus2, mut=()):
    top = pivot_idx == num_pivots_minus2 + 1
    return np.where(top, 0 if 'top-first' in mut else num_pivots_minus2, pivot_idx)


def poly_bound(pivot_value, poly_order_minus1, poly_coef, BL_bit_depth):
    smax = max(abs(pivot_value[0]), abs(pivot_value[-1]))
    ss, shift, tot = 1, 20, 0
    for i in range(poly_order_minus1 + 2):
        tot += abs(poly_coef[i]) * (ss << shift)
        ss *= smax
        shift -= BL_bit_depth
    return tot


def v_poly(s, pivot_value, poly_order_minus1, poly_coef, BL_bit_depth, coefficient_log2_denom,
           mut=()):
    if poly_bound(pivot_value, poly_order_minus1, poly_coef, BL_bit_depth) > INT64_MAX:
        raise Fit64Error('polynomial piece may leave 64 bits')
    s = _a(s)
    if 'poly-noclamp' not in mut:
        s = np.where(s < pivot_value[0], pivot_value[0], s)
        s = np.where(s > pivot_value[-1], pivot_value[-1], s)
    ss = np.ones_like(s)
    shift = 20
    vv = np.zeros_like(s)
    for i in range(poly_order_minus1 + 2):
        vv = vv + I64(poly_coef[i]) * (ss << shift)
        ss = ss * s
        shift -= BL_bit_depth
    if 'poly-noneg' not in mut:
        vv = np.where(vv < 0, 0, vv)
    v = vv >> (4 + coefficient_log2_denom + (1 if 'poly-shift' in mut else 0))
    return np.where(v > 0xffff, 0xffff, v)


def mmr_bound(pivot_values, MMR_order, mmr_constant, mmr_coef, BL_bit_depth):
    """Upper bound of every intermediate value of v_mmr. Pivots are non-negative and the samples
    are clamped to them, so every tt is non-negative and largest at the largest pivot; one
    common largest sample m is used for the three components (covers every mutation too)."""
    B = BL_bit_depth
    m = max(max(pv[0], pv[-1]) for pv in pivot_values)
    tt = [0] * 22
    tt[0] = 1 << 20
    tt[1] = tt[2] = tt[3] = m << (20 - B)
    tt[4] = tt[5] = tt[6] = tt[8] = tt[9] = tt[10] = (m * m) << (20 - 2 * B)
    prods = [tt[4] * tt[3]]
    tt[7] = prods[-1] >> 20
    for k in (11, 12, 13, 14):
        src = tt[7] if k == 14 else tt[4]
        prods.append(src * src)
        tt[k] = prods[-1] >> 20
    for k, (a, b) in zip(range(15, 22), ((1, 8), (2, 9), (3, 10), (4, 11), (5, 12), (6, 13),
                                          (7, 14))):
        prods.append(tt[a] * tt[b])
        tt[k] = prods[-1] >> 20
    tot = abs(mmr_constant) * tt[0]
    for i in range(1, MMR_order + 1):
        for j in range(7):
            tot += abs(mmr_coef[i][j]) * max(tt[1:])
    return max([tot] + prods)


def v_mmr(s0, s1, s2, cmp, pivot_values, MMR_order, mmr_constant, mmr_coef, BL_bit_depth,
          coefficient_log2_denom, mut=()):
    if mmr_bound(pivot_values, MMR_order, mmr_constant, mmr_coef, BL_bit_depth) > INT64_MAX:
        raise Fit64Error('MMR piece may leave 64 bits')
    s = [_a(s0), _a(s1), _a(s2)]
    for k in range(3):
        pv = pivot_values[cmp] if ('mmr-ownclamp' in mut and k == 0) else pivot_values[k]
        s[k] = np.where(s[k] < pv[0], pv[0], s[k])
        s[k] = np.where(s[k] > pv[-1], pv[-1], s[k])
    s0, s1, s2 = s
    B = BL_bit_depth
    tt = [None] * 22
    tt[0] = I64(1 << 20)
    if MMR_order >= 1:
        tt[1] = s0 << (20 - B)
        tt[2] = s1 << (20 - B)
        tt[3] = s2 << (20 - B)
        tt[4] = (s0 * s1) << (20 - 2 * B)
        tt[5] = (s0 * s2) << (20 - 2 * B)
        tt[6] = (s1 * s2) << (20 - 2 * B)
        tt[7] = (tt[4] * (tt[2] if 'mmr-tt7' in mut else tt[3])) >> 20
    if MMR_order >= 2:
        tt[8] = (s0 * s0) << (20 - 2 * B)
        tt[9] = (s1 * s1) << (20 - 2 * B)
        tt[10] = (s2 * s2) << (20 - 2 * B)
        tt[11] = (tt[4] * tt[4]) >> 20
        tt[12] = (tt[5] * tt[5]) >> 20
        tt[13] = (tt[6] * tt[6]) >> 20
        tt[14] = (tt[7] * tt[7]) >> 20
    if MMR_order >= 3:
        tt[15] = (tt[1] * tt[8]) >> 20
        tt[16] = (tt[2] * tt[9]) >> 20
        tt[17] = (tt[3] * tt[10]) >> 20
        tt[18] = (tt[4] * tt[11]) >> 20
        tt[19] = (tt[5] * tt[12]) >> 20
        tt[20] = (tt[6] * tt[13]) >> 20
        tt[21] = (tt[7] * tt[14]) >> 20
    rr = np.zeros_like(s0) + I64(mmr_constant) * tt[0]
    cnt = 1
    for i in range(1, MMR_order + 1):
        for j in range(7):
            k = (1 + j * MMR_order + (i - 1)) if 'mmr-transpose' in mut else cnt
            rr = rr + I64(mmr_coef[i][j]) * tt[k]
            cnt += 1
    rr = np.where(rr < 0, 0, rr)
    v = rr >> (4 + coefficient_log2_denom + (1 if 'mmr-shift' in mut else 0))
    return np.where(v > 0xffff, 0xffff, v)


def v_downsample(L, readings, mut=()):
    L = _a(L)
    H, W = L.shape
    HC, WC = H // 2, W // 2
    axes = 'i-is-row' if 'ds-axes' in mut else readings['ds-axes']
    if axes == 'i-is-column':
        # i = column: the 1-2-1 filter runs along x around 2i, the average along rows 2j, 2j+1
        cx = np.arange(WC)
        cy = np.arange(HC)
        cols = [np.clip(2 * cx + d, 0, W - 1) for d in (-1, 0, 1)]
        rows = [np.clip(2 * cy + d, 0, H - 1) for d in (0, 1)]
        def row_sum(r):
            Lr = L[r, :]
            return Lr[:, cols[0]] + 2 * Lr[:, cols[1]] + Lr[:, cols[2]]
        a = row_sum(rows[0])
        b = row_sum(rows[1])
    else:
        cx = np.arange(WC)
        cy = np.arange(HC)
        rws = [np.clip(2 * cy + d, 0, H - 1) for d in (-1, 0, 1)]
        cls = [np.clip(2 * cx + d, 0, W - 1) for d in (0, 1)]
        def col_sum(c):
            Lc = L[:, c]
            return Lc[rws[0], :] + 2 * Lc[rws[1], :] + Lc[rws[2], :]
        a = col_sum(cls[0])
        b = col_sum(cls[1])
    if 'ds-noround' in mut:
        return ((a >> 2) + (b >> 2)) >> 1
    if readings['ds-round'] == 'joint':
        return (a + b + 4) >> 3
    return (((a + 2) >> 2) + ((b + 2) >> 2) + 1) >> 1


def v_nlq(e, nlq_offset, T, S, R, EL_bit_depth, coefficient_log2_denom, mut=()):
    rrmax = ((0xFFFF + nlq_offset) * 2 + 1) << (10 - EL_bit_depth)
    if rrmax * S + (T << (11 - EL_bit_depth)) + (R << (11 - EL_bit_depth)) > INT64_MAX:
        raise Fit64Error('NLQ may leave 64 bits')
    e = _a(e)
    rr = e - nlq_offset
    nz = rr != 0
    sign = np.where(rr < 0, -1, 1).astype(I64)
    rr = rr << 1
    if 'nlq-nosign' not in mut:
        rr = rr - sign
    rr = rr << (10 - EL_bit_depth)
    dq = rr * I64(S)
    TT = I64(T << (10 - EL_bit_depth + 1)) * (np.ones_like(sign) if 'nlq-thr' in mut else sign)
    dq = dq + TT
    RR = I64(R << (10 - EL_bit_depth + 1))
    if 'nlq-noclamp' not in mut:
        dq = np.where(dq > RR, RR, np.where(dq < -RR, -RR, dq))
    r = dq >> (coefficient_log2_denom - 5 - EL_bit_depth + (1 if 'nlq-shift' in mut else 0))
    return np.where(nz, r, 0)


def v_recon(v, r, disable_residual_flag, out_bit_depth, mut=()):
    MAXOUT = (1 << out_bit_depth) - 1
    h = _a(v)
    if not disable_residual_flag and 'recon-noresidual' not in mut:
        h = h + _a(r)
    if 'recon-noround' not in mut:
        h = h + (1 << (15 - out_bit_depth))
    h = h >> (16 - out_bit_depth)
    h = np.where(h < 0, 0, h)
    return np.where(h > MAXOUT, MAXOUT, h)


def _taps(A, axis, offsets, weights, add, shift):
    n = A.shape[axis]
    acc = None
    for d, w in zip(offsets, weights):
        idx = np.clip(np.arange(n) + d, 0, n - 1)
        t = np.take(A, idx, axis=axis) * w
        acc = t if acc is None else acc + t
    return np.clip((acc + add) >> shift, 0, 0xFFFF)


def _interleave(even, odd, axis):
    shp = list(even.shape)
    shp[axis] *= 2
    out = np.empty(shp, dtype=I64)
    sl_e = [slice(None)] * len(shp)
    sl_o = [slice(None)] * len(shp)
    sl_e[axis] = slice(0, None, 2)
    sl_o[axis] = slice(1, None, 2)
    out[tuple(sl_e)] = even
    out[tuple(sl_o)] = odd
    return out


def v_up_vertical(A, cmp):
    A = _a(A)
    if cmp == 0:
        ev = _taps(A, 0, (-2, -1, 0, 1), (-3, 29, 111, -9), 64, 7)
        od = _taps(A, 0, (-1, 0, 1, 2), (-9, 111, 29, -3), 64, 7)
    else:
        ev = _taps(A, 0, (-1, 0), (64, 192), 128, 8)
        od = _taps(A, 0, (0, 1), (192, 64), 128, 8)
    return _interleave(ev, od, 0)


def v_up_horizontal(A):
    A = _a(A)
    od = _taps(A, 1, (-3, -2, -1, 0, 1, 2, 3, 4), (22, 94, -524, 2456, 2456, -524, 94, 22), 2048, 12)
    return _interleave(A, od, 1)


def v_el_upsample(E, cmp, mut=()):
    """Annex B.3: vertical up-sampling first, then horizontal."""
    if 'el-nearest' in mut:
        return np.repeat(np.repeat(_a(E), 2, axis=0), 2, axis=1)
    if 'el-hfirst' in mut:
        return v_up_vertical(v_up_horizontal(E), cmp)
    return v_up_horizontal(v_up_vertical(E, cmp))


# ---------------------------------------------------------------------------------------------
# The composer (clause 5.4) on one picture
# ---------------------------------------------------------------------------------------------

def compose(P, bl, el=None, readings=None, mut=(), bl_transfer='pq'):
    """bl = [Y (H x W), Cb, Cr (H/2 x W/2)]; el = [Y, Cb, Cr] at BL size or a quarter of it.
    Returns dict with V (mapped BL, 16 bit), R (inverse-quantised EL), H (reconstructed),
    piece (piece index per sample), kind (0 polynomial, 1 MMR per sample)."""
    readings = dict(DEFAULT_READINGS, **(readings or {}))
    B = P.BL_bit_depth
    D = P.coefficient_log2_denom
    Y = _a(bl[0])
    Hh, Ww = Y.shape
    if Hh % 2 or Ww % 2:
        raise ValueError('5.2.2 b): chroma arrays are half the luma size; odd luma size')
    C = [Y, _a(bl[1]), _a(bl[2])]
    for c in (1, 2):
        if C[c].shape != (Hh // 2, Ww // 2):
            raise ValueError('5.2.2 b): chroma array %d has shape %s' % (c, C[c].shape))
    pvs = [cv['pivot_value'] for cv in P.curves]
    V = [None] * 3
    piece = [None] * 3
    kind = [None] * 3
    S0d = None
    for c in range(3):
        cv = P.curves[c]
        s = C[c]
        pidx = v_pivot_idx(s, cv['pivot_value'], mut)
        pc = v_piece(pidx, cv['num_pivots_minus2'], mut)
        v = np.zeros(s.shape, dtype=I64)
        kd = np.zeros(s.shape, dtype=np.int8)
        for k in range(cv['num_pivots_minus2'] + 1):
            m = pc == k
            if not m.any():
                continue
            if cv['mapping_idc'][k] == 0:
                v[m] = v_poly(s[m], cv['pivot_value'], cv['poly_order_minus1'][k],
                              cv['fp_poly_coef'][k], B, D, mut)
            else:
                if c == 0:
                    raise ValueError('5.3.3.1: MMR on component 0')
                if S0d is None:
                    S0d = v_downsample(Y, readings, mut)
                v[m] = v_mmr(S0d[m], C[1][m], C[2][m], c, pvs, cv['MMR_order'][k],
                             cv['fp_mmr_constant'][k], cv['fp_mmr_coef'][k], B, D, mut)
                kd[m] = 1
        V[c] = v
        piece[c] = pc.astype(np.int8)
        kind[c] = kd
    use_res = P.residual_used(el is not None)
    R = [np.zeros_like(V[c]) for c in range(3)]
    E_up = [None] * 3
    if use_res:
        for c in range(3):
            E = _a(el[c])
            mode = readings['el-up']
            if mode == 'auto':
                mode = 'none' if E.shape == V[c].shape else 'annex-b3'
            if mode == 'annex-b3':
                E = v_el_upsample(E, c, mut)
            if E.shape != V[c].shape:
                raise ValueError('EL component %d has shape %s, mapped BL %s'
                                 % (c, E.shape, V[c].shape))
            E_up[c] = E
            q = P.nlq[c]
            R[c] = v_nlq(E, q['nlq_offset'], q['fp_linear_deadzone_threshold'],
                         q['fp_linear_deadzone_slope'], q['fp_hdr_in_max'], P.EL_bit_depth, D, mut)
    ob = P.out_bit_depth(bl_transfer)
    Hout = [v_recon(V[c], R[c], 0 if use_res else 1, ob, mut) for c in range(3)]
    return {'V': V, 'R': R, 'H': Hout, 'piece': piece, 'kind': kind, 'E': E_up,
            'out_bit_depth': ob, 'residual': use_res, 'S0d': S0d}


def lut(P, mut=()):
    """Per component and BL code: pivot index (5.4.2.2), piece used (R1), mapping and, for a
    polynomial piece, the mapped value v of 5.4.2.3.2 (16 bit)."""
    out = []
    codes = np.arange(1 << P.BL_bit_depth, dtype=I64)
    for c in range(3):
        cv = P.curves[c]
        pidx = v_pivot_idx(codes, cv['pivot_value'], mut)
        pc = v_piece(pidx, cv['num_pivots_minus2'], mut)
        v = np.full(codes.shape, -1, dtype=I64)
        mp = np.zeros(codes.shape, dtype=I64)
        for k in range(cv['num_pivots_minus2'] + 1):
            m = pc == k
            if not m.any():
                continue
            mp[m] = cv['mapping_idc'][k]
            if cv['mapping_idc'][k] == 0:
                v[m] = v_poly(codes[m], cv['pivot_value'], cv['poly_order_minus1'][k],
                              cv['fp_poly_coef'][k], P.BL_bit_depth, P.coefficient_log2_denom, mut)
        out.append({'pivot_idx': pidx.tolist(), 'piece': pc.tolist(), 'mapping_idc': mp.tolist(),
                    'v': v.tolist()})
    return out


# ---------------------------------------------------------------------------------------------
# Self test: scalar == vectorised, negative control, hand-computed values
# ---------------------------------------------------------------------------------------------

def _rand_set(rng, B=10, D=23, mmr=True, dup=False):
    npiv = int(rng.integers(2, 6))
    top = (1 << B) - 1
    inner = sorted(int(x) for x in rng.integers(1, top, npiv - 2))
    pv = [int(rng.integers(0, 40))] + inner + [top - int(rng.integers(0, 40))]
    if dup and npiv >= 3:
        pv[1] = pv[0]
    curves = []
    for c in range(3):
        n = len(pv)
        mi = [0 if (c == 0 or not mmr) else int(rng.integers(0, 2)) for _ in range(n - 1)]
        po = [int(rng.integers(1, 3)) for _ in range(n - 1)]
        pcf = [[int(rng.integers(-(1 << 26), 1 << 26)) for _ in range(3)] for _ in range(n - 1)]
        mo = [int(rng.integers(1, 4)) for _ in range(n - 1)]
        mc = [int(rng.integers(-(1 << 26), 1 << 26)) for _ in range(n - 1)]
        mcf = [[[int(rng.integers(-(1 << 26), 1 << 26)) for _ in range(7)] for _ in range(3)]
               for _ in range(n - 1)]
        curves.append({'num_pivots': n, 'pivots': pv, 'mapping_idc': mi, 'poly_order': po,
                       'poly_coef': pcf, 'mmr_order': mo, 'mmr_constant': mc, 'mmr_coef': mcf})
    nlq = [{'nlq_offset': int(rng.integers(0, 1 << 10)), 'vdr_in_max': int(rng.integers(0, 2 << D)),
            'linear_deadzone_slope': int(rng.integers(0, 2 << D)),
            'linear_deadzone_threshold': int(rng.integers(0, 2 << D))} for _ in range(3)]
    return {'hdr': {'bl_bit_depth': B, 'el_bit_depth': 10, 'vdr_bit_depth': 12,
                    'coef_log2_denom': D, 'disable_residual_flag': 0},
            'map': {'nlq_method_idc': 0, 'curves': curves, 'nlq': nlq}}


def identity_set(B=10, D=23, el=False):
    cv = {'num_pivots': 2, 'pivots': [0, (1 << B) - 1], 'mapping_idc': [0], 'poly_order': [1],
          'poly_coef': [[0, 1 << D, 0]], 'mmr_order': [0], 'mmr_constant': [0],
          'mmr_coef': [[[0] * 7] * 3]}
    nlq = [{'nlq_offset': 0, 'vdr_in_max': 0, 'linear_deadzone_slope': 0,
            'linear_deadzone_threshold': 0}] * 3
    return {'hdr': {'bl_bit_depth': B, 'el_bit_depth': 10, 'vdr_bit_depth': 12,
                    'coef_log2_denom': D, 'disable_residual_flag': 0 if el else 1},
            'map': {'nlq_method_idc': 0 if el else -1, 'curves': [cv, cv, cv], 'nlq': nlq}}


def selftest(sets=(), samples=4000, seed=1, mut=()):
    rng = np.random.default_rng(seed)
    fails = 0
    checks = 0

    def expect(name, ok, detail=''):
        nonlocal fails, checks
        checks += 1
        if not ok:
            fails += 1
            print('SELFTEST FAIL %s %s' % (name, detail))

    readings = dict(DEFAULT_READINGS)
    # 1. hand-computed values from the pseudo-code (BL 10 bit, denom 23)
    #    identity polynomial: vv = (1<<23)*(s<<10) = s<<33, v = s<<33>>27 = s<<6
    for s in (0, 1, 64, 512, 940, 1023):
        expect('poly-identity', s_poly(s, [0, 1023], 0, [0, 1 << 23, 0], 10, 23, mut) == s << 6, s)
    #    reconstruction to 12 bit: h = (v + 8) >> 4
    expect('recon', s_recon(1000, 0, 1, 12, mut) == 63)
    expect('recon-res', s_recon(1000, 9, 0, 12, mut) == 63 and s_recon(1000, -9, 0, 12, mut) == 62)
    #    NLQ, Tron's constants: offset 512, R 1<<20, S 2048, T 0, EL 10, denom 23
    #    e = 513: rr = 1 -> 2 - 1 = 1; dq = 2048; r = 2048 >> 8 = 8
    expect('nlq-1', s_nlq(513, 512, 0, 2048, 1 << 20, 10, 23, mut) == 8)
    #    e = 511: rr = -1 -> -2 + 1 = -1; dq = -2048; r = -8
    expect('nlq-m1', s_nlq(511, 512, 0, 2048, 1 << 20, 10, 23, mut) == -8)
    #    e = 1023: rr = 511 -> 1021; dq = 1021*2048 = 2091008 < RR = 2097152; r = 8168
    expect('nlq-top', s_nlq(1023, 512, 0, 2048, 1 << 20, 10, 23, mut) == 8168)
    #    e = 0: rr = -512 -> -1023; dq = -2095104; r = -8184
    expect('nlq-bot', s_nlq(0, 512, 0, 2048, 1 << 20, 10, 23, mut) == -8184)
    #    pivots 0 0 1023: code 0 is not below pivot_value[1] = 0, so it is piece 1
    expect('pivot-dup', s_pivot_idx(0, [0, 0, 1023], mut) == 1)
    expect('pivot-top', piece_of(s_pivot_idx(1023, [0, 0, 1023], mut), 1, mut) == 1)
    expect('pivot-mid', s_pivot_idx(500, [0, 500, 1023], mut) == 1
           and s_pivot_idx(499, [0, 500, 1023], mut) == 0)
    #    MMR order 1 with only tt[1] (s0): coefficient 1<<23 -> v = (s0<<10<<23)>>27 = s0<<6
    cf = [None, [1 << 23, 0, 0, 0, 0, 0, 0]]
    expect('mmr-s0', s_mmr(700, 3, 9, 1, [[0, 1023]] * 3, 1, 0, cf, 10, 23, mut) == 700 << 6)
    cf = [None, [0, 0, 0, 0, 0, 0, 1 << 23]]   # tt[7] = s0*s1*s2 normalised
    expect('mmr-s012', s_mmr(1023, 1023, 1023, 1, [[0, 1023]] * 3, 1, 0, cf, 10, 23, mut)
           == ((((1023 * 1023) * (1023 << 10)) >> 20) << 23) >> 27)
    #    tt[7] with three different samples: tt4 = 1023*512 = 523776, tt3 = 256<<10 = 262144,
    #    tt7 = 523776*262144 >> 20 = 130944, v = (130944<<23) >> 27 = 8184
    cf = [None, [0, 0, 0, 0, 0, 0, 1 << 23]]
    expect('mmr-tt7', s_mmr(1023, 512, 256, 1, [[0, 1023]] * 3, 1, 0, cf, 10, 23, mut) == 8184)
    #    order 2, only mmr_coef[1][1] (term tt[2] = s1<<10): v = 512<<6 = 32768
    cf = [None, [0, 1 << 23, 0, 0, 0, 0, 0], [0] * 7]
    expect('mmr-order', s_mmr(1023, 512, 256, 2, [[0, 1023]] * 3, 2, 0, cf, 10, 23, mut) == 32768)
    #    down-sampling of a constant picture is the constant
    L = [[777] * 6 for _ in range(4)]
    expect('ds-const', s_downsample(L, 1, 1, readings, mut) == 777)
    #    ramp L[y][x] = 100x + y, chroma (1, 0): rows 0 and 1, columns 1..3:
    #    a = 100 + 400 + 300 = 800, b = 804 -> ((802>>2) + (806>>2) + 1) >> 1 = 201
    L = [[100 * x + y for x in range(6)] for y in range(4)]
    expect('ds-ramp', s_downsample(L, 1, 0, readings, mut) == 201)
    #    annex B.3 on a step 0,0,0,1000,1000,1000
    st = [0, 0, 0, 1000, 1000, 1000]
    expect('up-v-luma', s_up_v_luma(st)[6] == 797)      # (111000 - 9000 + 64) >> 7
    expect('up-v-chroma', s_up_v_chroma(st)[6] == 750)  # (192000 + 128) >> 8
    expect('up-h', s_up_h(st)[5] == 500)                 # (2048000 + 2048) >> 12
    #    up-sampling of a constant line keeps the constant (the taps sum to the divisor)
    expect('up-const', set(s_up_v_luma([600] * 7)) == {600} and set(s_up_v_chroma([600] * 7)) == {600}
           and set(s_up_h([600] * 9)) == {600})

    # 2. scalar == vectorised on random parameter sets and samples
    for t in range(12):
        d = _rand_set(rng, B=10 if t % 3 else 8, dup=(t % 4 == 0))
        P = Params(d)
        top = (1 << P.BL_bit_depth) - 1
        s = rng.integers(0, top + 1, samples)
        s1 = rng.integers(0, top + 1, samples)
        s2 = rng.integers(0, top + 1, samples)
        pvs = [cv['pivot_value'] for cv in P.curves]
        for c in range(3):
            cv = P.curves[c]
            vi = v_pivot_idx(_a(s), cv['pivot_value'], mut)
            vp = v_piece(vi, cv['num_pivots_minus2'], mut)
            sp = [piece_of(s_pivot_idx(int(x), cv['pivot_value'], mut), cv['num_pivots_minus2'], mut)
                  for x in s]
            expect('piece-vec', list(vp) == sp, 'set %d cmp %d' % (t, c))
            for k in range(cv['num_pivots_minus2'] + 1):
                a = v_poly(_a(s), cv['pivot_value'], cv['poly_order_minus1'][k], cv['fp_poly_coef'][k],
                           P.BL_bit_depth, P.coefficient_log2_denom, mut)
                b = [s_poly(int(x), cv['pivot_value'], cv['poly_order_minus1'][k],
                            cv['fp_poly_coef'][k], P.BL_bit_depth, P.coefficient_log2_denom, mut)
                     for x in s]
                expect('poly-vec', list(a) == b, 'set %d cmp %d piece %d' % (t, c, k))
                if c and cv['mapping_idc'][k] == 1:
                    try:
                        a = v_mmr(_a(s), _a(s1), _a(s2), c, pvs, cv['MMR_order'][k],
                                  cv['fp_mmr_constant'][k], cv['fp_mmr_coef'][k], P.BL_bit_depth,
                                  P.coefficient_log2_denom, mut)
                    except Fit64Error:
                        continue  # the bound refused; the scalar form must not be wrong silently
                    b = [s_mmr(int(x), int(y), int(z), c, pvs, cv['MMR_order'][k],
                               cv['fp_mmr_constant'][k], cv['fp_mmr_coef'][k], P.BL_bit_depth,
                               P.coefficient_log2_denom, mut) for x, y, z in zip(s, s1, s2)]
                    expect('mmr-vec', list(a) == b, 'set %d cmp %d piece %d' % (t, c, k))
        e = rng.integers(0, 1 << P.EL_bit_depth, samples)
        for c in range(3):
            q = P.nlq[c]
            a = v_nlq(_a(e), q['nlq_offset'], q['fp_linear_deadzone_threshold'],
                      q['fp_linear_deadzone_slope'], q['fp_hdr_in_max'], P.EL_bit_depth,
                      P.coefficient_log2_denom, mut)
            b = [s_nlq(int(x), q['nlq_offset'], q['fp_linear_deadzone_threshold'],
                       q['fp_linear_deadzone_slope'], q['fp_hdr_in_max'], P.EL_bit_depth,
                       P.coefficient_log2_denom, mut) for x in e]
            expect('nlq-vec', list(a) == b, 'set %d cmp %d' % (t, c))
        v = rng.integers(0, 0x10000, samples)
        r = rng.integers(-(1 << 16), 1 << 16, samples)
        for dis in (0, 1):
            a = v_recon(_a(v), _a(r), dis, 12, mut)
            b = [s_recon(int(x), int(y), dis, 12, mut) for x, y in zip(v, r)]
            expect('recon-vec', list(a) == b)
    # down-sampling and up-sampling, vectorised against scalar on a small random picture
    for axes in ('i-is-column', 'i-is-row'):
        for rnd in ('per-row', 'joint'):
            rd = {'ds-axes': axes, 'ds-round': rnd, 'el-up': 'auto'}
            L = rng.integers(0, 1024, (10, 14))
            a = v_downsample(L, rd, mut)
            b = [[s_downsample(L.tolist(), cx, cy, rd, mut) for cx in range(7)] for cy in range(5)]
            expect('ds-vec', a.tolist() == b, axes + ' ' + rnd)
    E = rng.integers(0, 1024, (6, 9))
    for c in (0, 1):
        a = v_up_horizontal(v_up_vertical(E, c))
        cols = [(s_up_v_luma if c == 0 else s_up_v_chroma)(list(E[:, x])) for x in range(9)]
        mid = [[cols[x][y] for x in range(9)] for y in range(12)]
        b = [s_up_h(row) for row in mid]
        expect('up-vec', a.tolist() == b, 'cmp %d' % c)

    # 3. negative control: identity mapping, no EL -> h = s << (12 - 10)
    P = Params(identity_set())
    Y = rng.integers(0, 1024, (8, 12))
    Cb = rng.integers(0, 1024, (4, 6))
    Cr = rng.integers(0, 1024, (4, 6))
    o = compose(P, [Y, Cb, Cr], None, readings, mut)
    expect('identity', all(np.array_equal(o['H'][c], _a(x) << 2) for c, x in enumerate((Y, Cb, Cr))))

    # 4. real sets given on the command line: scalar == vectorised on every BL code
    for path in sets:
        for line in open(path):
            d = json.loads(line)
            P = Params(d)
            codes = list(range(1 << P.BL_bit_depth))
            pvs = [cv['pivot_value'] for cv in P.curves]
            for c in range(3):
                cv = P.curves[c]
                for k in range(cv['num_pivots_minus2'] + 1):
                    if cv['mapping_idc'][k] == 0:
                        a = v_poly(_a(codes), cv['pivot_value'], cv['poly_order_minus1'][k],
                                   cv['fp_poly_coef'][k], P.BL_bit_depth, P.coefficient_log2_denom, mut)
                        b = [s_poly(x, cv['pivot_value'], cv['poly_order_minus1'][k],
                                    cv['fp_poly_coef'][k], P.BL_bit_depth, P.coefficient_log2_denom, mut)
                             for x in codes]
                        expect('real-poly', list(a) == b, path)
                    else:
                        s0 = rng.integers(0, 1 << P.BL_bit_depth, samples)
                        s1 = rng.integers(0, 1 << P.BL_bit_depth, samples)
                        s2 = rng.integers(0, 1 << P.BL_bit_depth, samples)
                        a = v_mmr(_a(s0), _a(s1), _a(s2), c, pvs, cv['MMR_order'][k],
                                  cv['fp_mmr_constant'][k], cv['fp_mmr_coef'][k], P.BL_bit_depth,
                                  P.coefficient_log2_denom, mut)
                        b = [s_mmr(int(x), int(y), int(z), c, pvs, cv['MMR_order'][k],
                                   cv['fp_mmr_constant'][k], cv['fp_mmr_coef'][k], P.BL_bit_depth,
                                   P.coefficient_log2_denom, mut) for x, y, z in zip(s0, s1, s2)]
                        expect('real-mmr', list(a) == b, path)
            if P.nlq_method_idc == 0:
                for c in range(3):
                    q = P.nlq[c]
                    e = list(range(0, 1 << P.EL_bit_depth))
                    a = v_nlq(_a(e), q['nlq_offset'], q['fp_linear_deadzone_threshold'],
                              q['fp_linear_deadzone_slope'], q['fp_hdr_in_max'], P.EL_bit_depth,
                              P.coefficient_log2_denom, mut)
                    b = [s_nlq(x, q['nlq_offset'], q['fp_linear_deadzone_threshold'],
                               q['fp_linear_deadzone_slope'], q['fp_hdr_in_max'], P.EL_bit_depth,
                               P.coefficient_log2_denom, mut) for x in e]
                    expect('real-nlq', list(a) == b, path)
    print('SELFTEST %d checks, %d failed' % (checks, fails))
    return fails


# ---------------------------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------------------------

def _readings(args):
    rd = dict(DEFAULT_READINGS)
    for kv in args.reading or []:
        k, v = kv.split('=', 1)
        if k not in READING_CHOICES or v not in READING_CHOICES[k]:
            raise SystemExit('unknown reading %s' % kv)
        rd[k] = v
    return rd


def _mut(args):
    for m in args.mutate or []:
        if m not in MUTATIONS:
            raise SystemExit('unknown mutation %s' % m)
    return tuple(args.mutate or [])


def _load_plane(path, w, h):
    a = np.fromfile(path, dtype='<u2')
    if a.size != w * h:
        raise SystemExit('%s: %d samples, expected %dx%d' % (path, a.size, w, h))
    return a.reshape(h, w).astype(I64)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('selftest')
    p.add_argument('--sets', nargs='*', default=[])
    p.add_argument('--mutate', action='append')
    p = sub.add_parser('lut')
    p.add_argument('--set', required=True, help='one JSON parameter set per line')
    p.add_argument('--out', required=True)
    p.add_argument('--mutate', action='append')
    p = sub.add_parser('compose')
    p.add_argument('--set', required=True, help='JSON file with one parameter set')
    p.add_argument('--width', type=int, required=True)
    p.add_argument('--height', type=int, required=True)
    p.add_argument('--bl', nargs=3, required=True, metavar=('Y', 'CB', 'CR'))
    p.add_argument('--el', nargs=3, metavar=('Y', 'CB', 'CR'))
    p.add_argument('--el-width', type=int)
    p.add_argument('--el-height', type=int)
    p.add_argument('--bl-transfer', default='pq', choices=('pq', 'bt1886'))
    p.add_argument('--out', required=True)
    p.add_argument('--reading', action='append')
    p.add_argument('--mutate', action='append')
    p = sub.add_parser('list')
    a = ap.parse_args(argv)
    if a.cmd == 'list':
        for k, v in READING_CHOICES.items():
            print('reading %s: %s (default %s)' % (k, ' | '.join(v), DEFAULT_READINGS[k]))
        for k, v in MUTATIONS.items():
            print('mutation %s: %s' % (k, v))
        return 0
    if a.cmd == 'selftest':
        return 1 if selftest(a.sets, mut=_mut(a)) else 0
    if a.cmd == 'lut':
        mut = _mut(a)
        with open(a.out, 'w') as f:
            for line in open(a.set):
                d = json.loads(line)
                f.write(json.dumps({'key': d.get('key'), 'lut': lut(Params(d), mut)}) + '\n')
        return 0
    if a.cmd == 'compose':
        d = json.load(open(a.set))
        P = Params(d)
        w, h = a.width, a.height
        bl = [_load_plane(a.bl[0], w, h), _load_plane(a.bl[1], w // 2, h // 2),
              _load_plane(a.bl[2], w // 2, h // 2)]
        el = None
        if a.el:
            ew, eh = a.el_width or w, a.el_height or h
            el = [_load_plane(a.el[0], ew, eh), _load_plane(a.el[1], ew // 2, eh // 2),
                  _load_plane(a.el[2], ew // 2, eh // 2)]
        o = compose(P, bl, el, _readings(a), _mut(a), a.bl_transfer)
        os.makedirs(a.out, exist_ok=True)
        names = ('y', 'cb', 'cr')
        for c in range(3):
            o['H'][c].astype('<u2').tofile(os.path.join(a.out, 'h_%s.u16' % names[c]))
            o['V'][c].astype('<u2').tofile(os.path.join(a.out, 'v_%s.u16' % names[c]))
            o['R'][c].astype('<i4').tofile(os.path.join(a.out, 'r_%s.i32' % names[c]))
            o['piece'][c].tofile(os.path.join(a.out, 'piece_%s.i8' % names[c]))
            o['kind'][c].tofile(os.path.join(a.out, 'kind_%s.i8' % names[c]))
        with open(os.path.join(a.out, 'oracle.json'), 'w') as f:
            json.dump({'out_bit_depth': o['out_bit_depth'], 'residual': o['residual'],
                       'readings': _readings(a), 'mutations': list(_mut(a))}, f)
        return 0
    return 2


if __name__ == '__main__':
    sys.exit(main())
