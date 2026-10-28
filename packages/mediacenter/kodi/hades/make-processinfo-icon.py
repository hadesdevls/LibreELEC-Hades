#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Draws processinfo.png, next to this script: the icon of the button of the video OSD of Estuary that opens the
# player information (kodi-9005), which the recipe of kodi copies into the skin (post_patch). Drawn for this build;
# nothing of it is taken from Estuary or any other source. Usage: make-processinfo-icon.py <file to write>
# Draw the Estuary OSD icon "player process info": a white panel outline with three text lines, 128x128,
# 8-bit grey + alpha like the other OSD button textures, antialiased by 8x8 supersampling. Standard library only.
import struct
import sys
import zlib

W = H = 128
S = 8  # supersampling factor per axis


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def covered(x, y):
    # panel outline: outer rounded rectangle minus inner one (stroke 8 px)
    outer = inside_round_rect(x, y, 16, 24, 112, 104, 12)
    inner = inside_round_rect(x, y, 24, 32, 104, 96, 5)
    if outer and not inner:
        return True
    # three text lines with round caps, 8 px thick
    for (ly, lx1) in ((48, 88), (64, 78), (80, 66)):
        if inside_round_rect(x, y, 38, ly - 4, lx1, ly + 4, 4):
            return True
    return False


rows = []
for py in range(H):
    row = bytearray([0])  # filter type 0
    for px in range(W):
        hits = 0
        for sy in range(S):
            for sx in range(S):
                if covered(px + (sx + 0.5) / S, py + (sy + 0.5) / S):
                    hits += 1
        alpha = round(255 * hits / (S * S))
        row += bytes([255, alpha])  # white glyph, coverage as alpha
    rows.append(bytes(row))


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 4, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(b''.join(rows), 9))
png += chunk(b'IEND', b'')
open(sys.argv[1], 'wb').write(png)
print(f'{sys.argv[1]}: {len(png)} bytes')
