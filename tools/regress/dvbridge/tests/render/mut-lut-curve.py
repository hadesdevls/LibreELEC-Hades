# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_lut.c for lut3d_test.c (rg_edit): the 1D LUT of a LUT left out (a .cal, a .cube
# of LUT_1D_SIZE alone, Resolve's shaper). The pictures of the 1D LUTs must differ from them applied in Python.
PAIRS = [
    ("    if (lut->curve && lut->curve_size >= 2 && !curve_shader(gpu, sh, lut, state))",
     "    if (0 && lut->curve && lut->curve_size >= 2 && !curve_shader(gpu, sh, lut, state))"),
]
