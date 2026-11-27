#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# The layout of the settings of a Kodi tree (system/settings/settings.xml), as LibreELEC Hades lays them out by what goes
# out (docs/settings.md, "How the settings are laid out"):
#  - every setting id of 1.1 (ref/settings-ids-1.1.txt) is still there, and no id twice: Kodi keeps a value by its id,
#    so a value that a user set survives a move, and 1.1 reads the same guisettings.xml (the migration both ways);
#  - the groups of System > Display, Player > Videos, Player > Subtitles and System > Audio in their order, each
#    setting named below in its group and in that order (other settings may sit between them), at its level;
#  - a setting with a parent comes after it, in its category; every setting that a dependency or a parent names exists;
#  - every setting that an override file of the build names (LibreELEC's appliance*.xml, Kodi's system/settings/linux.xml)
#    is in the section, category and group that the file names, which is where Kodi looks for it;
#  - the heading and help strings of the layout are in the core strings, mirrored in Estuary's en_gb, translated in
#    es_es, es_ar and es_mx, and mapped to Estuary by GUIDialogSettingsBase::GetLocalizedString();
#  - and so is every string of Hades (59000 to 62999) that a settings file of the tree uses as a label, a help, the
#    label of an option or a format: a string without that mapping is shown in English in Spanish, as
#    GetLocalizedString() takes it from the core strings alone (the options of the upscalers 62082 and 62083, 1.2).
# usage: settings_layout_check.py <Kodi tree> <ids of 1.1> [<override file>...]
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

tree = pathlib.Path(sys.argv[1])
ids11 = [l.strip() for l in open(sys.argv[2], encoding='utf-8') if l.strip() and not l.startswith('#')]
overrides = [pathlib.Path(p) for p in sys.argv[3:]]
fails = 0


def check(ok, what):
    global fails
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + what)


root = ET.parse(tree / 'system/settings/settings.xml').getroot()
where = {}        # id -> (section, category, group, index in the group, level, parent)
groups = {}       # (section, category) -> [(group id, label)]
order = {}        # (section, category, group) -> [ids]
dup = []
for sec in root.findall('section'):
    for cat in sec.findall('category'):
        key = (sec.get('id'), cat.get('id'))
        groups[key] = []
        for grp in cat.findall('group'):
            groups[key].append((grp.get('id'), grp.get('label')))
            ids = order.setdefault(key + (grp.get('id'),), [])
            for s in grp.findall('setting'):
                sid = s.get('id')
                if sid in where:
                    dup.append(sid)
                lvl = s.findtext('level')
                where[sid] = key + (grp.get('id'), len(ids), int(lvl) if lvl else None, s.get('parent'))
                ids.append(sid)
check(not dup, f'no setting id twice ({len(where)} settings){": " + ", ".join(dup) if dup else ""}')
gone = [i for i in ids11 if i not in where]
check(not gone, f'every setting id of 1.1 is still there ({len(ids11)}){": gone " + ", ".join(gone[:8]) if gone else ""}')

