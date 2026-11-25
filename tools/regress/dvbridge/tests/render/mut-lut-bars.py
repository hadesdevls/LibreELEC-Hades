# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for lut3d_test.c (rg_edit): Low-Latency without the bars put back to
# code 0 after the LUT. Where the bars are not written first, the LUT lights them with the colour it maps black to.
PAIRS = [
    ("mask->lut_called = dvbridge_lut_shader(p->sh, mask->lut, mask->lut_state) && pl_shader_custom(p->sh, &bars);",
     "mask->lut_called = dvbridge_lut_shader(p->sh, mask->lut, mask->lut_state) && (bars.body != NULL);"),
]
