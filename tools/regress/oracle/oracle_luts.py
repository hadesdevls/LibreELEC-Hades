#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""libplacebo's composer tables (pl_dovi_ccm_prepare(), pl_dovi_ccm_mmr_table(), through lutcheck)
against the CCM 001 oracle, for every distinct RPU of a clip.

usage: oracle_luts.py <lutcheck binary> <sets.jsonl> <name> [--mutate m]... [--expect same|differ]

Checked for each RPU, exactly:
  rows 0-2   v of 5.4.2.3.2 for every BL code whose piece (5.4.2.2) is a polynomial;
  rows 3-5   r of 5.4.3.2 for every code 0..2047 of the resampled EL (when a residual is added);
  chroma     the number of pieces, the interior pivots, which pieces are MMR, the number of terms;
             every MMR coefficient C of the piece: q * 2^7 + r = C * 2^(23 - denom) exactly
             (q as a signed 32-bit integer, 0 <= r < 128), and f within one float ulp of C'/2^27;
  MMR table  for every MMR piece, axis (s0*, Cb, Cr) and BL code s: the terms of 5.4.2.3.3 that depend
             on that sample alone (tt[1+a] = s << (20-B), tt[8+a] = s^2 << (20-2B),
             tt[15+a] = (tt[1+a] tt[8+a]) >> 20, and tt[0] = 2^20 on axis 0) summed as the table holds
             them: sum floor(C'/2^7) tt mod 2^32, sum r (tt >> 10), sum r (tt & 1023) (exact) and the
             float estimate sum f tt (relative 2^-20).
Prints one ORACLE-ITEM line for the clip.
"""
import json
import os
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ccm001_oracle as O  # noqa: E402

LUT_W = 2048
TERMS = 24


def record(d):
    h = d['hdr']
    m = d['map']
    out = [h['bl_bit_depth'], h['el_bit_depth'], h['vdr_bit_depth'], h['coef_log2_denom'],
           h['coef_data_type'], h['disable_residual_flag'], m['nlq_method_idc']]
    for c in m['curves']:
        n = c['num_pivots']
        out += [n] + c['pivots'][:n]
        for k in range(n - 1):
            coefs = [x for row in c['mmr_coef'][k] for x in row]
            out += [c['mapping_idc'][k], c['poly_order'][k]] + list(c['poly_coef'][k]) + \
                   [c['mmr_order'][k], c['mmr_constant'][k]] + coefs
    nlq = m.get('nlq') or [{'nlq_offset': 0, 'vdr_in_max': 0, 'linear_deadzone_slope': 0,
                             'linear_deadzone_threshold': 0}] * 3
    for q in nlq:
        out += [q['nlq_offset'], q['vdr_in_max'], q['linear_deadzone_slope'], q['linear_deadzone_threshold']]
    return ' '.join(str(int(x)) for x in out) + '\n'


def read_exact(f, n):
    b = f.read(n)
    if len(b) != n:
        raise EOFError('lutcheck output is short')
    return b


def to_i32(u):
    u &= 0xFFFFFFFF
    return u - (1 << 32) if u >= (1 << 31) else u


def check_one(P, res, mut, B):
    """Returns a list of difference descriptions (empty when exact)."""
    errs = []
    lut, rng, chroma, table = res
    expect = O.lut(P, mut)
    codes = 1 << B
    for c in range(3):
        L = expect[c]
        v = np.array(L['v'])
        poly = np.array(L['mapping_idc']) == 0
        got = lut[c][:codes]
        bad = np.nonzero(poly & (got != v))[0]
        if bad.size:
            s = int(bad[0])
            errs.append('row %d: %d codes differ, first s=%d oracle %d libplacebo %s' % (
                c, bad.size, s, v[s], got[s]))
        cv = P.curves[c]
        if rng[c] != (cv['pivot_value'][0], cv['pivot_value'][-1]):
            errs.append('range %d: %s, oracle %s' % (c, rng[c], (cv['pivot_value'][0], cv['pivot_value'][-1])))
    if P.residual_used(True):
        e = np.arange(LUT_W, dtype=np.int64)
        for c in range(3):
            q = P.nlq[c]
            r = O.v_nlq(e, q['nlq_offset'], q['fp_linear_deadzone_threshold'], q['fp_linear_deadzone_slope'],
                        q['fp_hdr_in_max'], P.EL_bit_depth, P.coefficient_log2_denom, mut)
            got = lut[3 + c]
            bad = np.nonzero(got != r)[0]
            if bad.size:
                x = int(bad[0])
                errs.append('row %d (NLQ): %d codes differ, first e=%d oracle %d libplacebo %s' % (
                    3 + c, bad.size, x, r[x], got[x]))
    D = P.coefficient_log2_denom
    sh1, sh2 = 20 - B, 20 - 2 * B
    s = np.arange(codes, dtype=np.int64)
    t = {0: np.full(codes, 1 << 20, dtype=np.int64), 1: s << sh1, 2: (s * s) << sh2}
    t[3] = (t[1] * t[2]) >> 20
    for ci in range(2):
        cv = P.curves[1 + ci]
        num_pieces, pivots, mask, terms, qq, rr, ff = chroma[ci]
        npc = cv['num_pivots_minus2'] + 1
        want_mask = sum(1 << i for i in range(npc) if cv['mapping_idc'][i] == 1)
        want_terms = 8 * max([cv['MMR_order'][i] for i in range(npc) if cv['mapping_idc'][i] == 1] or [0])
        if num_pieces != npc or list(pivots[:npc - 1]) != cv['pivot_value'][1:-1] or mask != want_mask \
                or terms != want_terms:
            errs.append('chroma %d: pieces %d pivots %s mask %d terms %d, oracle %d %s %d %d' % (
                ci, num_pieces, list(pivots[:npc - 1]), mask, terms, npc, cv['pivot_value'][1:-1],
                want_mask, want_terms))
            continue
        for i in range(npc):
            if cv['mapping_idc'][i] != 1:
                continue
            order = cv['MMR_order'][i]
            C = [cv['fp_mmr_constant'][i]] + [cv['fp_mmr_coef'][i][o][j] if o <= order else 0
                                              for o in range(1, 4) for j in range(7)]
            C += [0] * (TERMS - len(C))
            for k in range(TERMS):
                Cs = C[k] << (23 - D)
                q, r, f = int(qq[i][k]), float(rr[i][k]), float(ff[i][k])
                if to_i32(q) * 128 + int(r) != Cs or not (0 <= r < 128) or r != int(r):
                    errs.append('chroma %d piece %d term %d: q %d r %s, C\' %d' % (ci, i, k, to_i32(q), r, Cs))
                exact = Cs / 134217728.0
                if abs(f - exact) > abs(np.spacing(np.float32(exact))):
                    errs.append('chroma %d piece %d term %d: f %r, C\'/2^27 %r' % (ci, i, k, f, exact))
            for a in range(3):
                row = table[(ci * 8 + i) * 3 + a][:codes]
                ks = [(0 if a == 0 else None), 1 + a, 8 + a, 15 + a]
                qs = np.zeros(codes, dtype=np.int64)
                rh = np.zeros(codes, dtype=np.int64)
                rl = np.zeros(codes, dtype=np.int64)
                est = np.zeros(codes, dtype=np.float64)
                mag = np.zeros(codes, dtype=np.float64)
                for n, k in enumerate(ks):
                    if k is None or k >= want_terms:
                        continue
                    Cs = C[k] << (23 - D)
                    qk = Cs >> 7            # floor(C' / 2^7)
                    rk = Cs - (qk << 7)
                    qs = (qs + (qk & 0xFFFFFFFF) * t[n]) & 0xFFFFFFFF
                    rh += rk * (t[n] >> 10)
                    rl += rk * (t[n] & 1023)
                    # the estimate: the float f of each coefficient times the term, summed in doubles
                    fk = float(np.float32(Cs / 134217728.0))
                    est += fk * t[n].astype(np.float64)
                    mag += abs(fk) * t[n].astype(np.float64)
                gq = row[:, 0].astype(np.int64)
                grh = row[:, 1].view(np.float32).astype(np.float64)
                grl = row[:, 2].view(np.float32).astype(np.float64)
                gest = row[:, 3].view(np.float32).astype(np.float64)
                bad = np.nonzero((gq != qs) | (grh != rh) | (grl != rl))[0]
                if bad.size:
                    x = int(bad[0])
                    errs.append('mmr table chroma %d piece %d axis %d: %d codes differ, first s=%d '
                                '(q %d/%d rh %r/%d rl %r/%d)' % (ci, i, a, bad.size, x, gq[x], qs[x], grh[x],
                                                                 rh[x], grl[x], rl[x]))
                # the table holds the double sum rounded to a float: one float ulp, and the order of
                # four double additions
                tol = np.abs(np.spacing(est.astype(np.float32))).astype(np.float64) + mag * 2.0 ** -50
                bad = np.nonzero(np.abs(gest - est) > tol)[0]
                if bad.size:
                    x = int(bad[0])
                    errs.append('mmr table chroma %d piece %d axis %d: estimate off at %d codes, first s=%d '
                                '%r vs %r' % (ci, i, a, bad.size, x, gest[x], est[x]))
    return errs


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('lutcheck')
    ap.add_argument('sets')
    ap.add_argument('name')
    ap.add_argument('--mutate', action='append')
    ap.add_argument('--expect', default='same', choices=('same', 'differ'))
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    mut = tuple(a.mutate or [])
    sets = [json.loads(line) for line in open(a.sets)]
    if a.limit:
        sets = sets[:a.limit]
    proc = subprocess.Popen([a.lutcheck], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    # feed in a thread-free way: write everything first (small), then read
    data = ''.join(record(d) for d in sets).encode()
    import threading
    th = threading.Thread(target=lambda: (proc.stdin.write(data), proc.stdin.close()))
    th.start()
    out = proc.stdout
    n_ok = n_diff = n_refused = 0
    shown = []
    kinds = {}
    for d in sets:
        P = O.Params(d)
        line = out.readline().decode().rstrip('\n')
        parts = line.split(' ', 3)
        if len(parts) < 4 or parts[0] != 'LUTCHECK':
            print('ORACLE-ITEM %s: FAILED | lutcheck said %r' % (a.name, line))
            return 1
        if parts[2] != '1':
            n_refused += 1
            shown.append('set %s refused by pl_dovi_ccm_prepare(): %s' % (d.get('key'), parts[3]))
            continue
        B = P.BL_bit_depth
        lut = np.frombuffer(read_exact(out, 6 * LUT_W * 4), dtype='<f4').reshape(6, LUT_W).astype(np.float64)
        rr = struct.unpack('<6i', read_exact(out, 24))
        rng = [(rr[0], rr[1]), (rr[2], rr[3]), (rr[4], rr[5])]
        chroma = []
        for ci in range(2):
            hd = struct.unpack('<10i', read_exact(out, 40))
            q = np.frombuffer(read_exact(out, 8 * TERMS * 4), dtype='<u4').reshape(8, TERMS)
            r = np.frombuffer(read_exact(out, 8 * TERMS * 4), dtype='<f4').reshape(8, TERMS)
            f = np.frombuffer(read_exact(out, 8 * TERMS * 4), dtype='<f4').reshape(8, TERMS)
            chroma.append((hd[0], hd[1:8], hd[8], hd[9], q, r, f))
        w = 1 << B
        table = np.frombuffer(read_exact(out, 48 * w * 4 * 4), dtype='<u4').reshape(48, w, 4)
        errs = check_one(P, (lut, rng, chroma, table), mut, B)
        for c in range(3):
            cv = P.curves[c]
            for k in range(cv['num_pivots_minus2'] + 1):
                key = 'c%d-%s' % (c, ('mmr%d' % cv['MMR_order'][k]) if cv['mapping_idc'][k]
                                  else ('poly%d' % (cv['poly_order_minus1'][k] + 1)))
                kinds[key] = kinds.get(key, 0) + 1
        if errs:
            n_diff += 1
            if len(shown) < 4:
                shown.append('set %s: %s' % (d.get('key'), '; '.join(errs[:3])))
        else:
            n_ok += 1
    th.join()
    proc.wait()
    total = len(sets)
    if a.expect == 'same':
        ok = n_diff == 0 and n_refused == 0 and n_ok == total
    else:
        ok = n_diff > 0
    print('ORACLE-ITEM %s: %s | %d RPUs: %d exact, %d with differences, %d refused; pieces checked %s; '
          'expected %s%s%s' % (a.name, 'PASSED' if ok else 'FAILED', total, n_ok, n_diff, n_refused,
                               json.dumps(kinds, sort_keys=True), a.expect,
                               ('; mutations ' + ','.join(mut)) if mut else '',
                               ('; ' + ' | '.join(shown)) if shown else ''))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
