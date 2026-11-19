# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# An edit of a copy of src/fidbench.c (rg_edit): with FB_HDR10=1, the picture for output as HDR10 mapped as the
# settings map it before it is drawn: FB_SDR=1 to an SDR output (dvbridge_sdr_mapping(), the DCI-P3 mastering
# primaries of the stream's base layer), FB_PROJ=<peak> to a display of that peak in DCI-P3 with the BT.2390 curve
# (a projector's). Without either, the picture as it is.
PAIRS = [
    ('  const struct dvbridge_view whole = {0, 0, (float)w, (float)h, 0, 0, W, H};\n'
     '  const int ok = dvbridge_render_hdr10(',
     '  const struct dvbridge_view whole = {0, 0, (float)w, (float)h, 0, 0, W, H};\n'
     '  static int sdr = -1;\n'
     '  if (sdr < 0)\n'
     '  {\n'
     '    sdr = envi("FB_SDR", 0);\n'
     '    const int proj = envi("FB_PROJ", 0);\n'
     '    if (proj > 0)\n'
     '    {\n'
     '      struct dvbridge_hdr10_mapping m = {0};\n'
     '      m.enabled = true;\n'
     '      m.peak = (float)proj;\n'
     '      m.gamut = DVBRIDGE_HDR10_GAMUT_P3_D65;\n'
     '      m.curve = DVBRIDGE_HDR10_CURVE_BT2390;\n'
     '      m.source_known = dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, m.source_xy);\n'
     '      if (!dvbridge_render_hdr10_mapping(g->renderer, &m))\n'
     '      {\n'
     '        printf("FAIL: the mapping to a display of %d cd/m2 is not taken\\n", proj);\n'
     '        ++g_failures;\n'
     '      }\n'
     '      else\n'
     '        printf("the pictures are mapped to a display of %d cd/m2 in DCI-P3 (BT.2390)\\n", proj);\n'
     '    }\n'
     '    else if (sdr)\n'
     '    {\n'
     '      float xy[8];\n'
     '      dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, xy);\n'
     '      const struct dvbridge_hdr10_mapping m = dvbridge_sdr_mapping(xy);\n'
     '      if (!dvbridge_render_hdr10_mapping(g->renderer, &m))\n'
     '      {\n'
     '        printf("FAIL: the SDR mapping is not taken\\n");\n'
     '        ++g_failures;\n'
     '      }\n'
     '      else\n'
     '        printf("the pictures are mapped to SDR (P3 mastering primaries)\\n");\n'
     '    }\n'
     '  }\n'
     '  const int ok = dvbridge_render_hdr10('),
]
