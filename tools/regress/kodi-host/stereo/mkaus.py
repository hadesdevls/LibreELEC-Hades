#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Makes the access units of the synthetic plane-offset test from an H.264 MVC elementary stream (Annex B, base and
# second view together, as the JM encoder writes it): the stream's access units repeated REPS times (each repetition
# starts at its IDR access unit, like a GOP), and the same with a Blu-ray 3D offset metadata SEI (a
# user_data_unregistered message with the OFMD UUID in an mvc_scalable_nesting SEI, NAL type 6) in the first access
# unit of each repetition, before its first slice, as Annex H orders the SEI of an access unit. The payload is
# escaped (emulation prevention), and its offsets are chosen so that escaping is needed.
# Writes, in <out dir>: combined-plain.annexb, combined-sei.annexb (Annex B, one access unit per record),
# combined-plain.avcc, combined-sei.avcc (4-byte lengths before each unit: what a Matroska remux carries),
# dependent-sei.annexb (the second view alone, with the SEI before its slices: what the dependent view clip of a
# disc gives), combined-othermarker.annexb and .avcc (the same SEI with the OFMD UUID but another marker than
# "OFMD" after it: not offset metadata, nothing to read) and expected.txt. A record is a 4-byte big-endian length and the bytes.
# Usage: mkaus.py <stream.264> <out dir>
import os
import struct
import sys

REPS = 4
SEQUENCES = 3
UUID = bytes([0x17, 0xee, 0x8c, 0x60, 0xf8, 0x4d, 0x11, 0xd9, 0x8c, 0xd6, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66])

src, out = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()

# NAL units (without their start codes)
starts = []
i = 0
while True:
    j = data.find(b"\0\0\1", i)
    if j < 0:
        break
    starts.append(j + 3)
    i = j + 3
nals = []
for k, s in enumerate(starts):
    e = starts[k + 1] - 3 if k + 1 < len(starts) else len(data)
    while e > s and data[e - 1] == 0:  # trailing zeros / the zero byte of the next start code
        e -= 1
    nals.append(data[s:e])

# access units: a new one at SPS/AUD after a slice, or at a prefix NAL (14) after a slice of the second view (20)
aus = []
cur = []
prev = None
for n in nals:
    t = n[0] & 0x1F
    vcl_seen = any((x[0] & 0x1F) in (1, 5, 20) for x in cur)
    if cur and ((t in (7, 9) and vcl_seen) or (t == 14 and prev == 20)):
        aus.append(cur)
        cur = []
    cur.append(n)
    prev = t
if cur:
    aus.append(cur)
frames = len(aus)
assert (aus[0][1][0] & 0x1F) == 15 or any((x[0] & 0x1F) == 5 for x in aus[0]), "the first access unit is no IDR"


def offset(g, s, k):
    # values in -20..19, many of them 0 and small: escaping is needed
    v = ((g * 11 + s * 5 + k) % 40) - 20
    if (k + s) % 4 == 0:
        v = 0
    return v


def code(v):
    return (0x80 | -v) if v < 0 else v


def escape(rbsp):
    out = bytearray()
    zeros = 0
    for b in rbsp:
        if zeros >= 2 and b <= 3:
            out.append(3)
            zeros = 0
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def sei(g, marker=b"OFMD"):
    offsets = bytes(code(offset(g, s, k)) for s in range(SEQUENCES) for k in range(frames))
    user = UUID + marker + bytes([0x81, 0, 0, 0, 0, 0, 0x80 | SEQUENCES, frames, 0x80, 0x00]) + offsets
    inner = bytes([5]) + bytes([0xFF] * (len(user) // 255)) + bytes([len(user) % 255]) + user
    payload = bytes([0x40]) + inner  # mvc_scalable_nesting: all view components, then the message
    msg = bytes([37]) + bytes([0xFF] * (len(payload) // 255)) + bytes([len(payload) % 255]) + payload
    return bytes([0x06]) + escape(msg + bytes([0x80]))  # rbsp trailing bits


def annexb(units):
    return b"".join(b"\0\0\0\1" + u for u in units)


def avcc(units):
    return b"".join(struct.pack(">I", len(u)) + u for u in units)


def records(name, items):
    with open(os.path.join(out, name), "wb") as f:
        for it in items:
            f.write(struct.pack(">I", len(it)) + it)


os.makedirs(out, exist_ok=True)
plain, withsei, dep, other = [], [], [], []
expected = []
for g in range(REPS):
    for k, au in enumerate(aus):
        units = list(au)
        u2 = list(au)
        u3 = list(au)
        if k == 0:
            first_vcl = next(i for i, x in enumerate(u2) if (x[0] & 0x1F) in (1, 5, 14, 20))
            u2.insert(first_vcl, sei(g))
            u3.insert(first_vcl, sei(g, b"OFMX"))
        plain.append(units)
        withsei.append(u2)
        other.append(u3)
        d = [x for x in au if (x[0] & 0x1F) == 20]
        if k == 0:
            d = [sei(g)] + d
        dep.append(d)
        expected.append(f"{g * frames + k} {g} {k} {1 if k == 0 else 0}")
records("combined-plain.annexb", [annexb(u) for u in plain])
records("combined-sei.annexb", [annexb(u) for u in withsei])
records("combined-plain.avcc", [avcc(u) for u in plain])
records("combined-sei.avcc", [avcc(u) for u in withsei])
records("dependent-sei.annexb", [annexb(u) for u in dep])
records("combined-othermarker.annexb", [annexb(u) for u in other])
records("combined-othermarker.avcc", [avcc(u) for u in other])
with open(os.path.join(out, "expected.txt"), "w") as f:
    f.write(f"frames {frames} reps {REPS} sequences {SEQUENCES}\n")
    for g in range(REPS):
        f.write(" ".join(str(offset(g, s, k)) for s in range(SEQUENCES) for k in range(frames)) + "\n")
types = [[x[0] & 0x1F for x in au] for au in withsei[:2]]
escaped = sum(sei(g).count(b"\0\0\3") for g in range(REPS))
print(f"{frames} access units x {REPS}; NAL types of the first two with the SEI: {types}; {escaped} escapes in the SEIs")
