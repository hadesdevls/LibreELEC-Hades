#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""gui_smoke_verdict.py <out dir> : the verdict of gui_smoke.sh, counted from the files the VM run left.

Reads <out>/smoke.out (the SMOKE-ITEM / SMOKE-SHOT / SMOKE-DONE lines of smoke.py), the screenshots <out>/<name>.png
and <out>/kodi.log. Prints one GUISMOKE-ITEM line per check and
  GUISMOKE-DONE <p> passed, <f> failed, <r> not run
  GUISMOKE-VERDICT PASSED|FAILED
Exit 0 only if every expected item exists and passed and the run ended with SMOKE-DONE. Nothing passes by default:
an expected item with no line is NOT-RUN, and NOT-RUN fails the verdict."""
import os, re, sys

out = sys.argv[1]
EXPECTED = ['window-home', 'window-settings', 'window-systemsettings', 'playback', 'info-page', 'playback-stop', 'power-menu']
SHOTS = ['window_home', 'window_settings', 'window_systemsettings', 'playback', 'info_page', 'power_menu']
items = []  # (name, verdict, detail)


def add(n, v, d):
    items.append((n, v, d))
    print('GUISMOKE-ITEM %s: %s | %s' % (n, v, d))


try:
    lines = open(os.path.join(out, 'smoke.out'), encoding='utf-8', errors='replace').read().split('\n')
except OSError:
    lines = []
got = {}
size = {}  # the sizes of the screen just before and after each shot (Kodi's System.ScreenWidth/Height)
done = False
for l in lines:
    m = re.match(r'SMOKE-ITEM ([^:]+): (PASSED|FAILED) \| (.*)$', l)
    if m:
        got[m.group(1)] = (m.group(2), m.group(3))
    m = re.match(r'SMOKE-SHOT ([a-z_0-9]+)((?: [0-9]+x[0-9]+)+)\s*$', l)
    if m:
        size[m.group(1)] = [tuple(int(v) for v in wh.split('x')) for wh in m.group(2).split()]
    if l.strip() == 'SMOKE-DONE':
        done = True
for n in EXPECTED:
    if n in got:
        add(n, got[n][0], got[n][1])
    else:
        add(n, 'NOT-RUN', 'no line in smoke.out')
for n, (v, d) in got.items():
    if n not in EXPECTED:
        add(n, v, d)
add('script-end', 'PASSED' if done else 'FAILED', 'SMOKE-DONE line %s' % ('present' if done else 'missing: the script stopped early'))

# the screenshots: not blank (a black or one-colour frame is a GUI that did not draw)
try:
    from PIL import Image
except ImportError:
    Image = None
for s in SHOTS:
    p = os.path.join(out, s + '.png')
    if not os.path.exists(p):
        add('shot-' + s, 'NOT-RUN', 'no file')
        continue
    if Image is None:
        add('shot-' + s, 'NOT-RUN', 'PIL missing')
        continue
    try:
        im = Image.open(p).convert('RGB')
    except Exception as e:
        add('shot-' + s, 'FAILED', 'unreadable image: %r' % (e,))
        continue
    small = im.resize((192, 108))
    px = list(small.tobytes()[i:i + 3] for i in range(0, 192 * 108 * 3, 3))
    cols = len(set(px))
    mean = sum(sum(c) for c in px) / (3.0 * len(px))
    # the shot has the size of the screen at that moment: 1920x1080 for the GUI; during the playback Kodi may have
    # changed the mode (Adjust display refresh rate: in QEMU 1920x1440 at 60.000 Hz, a 3:2 pulldown of 24 fps). The
    # screen must have had that size before and after the shot (a mode change during the shot leaves it unknown).
    want = size.get(s, [(1920, 1080)])
    ok = all(im.size == x for x in want) and want[0][0] >= 640 and want[0][1] >= 480 and cols >= 40 and mean > 8
    add('shot-' + s, 'PASSED' if ok else 'FAILED', '%dx%d (the screen %s), %d colours at 192x108, mean level %.1f'
        % (im.size[0], im.size[1], ' then '.join('%dx%d' % x for x in want), cols, mean))

# kodi.log (summarised in the guest by gui_smoke.sh: crash lines, ERROR lines, total lines, the first crash lines)
try:
    ks = [l.strip() for l in open(os.path.join(out, 'kodi-summary.txt'), encoding='utf-8', errors='replace') if l.strip()]
except OSError:
    ks = []
if len(ks) < 3 or not all(x.isdigit() for x in ks[:3]):
    add('kodi-log-no-crash', 'NOT-RUN', 'no usable summary of kodi.log')
else:
    crash, errs, total = int(ks[0]), int(ks[1]), int(ks[2])
    add('kodi-log-no-crash', 'PASSED' if crash == 0 and total > 100 else 'FAILED', '%d lines; %d crash/abort lines%s; %d ERROR lines (informative)' % (total, crash, (': ' + ks[3][:120]) if len(ks) > 3 else '', errs))

p = sum(1 for i in items if i[1] == 'PASSED')
f = sum(1 for i in items if i[1] == 'FAILED')
r = sum(1 for i in items if i[1] == 'NOT-RUN')
print('GUISMOKE-DONE %d passed, %d failed, %d not run' % (p, f, r))
v = f == 0 and r == 0
print('GUISMOKE-VERDICT %s' % ('PASSED' if v else 'FAILED'))
sys.exit(0 if v else 1)
