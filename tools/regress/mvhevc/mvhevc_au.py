#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# mvhevc_au.py: access units of an HEVC Annex B stream (one or two layers), for the frame-threading test of the
# MV-HEVC decoder (run-mvhevc.sh, stage threads): what each access unit holds, and streams made from one that put
# the decoder in the cases the test needs.
#   info <in>                    per stream: access units, how many have a base-layer picture whose slices wait
#                                for the end of setup at the second layer's first slice (a plain form, which ffmpeg-9974 sets aside
#                                with any other where setup ends later: the base layer's
#                                first slice, then only its other slices, VPS, SPS, PPS, SEI, AUD, filler or NAL 62/63, then the
#                                second layer's first slice), base-layer pictures of more than one slice, and the slices (both layers)
#                                of those access units
#   midgop <in> <out>            the stream from its first access unit after the first that is not IRAP, with every
#                                VPS, SPS and PPS before it put in front (a start in the middle of a GOP, as after a
#                                seek to a point that is not a key frame)
#   dropview <in> <out> <a> <b>  the second layer's NAL units taken out of access units a to b-1 (a view missing)
#   dropbase <in> <out> <n>      the base layer's slices taken out of access unit n (a second view without its base)
#   cut <in> <out> <n> <part>    the last base-layer slice of access unit n cut to <part> % of its bytes (an error
#                                inside the slice data the change keeps for later)
#   flip <in> <out> <n>          every 7th byte of the payload of the last base-layer slice of access unit n inverted,
#                                past its first 8 bytes (slice data that decodes to something else, or fails)
import sys

VCL_MAX = 31


def nals(data):
    """(start, end) of each NAL unit's payload (after its start code), in order"""
    out = []
    i, n = 0, len(data)
    starts = []
    while True:
        j = data.find(b'\x00\x00\x01', i)
        if j < 0:
            break
        starts.append(j + 3)
        i = j + 3
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else n
        while e > s and data[e - 1] == 0:  # trailing zero bytes (and the 4-byte start code's first zero)
            e -= 1
        out.append((s, e))
    return out


def head(data, s):
    b0, b1 = data[s], data[s + 1]
    return (b0 >> 1) & 0x3f, ((b0 & 1) << 5) | (b1 >> 3)


def is_slice(t):
    return t <= 9 or 16 <= t <= 21


def first_slice(data, s):
    return bool(data[s + 2] & 0x80)


def split_aus(data):
    """list of access units, each a list of (type, layer, start, end)"""
    aus, cur, pending = [], [], []
    seen_vcl = False
    for s, e in nals(data):
        if e - s < 2:
            continue
        t, layer = head(data, s)
        unit = (t, layer, s, e)
        prefix = layer == 0 and (t in (32, 33, 34, 35, 39) or 41 <= t <= 44 or 48 <= t <= 55)
        if t == 35 and layer == 0 and seen_vcl:  # an AUD always starts one
            aus.append(cur + pending)
            cur, pending, seen_vcl = [], [], False
        if prefix:
            pending.append(unit)
            continue
        if is_slice(t) and layer == 0 and first_slice(data, s) and seen_vcl:
            aus.append(cur)
            cur, seen_vcl = pending[:], False
            pending = []
        cur += pending
        pending = []
        cur.append(unit)
        if t <= VCL_MAX:
            seen_vcl = True
    cur += pending
    if cur:
        aus.append(cur)
    return aus


def kept_form(data, au):
    """the form ffmpeg-9974 defers: base first slice, then only other base slices and NAL units that change nothing
    the base slices decode with, then the second layer's first slice"""
    first = next((k for k, u in enumerate(au) if is_slice(u[0]) and u[1] == 0 and first_slice(data, u[2])), None)
    if first is None:
        return False
    for u in au[first + 1:]:
        t, layer, s, _ = u
        if t in (32, 33, 34, 39, 40, 35, 38, 62, 63):
            continue
        if is_slice(t) and layer == 0 and not first_slice(data, s):
            continue
        return is_slice(t) and layer > 0 and first_slice(data, s)
    return False


def write(path, data, units):
    with open(path, 'wb') as f:
        for (_, _, s, e) in units:
            f.write(b'\x00\x00\x00\x01' + data[s:e])


def main():
    cmd, src = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    aus = split_aus(data)
    if cmd == 'info':
        kept = sum(1 for au in aus if kept_form(data, au))
        two = sum(1 for au in aus if any(u[1] > 0 and is_slice(u[0]) for u in au))
        multi = sum(1 for au in aus if sum(1 for u in au if is_slice(u[0]) and u[1] == 0) > 1)
        kslices = sum(sum(1 for u in au if is_slice(u[0])) for au in aus if kept_form(data, au))
        print('aus %d two-layer %d kept-form %d base-multislice %d kept-slices %d' % (len(aus), two, kept, multi, kslices))
        return
    out = sys.argv[3]
    if cmd == 'midgop':
        irap = [any(is_slice(u[0]) and u[1] == 0 and 16 <= u[0] <= 21 for u in au) for au in aus]
        k = next(i for i in range(1, len(aus)) if not irap[i] and any(irap[:i]))
        params = [u for au in aus[:k] for u in au if u[0] in (32, 33, 34)]
        write(out, data, params + [u for au in aus[k:] for u in au])
        print('midgop from access unit %d of %d' % (k, len(aus)))
    elif cmd == 'dropview':
        a, b = int(sys.argv[4]), int(sys.argv[5])
        units = []
        for i, au in enumerate(aus):
            units += [u for u in au if not (a <= i < b and u[1] > 0)]
        write(out, data, units)
    elif cmd == 'dropbase':
        n = int(sys.argv[4])
        units = []
        for i, au in enumerate(aus):
            units += [u for u in au if not (i == n and u[1] == 0 and is_slice(u[0]))]
        write(out, data, units)
    elif cmd in ('cut', 'flip'):
        n = int(sys.argv[4])
        au = aus[n]
        last = max(k for k, u in enumerate(au) if is_slice(u[0]) and u[1] == 0)
        t, layer, s, e = au[last]
        body = bytearray(data[s:e])
        if cmd == 'cut':
            body = body[:max(8, len(body) * int(sys.argv[5]) // 100)]
        else:
            for k in range(10, len(body), 7):
                body[k] ^= 0xff
            # no emulated start code made by the change
            body = body.replace(b'\x00\x00\x01', b'\x00\x00\x05').replace(b'\x00\x00\x00', b'\x00\x00\x05')
        data2 = data[:s] + bytes(body) + data[e:]
        shift = len(body) - (e - s)
        aus2 = []
        for i, a in enumerate(aus):
            row = []
            for (tt, ll, ss, ee) in a:
                if ss > s:
                    ss, ee = ss + shift, ee + shift
                elif ss == s:
                    ee = ee + shift
                row.append((tt, ll, ss, ee))
            aus2.append(row)
        write(out, data2, [u for a in aus2 for u in a])
    else:
        sys.exit('unknown command ' + cmd)


main()