# the layout: (section, category): [(group id, label, [(setting, level)...])...] in the order of the groups
L = {
    ('system', 'display'): [
        ('1', '16000', [('videoscreen.monitor', 0), ('videoscreen.screen', 0), ('videoscreen.resolution', 0),
                        ('videoscreen.screenmode', 0), ('videoscreen.hadeshdmivrr', 3), ('videoscreen.hadesvrrvideomode', 3),
                        ('videoscreen.hadescontenttype', 3)]),
        ('3', '14126', [('videoscreen.whitelist', 3), ('videoscreen.hadeswholemultiple', 3)]),
        ('2', '39191', [('winsystem.ishdrdisplay', 2), ('videoscreen.hadeshdrassdr', 2), ('videoscreen.hadesdvsdrtarget', 2),
                        ('videoscreen.hadesdvsdrwhite', 3), ('videoscreen.hadesdvsdrgamma', 3),
                        ('videoscreen.hadessdrhdrgamut', 3), ('videoscreen.hadessdrbt2020', 2), ('videoscreen.hadesguiwhite', 2), ('videoscreen.hadesguicurve', 3)]),
        ('7', '61104', [('videoscreen.hadesdvnative', 2), ('videoscreen.hadesdvmenus', 2), ('videoscreen.hadesdvlowlatency', 2),
                        ('videoscreen.hadesdvllalways', 3), ('videoscreen.hadesdvllblackedid', 3),
                        ('videoscreen.hadesdvllmanual', 3), ('videoscreen.hadesdvllpeak', 3), ('videoscreen.hadesdvllgamut', 3),
                        ('videoscreen.hadesdvllblack', 3), ('videoscreen.hadesdvhdmi14b', 2),
                        ('videoscreen.hadesdvconverter', 3), ('videoscreen.hadesdvdce', 3), ('videoscreen.hadesdvnotrims', 3),
                        ('videoscreen.hadesdvtrimpass', 3), ('videoscreen.hadesdvcm40', 3),
                        ('videoscreen.hadesdvcm40manual', 3), ('videoscreen.hadesdvcm40peak', 3)]),
        # (1.2.3: "Play HDR10+ videos as Dolby Vision" of three values; its boolean of before stays, hidden)
        ('10', '62500', [('videoscreen.hadesdvfromhdr10plusmode', 2), ('videoscreen.hadesdvfromhdr10', 2),
                         ('videoscreen.hadeshlgasdv', 2), ('videoscreen.hadesdualhdr', 3)]),
        ('8', '61106', [('videoscreen.hadesdvashdr10', 2), ('videoscreen.hadesdvashdr10fel', 3),
                        ('videoscreen.hadesdvashdr10plus', 3), ('videoscreen.hadesdvhdr10map', 3),
                        ('videoscreen.hadesdvhdr10peak', 3), ('videoscreen.hadesdvhdr10gamut', 3),
                        ('videoscreen.hadesdvhdr10curve', 3), ('videoscreen.hadesdvhdr10black', 3),
                        ('videoscreen.hadeshdr10adapt', 3), ('videoscreen.hadeshdr10scenes', 3),
                        ('videoscreen.hadeshdr10limiton', 3), ('videoscreen.hadeshdr10limit', 3)]),
        # (the network control of a projector, a group of its own after the HDR of projectors: network/ProjectorControl.h)
        ('11', '62700', [('videoscreen.hadesprojector', 2), ('videoscreen.hadesprojectoraddress', 2),
                         ('videoscreen.hadesprojectorpassword', 2), ('videoscreen.hadesprojectorconfirmed', 4),
                         ('videoscreen.hadesprojectortest', 2), ('videoscreen.hadesprojectorlens', 2),
                         # (1.2.3: the anamorphic mode of a JVC next to the lens memory of each class of shape)
                         ('videoscreen.hadesprojectorlensnarrow', 2), ('videoscreen.hadesprojectoranamorphicnarrow', 2),
                         ('videoscreen.hadesprojectorlenswidefrom', 2),
                         ('videoscreen.hadesprojectorlenswide', 2), ('videoscreen.hadesprojectoranamorphicwide', 2),
                         ('videoscreen.hadesprojectorlenswidest', 3), ('videoscreen.hadesprojectoranamorphicwidest', 3),
                         ('videoscreen.hadesprojectorlenswidestfrom', 3), ('videoscreen.hadesprojectorlenshold', 3),
                         # (1.2.3: the menus before SDR video, SDR in BT.2020 and DCI-P3 after it; the player controls
                         # inside a scope picture last)
                         ('videoscreen.hadesprojectorlensstop', 2), ('videoscreen.hadesprojectormodemenus', 2),
                         ('videoscreen.hadesprojectormodesdr', 2), ('videoscreen.hadesprojectormodesdr2020', 2),
                         ('videoscreen.hadesprojectormodesdrp3', 3),
                         ('videoscreen.hadesprojectormodehdr10', 2), ('videoscreen.hadesprojectormodehlg', 2),
                         ('videoscreen.hadesprojectormodedv', 2), ('videoscreen.hadesprojectormode3d', 2),
                         ('videoscreen.hadesprojectorosd', 2)]),
        # (the 3D LUT of the display, after the mapping to it: a group of its own, expert)
        ('12', '62800', [('videoscreen.hadeslut3d', 3), ('videoscreen.hadeslut3dsdr', 3), ('videoscreen.hadeslut3dhdr', 3),
                         ('videoscreen.hadeslut3drange', 3), ('videoscreen.hadeslut3dsdrgamut', 3)]),
        ('9', '61105', [('videoscreen.hadeshdr10tunnel', 2), ('videoscreen.hadeshdr420', 3), ('videoscreen.hadescolorformat', 3),
                        ('videoscreen.hadesmaxbpc', 3), ('videoscreen.hadesfloatoutput', 3), ('videoscreen.hadeshdrdepth', 3),
                        ('videoscreen.hadeshdrmorebits', 3), ('videoscreen.hadeshdrdeepcolour', 3), ('videoscreen.hadeshdrgmp', 3)]),
        ('6', '59964', [('videoscreen.hadesdvpathmode', 3), ('videoscreen.hadesdvtransport', 3), ('videoscreen.hadesdvgamutfine', 3)]),
        ('4', '14232', [('videoscreen.stereoscopicmode', 2), ('videoscreen.hades3doutput', 3)]),
        ('5', '496', [('videoscreen.guicalibration', 3)]),
    ],
    ('player', 'videoplayer'): [
        ('1', None, [('videoplayer.hadesmkveditions', 2)]),
        ('2', None, [('videoplayer.adjustrefreshrate', 1), ('videoplayer.hadesadjustresolution', 1),
                        ('videoplayer.usedisplayasclock', 1), ('videoplayer.hadesmeasuredlatency', 2),
                        ('videoplayer.hadesactiveareazoom', 3)]),
        ('6', '62501', [('videoscreen.hadesplaybackmode', 2), ('videoscreen.hadeskodiplayerinfo', 2)]),
        ('3', None, [('videoplayer.hadesupscalerhd', 3), ('videoplayer.hadesfilmfields', 2)]),
        ('4', '14232', [('videoplayer.hades3dmvc', 3), ('videoplayer.hades3dmvcbase', 3)]),
        ('5', None, []),
    ],
    ('player', 'subtitles'): [
        ('1', None, [('subtitles.align', 2), ('subtitles.hadesinbars', 1)]),
        ('2', None, [('subtitles.bitmapzoom', 2), ('subtitles.hadesbitmaprange', 2)]),
        ('5', '14232', [('subtitles.hadesstereodepth', 3), ('subtitles.stereoscopicdepth', 2)]),
        ('3', None, []), ('4', None, []),
    ],
    ('system', 'audio'): [
        ('1', None, []), ('2', None, []),
        ('3', None, [('audiooutput.passthrough', 1), ('audiooutput.dtshdcorefallback', 2), ('audiooutput.hadespconaudio', 3),
                     ('audiooutput.hadeslivepassthrough', 3)]),
    ],
}
for key, spec in L.items():
    got = [g for g, _ in groups.get(key, [])]
    want = [g for g, _, _ in spec]
    check(got == want, f'{"/".join(key)}: the groups in the order {want} (found {got})')
    labels = dict(groups.get(key, []))
    for gid, label, settings in spec:
        if label is not None:
            check(labels.get(gid) == label, f'{"/".join(key)} group {gid}: heading #{label} (found #{labels.get(gid)})')
        last = -1
        for sid, level in settings:
            w = where.get(sid)
            if not w or w[:3] != key + (gid,):
                check(False, f'{sid}: in {"/".join(key)} group {gid} (found {"/".join(w[:3]) if w else "nowhere"})')
                continue
            check(w[3] > last, f'{sid}: in {"/".join(key)} group {gid}, after the one named before it')
            last = max(last, w[3])
            check(w[4] == level, f'{sid}: level {level} (found {w[4]})')

