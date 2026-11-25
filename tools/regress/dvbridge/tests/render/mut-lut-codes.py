# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for lut3d_test.c (rg_edit): a picture kept as 10-bit codes quantized
# before the LUT, not after it. The codes of the picture with the LUT must differ from the LUT applied to the signal.
PAIRS = [
    ("return dvbridge_lut_shader(p->sh, lut, state) && (!codes || pl_shader_custom(p->sh, &quantize));",
     "return (!codes || pl_shader_custom(p->sh, &quantize)) && dvbridge_lut_shader(p->sh, lut, state);"),
]
