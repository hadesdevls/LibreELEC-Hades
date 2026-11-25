# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d.frag for lutsdr_test.c (rg_edit): the shaper of two slopes of a .3dl
# of Nuke's left out (its grid taken as uniform). The picture of nuke17.3dl must differ from the LUT applied in Python.
PAIRS = [
    ("  if (m_lut3dShaper.x > 0.0)\n", "  if (false)\n"),
]
