#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""A few files of the kernel of the recipe, as the image's build has them: taken from the source archive and patched
with every part of the recipe's patches (in the order LibreELEC's scripts/unpack applies them) that changes them, with
patch -F0. A patch changes a file in its own sections only, so that those files come out as in the whole tree.

usage: kernel_files.py <archive .tar.xz or .tar.gz> <out folder> <patch>... -- <file of the tree>...
Prints one line per patch that changed one of the files, and KERNEL-FILES-DONE; status 1 when a section does not
apply or a file is not there after all of them."""
import os
import re
import subprocess
import sys
import tarfile

args = sys.argv[1:]
cut = args.index('--')
archive, out, patches, wanted = args[0], args[1], args[2:cut], set(args[cut + 1:])
os.makedirs(out, exist_ok=True)
found = 0
with tarfile.open(archive, 'r:*') as tar:
    for member in tar:
        if not member.isfile():
            continue
        name = member.name.split('/', 1)[1] if '/' in member.name else member.name
        if name in wanted:
            os.makedirs(os.path.dirname(os.path.join(out, name)), exist_ok=True)
            with tar.extractfile(member) as src, open(os.path.join(out, name), 'wb') as dst:
                dst.write(src.read())
            found += 1
print('%d of the %d files are in %s' % (found, len(wanted), os.path.basename(archive)))
bad = 0
for p in patches:
    text = open(p, encoding='utf-8', errors='surrogateescape').read()
    # the sections of a git patch: from one "diff --git" to the next
    parts = re.split(r'(?m)^(?=diff --git )', text)
    mine = []
    for part in parts:
        m = re.match(r'diff --git a/(\S+) b/(\S+)', part)
        if m and (m.group(1) in wanted or m.group(2) in wanted):
            mine.append(part)
    if not mine:
        continue
    body = ''.join(mine)
    r = subprocess.run(['patch', '-d', out, '-p1', '-F0', '--no-backup-if-mismatch', '-s'], input=body.encode('utf-8', 'surrogateescape'),
                       capture_output=True)
    names = [re.match(r'diff --git a/\S+ b/(\S+)', s).group(1) for s in mine]
    if r.returncode != 0:
        bad += 1
        print('PATCH SECTION FAILED %s (%s): %s' % (os.path.basename(p), ' '.join(names), r.stdout.decode(errors='replace')[-300:]))
    else:
        print('%s: %s' % (os.path.basename(p), ' '.join(names)))
missing = [w for w in sorted(wanted) if not os.path.isfile(os.path.join(out, w))]
if missing:
    print('NOT THERE after the patches: ' + ' '.join(missing))
    bad += 1
print('KERNEL-FILES-DONE' if not bad else 'KERNEL-FILES-FAILED')
sys.exit(1 if bad else 0)
