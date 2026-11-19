# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutants of the default content mapping v4.0 part of the serializer (dvbridge_metadata.h) and of the average floor
# of HDR10 measured as Dolby Vision (dvbridge_measured.c), for the stage mapping of run-dvbridge-meta.sh.
DIRS = ['tools/dvbridge']
MUTANTS = [
    ('cm40-always', 'tools/dvbridge/dvbridge_metadata.h',
     [('    if ((d->cm40_default || (d->cm40_auto && d->cm40_auto_on)) && !rpu_v40 && !d->cm29_only &&',
       '    if ((true || d->cm40_default || (d->cm40_auto && d->cm40_auto_on)) && !rpu_v40 && !d->cm29_only &&')],
     'cm40', '-', 'the default v4.0 part added also where it is switched off'),
    ('avgfloor-half', 'tools/dvbridge/dvbridge_measured.c',
     [('    avg = avg < avg_floor ? avg_floor : avg;', '    avg = avg < avg_floor / 2 ? avg_floor / 2 : avg;')],
     'avgfloor', '-', 'the average floor at half of its parameter'),
    ('l3-max-bias', 'tools/dvbridge/dvbridge_placebo.c',
     [('    out->offset[1] = (int)l3->l3.max_pq_offset - 2048;', '    out->offset[1] = (int)l3->l3.max_pq_offset - 1948;')],
     'level3', '-', 'the maximum offset of level 3 read 100 codes off: a neutral level 3 changes the mapping'),
    ('l3-no-guard', 'tools/dvbridge/dvbridge_placebo.c',
     [('    if (!(0 <= mn && mn < av && av < mx && mx <= 1))\n        return true; /* ST 2094-10 6.1.9: the level 1 block alone */',
       '    if (false)\n        return true; /* ST 2094-10 6.1.9: the level 1 block alone */')],
     'level3', '-', 'level 3 taken where ST 2094-10 6.1.9 refuses it (a black shot)'),
]
