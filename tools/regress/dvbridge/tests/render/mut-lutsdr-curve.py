# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d.frag for lutsdr_test.c (rg_edit): the 1D LUT of a LUT left out in
# Kodi's shaders. The pictures of the 1D LUTs must differ from them applied in Python.
PAIRS = [
    ("  c = KodiLut3dCurve(c);\n", "  c = c;\n"),
]
