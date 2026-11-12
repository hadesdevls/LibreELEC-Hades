#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades: an Annex B H.264 MVC stream in packets ([u32 little-endian length][bytes]...).
  asis     one packet per access unit, its NAL units in the order of the file
  shifted  the second view of access unit k-1 at the head of the packet of the base view of access unit k
           (the layout that commit 0014 of Dom Cobley's series describes for some Matroska files); the last
           packet holds the second view of the last unit alone
The base view part of a unit is everything up to its first coded slice extension (NAL 20); the rest of the
unit is the second view part. Parameter sets stay where they are in the file.
Usage: mvcpack.py <stream.264> <asis|shifted> <out.pkt>   (prints the number of packets)"""
import struct, sys

SC = b"\x00\x00\x00\x01"


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


def first_mb(n):
    # first_mb_in_slice of a slice (1, 5): after the one byte header; enough bytes for a small number
    try:
        return ue(n[1:8], 0)[0]
    except IndexError:
        return -1


def units(ns):
    """Access units: a new one begins at a base view slice with first_mb 0 once the unit has one; the NAL
    units before such a slice that are not slices (AUD, parameter sets, SEI, prefix) go with the new unit."""
    aus, cur, pending, has_base = [], [], [], False
    for n in ns:
        t = n[0] & 0x1F
        if t in (1, 5):
            if first_mb(n) == 0 and has_base:
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


def split(au):
    base, second, seen = [], [], False
    for n in au:
        t = n[0] & 0x1F
        if t == 20:
            seen = True
        if seen and t not in (1, 5, 14):
            second.append(n)
        else:
            base.append(n)
    return b"".join(SC + n for n in base), b"".join(SC + n for n in second)


def main():
    src, mode, out = sys.argv[1], sys.argv[2], sys.argv[3]
    aus = units(nals(open(src, "rb").read()))
    parts = [split(au) for au in aus]
    if mode == "asis":
        packets = [b + s for b, s in parts]
    elif mode == "shifted":
        packets = []
        for k, (b, s) in enumerate(parts):
            packets.append((parts[k - 1][1] if k else b"") + b)
        if parts[-1][1]:
            packets.append(parts[-1][1])
    else:
        sys.exit("mode: asis or shifted")
    with open(out, "wb") as f:
        for p in packets:
            if p:
                f.write(struct.pack("<I", len(p)))
                f.write(p)
    print(len([p for p in packets if p]))


main()
