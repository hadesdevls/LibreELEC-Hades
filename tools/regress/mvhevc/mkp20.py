#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# mkp20.py: a Dolby Vision profile 20 3D test file made of an ITU-T MV-HEVC conformance stream (TEST ONLY: there is no
# public profile 20 sample; a real one exists only in films with Apple's DRM).
#
#   inject <MV-HEVC Annex B> <RPU (Annex B, as dovi_tool's test assets keep one)> <out Annex B>
#       the RPU as a NAL unit of type 62 (UNSPEC62), layer 0, temporal id 0, put last in every access unit: where
#       Dolby Vision carries it, and where FFmpeg's HEVC decoder reads it (the last NAL unit of the packet, of the
#       base layer, hevcdec.c). The access units are cut where the base layer starts a picture (its first slice, or
#       the parameter sets, SEI or delimiter before it), after the last picture of any layer.
#   dvvc <MP4 of one HEVC track with 'lhvC', moov after mdat> <out MP4> <profile> <compatibility>
#       a 'dvvC' box (DOVIDecoderConfigurationRecord: version 1.0, the profile, level 6, an RPU and a base layer, no
#       enhancement layer, the base layer's signal compatibility) put last in the sample entry, as the 'dvvC' of
#       Apple's MV-HEVC Dolby Vision files is (Dolby's ISOBMFF specification: dvvC for profiles above 10). The sizes
#       of the boxes around it are made larger by its 32 bytes; nothing moves in mdat (moov is after it).
import struct
import sys


def nal_units(data):
    """(start, end) of the payload of each NAL unit of an Annex B stream (the header included)"""
    out, i, n = [], 0, len(data)
    starts = []
    while True:
        j = data.find(b'\x00\x00\x01', i)
        if j < 0:
            break
        starts.append(j + 3)
        i = j + 3
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else n
        # (the zero before a four-byte start code is not of the unit before it)
        while e > s and data[e - 1] == 0:
            e -= 1
        out.append((s, e))
    return out


def inject(src, rpu_path, dst):
    data = open(src, 'rb').read()
    rpu = open(rpu_path, 'rb').read()
    if rpu.startswith(b'\x00\x00\x00\x01'):
        rpu = rpu[4:]
    elif rpu.startswith(b'\x00\x00\x01'):
        rpu = rpu[3:]
    if not rpu or rpu[0] != 0x19:
        sys.exit('the RPU does not begin with its prefix 0x19')
    rpu_nal = b'\x7c\x01' + rpu
    units = nal_units(data)
    aus, cur, vcl_seen = [], [], False
    for s, e in units:
        t = (data[s] >> 1) & 0x3f
        layer = ((data[s] & 1) << 5) | (data[s + 1] >> 3)
        starts = False
        if layer == 0 and vcl_seen:
            if t < 32:
                starts = (data[s + 2] & 0x80) != 0  # first_slice_segment_in_pic_flag
            elif t in (32, 33, 34, 35, 39) or 41 <= t <= 44 or 48 <= t <= 55:
                starts = True
        if starts:
            aus.append(cur)
            cur, vcl_seen = [], False
        cur.append(data[s:e])
        if t < 32:
            vcl_seen = True
    if cur:
        aus.append(cur)
    with open(dst, 'wb') as f:
        for au in aus:
            for nal in au + [rpu_nal]:
                f.write(b'\x00\x00\x00\x01' + nal)
    print(f'inject: {len(aus)} access units, an RPU of {len(rpu)} bytes last in each')


def boxes(data, start, end):
    i = start
    while i + 8 <= end:
        size, kind = struct.unpack('>I4s', data[i:i + 8])
        if size < 8 or i + size > end:
            sys.exit(f'a box of size {size} at {i}')
        yield i, size, kind.decode('latin-1')
        i += size


def find(data, start, end, kind):
    for i, size, k in boxes(data, start, end):
        if k == kind:
            return i, size
    sys.exit(f'no {kind}')


def dvvc(src, dst, profile, compatibility):
    data = bytearray(open(src, 'rb').read())
    top = {k: (i, s) for i, s, k in boxes(data, 0, len(data))}
    if 'moov' not in top or 'mdat' not in top or top['moov'][0] < top['mdat'][0]:
        sys.exit('moov is not after mdat')
    path = []
    i, s = top['moov']
    path.append(i)
    for kind, skip in (('trak', 8), ('mdia', 8), ('minf', 8), ('stbl', 8), ('stsd', 16)):
        i, s = find(data, i + 8 if kind != 'trak' else i + 8, i + s, kind)
        path.append(i)
    # the sample entry: the first of stsd, after its version, flags and count
    stsd = path[-1]
    entry = stsd + 16
    esize, ekind = struct.unpack('>I4s', data[entry:entry + 8])
    if ekind not in (b'hvc1', b'hev1'):
        sys.exit(f'the sample entry is {ekind!r}')
    level, rpu, el, bl = 6, 1, 0, 1
    bits = (profile << 9) | (level << 3) | (rpu << 2) | (el << 1) | bl
    payload = struct.pack('>BBHI', 1, 0, bits, compatibility << 28) + b'\x00' * 16
    box = struct.pack('>I4s', 8 + len(payload), b'dvvC') + payload
    end = entry + esize
    data[end:end] = box
    for at in path + [entry]:
        size = struct.unpack('>I', data[at:at + 4])[0]
        data[at:at + 4] = struct.pack('>I', size + len(box))
    open(dst, 'wb').write(data)
    print(f'dvvc: profile {profile}, compatibility {compatibility}, {len(box)} bytes in the {ekind.decode()} entry')


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == 'inject':
        inject(*sys.argv[2:5])
    elif len(sys.argv) == 6 and sys.argv[1] == 'dvvc':
        dvvc(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]))
    else:
        sys.exit(__doc__ if __doc__ else 'usage')
