#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""mutate_range.py <test.cpp> <header relative to the tree> <first line> <last line>: the method of
the mutation tool of the project (mutate.py) (same operators, a shadow copy of the header, the tree never touched),
but on EVERY mutation site of the given lines of the header (not a random sample of the whole header), for the
logic a test is about. Killed = the test does not pass (non-zero exit, a FAIL/FAILED line or no PASS line).
A mutant that does not compile is not counted. Survivors are listed: the reader judges if equivalent."""
import os, re, subprocess, sys, tempfile, shutil

test, hdr, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
K = os.environ['MVC_KODI']  # the Kodi tree (read only)
src = open(os.path.join(K, hdr), encoding='utf-8').read().split('\n')
code, in_block = [], False
for i, l in enumerate(src):
    s = l.strip()
    if in_block:
        if '*/' in s:
            in_block = False
        continue
    if s.startswith('/*'):
        if '*/' not in s:
            in_block = True
        continue
    if not s or s.startswith('//') or s.startswith('#') or s.startswith('*'):
        continue
    if first <= i + 1 <= last:
        code.append(i)
OPS = [('==', '!='), ('!=', '=='), ('<=', '<'), ('>=', '>'), ('&&', '||'), ('||', '&&'), (' true', ' false'), (' false', ' true')]
S = []
for i in code:
    cut = src[i].split('//')[0]
    for a, b in OPS:
        for m in re.finditer(re.escape(a), cut):
            S.append((i, m.start(), a, b))
    for m in re.finditer(r'(?<![\w.])(\d+)(?![\w.])', cut):
        S.append((i, m.start(), m.group(1), str(int(m.group(1)) + 1)))
work = os.environ['MVC_WORK']  # the shadow copies are made here
tmp = tempfile.mkdtemp(prefix='mutr', dir=work)
killed, survived, skipped = [], [], 0
try:
    for (i, col, a, b) in S:
        lines = list(src)
        lines[i] = lines[i][:col] + b + lines[i][col + len(a):]
        os.makedirs(os.path.join(tmp, os.path.dirname(hdr)), exist_ok=True)
        open(os.path.join(tmp, hdr), 'w', encoding='utf-8').write('\n'.join(lines))
        exe = os.path.join(tmp, 'm.out')
        # with AddressSanitizer and UndefinedBehaviorSanitizer: a mutant that reads out of bounds dies
        r = subprocess.run(['g++', '-std=c++20', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-I' + os.path.join(tmp, os.path.dirname(hdr)), test, '-o', exe],
                           capture_output=True, text=True)
        if r.returncode:
            skipped += 1
            continue
        try:
            p = subprocess.run([exe], capture_output=True, text=True, timeout=120)
            out = p.stdout + p.stderr
            ok = p.returncode == 0 and not re.search(r'^FAIL|FAILED', out, re.M) and re.search(r'PASS', out)
        except subprocess.TimeoutExpired:
            ok = False
        desc = '%s:%d: %s -> %s | %s' % (hdr, i + 1, a, b, src[i].strip()[:100])
        (survived if ok else killed).append(desc)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
print('%s with %s lines %d-%d: %d sites, %d mutants compiled, %d killed, %d survived, %d did not compile' % (
    os.path.basename(test), os.path.basename(hdr), first, last, len(S), len(killed) + len(survived), len(killed),
    len(survived), skipped))
for s in survived:
    print('  SURVIVED', s)
