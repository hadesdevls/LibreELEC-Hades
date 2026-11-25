# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d.frag for lutsdr_test.c (rg_edit): a shader made with the LUT applies it
# even where it is told not to (a picture drawn elsewhere than into the window). That picture must differ from the one
# without the LUT, on every way.
PAIRS = [
    ("  if (m_lut3dSignal.z < 0.5)\n    return signal;\n", ""),
]
