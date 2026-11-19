# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# The control of lp-mut-4096.py (rg_libplacebo edit file): the same +4096 and -4096, with the rows of the enhancement
# layer kept in 32-bit floats instead of half floats (libplacebo-9971 taken out of its one call). 32-bit floats hold
# every value of those rows exactly, so the composed planes must be the same bytes as without any change: the
# difference that lp-mut-4096.py makes comes from the half floats alone.
FILE = 'src/renderer.c'
PAIRS = [
    ('color[%d] = float(clamp(v, 0, 65535));', 'color[%d] = float(clamp(v, 0, 65535) + 4096);'),
    ('#define DV_ELV(k) ivec2(texelFetch("$", ivec2(clamp(n + (k), 0, %d), p.y), 0).xy) \\n',
     '#define DV_ELV(k) (ivec2(texelFetch("$", ivec2(clamp(n + (k), 0, %d), p.y), 0).xy) - 4096) \\n'),
    ('return ccm_end(pass, sh, luma ? PL_DEF(x->fmt1h, x->fmt1) : PL_DEF(x->fmt2h, x->fmt2),',
     'return ccm_end(pass, sh, luma ? x->fmt1 : x->fmt2,'),
]
