#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): the MD5 of each picture of a raw 4:2:0 8-bit file, one a line.
Usage: picmd5.py <file.yuv> <width> <height>"""
import hashlib
import sys

w, h = int(sys.argv[2]), int(sys.argv[3])
n = w * h * 3 // 2
with open(sys.argv[1], "rb") as f:
    while True:
        b = f.read(n)
        if len(b) < n:
            if b:
                print("PARTIAL-%d" % len(b))
            break
        print(hashlib.md5(b).hexdigest())
