#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# make-slswitch.sh <x265 8-bit> <x265 10-bit> <dest>: how the inputs of the stage slkeep of run-ffmpeg.sh were made
# (by hand, once; the runner reads them by their SHA256 from inputs-ffmpeg.tsv). x265 4.2 of the recipe
# packages/addons/addon-depends/ffmpegx-depends/x265, built for the host without assembly (8 and 10 bits). Each stream is
# segments of 8 pictures (an IDR, then P and B pictures), each encoded on its own with the same settings but those
# named, put one after the other: at each joint the VPS, SPS and PPS come again with the same ids, and the SPS
# changes as each segment's settings say.
#   slswitch8.hevc   segments A B C D A: the SPS changes only in its scaling lists (A: the default lists, B and D:
#                    lists of their own, C: none), at the 4 joints
#   slswitch10.hevc  the same in Main 10: A B A
#   slother8.hevc    segments A E A F G A10: E has no SAO, F more B pictures (another DPB size and reorder) and no
#                    strong intra smoothing, G the lists of B and no SAO, A10 is A in Main 10: the SPS changes in more
#                    than its scaling lists, at the 5 joints
set -euo pipefail
X8=$1 X10=$2 D=$3
mkdir -p "$D"
T=$(mktemp -d)
trap 'rm -rf -- "$T"' EXIT
# 48 pictures of 416x240 4:2:0, 8 bits: moving gradients and a fixed pseudo-random texture (so that quantisation, and
# so the scaling lists, change the pictures)
python3 -I -B - "$T/src.yuv" <<'PY'
import sys, random
w, h, n = 416, 240, 48
rnd = random.Random(20261127)
tex = bytes(rnd.randrange(0, 48) for _ in range(w * h))
with open(sys.argv[1], 'wb') as f:
    for k in range(n):
        y = bytearray(w * h)
        for j in range(h):
            for i in range(w):
                y[j * w + i] = (((i + 3 * k) * 255 // w + ((j * 7 + k * 5) % 64)) // 2 + tex[j * w + i]) & 255
        u = bytes(((i * 2 + k * 4) & 255) for j in range(h // 2) for i in range(w // 2))
        v = bytes(((j * 3 + k * 2) & 255) for j in range(h // 2) for i in range(w // 2))
        f.write(y); f.write(u); f.write(v)
PY
# two HM-style scaling list files of their own
python3 -I -B - "$T" <<'PY'
import sys
names = {0: ['INTRA4X4_LUMA', 'INTRA4X4_CHROMAU', 'INTRA4X4_CHROMAV', 'INTER4X4_LUMA', 'INTER4X4_CHROMAU', 'INTER4X4_CHROMAV'],
         1: ['INTRA8X8_LUMA', 'INTRA8X8_CHROMAU', 'INTRA8X8_CHROMAV', 'INTER8X8_LUMA', 'INTER8X8_CHROMAU', 'INTER8X8_CHROMAV'],
         2: ['INTRA16X16_LUMA', 'INTRA16X16_CHROMAU', 'INTRA16X16_CHROMAV', 'INTER16X16_LUMA', 'INTER16X16_CHROMAU', 'INTER16X16_CHROMAV'],
         3: ['INTRA32X32_LUMA', '', '', 'INTER32X32_LUMA', '', '']}
for tag, base, step in (('b', 12, 3), ('d', 20, 5)):
    out = []
    for size in range(4):
        n = 16 if size == 0 else 64
        for lst in range(0, 6, 3 if size == 3 else 1):
            out.append(names[size][lst] + ' =')
            vals = [base + ((i * step + lst * 7 + size * 3) % 40) for i in range(n)]
            out.append(','.join(str(v) for v in vals) + ',')
            if size >= 2:
                out.append(names[size][lst] + '_DC =')
                out.append('%d,' % (base + lst + size))
    open('%s/list-%s.txt' % (sys.argv[1], tag), 'w').write('\n'.join(out) + '\n')
PY
seg() { # <name> <x265> <first picture> <settings...>
  local name=$1 x=$2 first=$3
  shift 3
  "$x" --input "$T/src.yuv" --input-res 416x240 --fps 24 --input-depth 8 --seek "$first" --frames 8 \
    --preset medium --keyint 8 --min-keyint 8 --no-scenecut --bframes 2 --no-open-gop --repeat-headers \
    --no-info --frame-threads 1 --pools 1 --no-wpp "$@" --output "$T/$name.hevc" > "$T/$name.log" 2>&1 \
    || { cat "$T/$name.log"; exit 1; }
}
seg A   "$X8" 0  --ref 3 --scaling-list default
seg B   "$X8" 8  --ref 3 --scaling-list "$T/list-b.txt"
seg C   "$X8" 16 --ref 3 --scaling-list off
seg D   "$X8" 24 --ref 3 --scaling-list "$T/list-d.txt"
seg A2  "$X8" 32 --ref 3 --scaling-list default
seg E   "$X8" 8  --ref 3 --scaling-list default --no-sao
seg F   "$X8" 24 --ref 3 --scaling-list default --bframes 4 --no-strong-intra-smoothing
seg G   "$X8" 32 --ref 3 --scaling-list "$T/list-b.txt" --no-sao
seg A10 "$X10" 40 --ref 3 --scaling-list default --output-depth 10
seg B10 "$X10" 8  --ref 3 --scaling-list "$T/list-b.txt" --output-depth 10
seg C10 "$X10" 16 --ref 3 --scaling-list default --output-depth 10
cat "$T"/A.hevc "$T"/B.hevc "$T"/C.hevc "$T"/D.hevc "$T"/A2.hevc > "$D/slswitch8.hevc"
cat "$T"/A10.hevc "$T"/B10.hevc "$T"/C10.hevc > "$D/slswitch10.hevc"
cat "$T"/A.hevc "$T"/E.hevc "$T"/A2.hevc "$T"/F.hevc "$T"/G.hevc "$T"/A10.hevc > "$D/slother8.hevc"
cp "$T"/list-b.txt "$T"/list-d.txt "$D/"
( cd "$D" && sha256sum slswitch8.hevc slswitch10.hevc slother8.hevc && wc -c slswitch8.hevc slswitch10.hevc slother8.hevc )
