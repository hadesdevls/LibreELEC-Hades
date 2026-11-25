#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""check_verdict_test.py <resources/lib of the LibreELEC settings add-on> <Kodi tree>: the sentence of the player
information as Check this setup says it (hades_check.picture_lines(), with stand-ins for Kodi): every code of the
sentence that Estuary's Variables.xml has is in the table SUM of hades_check.py with the same string of Estuary, and
the frames that Kodi lost in steady playback (frames_dropped, frames_skipped: their number first; frames_gpu: the
percent after; frames_buffering) are said, never taken for "as intended". Prints ok:/FAIL: lines and ALL PASSED last."""
import importlib
import re
import sys
import types

LIB, KODI = sys.argv[1], sys.argv[2]
fails = 0


def check(ok, text):
    global fails
    print(('ok: ' if ok else 'FAIL: ') + text)
    fails += not ok


# stand-ins for Kodi's modules, as the other tests of the add-on have them
for name in ('xbmc', 'xbmcgui', 'xbmcaddon', 'xbmcvfs'):
    m = types.ModuleType(name)
    m.getLocalizedString = lambda i: '#%d' % i
    m.log = lambda *a, **k: None
    m.LOGINFO = m.LOGWARNING = m.LOGERROR = m.LOGDEBUG = 0
    m.translatePath = lambda p: p
    m.executeJSONRPC = lambda q: '{"result": {}}'
    m.getInfoLabel = lambda l: ''
    m.getCondVisibility = lambda c: False

    class Addon:
        def __init__(self, *a, **k):
            pass

        def getLocalizedString(self, i):
            return '#%d' % i

        def getAddonInfo(self, k):
            return ''

    m.Addon = Addon
    sys.modules[name] = m
sys.path.insert(0, LIB)
hc = importlib.import_module('hades_check')

variables = open(KODI + '/addons/skin.estuary/xml/Variables.xml', encoding='utf-8').read()
pairs = re.findall(r'Property\(hades\.sum\.code\),(\w+)\)">[^<]*?\$ADDON\[skin\.estuary (\d+)\]', variables)
check(len(pairs) >= 10, f'the codes of the sentence in Variables.xml ({len(pairs)})')
for code, sid in pairs:
    check(hc.SUM.get(code) == int(sid), f'{code}: SUM has string {hc.SUM.get(code)}, Variables.xml {sid}')


def said(code, arg):
    info = {'hades.sum.head': 'hdr10', 'hades.sum.code': code, 'hades.sum.arg': arg, 'hades.sum.todo': ''}
    lines = hc.picture_lines('hdr10', info, None)
    return [part for line in lines for part in line]


for code in ('frames_dropped', 'frames_skipped'):
    s = said(code, '12')
    check(('t', '12 ') in s and ('e', hc.SUM.get(code, -1)) in s and ('e', hc.SUM['ok']) not in s,
          f'{code}: the number, then its sentence, never "as intended" ({s})')
s = said('frames_gpu', '97')
check(('e', hc.SUM.get('frames_gpu', -1)) in s and ('t', ' 97 %.') in s and ('e', hc.SUM['ok']) not in s,
      f'frames_gpu: its sentence, then the percent ({s})')
s = said('frames_buffering', '')
check(('e', hc.SUM.get('frames_buffering', -1)) in s and ('e', hc.SUM['ok']) not in s, f'frames_buffering: its sentence ({s})')
s = said('', '')
check(('e', hc.SUM['ok']) in s, 'nothing to say: "as intended", as before')
print('ALL PASSED' if not fails else f'FAILED ({fails})')
sys.exit(1 if fails else 0)
