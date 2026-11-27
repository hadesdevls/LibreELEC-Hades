#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# mvhevc_base_check.py <reference.txt> <run.txt>: for the stage base of run-mvhevc.sh (ffmpeg-9976), after a fault of
# the separate base-layer decoder: every picture of the run that is not marked corrupt is the reference's picture of
# the same presentation time and view (mvhevc_frames.c lines; an input with timestamps), and the run ended.
# Prints "ok <pictures> <corrupt> <missing>" or what is wrong; exit 1 if wrong.
import sys


def pictures(path):
    out, ended = {}, False
    for line in open(path):
        f = line.split()
        if f and f[0] == 'frame':
            out[(f[5], f[3])] = (f[11], len(f) > 12 and f[12] == 'corrupt')
        elif line.strip() == 'eof':
            ended = True
    return out, ended


ref, _ = pictures(sys.argv[1])
run, ended = pictures(sys.argv[2])
bad = []
if not ended:
    bad.append('did not end')
corrupt = 0
for key, (md5, is_corrupt) in run.items():
    if is_corrupt:
        corrupt += 1
        continue
    if key not in ref:
        bad.append('pts %s view %s is not in the reference' % key)
    elif ref[key][0] != md5:
        bad.append('pts %s view %s differs and is not marked corrupt' % key)
if bad:
    print('; '.join(bad[:5]) + (' (+%d)' % (len(bad) - 5) if len(bad) > 5 else ''))
    sys.exit(1)
print('ok %d %d %d' % (len(run), corrupt, len(set(ref) - set(run))))
