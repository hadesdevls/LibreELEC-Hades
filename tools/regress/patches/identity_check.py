#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""LibreELEC Hades, TEST ONLY: the author of every patch of Hades in a tree is Hades' own identity.

usage: identity_check.py <folder>
Reads every *.patch under <folder> (its mail header: the lines before the first "---" or "diff --git", at most 40).
A patch is Hades' when the name or the address of its "From:" line has "hades" in it (any case), which also catches an
address of another account of the owner, which never signs anything of Hades. Such a line must be exactly IDENTITY. The patches of other authors
(LibreELEC, the kernel, FFmpeg, NVIDIA...) are left as they are.
Prints one "WRONG <path>: <line>" per patch that is Hades' and not signed so, then "<n> patches, <h> of Hades, <w>
wrong"; exit status 1 when one is wrong or when no patch of Hades was found (a folder that is not a tree)."""
import os
import re
import sys

IDENTITY = 'From: Hades <15710513+hadesdevls@users.noreply.github.com>'
FROM = re.compile(r'^From: (.*)$')


def author(path):
    with open(path, 'rb') as f:
        for n, raw in enumerate(f):
            if n >= 40:
                break
            line = raw.decode('utf-8', 'replace').rstrip('\r\n')
            if line == '---' or line.startswith('diff --git '):
                break
            if FROM.match(line):
                return line
    return None


def main():
    root = sys.argv[1]
    total = hades = wrong = 0
    for top, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            if not name.endswith('.patch'):
                continue
            total += 1
            path = os.path.join(top, name)
            line = author(path)
            if line is None:
                continue
            if 'hades' not in line.lower():
                continue
            hades += 1
            if line != IDENTITY:
                wrong += 1
                print('WRONG %s: %s' % (os.path.relpath(path, root), line))
    print('%d patches, %d of Hades, %d wrong' % (total, hades, wrong))
    return 1 if wrong or not hades else 0


if __name__ == '__main__':
    sys.exit(main())
