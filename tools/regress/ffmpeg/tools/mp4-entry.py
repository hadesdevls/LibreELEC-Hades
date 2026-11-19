#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Rewrites the four-character code of the first HEVC sample entry of one track of an MP4 file, in place of the
# old one (same size: nothing else moves). Used to make the dual-track Dolby Vision carriage with the
# enhancement track in an ordinary HEVC sample entry ('hvc1'/'hev1') from a file that has it in a Dolby Vision
# one ('dvh1'/'dvhe'), as some muxers write it.
# Usage: mp4-entry.py <in.mp4> <out.mp4> <track id> <hvc1|hev1|dvh1|dvhe>
import struct
import sys

HEVC = (b"hvc1", b"hev1", b"dvh1", b"dvhe")


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


def child(data, start, end, path):
    for pos, size, kind, header in boxes(data, start, end):
        if kind == path[0]:
            if len(path) == 1:
                return pos, size, header
            return child(data, pos + header, pos + size, path[1:])
    raise SystemExit(f"no {path[0].decode()} box")


def main():
    src, dst, track, new = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4].encode()
    if new not in HEVC:
        raise SystemExit("hvc1, hev1, dvh1 or dvhe")
    data = bytearray(open(src, "rb").read())
    mpos, msize, mh = child(data, 0, len(data), [b"moov"])
    done = False
    for tpos, tsize, kind, th in boxes(data, mpos + mh, mpos + msize):
        if kind != b"trak":
            continue
        kpos, ksize, kh = child(data, tpos + th, tpos + tsize, [b"tkhd"])
        version = data[kpos + kh]
        off = kpos + kh + 4 + (16 if version == 1 else 8)
        if struct.unpack(">I", data[off:off + 4])[0] != track:
            continue
        spos, ssize, sh = child(data, tpos + th, tpos + tsize, [b"mdia", b"minf", b"stbl", b"stsd"])
        # full box header (4) and entry count (4), then the sample entries
        entries = struct.unpack(">I", data[spos + sh + 4:spos + sh + 8])[0]
        epos = spos + sh + 8
        for _ in range(entries):
            esize, ekind = struct.unpack(">I4s", data[epos:epos + 8])
            if ekind in HEVC:
                data[epos + 4:epos + 8] = new
                print(f"{dst}: track {track}: sample entry {ekind.decode()} -> {new.decode()}")
                done = True
                break
            epos += esize
        break
    if not done:
        raise SystemExit(f"no HEVC sample entry in track {track}")
    open(dst, "wb").write(data)


main()
