# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c (rg_edit): FB_IPT=1 asks the bridge for the IPT tunnel
# (dvbridge_render_use_ipt(), as Kodi's setting asks for it); without it, the YCbCr one, as before.
PAIRS = [
    ('  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n',
     '  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n'
     '  dvbridge_render_use_ipt(g.renderer, envi("FB_IPT", 0) != 0);\n'
     '  printf("IPT tunnel asked: %d\\n", envi("FB_IPT", 0) != 0);\n'),
]
