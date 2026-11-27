# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of libplacebo (rg_libplacebo edit file) for the fidelity bench: the rows of the enhancement layer that the
# vertical pass writes (libplacebo-9970) get 4096 added, and the horizontal pass takes it away again. With the rows
# in half floats (libplacebo-9971) the values above 2048 lose their low bits, so the composed planes must change:
# the bench sees what the precision of those rows does. (The horizontal pass reads the rows with & 0xFFFF since
# libplacebo-9988, which leaves every value the vertical pass writes as it is; with 4096 added they may pass 2^16,
# so the mutant takes the mask out where it takes the 4096 away.)
FILE = 'src/renderer.c'
PAIRS = [
    ('color[%d] = float(clamp(v, 0, 65535));', 'color[%d] = float(clamp(v, 0, 65535) + 4096);'),
    ('#define DV_ELV(k) (ivec2(texelFetch("$", ivec2(clamp(n + (k), 0, %d), p.y), 0).xy) & 0xFFFF) \\n',
     '#define DV_ELV(k) (ivec2(texelFetch("$", ivec2(clamp(n + (k), 0, %d), p.y), 0).xy) - 4096) \\n'),
]
