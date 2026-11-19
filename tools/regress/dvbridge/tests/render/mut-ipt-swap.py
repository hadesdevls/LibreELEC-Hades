# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): Cb and Cr swapped where the pass of
# the IPT tunnel reads the chroma of a pair (three places). The comparison with the float64 oracle must fail.
PAIRS = [
    ('"        c = vec2(pair(n, q.y));\\n"', '"        c = vec2(pair(n, q.y)).yx;\\n"'),
    ('"            acc += hweights[k] * ivec2(pair(n - 5 + k, q.y));\\n"',
     '"            acc += hweights[k] * ivec2(pair(n - 5 + k, q.y)).yx;\\n"'),
    ('vec2(pair(q.x, r)));\\n"', 'vec2(pair(q.x, r)).yx);\\n"'),
]
