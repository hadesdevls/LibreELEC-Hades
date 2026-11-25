# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for sdrgamut_test.c (rg_edit): the signal of an SDR target written in
# BT.709 whatever its container (the display's gamut the container's): the picture is not in the primaries the
# display is told. The test must see that the DCI-P3 and the BT.2020 pictures then disagree.
PAIRS = [
    (".primaries = sdr ? dvbridge_sdr_primaries(m) : PL_COLOR_PRIM_BT_2020,",
     ".primaries = sdr ? PL_COLOR_PRIM_BT_709 : PL_COLOR_PRIM_BT_2020,"),
]
