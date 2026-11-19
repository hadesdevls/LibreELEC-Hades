#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Mutants of the Kodi tree for the tests of run-dvbridge-meta.sh: each takes out one thing that a test protects,
by exact edits of a copy (each text found exactly once). A spec is a Python file that defines

  DIRS = ['tools/dvbridge', ...]        the folders of the Kodi tree that a mutant tree copies
  MUTANTS = [(name, path, [(old, new), ...], test, expected, what), ...]
      path      the file of the tree that the edits change (an edit (file, old, new) changes another file)
      test      which test of the runner must catch it (the runner's own name for it)
      expected  the number of failed checks that the test then has, "-" for any number above 0, "nobuild" when
                the test must not build against it
      what      what it takes out, in words

usage: mutants.py list <spec> [<test>]          "<name> <test> <expected>", one line each
       mutants.py what <spec> <name>            the words of a mutant
       mutants.py check <spec> <tree>           MUTANT-TEXTS lines: each text of each mutant found once in the tree
       mutants.py make <spec> <name> <tree> <dest>
                                                <dest>/<DIRS> copied from the tree, the mutant's edits applied there
                                                (status 3: "MUTANT NOT APPLIED", a text that is not there once)
"""
import os
import runpy
import shutil
import sys


def load(spec):
    ns = runpy.run_path(spec)
    names = [m[0] for m in ns['MUTANTS']]
    if len(names) != len(set(names)):
        sys.exit('%s: a mutant name given twice' % spec)
    return ns['DIRS'], ns['MUTANTS']


def find(mutants, name):
    for m in mutants:
        if m[0] == name:
            return m
    sys.exit('no mutant %s' % name)


def edits(m):
    """the edits of a mutant as (file, old, new)"""
    return [e if len(e) == 3 else (m[1], e[0], e[1]) for e in m[2]]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmd, spec = sys.argv[1], sys.argv[2]
    dirs, mutants = load(spec)
    if cmd == 'list':
        for m in mutants:
            if len(sys.argv) < 4 or m[3] == sys.argv[3]:
                print('%s %s %s' % (m[0], m[3], m[4]))
        return 0
    if cmd == 'what':
        print(find(mutants, sys.argv[3])[5])
        return 0
    if cmd == 'check':
        tree, bad = sys.argv[3], 0
        for m in mutants:
            counts = [open(os.path.join(tree, f), encoding='utf-8').read().count(old) for f, old, new in edits(m)]
            ok = all(c == 1 for c in counts)
            bad += not ok
            files = sorted(set(f for f, old, new in edits(m)))
            print('MUTANT-TEXTS %s: %s, %d edit(s), found %s' % (m[0], ' '.join(files), len(counts),
                                                                  counts if not ok else 'once each'))
        print('MUTANT-TEXTS %d mutants, %d with a text not found once' % (len(mutants), bad))
        return 1 if bad else 0
    if cmd == 'make':
        name, tree, dest = sys.argv[3], sys.argv[4], sys.argv[5]
        m = find(mutants, name)
        if os.path.exists(dest):
            shutil.rmtree(dest)
        for d in dirs:
            shutil.copytree(os.path.join(tree, d), os.path.join(dest, d), symlinks=True)
        texts = {}
        for f, old, new in edits(m):
            path = os.path.join(dest, f)
            if f not in texts:
                if not os.path.isfile(path):
                    os.makedirs(os.path.dirname(path), exist_ok=True)
                    shutil.copy(os.path.join(tree, f), path)
                texts[f] = open(path, encoding='utf-8').read()
            n = texts[f].count(old)
            if n != 1:
                print('MUTANT NOT APPLIED %s: %s has the text %d times: %r' % (name, f, n, old[:100]))
                return 3
            texts[f] = texts[f].replace(old, new)
        for f, text in texts.items():
            path = os.path.join(dest, f)
            os.remove(path)  # (a new file: never written through a link)
            open(path, 'w', encoding='utf-8').write(text)
        print('mutant %s made in %s (%s)' % (name, dest, ' '.join(sorted(texts))))
        return 0
    sys.exit(__doc__)


if __name__ == '__main__':
    sys.exit(main())
