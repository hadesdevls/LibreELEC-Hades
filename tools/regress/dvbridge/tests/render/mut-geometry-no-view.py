# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_core.h for geom_bench (rg_edit): a geometry never has a view, so the bridge
# draws the whole picture at its place whatever part of it was asked for (the bridge before the view was drawn
# natively). geom_bench's check against the reference of the view asked for must fail.
PAIRS = [
    ('    return g->view[0] != 0.0f || g->view[1] != 0.0f || g->view[2] != 0.0f || g->view[3] != 0.0f;',
     '    return false;'),
]
