#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""skin_localize_check.py <kodi tree or unpacked folder> [skin id ...]

Static check of the labels of a skin: every $LOCALIZE[n] of the skin's XML must resolve. $LOCALIZE takes ids
31000-31999 from the skin's own strings.po and every other id from the core strings.po (resources/strings.po of
resource.language.en_gb); an id that is in neither (the skin's 32xxx strings, for instance, which are reached only
as $ADDON[<skin id> <n>]) comes out BLANK in a label.

Output: one line per finding, and a last line
  LOCALIZE-CHECK <skin>: <n> ids used, <f> findings
Exit status 1 with any finding. Python only (use `python3 -I`)."""
import os, re, sys

tree = sys.argv[1]
skins = sys.argv[2:] or ['skin.estuary', 'skin.estouchy']


def po_ids(path):
    ids = set()
    try:
        for line in open(path, encoding='utf-8'):
            m = re.match(r'msgctxt "#(\d+)"', line)
            if m:
                ids.add(int(m.group(1)))
    except OSError:
        return None
    return ids


core = po_ids(os.path.join(tree, 'addons/resource.language.en_gb/resources/strings.po'))
if core is None:
    print('LOCALIZE-CHECK: no core strings.po under %s' % tree)
    sys.exit(2)
total = 0
for skin in skins:
    sd = os.path.join(tree, 'addons', skin)
    if not os.path.isdir(sd):
        continue
    own = po_ids(os.path.join(sd, 'language/resource.language.en_gb/strings.po')) or set()
    findings = 0
    used = set()
    for root, _, files in os.walk(os.path.join(sd, 'xml')):
        for f in sorted(files):
            if not f.endswith('.xml'):
                continue
            p = os.path.join(root, f)
            for n, line in enumerate(open(p, encoding='utf-8', errors='replace'), 1):
                for m in re.finditer(r'\$LOCALIZE\[(\d+)\]', line):
                    i = int(m.group(1))
                    used.add(i)
                    if 31000 <= i <= 31999:
                        if i in own:
                            continue
                        why = "not in the skin's strings.po (31000-31999)"
                    elif i in core:
                        continue
                    elif i >= 32000:
                        why = 'neither a core string nor in 31000-31999: blank in a label (the skin\'s 32xxx strings are said as $ADDON[%s %d])' % (skin, i)
                    else:
                        why = 'not in the core strings.po'
                    findings += 1
                    print('%s:%d: $LOCALIZE[%d]: %s' % (os.path.relpath(p, tree), n, i, why))
    print('LOCALIZE-CHECK %s: %d ids used, %d findings' % (skin, len(used), findings))
    total += findings
sys.exit(1 if total else 0)
