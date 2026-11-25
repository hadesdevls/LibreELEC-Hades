# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for lut3d_test.c (rg_edit): the LUT applied to HDR over the tunnel too.
# The picture of the tunnel with a LUT set must then differ from the one without.
PAIRS = [
    ("r->lut_tried = !tunnel && r->lut && !r->lut_failed;", "r->lut_tried = r->lut && !r->lut_failed;"),
]
