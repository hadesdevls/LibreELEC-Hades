#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades: the private data (extradata) of the first video stream of a file, as ffprobe gives it after
avformat_find_stream_info(), written to a file; prints its size and the stream tag mvc_configuration.
Usage: ff-extradata.py <ffprobe> <media file> <out file>"""
import re, subprocess, sys

ffprobe, media, out = sys.argv[1:4]
text = subprocess.run([ffprobe, "-hide_banner", "-v", "error", "-select_streams", "v:0", "-show_streams", "-show_data",
                       media], capture_output=True, text=True, stdin=subprocess.DEVNULL).stdout
data, tag, inside = bytearray(), "", False
for line in text.splitlines():
    if line.startswith("extradata="):
        inside = True
        continue
    if inside:
        # "00000000: 0164 0029 ffe1 ...  .d.)..": the hex, then two spaces and the characters
        if not re.match(r"^[0-9a-f]{8}: ", line):
            inside = False
        else:
            data += bytes.fromhex(line[10:].split("  ")[0].replace(" ", ""))
            continue
    if line.startswith("TAG:mvc_configuration="):
        tag = line.split("=", 1)[1]
open(out, "wb").write(bytes(data))
print("%d %s" % (len(data), tag or "-"))
