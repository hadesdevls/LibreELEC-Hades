# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of libplacebo (rg_libplacebo edit file) for the profile 8.4 composer test: a base layer in HLG refused as
# any transfer that clause 5.2.2 a) does not name, whatever its RPU declares, as the composer did before
# libplacebo-9975. The checks of p84-hlg-compose that tell HLG apart must fail with it.
FILE = 'src/renderer.c'
PAIRS = [
    ('    case PL_COLOR_TRC_HLG:\n        // Hades: a base layer in HLG (profile 8.4)',
     '    case PL_COLOR_TRC_HLG:\n'
     '        return "a base layer whose transfer is neither SMPTE ST 2084 nor Recommendation "\n'
     '               "ITU-R BT.1886 (clause 5.2.2 a))";\n'
     '        // Hades: a base layer in HLG (profile 8.4)'),
]
