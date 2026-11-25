# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d.frag for lutsdr_test.c (rg_edit): the input of a LUT of video levels
# taken as full range (the output still converted). The pictures of the .cube read in video levels and of madVR's must
# differ from the LUT applied in Python.
PAIRS = [
    ("  c = c * m_lut3dIn.x + m_lut3dIn.y;\n", "  c = c;\n"),
]
