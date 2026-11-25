# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d.frag for lutsdr_test.c (rg_edit): an output of limited range loses
# what is below its black and above its white (held at them by the LUT). An identity LUT on such an output and the
# known LUT there must differ.
PAIRS = [
    ("  highp vec3 beyond = m_lut3dSignal.x < 1.0 ? full - c : vec3(0.0);",
     "  highp vec3 beyond = vec3(0.0);"),
]
