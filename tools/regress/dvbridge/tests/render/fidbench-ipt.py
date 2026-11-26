# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c (rg_edit): FB_IPT=1 asks the bridge for the IPT tunnel
# (dvbridge_render_use_ipt(), as Kodi's setting asks for it); without it, the YCbCr one, as before. FB_COMPUTE=1 asks
# for the compute forms of the source transport (dvbridge_render_use_compute_rows(), as Kodi's dvcomputerows does by
# default), and each direct way says whether the conversion to IPT was made by its compute pass ("picture <n>: IPT
# <0|1>, computed <0|1>").
PAIRS = [
    ('  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n',
     '  dvbridge_render_set_step(g.renderer, envi("FB_STEP", 0));\n'
     '  dvbridge_render_use_ipt(g.renderer, envi("FB_IPT", 0) != 0);\n'
     '  printf("IPT tunnel asked: %d\\n", envi("FB_IPT", 0) != 0);\n'
     '  dvbridge_render_use_compute_rows(g.renderer, envi("FB_COMPUTE", 0) != 0);\n'
     '  printf("compute forms asked: %d\\n", envi("FB_COMPUTE", 0) != 0);\n'),
    ('    if (direct)\n      WriteFile(dir, tag, number, "direct.rgba", g->bytes, (size_t)W * H * 4);\n',
     '    if (direct)\n      WriteFile(dir, tag, number, "direct.rgba", g->bytes, (size_t)W * H * 4);\n'
     '    printf("picture %u: IPT %d, computed %d\\n", number, dvbridge_render_ipt(g->renderer),\n'
     '           dvbridge_render_ipt_computed(g->renderer));\n'),
]
