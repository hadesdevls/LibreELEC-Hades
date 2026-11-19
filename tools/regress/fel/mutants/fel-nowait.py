# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of the enhancement-layer module: an access unit of the layer does not wait for a surface to be given back
# while the layer holds as many as it may (pump()), so a fixed pool of the layer's decoder can run out.
FILE = 'tools/dvbridge/dvbridge_fel.c'
PAIRS = [
    ('            if (surfaces_live(f) >= f->surface_limit) {\n',
     '            if (0 && surfaces_live(f) >= f->surface_limit) {\n'),
]
