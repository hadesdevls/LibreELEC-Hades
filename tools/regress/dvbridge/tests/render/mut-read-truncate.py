# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for r8-planes (rg_edit): every code that the source transport reads
# from the composed planes through code_scale cut off (floor(v * 2^bits)) instead of rounded (floor(v * 2^bits +
# 0.5)). With the planes in 32-bit floats a code is read exactly either way; in 16 bits it is stored within 1/16 of a
# code below or above, so cutting it off gives another code: the comparison of the two sides must see it.
PAIRS = [
    ('"        acc += weights[i] * ivec2(floor(v * code_scale + 0.5));\\n"',
     '"        acc += weights[i] * ivec2(floor(v * code_scale));\\n"'),
    ('"        acc += w[i] * ivec2(floor(v * code_scale + 0.5));\\n"',
     '"        acc += w[i] * ivec2(floor(v * code_scale));\\n"'),
    ('"    uint v = uint(floor(texelFetch(luma_plane, q, 0).r * code_scale + 0.5));\\n"',
     '"    uint v = uint(floor(texelFetch(luma_plane, q, 0).r * code_scale));\\n"'),
    ('"                                         code_scale + 0.5)) : uint(black.x);\\n"',
     '"                                         code_scale)) : uint(black.x);\\n"'),
]