# parents and dependencies
for sid, w in where.items():
    p = w[5]
    if p:
        # (Kodi's whitelist has its parent, the display mode, in the group General: a parent in an earlier group of
        # the same category is Kodi's own way too)
        pw = where.get(p)
        gs = [g for g, _ in groups[w[:2]]]
        if not pw or pw[:2] != w[:2] or (gs.index(pw[2]), pw[3]) > (gs.index(w[2]), w[3]):
            check(False, f'{sid}: its parent {p} is before it, in its category (found {"/".join(pw[:3]) if pw else "nowhere"})')
named = set(re.findall(r'(?:setting|parent)="([a-z0-9_]+\.[a-z0-9_.]+)"',
                       (tree / 'system/settings/settings.xml').read_text(encoding='utf-8')))
missing = sorted(n for n in named if n not in where)
check(not missing, f'every setting that a dependency or a parent names exists{": " + ", ".join(missing) if missing else ""}')

# the override files: Kodi looks for a setting in the section, category and group the file names
for f in overrides:
    o = ET.parse(f).getroot()
    n = 0
    for sec in o.findall('section'):
        for cat in sec.findall('category'):
            for grp in cat.findall('group'):
                for s in grp.findall('setting'):
                    n += 1
                    w = where.get(s.get('id'))
                    if w and w[:3] != (sec.get('id'), cat.get('id'), grp.get('id')):
                        check(False, f'{f.name}: {s.get("id")} named in {sec.get("id")}/{cat.get("id")} group {grp.get("id")}, '
                                     f'but it is in {"/".join(w[:3])}')
    print(f'note {f.name}: {n} settings named')

