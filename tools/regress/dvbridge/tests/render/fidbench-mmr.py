# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c (rg_edit): FB_MMRT=1 turns on the MMR table of the composer
# (dvbridge_render_use_mmr_table(), advancedsettings.xml video/dvmmrtable in Kodi, off by default), and says whether
# the renderer has it on.
PAIRS = [
    ('  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n',
     '  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n'
     '  if (envi("FB_MMRT", 0))\n'
     '    dvbridge_render_use_mmr_table(g.renderer, true);\n'
     '  printf("MMR table: %s\\n", dvbridge_render_mmr_table(g.renderer) ? "on" : "off");\n'),
]
