#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): a stream of two views made a stream of one view, for the decoders as 2D
decoders: the NAL units of the second view and of MVC (14 prefix, 15 subset SPS, 20 coded slice extension) are
taken out, everything else is kept as it is. Usage: base2d.py <in.264> <out.264>"""
import sys
b = open(sys.argv[1], "rb").read()
starts, i = [], 0
while True:
    j = b.find(b"\x00\x00\x01", i)
    if j < 0:
        break
    starts.append(j + 3)
    i = j + 3
out, kept = bytearray(), 0
for k, s in enumerate(starts):
    e = starts[k + 1] - 3 if k + 1 < len(starts) else len(b)
    while e > s and b[e - 1] == 0 and k + 1 < len(starts):
        e -= 1
    if (b[s] & 0x1F) in (14, 15, 20):
        continue
    out += b"\x00\x00\x00\x01" + b[s:e]
    kept += 1
open(sys.argv[2], "wb").write(out)
print("%s: %d NAL units kept" % (sys.argv[2].split("/")[-1], kept))
