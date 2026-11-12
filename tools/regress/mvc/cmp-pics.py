#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): two lists of picture MD5s (picmd5.py), the reference first. Prints one word
and a detail: identical, DIFFERENT (how many pictures differ, the first one, the counts), NOTHING (the second
list is empty), NO-REFERENCE.
Usage: cmp-pics.py <reference list> <list>"""
import sys


def load(p):
    try:
        return [l.strip() for l in open(p) if l.strip()]
    except OSError:
        return []


a, b = load(sys.argv[1]), load(sys.argv[2])
if not a:
    print("NO-REFERENCE")
elif not b:
    print("NOTHING (reference %d pictures)" % len(a))
elif a == b:
    print("identical (%d pictures)" % len(a))
else:
    diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    first = diff[0] if diff else min(len(a), len(b))
    print("DIFFERENT (%d of %d pictures differ, the first at %d; reference %d pictures, this %d)"
          % (len(diff) + abs(len(a) - len(b)), max(len(a), len(b)), first, len(a), len(b)))
