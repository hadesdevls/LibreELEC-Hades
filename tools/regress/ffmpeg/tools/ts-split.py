#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Re-packetizes the video PID of an MPEG transport stream (one access unit per PES, as FFmpeg's muxer writes it)
# and splits chosen access units across two PES packets, to see what the HEVC parser does with them.
# Usage: ts-split.py <in.ts> <out.ts> <mode> [<access unit numbers, comma separated>]
#   none    : no change (the control: the same stream, packetized by this script)
#   headers : the units before the first VCL unit go in a PES of their own without a time; the slices follow
#             in a PES with the times of the access unit (the PES of the first picture "without a time")
#   next    : the same headers PES without a time, then the slices of the access unit together with the whole
#             next access unit, in one PES with the times of that next access unit (the conforming case: a time
#             is the one of the first access unit that begins in its PES)
# Only the video PID (the first PID whose PES carry 0x000001 E0..EF) is rewritten; PAT and PMT packets are kept as
# they are; packets of other PIDs are dropped. No PCR is written (the demuxer does not need one).
import struct
import sys

src, dst, mode = sys.argv[1], sys.argv[2], sys.argv[3]
which = {int(x) for x in sys.argv[4].split(",")} if len(sys.argv) > 4 and sys.argv[4] else {0}
data = open(src, "rb").read()
if len(data) % 188:
    raise SystemExit("not 188-byte packets")

psi = []          # PAT/PMT packets kept as they are
pes = []          # (pts, dts, payload) of the video PID
video = None
cur = None
pmt_pids = set()
for off in range(0, len(data), 188):
    p = data[off:off + 188]
    if p[0] != 0x47:
        raise SystemExit("lost sync")
    pid = ((p[1] & 0x1F) << 8) | p[2]
    pusi = p[1] & 0x40
    afc = (p[3] >> 4) & 3
    i = 4
    if afc & 2:
        i += 1 + p[4]
    payload = p[i:] if afc & 1 else b""
    if pid == 0:
        psi.append(p)
        if pusi:
            q = 1 + payload[0]
            sec = payload[q:]
            n = ((sec[1] & 0x0F) << 8) | sec[2]
            for k in range(8, 3 + n - 4, 4):
                prog = (sec[k] << 8) | sec[k + 1]
                if prog:
                    pmt_pids.add(((sec[k + 2] & 0x1F) << 8) | sec[k + 3])
        continue
    if pid in pmt_pids:
        psi.append(p)
        continue
    if pusi and payload[:3] == b"\0\0\1" and 0xE0 <= payload[3] <= 0xEF:
        if video is None:
            video = pid
    if pid != video:
        continue
    if pusi:
        if cur:
            pes.append(cur)
        flags = payload[7]
        hlen = payload[8]
        pts = dts = None
        def ts(b):
            return ((b[0] >> 1) & 7) << 30 | b[1] << 22 | (b[2] >> 1) << 15 | b[3] << 7 | b[4] >> 1
        if flags & 0x80:
            pts = ts(payload[9:14])
            dts = pts
        if flags & 0x40:
            dts = ts(payload[14:19])
        cur = [pts, dts, bytearray(payload[9 + hlen:])]
    elif cur:
        cur[2] += payload
if cur:
    pes.append(cur)
psi_pat = [p for p in psi if (((p[1] & 0x1F) << 8) | p[2]) == 0][:1]
psi_pmt = [p for p in psi if (((p[1] & 0x1F) << 8) | p[2]) in pmt_pids][:1]


def first_vcl(au):
    i = 0
    while True:
        j = au.find(b"\0\0\1", i)
        if j < 0:
            return None
        nut = (au[j + 3] >> 1) & 0x3F
        if nut < 32:
            # a zero byte before the start code belongs to this unit
            return j - 1 if j > 0 and au[j - 1] == 0 else j
        i = j + 3


def ts_bytes(prefix, v):
    return bytes([(prefix << 4) | (((v >> 30) & 7) << 1) | 1, (v >> 22) & 0xFF, (((v >> 15) & 0x7F) << 1) | 1,
                  (v >> 7) & 0xFF, ((v & 0x7F) << 1) | 1])


def pes_header(pts, dts):
    if pts is None:
        return bytes([0, 0, 1, 0xE0, 0, 0, 0x80, 0x00, 0])
    if dts is None or dts == pts:
        return bytes([0, 0, 1, 0xE0, 0, 0, 0x80, 0x80, 5]) + ts_bytes(2, pts)
    return bytes([0, 0, 1, 0xE0, 0, 0, 0x80, 0xC0, 10]) + ts_bytes(3, pts) + ts_bytes(1, dts)


out = []
cc = 0


def emit(pts, dts, payload):
    global cc
    body = pes_header(pts, dts) + bytes(payload)
    first = True
    i = 0
    while i < len(body):
        chunk = body[i:i + 184]
        i += len(chunk)
        hdr = bytes([0x47, (0x40 if first else 0) | (video >> 8), video & 0xFF])
        if len(chunk) == 184:
            pkt = hdr + bytes([0x10 | cc]) + chunk
        else:
            stuff = 184 - len(chunk)
            if stuff == 1:
                af = bytes([0])
            else:
                af = bytes([stuff - 1, 0]) + b"\xff" * (stuff - 2)
            pkt = hdr + bytes([0x30 | cc]) + af + chunk
        assert len(pkt) == 188
        out.append(pkt)
        cc = (cc + 1) & 15
        first = False


out += psi_pat + psi_pmt
n = 0
k = 0
split = 0
while k < len(pes):
    pts, dts, au = pes[k]
    if mode != "none" and n in which:
        v = first_vcl(au)
        if v is None or v == 0:
            raise SystemExit(f"access unit {n}: no units before its first slice")
        emit(None, None, au[:v])
        if mode == "headers":
            emit(pts, dts, au[v:])
        elif mode == "next":
            if k + 1 >= len(pes):
                raise SystemExit("no next access unit")
            npts, ndts, nau = pes[k + 1]
            emit(npts, ndts, au[v:] + nau)
            k += 1
            n += 1
        else:
            raise SystemExit("mode: none, headers or next")
        split += 1
    else:
        emit(pts, dts, au)
    k += 1
    n += 1
open(dst, "wb").write(b"".join(out))
print(f"{dst}: {len(pes)} PES of PID {video:#x}, {split} split ({mode})")
