#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""mutate.py: the mutants of the kodi-host group (mutants.py), each a copy of the code under test with one rule of
it changed, which a test must catch.

usage: mutate.py list [<test>...]            one line per (mutant, test): <mutant> <test> <expected>
       mutate.py make <mutant> <tree> <dest>   <dest>: the tree's xbmc and tools as hard links, the mutant's file
                                             written anew (never through a link: the tree is not changed)
Status 3 when an edit no longer applies (its text is not in the file as many times as the mutant says): the mutant
is then out of date, and whoever keeps the test updates it. <expected>: the number of failed checks the test prints
on the mutant (its lines that start with FAIL, the FAILED of
its last line included), '-' for any number above 0, 'static' for a test that no longer compiles because a static
assertion of it fails, or 'exit' for a test that ends with a status other than 0 without a check of its own failing
(a sanitizer stops it).
"""
import os
import runpy
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MUTANTS = runpy.run_path(os.path.join(HERE, 'mutants.py'))['MUTANTS']


def by_name(name):
    for m in MUTANTS:
        if m['name'] == name:
            return m
    sys.exit('no mutant %s' % name)


def edit(text, pairs, where):
    for p in pairs:
        old, new, n = p if len(p) == 3 else (p[0], p[1], 1)
        if text.count(old) != n:
            print('EDIT NOT APPLIED to %s: the text is there %d times, not %d: %r' % (where, text.count(old), n, old[:120]))
            sys.exit(3)
        text = text.replace(old, new)
    return text


def link_tree(src, dst, top):
    for root, dirs, files in os.walk(os.path.join(src, top)):
        rel = os.path.relpath(root, src)
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            os.link(os.path.join(root, f), os.path.join(dst, rel, f))


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == 'list':
        want = set(sys.argv[2:])
        names = set()
        for m in MUTANTS:
            if m['name'] in names:
                sys.exit('mutant %s given twice' % m['name'])
            names.add(m['name'])
            for test, expected in m['tests'].items():
                if not want or test in want:
                    print('%s %s %s' % (m['name'], test, expected))
        return
    if len(sys.argv) == 5 and sys.argv[1] == 'make':
        m = by_name(sys.argv[2])
        tree, dest = sys.argv[3], sys.argv[4]
        if os.path.exists(dest):
            sys.exit('%s exists' % dest)
        os.makedirs(dest)
        # (what the tests read: the sources, the shaders, the keymaps, the settings, and the strings of the core and
        # of Estuary)
        for top in ('xbmc', 'tools/dvbridge', 'system/shaders', 'system/keymaps', 'system/settings',
                    'addons/resource.language.en_gb',
                    'addons/skin.estuary/language', 'addons/skin.estuary/xml', 'addons/skin.estuary/fonts'):
            if os.path.isdir(os.path.join(tree, top)):
                link_tree(tree, dest, top)
        path = os.path.join(dest, m['file'])
        if not os.path.isfile(path):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            shutil.copyfile(os.path.join(tree, m['file']), path + '.orig')
            src = path + '.orig'
        else:
            src = path
        text = edit(open(src, encoding='utf-8').read(), m['pairs'], m['file'])
        os.unlink(src)
        with open(path, 'w', encoding='utf-8') as out:
            out.write(text)
        print('mutant %s: %s changed (%d edits)' % (m['name'], m['file'], len(m['pairs'])))
        return
    sys.exit(__doc__)


if __name__ == '__main__':
    main()
