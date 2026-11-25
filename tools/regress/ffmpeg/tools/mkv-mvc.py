#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades: Matroska files of one H.264 MVC stream (two views in one track, as a file made of a 3D
Blu-ray has them), in each of the forms that declare its second view, and a 2D one.
  legacy    the CodecPrivate is the AVCDecoderConfigurationRecord of the base view and, behind it, a box 'mvcC'
            (the form MakeMKV writes; the legacy form of the Matroska codec specifications)
  mapping   the CodecPrivate is the record of the base view; a BlockAdditionMapping of type 'mvcC' carries the
            MVCDecoderConfigurationRecord (the form the specifications prefer; MakeMKV 1.15.2 wrote only that)
  maponly   as mapping, with no parameter set in the blocks: the subset SPS and the PPS of the second view are only
            in the mapping
  both      the box behind the CodecPrivate and the mapping (MakeMKV 1.15.3 and later)
  stream    the CodecPrivate is the record of the base view and nothing else: the subset SPS is only in the stream
            (a file whose CodecPrivate a remuxer rewrote)
  plain     a 2D stream: the base view alone (no NAL unit 14, 15 or 20, nor the PPS of the second view)
The record of the base view takes the SPS (NAL unit 7) of the first access unit and the PPS that the slices of the
base view use (by pic_parameter_set_id), the 'mvcC' record its subset SPS (NAL unit 15) and the PPS that the slices
of the second view (NAL unit 20) use. Each access unit is
one SimpleBlock with 4-byte NAL unit lengths, at 24000/1001 frames a second; an access unit with an IDR slice is a
key frame. Usage: mkv-mvc.py <stream.264> <out folder>   (writes <form>.mkv and prints one line per file)"""
import os, struct, sys

ID_MVCC = 0x6D766343


def nals(b):
    starts, i = [], 0
    while True:
        j = b.find(b"\x00\x00\x01", i)
        if j < 0:
            break
        starts.append(j)
        i = j + 3
    out = []
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else len(b)
        p = b[s + 3:e]
        while p.endswith(b"\x00") and k + 1 < len(starts):
            p = p[:-1]
        if p:
            out.append(p)
    return out


def ue(data, bitpos):
    def bit(p):
        return (data[p >> 3] >> (7 - (p & 7))) & 1
    z = 0
    while bit(bitpos) == 0:
        z += 1
        bitpos += 1
    bitpos += 1
    v = 0
    for _ in range(z):
        v = (v << 1) | bit(bitpos)
        bitpos += 1
    return (1 << z) - 1 + v, bitpos


def units(ns):
    """Access units: one begins at a base view slice with first_mb 0 once the unit has one; the NAL units before
    such a slice that are not slices go with the new unit (as tools/regress/mvc/mvcpack.py splits them)."""
    aus, cur, pending, has_base = [], [], [], False
    for n in ns:
        t = n[0] & 0x1F
        if t in (1, 5):
            if ue(n[1:8], 0)[0] == 0 and has_base:
                aus.append(cur)
                cur, has_base = [], False
            cur += pending
            pending = []
            cur.append(n)
            has_base = True
        elif t == 20:
            cur += pending
            pending = []
            cur.append(n)
        else:
            pending.append(n)
    cur += pending
    if cur:
        aus.append(cur)
    return aus


def vint_size(n):
    return bytes([0x01]) + n.to_bytes(7, "big")


def el(eid, payload):
    b = eid.to_bytes((eid.bit_length() + 7) // 8, "big")
    return b + vint_size(len(payload)) + payload


def uint(eid, v):
    return el(eid, v.to_bytes(max(1, (v.bit_length() + 7) // 8), "big"))


def avcc(sps, pps):
    out = bytes([1, sps[0][1], sps[0][2], sps[0][3], 0xFF, 0xE0 | len(sps)])
    for s in sps:
        out += struct.pack(">H", len(s)) + s
    out += bytes([len(pps)])
    for p in pps:
        out += struct.pack(">H", len(p)) + p
    return out


def mvcc_record(ssps, pps):
    out = bytes([1, ssps[0][1], ssps[0][2], ssps[0][3], 0xFF, len(ssps)])
    for s in ssps:
        out += struct.pack(">H", len(s)) + s
    out += bytes([len(pps)])
    for p in pps:
        out += struct.pack(">H", len(p)) + p
    return out


def mkv(path, codec_private, mapping, blocks):
    ebml = el(0x1A45DFA3, uint(0x4286, 1) + uint(0x42F7, 1) + uint(0x42F2, 4) + uint(0x42F3, 8) +
              el(0x4282, b"matroska") + uint(0x4287, 4) + uint(0x4285, 2))
    info = el(0x1549A966, uint(0x2AD7B1, 1000000) + el(0x4D80, b"hades mkv-mvc.py") + el(0x5741, b"hades mkv-mvc.py"))
    video = el(0xE0, uint(0xB0, 1920) + uint(0xBA, 1080))
    entry = (uint(0xD7, 1) + uint(0x73C5, 1) + uint(0x83, 1) + el(0x86, b"V_MPEG4/ISO/AVC") +
             uint(0x23E383, 41708333) + el(0x63A2, codec_private) + video)
    if mapping is not None:
        entry += el(0x41E4, uint(0x41E7, ID_MVCC) + el(0x41ED, mapping))
    tracks = el(0x1654AE6B, el(0xAE, entry))
    cluster = uint(0xE7, 0)
    for i, (data, key) in enumerate(blocks):
        tc = round(i * 1001 / 24)  # ms
        cluster += el(0xA3, bytes([0x81]) + struct.pack(">h", tc) + bytes([0x80 if key else 0]) + data)
    segment = info + tracks + el(0x1F43B675, cluster)
    with open(path, "wb") as f:
        f.write(ebml + el(0x18538067, segment))


def main():
    src, out = sys.argv[1], sys.argv[2]
    aus = units(nals(open(src, "rb").read()))
    first = aus[0]
    kinds = [n[0] & 0x1F for n in first]
    if 15 not in kinds:
        sys.exit("the first access unit has no subset SPS")
    sps = [n for n in first if n[0] & 0x1F == 7]
    ssps = [n for n in first if n[0] & 0x1F == 15]
    def slice_pps(n):
        # first_mb_in_slice, slice_type, pic_parameter_set_id, after the header (1 byte; 4 for NAL unit 20)
        b = n[4:20] if n[0] & 0x1F == 20 else n[1:17]
        return ue(b, ue(b, ue(b, 0)[1])[1])[0]

    def pps_id(n):
        return ue(n[1:16], 0)[0]
    used_base = {slice_pps(n) for au in aus for n in au if n[0] & 0x1F in (1, 5)}
    used_second = {slice_pps(n) for au in aus for n in au if n[0] & 0x1F == 20}
    pps = [n for n in first if n[0] & 0x1F == 8 and pps_id(n) in used_base]
    pps2 = [n for n in first if n[0] & 0x1F == 8 and pps_id(n) in used_second]
    second_only = used_second - used_base
    if not sps or not pps or not ssps or not pps2:
        sys.exit("parameter sets not found where expected")
    print("PPS of the base view %s, of the second view %s" % (sorted(used_base), sorted(used_second)))
    base = avcc(sps, pps)
    rec = mvcc_record(ssps, pps2)
    box = struct.pack(">I", 8 + len(rec)) + b"mvcC" + rec

    def block(au, drop):
        data = b"".join(struct.pack(">I", len(n)) + n for n in au if (n[0] & 0x1F) not in drop)
        return data, any(n[0] & 0x1F == 5 for n in au)

    full = [block(au, ()) for au in aus]
    nosets = [block(au, (7, 8, 15)) for au in aus]
    plain = []
    for au in aus:
        keep = [n for n in au if (n[0] & 0x1F) not in (14, 15, 20) and
                not (n[0] & 0x1F == 8 and pps_id(n) in second_only)]
        plain.append((b"".join(struct.pack(">I", len(n)) + n for n in keep), any(n[0] & 0x1F == 5 for n in au)))
    os.makedirs(out, exist_ok=True)
    forms = {
        "legacy": (base + box, None, full),
        "mapping": (base, rec, full),
        "maponly": (base, rec, nosets),
        "both": (base + box, rec, full),
        "stream": (base, None, full),
        "plain": (base, None, plain),
    }
    for name, (cp, mapping, blocks) in forms.items():
        mkv(os.path.join(out, name + ".mkv"), cp, mapping, blocks)
        print("%s.mkv: %d blocks (%d key), CodecPrivate %d bytes, mapping %s" % (
            name, len(blocks), sum(1 for _, k in blocks if k), len(cp), len(mapping) if mapping else "none"))
    with open(os.path.join(out, "expected-legacy.bin"), "wb") as f:
        f.write(base + box)
    with open(os.path.join(out, "expected-base.bin"), "wb") as f:
        f.write(base)


main()
