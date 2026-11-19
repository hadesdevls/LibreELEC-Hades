# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A shader of tools/dvbridge/pack_gles300.frag without the fallback fetch of the even pixel in its quad form (the
# line that the render test edits to make its strict packer): the test must stop, never draw with Kodi's packer in
# place of the strict one.
PAIRS = [
    ('            e = clamp(picture_alone(pe), 0.0, 1.0);\n',
     '            e = clamp(picture_alone(pe), 0.0, 1.0) ;\n'),
]
