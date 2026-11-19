#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades (1.0 RC2): the strings of the settings of the projector (the mapping of Dolby Vision to the display and the
# choice of output of a video) in the core and in Estuary: every core string 59820-59826 and 59830-59847 has its
# Estuary string (51762-51768, 51770-51787) with the same msgid in en_gb, es_es, es_ar and es_mx, translated in the
# three Spanish files ("video" without an accent in es_ar and es_mx, "vídeo" in es_es), and the Estuary strings
# 51760 and 51761 are translated.
# usage: projector_strings.py <kodi tree>
import pathlib
import re
import sys

tree = pathlib.Path(sys.argv[1])
core = (tree / 'addons/resource.language.en_gb/resources/strings.po').read_text(encoding='utf-8')
langs = {l: (tree / f'addons/skin.estuary/language/resource.language.{l}/strings.po').read_text(encoding='utf-8')
         for l in ('en_gb', 'es_es', 'es_ar', 'es_mx')}


def entry(t, i):
    m = re.search(r'msgctxt "#%d"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"' % i, t)
    return m.groups() if m else None


bad = 0
pairs = [(c, 51762 + c - 59820) for c in range(59820, 59827)] + [(c, 51770 + c - 59830) for c in range(59830, 59848)]
for c, e in pairs:
    ce = entry(core, c)
    for l, t in langs.items():
        ee = entry(t, e)
        if not ce or not ee or ee[0] != ce[0] or (l != 'en_gb' and not ee[1]):
            bad += 1
            print('FAIL: missing or different: core %d, Estuary %d, %s' % (c, e, l))
        if ee and l in ('es_ar', 'es_mx') and 'vídeo' in ee[1]:
            bad += 1
            print('FAIL: vídeo in %s %d' % (l, e))
        if ee and l == 'es_es' and re.search(r'\bvideo', ee[1]):
            bad += 1
            print('FAIL: video in es_es %d' % e)
for e in (51760, 51761):
    for l in ('es_es', 'es_ar', 'es_mx'):
        ee = entry(langs[l], e)
        if not ee or not ee[1]:
            bad += 1
            print('FAIL: missing %d %s' % (e, l))
print('pairs checked: %d, problems: %d' % (len(pairs) * 4 + 6, bad))
print('PROJECTOR-STRINGS %s' % ('FAILED' if bad else 'PASSED'))
sys.exit(1 if bad else 0)
