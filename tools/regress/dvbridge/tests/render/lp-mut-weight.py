# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of libplacebo (rg_libplacebo edit file) for the fidelity bench: a weight of the luma filter of annex B.3
# in the vertical pass over the enhancement layer (src/renderer.c of the patched source), 110 instead of 111. The
# composed planes of the source transport must change.
FILE = 'src/renderer.c'
PAIRS = [
    ('"ivec4 w = odd ? ivec4(-9, 111, 29, -3)', '"ivec4 w = odd ? ivec4(-9, 110, 29, -3)'),
]
