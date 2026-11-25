# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for sdrgamut_test.c (rg_edit): HDR mapped to SDR into BT.709 and then
# put in the wider container that the display is told (the gamut of the display BT.709 whatever the container), as a
# "BT.709 in BT.2020" output does. The test must see that the wide containers then hold nothing outside BT.709.
PAIRS = [
    ("""    if (m->gamut == DVBRIDGE_HDR10_GAMUT_CUSTOM)
        dvbridge_hdr10_custom_xy(m, xy);
    else
        dvbridge_hdr10_gamut_xy(m->gamut, xy);
    const bool sdr = m->target == DVBRIDGE_HDR10_TARGET_SDR;""",
     """    if (m->gamut == DVBRIDGE_HDR10_GAMUT_CUSTOM)
        dvbridge_hdr10_custom_xy(m, xy);
    else
        dvbridge_hdr10_gamut_xy(m->target == DVBRIDGE_HDR10_TARGET_SDR ? DVBRIDGE_HDR10_GAMUT_BT709 : m->gamut, xy);
    const bool sdr = m->target == DVBRIDGE_HDR10_TARGET_SDR;"""),
]
