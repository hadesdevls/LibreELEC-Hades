# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A shader of tools/dvbridge/pack_gles300.frag with the line that the render test edits there twice (the fetch
# without quads given the twelve spaces of the quad form): the test must stop.
PAIRS = [
    ('        e = clamp(picture_alone(pe), 0.0, 1.0);\n#endif',
     '            e = clamp(picture_alone(pe), 0.0, 1.0);\n#endif'),
]
