# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the GUI cache (rg_edit): the tiles of the first row of the raster,
# whose pixels carry the Dolby Vision metadata of each frame, may be copied from the last frame like any other under
# an opaque GUI, so a frame goes out with the metadata of an earlier one. The cache's own check (the copied tiles
# against the same tiles packed again) must find them.
PAIRS = [
    ('"    bool can = allowed != 0 && t.y > 0 && opaque > 0.5 && changed < 0.5;\\n"',
     '"    bool can = allowed != 0 && opaque > 0.5 && changed < 0.5;\\n"'),
]
