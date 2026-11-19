# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# usage: validate_skin_mutants.py <Kodi tree> <work folder> <validate_skin.py>: every broken copy of the skin files
# must fail validate_skin.py (the other direction); the copies are made in the work folder, which is removed
import os, shutil, subprocess, sys

if len(sys.argv) < 4:
    sys.exit('usage: validate_skin_mutants.py <Kodi tree> <work folder> <validate_skin.py>')
SRC, TMP, VAL = [a if a.endswith('/') or a.endswith('.py') else a + '/' for a in sys.argv[1:4]]
FILES = ['addons/skin.estuary/xml/DialogPlayerProcessInfo.xml', 'addons/skin.estuary/xml/Includes_HadesPlayerInfo.xml',
         'addons/skin.estuary/xml/Variables.xml', 'addons/skin.estuary/xml/Includes_Maps.xml',
         'addons/resource.language.en_gb/resources/strings.po',
         'xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp', 'xbmc/windowing/gbm/WinSystem' + 'Gbm.cpp'] + \
        ['addons/skin.estuary/language/resource.language.%s/strings.po' % l for l in ('en_gb', 'es_es', 'es_ar', 'es_mx')]


def fresh():
    if os.path.isdir(TMP):
        shutil.rmtree(TMP)
    for f in FILES:
        os.makedirs(os.path.dirname(TMP + f), exist_ok=True)
        shutil.copy(SRC + f, TMP + f)


def edit(f, old, new, count=1):
    s = open(TMP + f, encoding='utf-8', newline='').read()
    if old not in s:
        sys.exit('mutant: %r not in %s' % (old[:50], f))
    open(TMP + f, 'w', encoding='utf-8', newline='').write(s.replace(old, new, count))


def run():
    r = subprocess.run([sys.executable, VAL, TMP], capture_output=True, text=True)
    return r.returncode, r.stdout


D = 'addons/skin.estuary/xml/DialogPlayerProcessInfo.xml'
I = 'addons/skin.estuary/xml/Includes_HadesPlayerInfo.xml'
V = 'addons/skin.estuary/xml/Variables.xml'
fresh()
rc, out = run()
print('unbroken copy: rc=%d %s' % (rc, out.strip().splitlines()[-1] if out.strip() else ''))
bad = 0 if rc == 0 else 1
mutants = [
    ('a string id that does not exist', lambda: edit(V, '$ADDON[skin.estuary 52700]', '$ADDON[skin.estuary 39999]')),
    ('a $LOCALIZE of an id of Estuary above 31999', lambda: edit(V, '$ADDON[skin.estuary 52700]', '$LOCALIZE[52700]')),
    ('a $LOCALIZE of an id of Estuary above 31999 in the dialog', lambda: edit(D, '$ADDON[skin.estuary 52740]', '$LOCALIZE[52740]')),
    ('a variable that does not exist', lambda: edit(D, '$VAR[HadesSumSubVar]', '$VAR[HadesSumNoSuchVar]')),
    ('a parameter that the include does not declare', lambda: edit(D, '<param name="kicker" value="$ADDON[skin.estuary 52740]"/>', '<param name="kicker" value="$ADDON[skin.estuary 52740]"/><param name="nonsense" value="1"/>')),
    ('a property that no one publishes', lambda: edit(D, 'hades.video.area', 'hades.video.nothing')),
    ('a flash key that is not lit', lambda: edit(D, '<param name="flash" value="dv.l1"/>', '<param name="flash" value="sys.cpu"/>')),
    ('a flash key that is not published', lambda: edit(D, '<param name="flash" value="dv.l1"/>', '<param name="flash" value="dv.l1nothing"/>')),
    ('a duplicate control id', lambda: edit(D, 'id="5559"', 'id="5558"')),
    ('a tab that does not set its page', lambda: edit(D, '<onfocus>SetProperty(PlayerInfoDialogFocus,5557,Home)</onfocus>', '')),
    ('a page without its group', lambda: edit(D, 'PlayerInfoDialogFocus),5559)</visible>', 'PlayerInfoDialogFocus),5599)</visible>')),
    ('a broken XML', lambda: edit(D, '</window>', '</windo>')),
    ('an include that does not exist', lambda: edit(D, '<include content="HadesPanelNote">', '<include content="HadesPanelNoNote">')),
    ('a Spanish string left out', lambda: edit('addons/skin.estuary/language/resource.language.es_es/strings.po', 'msgstr "Altavoces"', 'msgstr ""')),
    ('a Spanish string that writes vídeo for a Latin American one', lambda: edit('addons/skin.estuary/language/resource.language.es_mx/strings.po', 'msgstr "Altavoces"', 'msgstr "Altavoces del vídeo"')),
    ('English with a translation', lambda: edit('addons/skin.estuary/language/resource.language.en_gb/strings.po', 'msgid "Speakers"\nmsgstr ""', 'msgid "Speakers"\nmsgstr "x"')),
    ('a string that no file uses', lambda: edit(V, '$ADDON[skin.estuary 52811]', '$ADDON[skin.estuary 52810]')),
]
for name, fn in mutants:
    fresh()
    fn()
    rc, out = run()
    verdict = 'killed' if rc != 0 else 'SURVIVED'
    if rc == 0:
        bad += 1
    print('mutant %-60s %s' % (name, verdict))
shutil.rmtree(TMP)
print('mutants that survived or an unbroken copy that failed: %d' % bad)
sys.exit(1 if bad else 0)
