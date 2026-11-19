# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# usage: validate_skin.py <Kodi tree> [<labels.txt> | --labels <file to write>]
# Checks of the skin files of the player information. Every check has its other direction in
# validate_skin_mutants.py (a broken copy must fail).
import os, re, sys
import xml.etree.ElementTree as ET

if len(sys.argv) < 2:
    sys.exit('usage: validate_skin.py <Kodi tree>')
ROOT = sys.argv[1]
if not ROOT.endswith('/'):
    ROOT += '/'
SKIN = ROOT + 'addons/skin.estuary/'
fails = []
notes = []


def fail(msg):
    fails.append(msg)


def parse(path):
    try:
        return ET.parse(path).getroot()
    except Exception as e:
        fail('not well-formed: %s: %s' % (path, e))
        return None


def po_ids(path):
    ids = {}
    cur = None
    for line in open(path, encoding='utf-8'):
        m = re.match(r'msgctxt "#(\d+)"', line)
        if m:
            cur = int(m.group(1))
        m = re.match(r'msgid "(.*)"', line)
        if m and cur is not None:
            ids[cur] = [m.group(1), None]
        m = re.match(r'msgstr "(.*)"', line)
        if m and cur is not None and cur in ids:
            ids[cur][1] = m.group(1)
            cur = None
    return ids


dialog = parse(SKIN + 'xml/DialogPlayerProcessInfo.xml')
includes = parse(SKIN + 'xml/Includes_HadesPlayerInfo.xml')
variables = parse(SKIN + 'xml/Variables.xml')
maps = parse(SKIN + 'xml/Includes_Maps.xml')
if None in (dialog, includes, variables):
    print('\n'.join(fails))
    sys.exit(1)
for lang in ('en_gb', 'es_es', 'es_ar', 'es_mx'):
    pass

# ---- text of the files of the player information
def alltext(e):
    return ' '.join([x.text or '' for x in e.iter()] + [v for x in e.iter() for v in x.attrib.values()])


own_vars = {}
for v in variables.iter('variable'):
    own_vars[v.get('name')] = v
all_var_names = set(own_vars)
texts = alltext(dialog) + ' ' + alltext(includes)
for name in [n for n in own_vars if n.startswith('Hades')]:
    texts += ' ' + alltext(own_vars[name])

# 1. every string id resolves (Estuary's, or Kodi's own)
estuary = po_ids(SKIN + 'language/resource.language.en_gb/strings.po')
core = po_ids(ROOT + 'addons/resource.language.en_gb/resources/strings.po')
used = set(int(n) for n in re.findall(r'\$LOCALIZE\[(\d+)\]', texts))
# Kodi reads $LOCALIZE[n] as a string of the skin only for 31000 to 31999 (ADDON::IsSkinStringId): above that it is a
# string of Kodi itself, and an id of Estuary's there comes out blank (or as someone else's text). Estuary's own
# strings from 32000 are read as $ADDON[skin.estuary n].
used |= set(int(n) for n in re.findall(r'\$ADDON\[skin\.estuary (\d+)\]', texts))
for n in sorted(set(int(n) for n in re.findall(r'\$LOCALIZE\[(\d+)\]', texts))):
    if n in estuary and not 31000 <= n <= 31999:
        fail('$LOCALIZE[%d] is a string of Estuary outside 31000-31999: Kodi does not read it so; use $ADDON[skin.estuary %d]' % (n, n))
for n in sorted(set(int(n) for n in re.findall(r'\$ADDON\[skin\.estuary (\d+)\]', texts))):
    if n not in estuary:
        fail('$ADDON[skin.estuary %d] is not a string of Estuary' % n)
for n in sorted(used):
    if n not in estuary and n not in core:
        fail('$LOCALIZE[%d] is in neither Estuary\'s strings nor Kodi\'s' % n)
