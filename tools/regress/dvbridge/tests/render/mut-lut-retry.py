# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for lut3d_test.c (rg_edit): a render with a LUT that fails is not made
# again without it. The picture is then lost, and the renderer does not say that the LUT failed.
PAIRS = [
    ("    if (ok || !r->lut_tried)\n        return false;\n    r->lut_failed = true;",
     "    if (ok || !r->lut_tried || r->lut)\n        return false;\n    r->lut_failed = true;"),
]
