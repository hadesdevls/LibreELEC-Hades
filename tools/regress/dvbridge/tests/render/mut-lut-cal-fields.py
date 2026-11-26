# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_lut.c for lut3d_test.c (rg_edit): the red of a .cal taken from the column of
# RGB_G (its fields by another name). The points of its 1D LUT and its pictures must differ from those of its file.
PAIRS = [
    ('        *at_r = is(&w, "RGB_R") ? *fields : *at_r;', '        *at_r = is(&w, "RGB_G") ? *fields : *at_r;'),
]
