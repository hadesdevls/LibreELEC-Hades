#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc, finding 10): the sizes of the prefix NAL units (type 14) of each stream, and
how many are exactly the four bytes of a header with no payload. Usage: prefix-sizes.py <stream.264>..."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h264bits as hb

for f in sys.argv[1:]:
    sizes = {}
    for n in hb.split_annexb(open(f, "rb").read()):
        if hb.nal_type(n) == 14:
            sizes[len(n)] = sizes.get(len(n), 0) + 1
    print("%s: prefix NAL units by size %s" % (os.path.basename(f), sorted(sizes.items())))
