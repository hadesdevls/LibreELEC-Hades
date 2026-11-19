# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of libplacebo (rg_libplacebo edit file) for the MMR table of the composer (libplacebo-9977, src/dovi_ccm.c):
# the integer sum q of the terms of the Cb axis one step of v too high (2^20) in every entry. With the table on, the
# composed chroma must change; with it off (the default), nothing.
FILE = 'src/dovi_ccm.c'
PAIRS = [
    ('                    row[4 * s + 0] = q;\n',
     '                    row[4 * s + 0] = q + (a == 1 ? 1u << 20 : 0u);\n'),
]