# the strings of the layout: core, Estuary's mirror, the three Spanish, the mapping
core = (tree / 'addons/resource.language.en_gb/resources/strings.po').read_text(encoding='utf-8')
est = {l: (tree / f'addons/skin.estuary/language/resource.language.{l}/strings.po').read_text(encoding='utf-8')
       for l in ('en_gb', 'es_es', 'es_ar', 'es_mx')}
base = (tree / 'xbmc/settings/dialogs/GUIDialogSettingsBase.cpp').read_text(encoding='utf-8')


def entry(t, i):
    m = re.search(r'msgctxt "#%d"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"' % i, t)
    return m.groups() if m else None


for c in (62500, 62501, 62502):
    e = c - 8000
    ce = entry(core, c)
    ok = ce is not None and ce[0] != ''
    for l, t in est.items():
        ee = entry(t, e)
        ok = ok and ee is not None and ee[0] == ce[0] and (l == 'en_gb' or ee[1] != '')
    check(ok, f'core #{c} with its Estuary #{e}: the same English, translated in es_es, es_ar and es_mx')
check(re.search(r'if \(labelId >= 62500 && labelId <= 62502\)\s*return CGUIUtils::GetHadesString\(labelId, labelId - 8000\);',
                base) is not None, 'GetLocalizedString() maps #62500-#62502 to Estuary')
# every string of Hades that a settings file uses: mapped by GetLocalizedString() to its Estuary string, which has the
# same English and is translated
start = base.index('std::string CGUIDialogSettingsBase::GetLocalizedString(')
body = base[start:base.index('\n}\n', start)]
mapped = {}
for m in re.finditer(r'if \(([^{;]*?labelId[^{;]*?)\)\s*return CGUIUtils::GetHadesString\(([^;]*)\);', body):
    cond, call = m.group(1), m.group(2)
    ids = {int(a) for a in re.findall(r'labelId == (\d+)', cond)}
    for a, b in re.findall(r'labelId >= (\d+) && labelId <= (\d+)', cond):
        ids.update(range(int(a), int(b) + 1))
    second = call.split(',', 1)[1].strip()
    for i in ids:
        if i not in mapped and re.fullmatch(r'[0-9 +()-]*', second.replace('labelId', str(i))):
            mapped[i] = eval(second.replace('labelId', str(i)))
used = set()
for f in sorted((tree / 'system/settings').glob('*.xml')):
    t = f.read_text(encoding='utf-8')
    used.update(int(x) for x in re.findall(r'(?:label|help)="(\d+)"', t))
    used.update(int(x) for x in re.findall(r'<formatlabel>(\d+)</formatlabel>', t))
hades = sorted(i for i in used if 59000 <= i < 63000)
unmapped = [i for i in hades if i not in mapped]
check(not unmapped, f'every string of Hades that a settings file uses is mapped to Estuary by GetLocalizedString() '
                    f'({len(hades)} strings){": not #" + ", #".join(map(str, unmapped)) if unmapped else ""}')
bad = []
for i in hades:
    if i not in mapped:
        continue
    ce, e = entry(core, i), mapped[i]
    for l, t in est.items():
        ee = entry(t, e)
        if ce is None or ee is None or ee[0] != ce[0] or (l != 'en_gb' and ee[1] == ''):
            bad.append(f'#{i}->#{e} {l}')
check(not bad, f'... with the same English in Estuary, translated in es_es, es_ar and es_mx'
               f'{": " + ", ".join(bad[:12]) if bad else ""}')
ishdr = root.find(".//setting[@id='winsystem.ishdrdisplay']")
check(ishdr is not None and ishdr.get('help') == '62502', 'Adjust display HDR mode has the help of Hades (#62502)')
# no text of the settings says the old places of what moved
old = [s for s in ('(Player > Videos) is off', '> Submit Log)', 'Display, Expert level).') if s in core + est['en_gb']]
check(not old, f'no string names an old place{": " + ", ".join(old) if old else ""}')
print('SETTINGS-LAYOUT', 'ALL PASSED' if not fails else f'FAILED ({fails})')
sys.exit(1 if fails else 0)
