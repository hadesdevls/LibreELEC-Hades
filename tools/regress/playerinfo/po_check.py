#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# The settings and the language files of a Kodi tree are well formed: settings.xml parses; each strings.po has no duplicate id and every entry is msgctxt/msgid/
# msgstr; no entry but the header is without msgctxt (one without it is no string of the skin: a piece of a conflict); the Hades ids of the Estuary files
# are in order; and in es_es, es_ar and es_mx each msgctxt has the msgid of Estuary's en_gb, none of en_gb missing. Usage: po_check.py <Kodi tree>
import pathlib, re, sys, xml.dom.minidom

root = pathlib.Path(sys.argv[1])
fail = 0
msgids = {}
xml.dom.minidom.parse(str(root / "system/settings/settings.xml"))
print("settings.xml parses")
for rel in ["addons/resource.language.en_gb/resources/strings.po"] + [
        f"addons/skin.estuary/language/resource.language.{l}/strings.po" for l in ("en_gb", "es_es", "es_ar", "es_mx")]:
    text = (root / rel).read_text()
    ids = re.findall(r'^msgctxt "#(\d+)"$', text, re.M)
    dup = sorted({i for i in ids if ids.count(i) > 1})
    blocks = re.findall(r'^msgctxt "#\d+"\nmsgid "(?:[^"\\]|\\.)*"\nmsgstr "(?:[^"\\]|\\.)*"$', text, re.M)
    hades = [int(i) for i in ids if 51740 <= int(i) < 52000]
    order = hades == sorted(hades) if "skin.estuary" in rel else True
    # (an entry without msgctxt: its msgid not the empty one of the header)
    bare = [m.group(1)[:50] for b in re.split(r"\n\s*\n", text) if not re.search(r'^msgctxt ', b, re.M)
            for m in [re.search(r'^msgid "((?:[^"\\]|\\.)*)"$', b, re.M)] if m and m.group(1)]
    msgids[rel] = dict(re.findall(r'^msgctxt "#(\d+)"\nmsgid "((?:[^"\\]|\\.)*)"$', text, re.M))
    ok = not dup and len(blocks) == len(ids) and order and "\r" not in text and "<<<<<<<" not in text and not bare
    fail += not ok
    print(f"{'ok  ' if ok else 'FAIL'} {rel}: {len(ids)} strings, {len(blocks)} well formed, duplicates {dup}"
          + (f", ids from 51740 in order: {order} ({len(hades)})" if "skin.estuary" in rel else "")
          + (f", entries without msgctxt: {bare}" if bare else ""))
# the Spanish files of Estuary against its en_gb: the same msgctxt with the same msgid
en = msgids["addons/skin.estuary/language/resource.language.en_gb/strings.po"]
for l in ("es_es", "es_ar", "es_mx"):
    es = msgids[f"addons/skin.estuary/language/resource.language.{l}/strings.po"]
    other = sorted(i for i in es if i in en and es[i] != en[i])
    missing = sorted(i for i in en if i not in es)
    ok = not other and not missing
    fail += not ok
    print(f"{'ok  ' if ok else 'FAIL'} {l} against en_gb: another msgid {other[:10]}, missing {missing[:10]}")
print("PO-CHECK", "ALL PASSED" if not fail else f"FAILED ({fail})")
sys.exit(1 if fail else 0)
