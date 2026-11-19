# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# usage: validate_hades_dialog.py <Kodi tree>: checks of the generated pages of Hades for other skins (system/hades)
import os, re, sys
import xml.etree.ElementTree as ET

if len(sys.argv) < 2:
    sys.exit('usage: validate_hades_dialog.py <Kodi tree>')
ROOT = sys.argv[1]
if not ROOT.endswith('/'):
    ROOT += '/'
H = ROOT + 'system/hades/'
fails = []
for f in ('DialogHadesPlayerInfo.xml', 'HadesIncludes.xml'):
    try:
        ET.parse(H + f)
    except Exception as e:
        fails.append('%s is not well-formed: %s' % (f, e))
if fails:
    print('\n'.join(fails)); sys.exit(1)
dialog = open(H + 'DialogHadesPlayerInfo.xml', encoding='utf-8').read()
incl = open(H + 'HadesIncludes.xml', encoding='utf-8').read()
estuary = set(int(x) for x in re.findall(r'msgctxt "#(\d+)"', open(ROOT + 'addons/skin.estuary/language/resource.language.en_gb/strings.po', encoding='utf-8').read()))
core = set(int(x) for x in re.findall(r'msgctxt "#(\d+)"', open(ROOT + 'addons/resource.language.en_gb/resources/strings.po', encoding='utf-8').read()))
text = dialog + incl
for n in sorted(set(int(x) for x in re.findall(r'\$ADDON\[skin\.estuary (\d+)\]', text))):
    if n not in estuary:
        fails.append('$ADDON[skin.estuary %d] is not an Estuary string' % n)
for n in sorted(set(int(x) for x in re.findall(r'\$LOCALIZE\[(\d+)\]', text))):
    if n in estuary and n not in core:
        fails.append('$LOCALIZE[%d] is an Estuary string that a skin without it cannot read' % n)
    if n not in estuary and n not in core:
        fails.append('$LOCALIZE[%d] is nobody\'s' % n)
defined = set(re.findall(r'<variable name="(\w+)"', incl))
for n in sorted(set(re.findall(r'\$(?:ESC)?VAR\[\s*(\w+)', text))):
    if n not in defined:
        fails.append('$VAR[%s] is not defined in HadesIncludes.xml' % n)
maps = set(re.findall(r'<map name="(\w+)"', incl))
for n in sorted(set(re.findall(r'\$MAP\[\s*(\w+)', text))):
    if n not in maps:
        fails.append('$MAP[%s] is not defined in HadesIncludes.xml' % n)
for t in sorted(set(re.findall(r'special://xbmc/system/hades/([\w.\-]+)', text))):
    if not os.path.isfile(H + t):
        fails.append('texture %s is not in system/hades' % t)
for t in os.listdir(H):
    if t.endswith('.png') and 'special://xbmc/system/hades/' + t not in text:
        fails.append('%s is in system/hades and nothing uses it' % t)
for t in re.findall(r'<texture[^>]*>([^<$]*)</texture', dialog):
    if t.strip() and not t.startswith('special://'):
        fails.append('texture %r is not from system/hades' % t)
if 'PlayerInfoDialogFocus' in text:
    fails.append('the page property of Estuary is still in the generated files')
if '$INCLUDE' in text or '<include content=' in dialog:
    fails.append('an include is left unexpanded in the dialog')
# the tab of live TV and its page are not here; the five tabs are
for tid in ('5556', '5557', '5555', '5558', '5559'):
    if 'id="%s"' % tid not in dialog:
        fails.append('tab %s is missing' % tid)
if 'id="5551"' in dialog:
    fails.append('the tab of live TV is in the dialog of Hades')
if 'PVR.ActStream' in dialog:
    fails.append('the page of live TV is in the dialog of Hades')
# the output of this video: PlayerControl(HadesDVOutput) runs from a button that the overview (page 5556) and the Dolby
# Vision page (5555) both show (a button on each page, or one that no page condition hides), only where a choice is
# there; OK on a tab may run it too, only on those two tabs and only where a choice is there; nothing else runs it
ACTION, CHOICE = 'PlayerControl(HadesDVOutput)', '!String.IsEmpty(Player.Process(videodvoutputchoice))'
droot = ET.fromstring(dialog)
dparent = {c: q for q in droot.iter() for c in q}
PAGE = r'(?:HadesPlayerInfoPage|PlayerInfoDialogFocus)\),(\d+)\)'


def visibles(c):
    # the conditions of a control and of the groups it is in
    vis, q = [], c
    while q is not None:
        if q.tag == 'control':
            vis += [(v.text or '').strip() for v in q.findall('visible')]
        q = dparent.get(q)
    return vis


def shown_pages(vis):
    # the pages a control is shown on: those its conditions name, without those named inside a negation ("![page N +
    # ...]": the button of the output of this video gives way there to the upscaler's on the Video page); none named,
    # any page
    return set(re.findall(PAGE, re.sub(r'!\[[^\]]*\]', '', ' '.join(vis))))


shown_on, on_tabs, runs, bad = set(), [], 0, []
for c in droot.iter('control'):
    for o in c.findall('onclick'):
        if (o.text or '').strip() != ACTION:
            continue
        runs += 1
        if c.get('type') == 'togglebutton' and o.get('condition') == CHOICE:
            on_tabs.append(c.get('id'))
            continue
        vis = visibles(c)
        if c.get('type') != 'button' or o.get('condition') is not None or \
                not any(v == CHOICE or v.startswith(CHOICE + ' + ') for v in vis):
            bad.append('control %s (%s)' % (c.get('id'), c.get('type')))
            continue
        shown_on |= shown_pages(vis) or {'any'}
if bad or not ('any' in shown_on or {'5555', '5556'} <= shown_on) or sorted(on_tabs) not in ([], ['5555', '5556']):
    fails.append('the output of this video is not reachable as it must be (buttons shown on %s, OK on tabs %s, '
                 'other controls that run it: %s)' % (sorted(shown_on), sorted(on_tabs), ', '.join(bad) or 'none'))
# the upscaler of the videos of this kind: PlayerControl(HadesUpscaler) runs from a button of the Video page (5557) shown
# where the property says which upscaler, and from OK on that tab; nothing else runs it
UACTION, UCHOICE = 'PlayerControl(HadesUpscaler)', '!String.IsEmpty(Window(Home).Property(hades.video.upscaler))'
ushown, utabs, ubad = set(), [], []
for c in droot.iter('control'):
    for o in c.findall('onclick'):
        if (o.text or '').strip() != UACTION:
            continue
        if c.get('type') == 'togglebutton' and o.get('condition') == UCHOICE:
            utabs.append(c.get('id'))
            continue
        vis = visibles(c)
        if c.get('type') != 'button' or o.get('condition') is not None or not any(UCHOICE in v for v in vis):
            ubad.append('control %s (%s)' % (c.get('id'), c.get('type')))
            continue
        ushown |= shown_pages(vis) or {'any'}
if ubad or ushown != {'5557'} or utabs != ['5557']:
    fails.append('the upscaler of the videos of this kind is not reachable as it must be (buttons shown on %s, OK on tabs '
                 '%s, other controls that run it: %s)' % (sorted(ushown), sorted(utabs), ', '.join(ubad) or 'none'))
print('FAILED %d\n  ' % len(fails) + '\n  '.join(fails) if fails else 'generated pages: all checks passed (%d variables, %d maps)' % (len(defined), len(maps)))
sys.exit(1 if fails else 0)
