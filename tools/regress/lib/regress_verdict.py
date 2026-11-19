#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""The verdict of a run of a tools/regress runner, read line by line from its log.

usage: regress_verdict.py <log> [<stage> ...]
       regress_verdict.py --selftest

Every line that starts with "REGRESS-ITEM" must read "REGRESS-ITEM <group>/<name>: PASSED|FAILED|NOT-RUN | <detail>";
a line that starts so and does not is counted as failed (malformed), and so is a name given twice. Every stage named
on the command line must have a "REGRESS-STAGE <stage> START" line and a later "REGRESS-STAGE <stage> END" line; a
stage that did not start, or started and did not end, is a failed item. Prints "REGRESS-DONE n items, p passed,
f failed, r not run" and "REGRESS-VERDICT PASSED" only when n > 0, f = 0 and r = 0 (else "REGRESS-VERDICT FAILED"),
and exits 0 only then.
"""
import re
import sys

ITEM = re.compile(r'^REGRESS-ITEM ([A-Za-z0-9._+-]+/[A-Za-z0-9._+/-]+): (PASSED|FAILED|NOT-RUN) \| (.*)$')
STAGE = re.compile(r'^REGRESS-STAGE (\S+) (START|END)$')


def verdict(lines, stages):
    counts = {'PASSED': 0, 'FAILED': 0, 'NOT-RUN': 0}
    seen = set()
    notes = []
    started, ended = {}, {}
    for i, line in enumerate(lines, 1):
        line = line.rstrip('\n')
        if line.startswith('REGRESS-ITEM'):
            m = ITEM.match(line)
            if not m:
                counts['FAILED'] += 1
                notes.append('line %d: malformed item: %s' % (i, line[:120]))
                continue
            name, state = m.group(1), m.group(2)
            if name in seen:
                counts['FAILED'] += 1
                notes.append('line %d: item %s given twice' % (i, name))
                continue
            seen.add(name)
            counts[state] += 1
            if state != 'PASSED':
                notes.append('line %d: %s %s: %s' % (i, name, state, m.group(3)[:160]))
            continue
        m = STAGE.match(line)
        if m:
            (started if m.group(2) == 'START' else ended).setdefault(m.group(1), i)
    for s in stages:
        if s not in started:
            counts['FAILED'] += 1
            notes.append('stage %s did not run' % s)
        elif s not in ended or ended[s] < started[s]:
            counts['FAILED'] += 1
            notes.append('stage %s did not end' % s)
    n = sum(counts.values())
    ok = n > 0 and counts['FAILED'] == 0 and counts['NOT-RUN'] == 0
    out = notes[:60] + ['REGRESS-DONE %d items, %d passed, %d failed, %d not run'
                        % (n, counts['PASSED'], counts['FAILED'], counts['NOT-RUN']),
                        'REGRESS-VERDICT %s' % ('PASSED' if ok else 'FAILED')]
    return ok, out


def selftest():
    cases = [
        ('all passed', ['REGRESS-STAGE a START', 'REGRESS-ITEM g/x: PASSED | ok', 'REGRESS-STAGE a END'], ['a'], True),
        ('a failed', ['REGRESS-ITEM g/x: PASSED | ok', 'REGRESS-ITEM g/y: FAILED | no'], [], False),
        ('a not run', ['REGRESS-ITEM g/x: NOT-RUN | no input'], [], False),
        ('malformed', ['REGRESS-ITEM g/x: PASSED | ok', 'REGRESS-ITEM g/y: OK | ok'], [], False),
        ('no detail', ['REGRESS-ITEM g/x: PASSED'], [], False),
        ('no group', ['REGRESS-ITEM x: PASSED | ok'], [], False),
        ('twice', ['REGRESS-ITEM g/x: PASSED | ok', 'REGRESS-ITEM g/x: PASSED | ok'], [], False),
        ('stage not ended', ['REGRESS-STAGE a START', 'REGRESS-ITEM g/x: PASSED | ok'], ['a'], False),
        ('stage not run', ['REGRESS-ITEM g/x: PASSED | ok'], ['a'], False),
        ('stage ended before it started', ['REGRESS-STAGE a END', 'REGRESS-STAGE a START', 'REGRESS-ITEM g/x: PASSED | ok'], ['a'], False),
        ('empty', [], [], False),
        ('other lines only', ['FAIL: something', 'REGRESS-DONE 3 items'], [], False),
    ]
    wrong = 0
    for name, lines, stages, want in cases:
        ok, _ = verdict(lines, stages)
        if ok != want:
            wrong += 1
            print('selftest case %r: verdict %s, %s expected' % (name, ok, want))
    print('regress_verdict selftest: %d cases, %d wrong' % (len(cases), wrong))
    return wrong == 0


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == '--selftest':
        sys.exit(0 if selftest() else 1)
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    try:
        lines = open(sys.argv[1], encoding='utf-8', errors='replace').readlines()
    except OSError as e:
        lines = []
        print('cannot read %s: %s' % (sys.argv[1], e))
    ok, out = verdict(lines, sys.argv[2:])
    print('\n'.join(out))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
