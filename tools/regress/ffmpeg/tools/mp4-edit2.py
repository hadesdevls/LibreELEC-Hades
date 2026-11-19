#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Variants of a dual-track Dolby Vision MP4 file for the negative checks of the grouping rule (the moov box must be
# after the media data: only it changes). Usage:
#   mp4-edit2.py <in.mp4> <out.mp4> vdep2 <extra track id>   the 'vdep' reference names a second track
#   mp4-edit2.py <in.mp4> <out.mp4> profile <n>              the 'dvcC'/'dvvC' record says profile n
#   mp4-edit2.py <in.mp4> <out.mp4> bl                      the record says it has a base layer too
import struct
import sys


def boxes(data, start, end):
    i = start
    while i + 8 <= end:
        size, kind = struct.unpack(">I4s", data[i:i + 8])
        header = 8
        if size == 1:
            size = struct.unpack(">Q", data[i + 8:i + 16])[0]
            header = 16
        elif size == 0:
            size = end - i
        if size < header or i + size > end:
            raise SystemExit(f"bad box at {i}")
        yield i, size, kind, header
        i += size


def main():
    src, dst, what = sys.argv[1], sys.argv[2], sys.argv[3]
    data = bytearray(open(src, "rb").read())
    top = list(boxes(data, 0, len(data)))
    moov = [b for b in top if b[2] == b"moov"]
    mdat = [b for b in top if b[2] == b"mdat"]
    if len(moov) != 1 or not mdat or moov[0][0] < mdat[0][0]:
        raise SystemExit("need one moov after the mdat")
    mpos, msize, _, mh = moov[0]
    if what == "vdep2":
        extra = int(sys.argv[4])
        at = data.find(b"vdep", mpos, mpos + msize)
        if at < 0 or data.find(b"vdep", at + 4, mpos + msize) >= 0:
            raise SystemExit("not one vdep box")
        vpos = at - 4
        if struct.unpack(">I", data[vpos:vpos + 4])[0] != 12:
            raise SystemExit("vdep box is not of one track")
        tref = vpos - 8
        if data[tref + 4:tref + 8] != b"tref":
            raise SystemExit("vdep is not the first box of its tref")
        trak = None
        for tpos, tsize, kind, th in boxes(data, mpos + mh, mpos + msize):
            if kind == b"trak" and tpos < vpos < tpos + tsize:
                trak = (tpos, tsize)
        data[vpos + 12:vpos + 12] = struct.pack(">I", extra)
        for pos in (vpos, tref, trak[0], mpos):
            struct.pack_into(">I", data, pos, struct.unpack(">I", data[pos:pos + 4])[0] + 4)
        print(f"{dst}: vdep names a second track, {extra}")
    elif what in ("profile", "bl"):
        at = data.find(b"dvcC", mpos, mpos + msize)
        if at < 0:
            at = data.find(b"dvvC", mpos, mpos + msize)
        if at < 0:
            raise SystemExit("no record")
        p = at + 4 + 2  # after version major and minor
        bits = struct.unpack(">H", data[p:p + 2])[0]
        if what == "profile":
            bits = (bits & 0x01FF) | (int(sys.argv[4]) << 9)
        else:
            bits |= 0x0001
        struct.pack_into(">H", data, p, bits)
        print(f"{dst}: record changed ({what})")
    else:
        raise SystemExit("vdep2, profile or bl")
    open(dst, "wb").write(data)


main()
