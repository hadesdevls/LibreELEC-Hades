# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Makes, from a stream of two views whose first picture is an anchor, a stream in which the second view
# comes late: N copies of the stream one after the other, with every NAL unit of the second view (types 20
# and, optionally, 15 and 14) taken out of the first K access units.
#   late.py <stream> <copies> <first K access units without a second view> <stream to write>
import sys

data = open(sys.argv[1], "rb").read()
copies, lost = int(sys.argv[2]), int(sys.argv[3])
starts, i = [], 0
while True:
    j = data.find(b"\x00\x00\x01", i)
    if j < 0:
        break
    starts.append(j + 3)
    i = j + 3
nals = []
for k, s in enumerate(starts):
    e = starts[k + 1] - 3 if k + 1 < len(starts) else len(data)
    while e > s and data[e - 1] == 0:
        e -= 1
    nals.append(data[s:e])


def first_mb_is_zero(nal):
    # first_mb_in_slice is the first ue(v) of the slice header: 0 is the single bit 1
    return (nal[1] & 0x80) != 0


out = bytearray()
unit = -1
kept = dropped = 0
for c in range(copies):
    for nal in nals:
        t = nal[0] & 0x1F
        if t in (1, 5) and first_mb_is_zero(nal):
            unit += 1
        # (the slices of the second view and its sequence parameter set; the parameter sets of a copy stand
        # before its first slice, so they count with the unit before it)
        if t in (20, 15) and unit + (1 if t == 15 else 0) < lost:
            dropped += 1
            continue
        out += b"\x00\x00\x00\x01" + nal
        kept += 1
open(sys.argv[4], "wb").write(out)
print("%d access units in %d copies; NAL units kept %d, of the second view taken out %d (units 0 to %d)" % (unit + 1, copies, kept, dropped, lost - 1))
