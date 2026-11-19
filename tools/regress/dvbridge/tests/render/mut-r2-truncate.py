# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for the render test (rg_edit): the codes of the picture for output as
# HDR10 cut off instead of rounded (QUANTIZE_CODES without its +0.5). Its R2 check must see it.
PAIRS = [
    ('floor(color.rgb * dvbridge_levels + 0.5) + 0.00390625', 'floor(color.rgb * dvbridge_levels) + 0.00390625'),
]
