#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# hdr10plus_src.py <Kodi tree>: "Play HDR10+ videos as Dolby Vision" in the source (1.2.3), what policy_test.cpp cannot
# run: the setting has three values (Off, Where the display has no HDR10+, Always), with the label and help of the
# boolean of before; that boolean stays, hidden, for earlier builds; it is taken over when the settings load (the
# HadesTakeOvers() of Settings.cpp, with DVBRIDGE::Hdr10PlusModeFromOld/ToOld: true in guisettings.xml becomes "Where the
# display has no HDR10+", what it did); nothing but the take-over names the boolean; the decision reads the setting,
# keeps what sent a video as Dolby Vision to a display that takes HDR10+ and gives it to the player information,
# which has its sentence and what to do; the value has its Spanish in Estuary. Prints FAIL lines and HDR10PLUS-SRC
# PASSED or FAILED (n) last.
import os
import re
import sys
import xml.etree.ElementTree as ET

K = sys.argv[1]
fails = []


def need(cond, text):
    if not cond:
        fails.append(text)
        print('FAIL: ' + text)


def read(rel):
    return open(os.path.join(K, rel), encoding='utf-8').read()


def flat(text):
    return re.sub(r'\s+', '', text)


def body(text, signature):
    at = text.find(signature)
    if at < 0:
        return ''
    start = text.index('{', at)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return ''


# the setting and the boolean (system/settings/settings.xml)
root = ET.parse(os.path.join(K, 'system/settings/settings.xml')).getroot()
settings = {s.get('id'): s for s in root.iter('setting')}
mode = settings.get('videoscreen.hadesdvfromhdr10plusmode')
need(mode is not None and mode.get('type') == 'integer', 'the setting videoscreen.hadesdvfromhdr10plusmode, an integer')
if mode is not None:
    need((mode.findtext('default') or '').strip() == '0', 'its default is 0, off')
    opts = [(o.get('label'), (o.text or '').strip()) for o in mode.iter('option')]
    need(opts == [('351', '0'), ('62877', '1'), ('20422', '2')],
         'its values: Off 0, Where the display has no HDR10+ 1, Always 2 (found %s)' % opts)
    need(mode.get('label') == '60922' and mode.get('help') == '60923', 'its label and help are those of before')
old = settings.get('videoscreen.hadesdvfromhdr10plus')
need(old is not None and old.get('type') == 'boolean' and (old.findtext('visible') or '').strip() == 'false' and
     (old.findtext('default') or '').strip() == 'false',
     'the boolean of earlier builds kept, hidden, default false')

# the take-over when the settings load, and while Kodi runs
cpp = read('xbmc/settings/Settings.cpp')
need('takeOvers.push_back({CSettings::SETTING_VIDEOSCREEN_HADESDVFROMHDR10PLUSMODE,'
     'CSettings::SETTING_VIDEOSCREEN_HADESDVFROMHDR10PLUS,DVBRIDGE::Hdr10PlusModeFromOld,'
     'DVBRIDGE::Hdr10PlusModeToOld});' in flat(body(cpp, 'std::vector<HadesTakeOver> HadesTakeOvers()')),
     'HadesTakeOvers() takes the boolean over with Hdr10PlusModeFromOld/ToOld')
need('MigrateHadesSettings(*GetSettingsManager())' in body(cpp, 'bool CSettings::Load(const TiXmlElement* root, bool& updated)'),
     'CSettings::Load() runs the take-overs')
need('RegisterCallback(&g_hadesTakeOverSync,CHadesTakeOverSync::Modes())' in flat(cpp),
     'the boolean follows the setting while Kodi runs (CHadesTakeOverSync)')
need(re.search(r'SETTING_VIDEOSCREEN_HADESDVFROMHDR10PLUSMODE\s*=\s*"videoscreen\.hadesdvfromhdr10plusmode"',
               read('xbmc/settings/Settings.h')) is not None, 'the id of the setting')

# nothing but the take-over names the boolean
named = []
for top, dirs, files in os.walk(os.path.join(K, 'xbmc')):
    for f in files:
        if f.endswith(('.cpp', '.h')):
            p = os.path.join(top, f)
            t = open(p, encoding='utf-8', errors='replace').read()
            if re.search(r'SETTING_VIDEOSCREEN_HADESDVFROMHDR10PLUS\b', t) or '"videoscreen.hadesdvfromhdr10plus"' in t:
                named.append(os.path.relpath(p, K))
need(sorted(named) == ['xbmc/settings/Settings.cpp', 'xbmc/settings/Settings.h'],
     'the boolean is named only by the take-over (found %s)' % sorted(named))

# the decision reads the setting and keeps both answers; the player information gets them
g = read('xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp')
dec = flat(body(g, 'bool DVBRIDGE::Hdr10PlusAsDolbyVision()'))
need('Hdr10PlusModeOf(settings->GetInt(CSettings::SETTING_VIDEOSCREEN_HADESDVFROMHDR10PLUSMODE))' in dec,
     'Hdr10PlusAsDolbyVision() reads the setting of three values')
need('Hdr10PlusAsDolbyVisionWhy(mode,choice,native,takesHdr10Plus)' in dec and
     'Hdr10PlusAsDolbyVisionOver(mode,choice,native,takesHdr10Plus)' in dec,
     'it asks why not, and what sent it over HDR10+, on the same facts')
need('g_dvHdr10PlusWhy=why;' in dec and 'g_dvHdr10PlusOver=over;' in dec, 'it keeps both for the player information')
need('over=="always"' in dec and 'over=="choice"' in dec, 'kodi.log says what sent it over HDR10+')
need('returng_dvHdr10PlusOver;' in flat(body(g, 'std::string DVBRIDGE::Hdr10PlusAsDolbyVisionOverNow()')),
     'Hdr10PlusAsDolbyVisionOverNow() gives it')
need('g_dvHdr10PlusOver.clear();' in g, 'a new video starts without it')
need('input.hdr10PlusOver=DVBRIDGE::Hdr10PlusAsDolbyVisionOverNow();' in flat(g),
     'the player information is given it')

# the page: the sentence and what to do (Estuary; system/hades is made from it, make-playerinfo.py --check)
v = read('addons/skin.estuary/xml/Variables.xml')
need('Property(hades.sum.code),hdr10plus_as_dv)">$ADDON[skin.estuary 54969]' in v, 'the sentence of hdr10plus_as_dv')
need('Property(hades.sum.todo),hdr10plusmode)">$ADDON[skin.estuary 54970]' in v, 'what to do: hdr10plusmode')

# the value in Spanish (the settings show core strings, translated in Estuary)
need('if(labelId==62877)returnCGUIUtils::GetHadesString(62877,54968);' in
     flat(read('xbmc/settings/dialogs/GUIDialogSettingsBase.cpp')), 'the value 62877 shown from Estuary\'s 54968')

if fails:
    print('FAILED (%d)' % len(fails))
    sys.exit(1)
print('HDR10PLUS-SRC PASSED')
