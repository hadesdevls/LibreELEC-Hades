# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_lut_atlas.c for lutsdr_test.c (rg_edit): each slice of blue put one column of
# slices further on (within the texture), where the shader does not read it (dvbridge_lut_atlas_slice(), which both the
# copy of the table and the slices put straight from the LUT take). The grid points of the table and every picture with
# a LUT must differ.
PAIRS = [
    ("    *x0 = (b % atlas->cols) * atlas->size;",
     "    *x0 = ((b + 1) % atlas->cols) * atlas->size;"),
]
