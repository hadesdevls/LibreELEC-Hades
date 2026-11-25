# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_lut.c for lut3d_test.c (rg_edit): the input of a LUT of video levels taken as
# full range (the output still converted). The pictures of the .cube read in video levels and of madVR's must differ.
PAIRS = [
    ("    if (!full(lut->in))\n        at += snprintf(", "    if (0)\n        at += snprintf("),
]
