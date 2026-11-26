# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): the OETF of pass 9 without the low part
# of its values (L' at the start of a cell as one float, hi + lo as before). Every code stays within one of the
# float64 oracle; the shares of codes off grow, which the reference of ipt-oracle (IPT_F64) must see.
PAIRS = [
    ('"    return vec2(q.x, q.y + t * (q.z + t * q.w));\\n"', '"    return vec2(q.x, t * (q.z + t * q.w));\\n"'),
]