# the ids of the player information: in the four languages, translated in the three Spanish ones, English as the id
mine = sorted(n for n in estuary if 52700 <= n <= 52899)
for lang in ('es_es', 'es_ar', 'es_mx'):
    es = po_ids(SKIN + 'language/resource.language.%s/strings.po' % lang)
    for n in mine:
        if n not in es:
            fail('%s lacks #%d' % (lang, n))
        elif not es[n][1]:
            fail('%s: #%d has no translation' % (lang, n))
        elif es[n][0] != estuary[n][0]:
            fail('%s: #%d has another msgid than en_gb' % (lang, n))
for n in mine:
    if n not in used:
        fail('#%d is written and used by no skin file' % n)
    if estuary[n][1] != '':
        fail('en_gb #%d has a msgstr (English is the msgid)' % n)
for lang in ('es_ar', 'es_mx'):
    es = po_ids(SKIN + 'language/resource.language.%s/strings.po' % lang)
    for n in mine:
        if n in es and 'vídeo' in (es[n][1] or ''):
            fail('%s #%d writes vídeo' % (lang, n))

# 2. every $VAR resolves
for name in sorted(set(re.findall(r'\$(?:ESC)?VAR\[\s*([A-Za-z0-9_]+)', texts + ' ' + ' '.join(alltext(own_vars[n]) for n in own_vars if n.startswith('Hades'))))):
    if name not in all_var_names:
        fail('$VAR[%s] is not a variable of Estuary' % name)
# maps used
map_names = set(m.get('name') for m in maps.iter('map')) if maps is not None else set()
for name in sorted(set(re.findall(r'\$MAP\[\s*([A-Za-z0-9_]+)', texts))):
    if name not in map_names:
        fail('$MAP[%s] is not a map of Estuary' % name)

# 3. includes: every call names a definition, and gives what it needs
defs = {i.get('name'): i for i in includes.iter('include') if i.get('name')}
for c in dialog.iter('include'):
    name = c.get('content')
    if name is None:
        continue
    if name not in defs:
        fail('include %s is not defined' % name)
        continue
    d = defs[name]
    given = {p.get('name') for p in c.findall('param')}
    declared = {p.get('name') for p in d.findall('param')}
    for g in given:
        if g not in declared:
            fail('include %s: parameter %s is not one of its own' % (name, g))
    for p in d.findall('param'):
        pass
    body = alltext(d.find('definition'))
    for u in set(re.findall(r'\$PARAM\[(\w+)\]', body)):
        if u not in declared:
            fail('include %s uses $PARAM[%s] that it does not declare' % (name, u))
# a "flash" parameter names a key that the window system publishes and lights
cpp = open(ROOT + 'xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp', encoding='utf-8').read()
cpp_all = cpp + open(ROOT + 'xbmc/windowing/gbm/WinSystemGbm.cpp', encoding='utf-8').read()
prefixes = re.search(r'static const char\* const prefixes\[\] = \{(.*?)\};', cpp, re.S).group(1)
prefixes = re.findall(r'"([^"]+)"', prefixes)
for c in dialog.iter('include'):
    for p in c.findall('param'):
        if p.get('name') == 'flash':
            key = 'hades.' + p.get('value')
            if '"%s"' % key not in cpp:
                fail('flash key %s is not published by the window system' % key)
            if not any(key.startswith(pre) for pre in prefixes) or key in ('hades.dv.l1changes', 'hades.audio.bitrate', 'hades.video.bitrate'):
                fail('flash key %s is not one that the window system lights' % key)

# 4. every property that the skin reads is published
props = set(re.findall(r'Property\((hades\.[A-Za-z0-9_.]+)\)', texts))
for pr in sorted(props):
    if '"%s"' % pr not in cpp_all and pr not in ('hades.f.',):
        # the flash properties are made of a key: hades.f.<key>
        if pr.startswith('hades.f.'):
            continue
        fail('property %s is read by the skin and published by no one' % pr)

# 5. control ids: no duplicates
ids = [c.get('id') for c in dialog.iter('control') if c.get('id')]
dup = {i for i in ids if ids.count(i) > 1}
if dup:
    fail('duplicate control ids: %s' % sorted(dup))
