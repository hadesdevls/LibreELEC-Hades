# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_lut.c for lut3d_test.c (rg_edit): the green of a 1D LUT read from the curve of
# red at the upper point. The pictures of the 1D LUTs must differ from them applied in Python.
PAIRS = [
    ('"                  texelFetch(dvbridge_curve, ivec2(lut_j.g %% %d, lut_j.g / %d), 0).g, lut_f.g);\\n"',
     '"                  texelFetch(dvbridge_curve, ivec2(lut_j.g %% %d, lut_j.g / %d), 0).r, lut_f.g);\\n"'),
]
