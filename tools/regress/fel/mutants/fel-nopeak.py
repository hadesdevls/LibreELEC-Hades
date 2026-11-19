# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of the enhancement-layer module: the most surfaces the layer held at once is not counted
# (counted_buffer()), so what the module says of its surfaces is not what they were.
FILE = 'tools/dvbridge/dvbridge_fel.c'
PAIRS = [
    ('    if (live > f->surfaces_peak)\n        f->surfaces_peak = live;\n',
     '    (void)live;\n'),
]
