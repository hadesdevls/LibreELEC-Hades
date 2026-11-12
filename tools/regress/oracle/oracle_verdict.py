#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""The verdict of a run of run-oracle.sh, read line by line from its log.

usage: oracle_verdict.py <log> [<stage> ...]
       oracle_verdict.py --selftest

Every line that starts with "ORACLE-ITEM " must read "ORACLE-ITEM <name>: PASSED|FAILED|NOT-RUN | <detail>";
a line that starts so and does not is counted as failed (malformed), and so is a name given twice. Every
stage named on the command line must have an "ORACLE-STAGE <stage> START" and a later
"ORACLE-STAGE <stage> END" line; a stage that started and did not end, or did not start, is a failed
item. Prints "ORACLE-DONE n items, p passed, f failed, r not run" and "ORACLE-VERDICT PASSED" only when
n > 0, f = 0 and r = 0 (else "ORACLE-VERDICT FAILED"), and exits 0 only then.
"""
import re
import sys

ITEM = re.compile(r'^ORACLE-ITEM (\S+): (PASSED|FAILED|NOT-RUN) \| (.*)$')
STAGE = re.compile(r'^ORACLE-STAGE (\S+) (START|END)$')


def verdict(lines, stages):
    counts = {'PASSED': 0, 'FAILED': 0, 'NOT-RUN': 0}
    seen = set()
    notes = []
    started, ended = {}, {}
    for i, line in enumerate(lines, 1):
        line = line.rstrip('\n')
        if line.startswith('ORACLE-ITEM'):
            m = ITEM.match(line)
            if not m:
                counts['FAILED'] += 1
                notes.append('line %d: malformed item' % i)
                continue
            name, state = m.group(1), m.group(2)
            if name in seen:
                counts['FAILED'] += 1
                notes.append('line %d: item %s given twice' % (i, name))
                continue
            seen.add(name)
            counts[state] += 1
            if state != 'PASSED':
                notes.append('line %d: %s %s' % (i, name, state))
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
    out = notes[:40] + ['ORACLE-DONE %d items, %d passed, %d failed, %d not run'
                        % (n, counts['PASSED'], counts['FAILED'], counts['NOT-RUN']),
                        'ORACLE-VERDICT %s' % ('PASSED' if ok else 'FAILED')]
    return ok, out


def selftest():
    """The verdict in both directions, on logs whose answer is known."""
    good = ['ORACLE-STAGE a START', 'ORACLE-ITEM x: PASSED | ok', 'noise ORACLE-ITEM y: FAILED | not at the start',
            'ORACLE-ITEM y: PASSED | ok', 'ORACLE-STAGE a END']
    cases = [
        ('all passed', good, ['a'], True, (2, 2, 0, 0)),
        ('one failed', good + ['ORACLE-ITEM z: FAILED | x'], ['a'], False, (3, 2, 1, 0)),
        ('one not run', good + ['ORACLE-ITEM z: NOT-RUN | x'], ['a'], False, (3, 2, 0, 1)),
        ('malformed', good + ['ORACLE-ITEM z: OK | x'], ['a'], False, (3, 2, 1, 0)),
        ('lower case', good + ['ORACLE-ITEM z: passed | x'], ['a'], False, (3, 2, 1, 0)),
        ('no detail bar', good + ['ORACLE-ITEM z: PASSED'], ['a'], False, (3, 2, 1, 0)),
        ('twice', good + ['ORACLE-ITEM x: PASSED | again'], ['a'], False, (3, 2, 1, 0)),
        ('stage not ended', good[:-1], ['a'], False, (3, 2, 1, 0)),
        ('stage missing', good, ['a', 'b'], False, (3, 2, 1, 0)),
        ('empty', [], [], False, (0, 0, 0, 0)),
    ]
    fails = 0
    for name, lines, stages, want_ok, want in cases:
        ok, out = verdict(lines, stages)
        m = re.match(r'ORACLE-DONE (\d+) items, (\d+) passed, (\d+) failed, (\d+) not run', out[-2])
        got = tuple(int(x) for x in m.groups())
        good_case = ok == want_ok and got == want and out[-1] == 'ORACLE-VERDICT %s' % ('PASSED' if want_ok else 'FAILED')
        print('VERDICT-SELFTEST %s: %s (verdict %s, counts %s, expected %s %s)' % (
            name, 'ok' if good_case else 'WRONG', ok, got, want_ok, want))
        fails += not good_case
    print('VERDICT-SELFTEST %d cases, %d wrong' % (len(cases), fails))
    return fails


def main():
    if sys.argv[1:2] == ['--selftest']:
        return 1 if selftest() else 0
    lines = open(sys.argv[1], errors='replace').readlines()
    ok, out = verdict(lines, sys.argv[2:])
    print('\n'.join(out))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
