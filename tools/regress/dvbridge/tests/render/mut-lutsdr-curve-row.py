# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d_curve.frag for lutsdr_test.c (rg_edit): every point of a 1D LUT read
# from the first row of its texture. The picture of the 1D LUT of 5000 points (two rows) must differ from it applied in
# Python; those of a row alone not.
PAIRS = [
    ("vec2(col + 0.5, row + 0.5) * m_lut3dCurve.zw", "vec2(col + 0.5, 0.5) * m_lut3dCurve.zw"),
]
