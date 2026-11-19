# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutants of the menus in Dolby Vision (stage menus of run-dvbridge-meta.sh): the metadata of a frame of graphics alone
# (dvbridge_graphics.c), the decisions of the menus (DVMenus.h) and Kodi's GUI composite shader that draws them
# (system/shaders/GLES/2.0/gles_gui_composite.frag). Expected: "-" (the test fails).
DIRS = ['tools/dvbridge', 'xbmc/windowing/gbm', 'system/shaders/GLES/2.0']
MUTANTS = [
    ('meta-avg-max', 'tools/dvbridge/dvbridge_graphics.c',
     [('    range->avg = (range->source_min + range->source_max) / 2;', '    range->avg = range->source_max;')],
     'meta', '-', 'the level 1 average of a frame of graphics alone at its maximum'),
    ('unit-no-wait', 'xbmc/windowing/gbm/DVMenus.h',
     [('    if (waiting)\n      return Why::FAILED;', '    if (false && waiting)\n      return Why::FAILED;')],
     'unit', '-', 'no wait after the menus failed'),
    ('render-matrix', 'system/shaders/GLES/2.0/gles_gui_composite.frag',
     [('  0.627403896, 0.069097289, 0.016391439,', '  0.627403896, 0.069097289, 0.026391439,')],
     'render', '-', 'the exact BT.709 to BT.2020 conversion of the GUI composite with a coefficient off'),
]
