# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# tools/dvbridge/dvbridge_render.c without the composed planes in 16 bits (rg_edit): PL_DOVI_CCM_PLANES_UNORM16
# (libplacebo-9960) undefined before the one place that tests it, so the bridge asks for the planes in R32F/RG32F as
# before 9960. Not a mutant that must fail: the comparison of r8-planes, whose two sides must give the same bytes.
PAIRS = [
    ('#ifdef PL_DOVI_CCM_PLANES_UNORM16',
     '#undef PL_DOVI_CCM_PLANES_UNORM16 /* the bridge without the composed planes in 16 bits */\n#ifdef PL_DOVI_CCM_PLANES_UNORM16'),
]
