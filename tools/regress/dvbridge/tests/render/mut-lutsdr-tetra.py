# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of system/shaders/GLES/2.0/gles_lut3d.frag for lutsdr_test.c (rg_edit): the tetrahedron where red's fraction
# is the largest and blue's the smallest takes another corner (red and blue) for the one of red and green: an
# interpolation that is not libplacebo's, which an identity LUT and every known LUT must show.
PAIRS = [
    ("      v = (1.0 - f.r) * c0 + (f.r - f.g) * KodiLut3dAt(base + vec3(1.0, 0.0, 0.0)) +\n"
     "          (f.g - f.b) * KodiLut3dAt(base + vec3(1.0, 1.0, 0.0)) + f.b * c3;",
     "      v = (1.0 - f.r) * c0 + (f.r - f.g) * KodiLut3dAt(base + vec3(1.0, 0.0, 0.0)) +\n"
     "          (f.g - f.b) * KodiLut3dAt(base + vec3(1.0, 0.0, 1.0)) + f.b * c3;"),
]
