# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_lut.c for lut3d_test.c (rg_edit): the values of a .3dl taken in the order of the
# file (blue fastest) as the table's (red fastest). The grid points of the .3dl and its pictures must differ.
PAIRS = [
    ("const size_t from = ((size_t)r * n + g) * n + b, to = ((size_t)b * n + g) * n + r;",
     "const size_t from = ((size_t)r * n + g) * n + b, to = from;"),
]
