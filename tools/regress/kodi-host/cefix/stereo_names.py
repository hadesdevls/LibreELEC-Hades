#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""stereo_names.py <kodi tree>: the default patterns of the stereoscopic mode of a file name (AdvancedSettings.cpp, as
CStereoscopicsManager::DetectStereoModeByString() uses them: 3D first, then SBS, then TAB, without case) on names of
3D files: full (F) and half (H) side by side and top and bottom, and names that are not 3D. One line per name, "ok:" or
"FAIL:", then STEREO-NAMES PASSED or STEREO-NAMES FAILED."""
import re
import sys

text = open(sys.argv[1] + '/xbmc/settings/AdvancedSettings.cpp', encoding='utf-8').read()
pat = {}
for k in ('3d', 'sbs', 'tab'):
    m = re.search(r'm_stereoscopicregex_%s = "([^"]*)";' % k, text)
    if not m:
        print('FAIL: no default pattern m_stereoscopicregex_%s' % k)
        print('STEREO-NAMES FAILED')
        sys.exit(1)
    pat[k] = re.compile(m.group(1), re.I)


def mode(name):
    if not pat['3d'].search(name):
        return ''
    if pat['sbs'].search(name):
        return 'left_right'
    if pat['tab'].search(name):
        return 'top_bottom'
    return ''


CASES = [
    ('Avatar.2009.3D.FSBS.1080p.mkv', 'left_right'),
    ('Avatar.2009.3D.fsbs.1080p.mkv', 'left_right'),
    ('Avatar 2009 3D FSBS 1080p.mkv', 'left_right'),
    ('Avatar.2009.3D.FTAB.1080p.mkv', 'top_bottom'),
    ('Avatar-2009-3D-FTAB-1080p.mkv', 'top_bottom'),
    ('Avatar.2009.3D.HSBS.1080p.mkv', 'left_right'),
    ('Avatar.2009.3D.HTAB.1080p.mkv', 'top_bottom'),
    ('Avatar.2009.3D.SBS.1080p.mkv', 'left_right'),
    ('Avatar.2009.3D.TAB.1080p.mkv', 'top_bottom'),
    ('Avatar.2009.1080p.mkv', ''),
    ('Avatar.2009.FSBS.1080p.mkv', ''),          # no 3D in the name
    ('Avatar.2009.3D.XSBS.1080p.mkv', ''),        # no full nor half
    ('Avatar.2009.3D.1080p.mkv', ''),             # 3D without a layout
    ('Table.Tabs.3D.Reel.mkv', ''),               # "tab" inside a word
]
fails = 0
for name, want in CASES:
    got = mode(name)
    if got == want:
        print('ok: %s: %s' % (name, got or 'not 3D by its name'))
    else:
        fails += 1
        print('FAIL: %s: %s, %s expected' % (name, got or 'not 3D by its name', want or 'not 3D by its name'))
print('STEREO-NAMES PASSED' if fails == 0 else 'STEREO-NAMES FAILED')
sys.exit(1 if fails else 0)
