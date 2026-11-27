# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# fp_edid_mutants.py: the mutants of hades-fp-edid-test.py, each of which fp_edid_test.py must fail: each mutant is a
# copy of the tool with one rule changed, in a folder of its own; prints a line for each and ALL CAUGHT last.
# usage: fp_edid_mutants.py <work folder> <tool> <fp_edid_test.py> <its arguments after the tool>...
import os
import subprocess
import sys

WORK, TOOL, TEST, ARGS = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
MUTANTS = [
    ('checksum-not-made-again', '            nb[127] = checksum(nb)\n', ''),
    ('another-bit', '        p[at] |= 0x80\n', '        p[at] |= 0x40\n'),
    ('timings-offset-not-moved', '            nb[2] = d + grow\n', '            nb[2] = d\n'),
    ('off-names-a-connector', "OFF = 'hades-fp-test-off:none'", "OFF = 'HDMI-A-1:none'"),
    ('no-room-not-asked', '            if grow and any(nb[127 - grow:127]) or 127 - dtds < grow:\n',
     '            if False:\n'),
]
text = open(TOOL, encoding='utf-8').read()
missed = 0
os.makedirs(WORK, exist_ok=True)
for name, old, new in MUTANTS:
    if text.count(old) != 1:
        print('MUTANT %s: NO LONGER APPLIES' % name)
        missed += 1
        continue
    path = os.path.join(WORK, name + '.py')
    open(path, 'w', encoding='utf-8').write(text.replace(old, new))
    r = subprocess.run([sys.executable, '-I', '-B', TEST, path] + ARGS, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode('utf-8', 'replace')
    fails = sum(1 for line in out.splitlines() if line.startswith('FAIL'))
    if r.returncode != 0:
        print('MUTANT %s: caught (exit %d, %d checks failed)' % (name, r.returncode, fails))
    else:
        print('MUTANT %s: NOT CAUGHT' % name)
        missed += 1
    os.remove(path)
print('ALL CAUGHT' if not missed else '%d NOT CAUGHT' % missed)
sys.exit(1 if missed else 0)
