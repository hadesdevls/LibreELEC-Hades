# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_gl_part.c for stereo3d_test.c (rg_edit): every part copied from the corner of the
# picture, the left or top view whichever is asked. The right eye then shows the left view: stereo3d_test.c must see
# it in every right eye (side by side, frame packing, the cropped one, BT.2020, BT.709).
PAIRS = [
    ("""        copy(planes[i].texture, GL_TEXTURE_2D, 0, x / div, y / div, 0, part->textures[i], GL_TEXTURE_2D, 0, 0, 0,""",
     """        copy(planes[i].texture, GL_TEXTURE_2D, 0, 0 * x / div, 0 * y / div, 0, part->textures[i], GL_TEXTURE_2D, 0, 0, 0,"""),
]
