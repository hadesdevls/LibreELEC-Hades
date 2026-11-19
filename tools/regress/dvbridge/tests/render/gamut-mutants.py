# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# The bridges that gamut_table_test runs on (tools/dvbridge/dvbridge_render.c unless said), each an exact edit of the
# tree under test (rg_edit). usage: python3 -I gamut-mutants.py <name> <out .py>: writes the edit file of <name>
# (PAIRS, and FILE: the file under tools/dvbridge), or lists the names with "list".
#   fail  the finer table beyond the GPU's largest 3D texture: it cannot be made, the standard one takes its place
#   m1    ... and the render is not made again with the standard table after the finer one failed
#   m2    ... and the failure of the finer table not taken into account when the table of a mapping is chosen
#   m3    ... and asking for the finer table again (the setting changed) does not try it again
#   m4    the table reported as the finer one whatever was used
#   m0    no finer table to ask for at all (the bridge without dvbridge_render_set_gamut_table(): as before it)
#   size  (gamut-ident) the standard table of another size
import sys

BIG = ('static const int GAMUT_FINE[3] = {256, 192, 512};', 'static const int GAMUT_FINE[3] = {8192, 2, 2};')
MUTANTS = {
    'fail': ('dvbridge_render.c', [BIG]),
    'm1': ('dvbridge_render.c', [BIG, ('    if (ok || !fine)\n        return false;\n    r->gamut_fine_failed = true;',
                                      '    if (ok || !fine || 1)\n        return false;\n    r->gamut_fine_failed = true;')]),
    'm2': ('dvbridge_render.c', [BIG, (
        '    const bool fine = r->gamut_asked == DVBRIDGE_GAMUT_TABLE_FINE && !r->gamut_fine_failed;\n'
        '    const int *size = fine ? GAMUT_FINE : GAMUT_STANDARD;',
        '    const bool fine = r->gamut_asked == DVBRIDGE_GAMUT_TABLE_FINE;\n'
        '    const int *size = fine ? GAMUT_FINE : GAMUT_STANDARD;')]),
    'm3': ('dvbridge_render.c', [BIG, ('    if (changed)\n        r->gamut_fine_failed = false;\n', '')]),
    'm4': ('dvbridge_render.c', [('        .used = fine ? DVBRIDGE_GAMUT_TABLE_FINE : DVBRIDGE_GAMUT_TABLE_STANDARD,',
                                  '        .used = DVBRIDGE_GAMUT_TABLE_FINE,')]),
    'm0': ('dvbridge_render.h', [('#define DVBRIDGE_RENDER_GAMUT_TABLE 1\n', '')]),
    # for gamut-ident: the standard table one entry shorter in hue, so every mapped picture changes and those drawn
    # as they are do not
    'size': ('dvbridge_render.c', [('static const int GAMUT_STANDARD[3] = {96, 64, 512};',
                                    'static const int GAMUT_STANDARD[3] = {96, 64, 511};')]),
}

if sys.argv[1] == 'list':
    print(' '.join(MUTANTS))
    sys.exit(0)
f, pairs = MUTANTS[sys.argv[1]]
with open(sys.argv[2], 'w', encoding='utf-8') as out:
    out.write('FILE = %r\nPAIRS = %r\n' % (f, pairs))
