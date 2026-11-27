# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): Cb and Cr swapped where the passes of
# the IPT tunnel read the chroma of a pair (three places in the fragment passes, two in the compute pass). The
# comparison with the float64 oracle must fail.
PAIRS = [
    ('"        c = vec2(pair(n, q.y));\\n"', '"        c = vec2(pair(n, q.y)).yx;\\n"'),
    ('"            ivec2 t = ivec2(pair(n - 5 + k, q.y) & 0xFFFFu);\\n"',
     '"            ivec2 t = ivec2(pair(n - 5 + k, q.y) & 0xFFFFu).yx;\\n"'),
    ('vec2(pair(q.x, r)));\\n"', 'vec2(pair(q.x, r)).yx);\\n"'),
    ('"    ivec4 odd = clamp((acc + (1 << 13)) >> 14, ivec4(0), ivec4(4095));\\n"',
     '"    ivec4 odd = clamp((acc + (1 << 13)) >> 14, ivec4(0), ivec4(4095)).yxwz;\\n"'),
    ('"    uvec4 even = texelFetch(chroma_rows, ivec2(clamp(q.x, pairs.x, pairs.y), q.y), 0);\\n"',
     '"    uvec4 even = texelFetch(chroma_rows, ivec2(clamp(q.x, pairs.x, pairs.y), q.y), 0).yxwz;\\n"'),
]
