# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_metadata.h for margins_test.c (rg_edit): the level 5 active area computed as if
# the whole picture were drawn, the part of it that is drawn (the view) never taken into account, in both readings of
# the offsets. margins_test.c must fail where a view moves the margins.
PAIRS = [
    ('    if (d->has_view) {\n        v[0]=d->dest_x+dvbridge_dv_through_view',
     '    if (0) {\n        v[0]=d->dest_x+dvbridge_dv_through_view'),
    ('            if (d->has_view) {\n                const double at = left / s',
     '            if (0) {\n                const double at = left / s'),
]