# the tabs and their pages: each tab sets a page that a page group shows (a tab is a button, or a togglebutton, which
# alone draws its alternative texture for the page shown: GUIControlFactory.cpp, usealttexture)
tabs = {c.get('id'): c for c in dialog.iter('control') if c.get('type') in ('button', 'togglebutton') and c.get('id', '').startswith('55')}
pages = set(re.findall(r'PlayerInfoDialogFocus\),(\d+)\)</visible>', open(SKIN + 'xml/DialogPlayerProcessInfo.xml', encoding='utf-8').read()))
for tid in ('5556', '5551', '5557', '5555', '5558', '5559'):
    if tid not in tabs:
        fail('no tab %s' % tid)
    if tid not in pages:
        fail('no page for tab %s' % tid)
for p in pages:
    if p not in tabs and p not in ('5560', '5561', '5562'):
        fail('page %s has no tab' % p)
for tid, c in tabs.items():
    if tid in ('5560', '5561', '5562'):
        continue
    if 'SetProperty(PlayerInfoDialogFocus,%s,Home)' % tid not in alltext(c):
        fail('tab %s does not set its page' % tid)

# 6. the labels that the panel showed: nothing of them is gone (a label or a Player.Process); the reference is a file
# of them, one a line, given as the second argument ("--labels <file>" writes those of this tree there instead)
WRITE = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == '--labels' else None
old = open(sys.argv[2], encoding='utf-8').read() if len(sys.argv) > 2 and not WRITE else ''
old_labels = set(re.findall(r'(?:Player\.Process\([a-z.]+\)|VideoPlayer\.[A-Za-z]+|PVR\.[A-Za-z]+|System\.[A-Za-z]+(?:\([a-z.]+\))?|Player\.CacheLevel|hades\.[a-z0-9.]+)', old))
new_labels = set(re.findall(r'(?:Player\.Process\([a-z.]+\)|VideoPlayer\.[A-Za-z]+|PVR\.[A-Za-z]+|System\.[A-Za-z]+(?:\([a-z.]+\))?|Player\.CacheLevel|hades\.[a-z0-9.]+)', texts))
ignored = {'System.Memory(used.percent)', 'System.SupportsCPUUsage', 'System.CpuUsage', 'hades.sys.memory'}
# the same thing from the same place, written another way
equivalent = {
    'Player.Process(videowidth)': 'hades.video.size',   # the width and the height of the cache, together
    'Player.Process(videoheight)': 'hades.video.size',
    'VideoPlayer.SubtitleCodec': '$VAR[SubtitleCodecVar',  # which maps that label
    'hades.dv.compose': 'hades.dv.composebits',  # the composer, said in two rows: its bits and its maps
    # the source's audio: the same values of the data cache (GetAudioBitsPerSample(), GetAudioSampleRate()), the rate
    # in kHz
    'Player.Process(audiobitspersample)': 'hades.audio.srcbits',
    'Player.Process(audiosamplerate)': 'hades.audio.srcrate',
}
for old_label, new_text in equivalent.items():
    if old_label in old_labels and new_text in texts:
        ignored.add(old_label)
gone = sorted(l for l in old_labels - new_labels if l not in ignored)
for l in gone:
    fail('the panel showed %s (the reference of its labels) and does not' % l)
notes.append('labels of the reference: %d, of the panel: %d' % (len(old_labels), len(new_labels)))
if WRITE:
    open(WRITE, 'w', encoding='utf-8').write(''.join(l + '\n' for l in sorted(new_labels)))

print('\n'.join('note: ' + n for n in notes))
if fails:
    print('FAILED %d' % len(fails))
    print('\n'.join('  ' + f for f in fails))
    sys.exit(1)
print('skin checks: all passed (%d string ids of the player information, %d variables, %d includes, %d property reads)' % (len(mine), len(own_vars), len(defs), len(props)))
