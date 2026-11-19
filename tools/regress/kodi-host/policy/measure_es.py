#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades, host test: widths of the strings of the projector settings where Estuary shows them, with
# Estuary's own fonts (Noto Sans, sizes of Font.xml), in en_gb, es_es, es_ar and es_mx.
#   settings page (System > Display): setting row 1450 px with 40 px text offsets each side, font13 (30 px):
#     label + value must fit 1370 px (a gap of 30 px between them counted)
#   video settings of the player (DialogSettings.xml): row 1160 px, offsets 40: label + value in 1080 px
#   player information, Dolby Vision page: "<Reason>: <reason>" in 885 px, font14 (33 px)
#   notification (DialogNotification.xml): heading 450 px font37, text 450 px font27_narrow (fade labels
#     that scroll what is longer)
import pathlib, re, sys
from PIL import ImageFont

tree = pathlib.Path(sys.argv[1])
skin = tree / 'addons/skin.estuary'
fontfile = str(skin / 'fonts/NotoSans-Regular.ttf')
sizes = {}
for m in re.finditer(r'<name>([^<]+)</name>\s*<filename>NotoSans-Regular\.ttf</filename>\s*<size>(\d+)</size>',
                     (skin / 'xml/Font.xml').read_text()):
    sizes.setdefault(m.group(1), int(m.group(2)))
F = {n: ImageFont.truetype(fontfile, sizes[n]) for n in ('font13', 'font14', 'font37', 'font27_narrow')}


def strings(lang):
    t = (skin / f'language/resource.language.{lang}/strings.po').read_text()
    out = {}
    for m in re.finditer(r'msgctxt "#(\d+)"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"', t):
        i = int(m.group(1))
        out[i] = (m.group(3) or m.group(2)).replace('\\"', '"') if lang != 'en_gb' else m.group(2).replace('\\"', '"')
    return out


worst = 0
bad = 0
for lang in ('en_gb', 'es_es', 'es_ar', 'es_mx'):
    s = strings(lang)
    def w(font, text):
        return F[font].getlength(text)
    rows = []
    # settings page: label with each of its values (the longest decides)
    rows.append(('settings', 1370, s[51770], 'Off'))  # a toggle: its box is about 100 px
    rows.append(('settings', 1370, s[51772], max((s[51774], s[51775]), key=lambda v: w('font13', v))))
    rows.append(('settings', 1370, s[51776], s[51778].replace('{0:d}', '4000')))
    rows.append(('settings', 1370, s[51779], max((s[51781], s[51782], s[51783]), key=lambda v: w('font13', v))))
    rows.append(('settings', 1370, s[51784], max((s[51786], s[51787]), key=lambda v: w('font13', v))))
    # the video settings of the player
    values = (s[51763], s[51764], s[51765], s[51766].replace('{}', '4000'), s[51767])
    rows.append(('player', 1080, s[51762], max(values, key=lambda v: w('font13', v))))
    for kind, limit, label, value in rows:
        width = w('font13', label) + 30 + (100 if value == 'Off' else w('font13', value))
        worst = max(worst, width / limit)
        over = width > limit
        bad += over
        print(f'{lang} {kind:8} {width:7.1f} of {limit} {"OVER" if over else "ok  "}  {label} | {value}')
    for rid in (51760, 51761):
        text = f'{s[51681]}: {s[rid]}'
        width = w('font14', text)
        bad += width > 885
        print(f'{lang} info     {width:7.1f} of 885 {"OVER" if width > 885 else "ok  "}  {text}')
    for font, text in (('font37', 'Dolby Vision'), ('font27_narrow', s[51768])):
        width = w(font, text)
        bad += width > 450
        print(f'{lang} toast    {width:7.1f} of 450 {"OVER" if width > 450 else "ok  "} {text}')
print(f'rows over their space: {bad}')
