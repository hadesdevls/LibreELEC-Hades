# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_gl_frame.c for the padded layers (rg_edit): a plane in a texture larger than
# its picture is imported as if it were of the picture's size. The padded pictures must then be drawn, not refused.
PAIRS = [
    ('        if (!planes[i].texture || planes[i].width != pw || planes[i].height != ph)',
     '        if (!planes[i].texture)'),
]
