# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# lp-dump-hook.py and a mutant (rg_libplacebo edit file): a base layer in BT.1886 composed as if it were in SMPTE
# ST 2084, without the 14 bits of 5.4.3.3, the conversion of clause 5.5 or its refusals (the composer before
# libplacebo-9955). The checks of p82-run that tell BT.1886 apart must fail with it.
import os
import runpy

FILE = 'src/renderer.c'
PAIRS = runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lp-dump-hook.py'))['PAIRS'] + [
    ('    case PL_COLOR_TRC_BT_1886:\n        return ccm_check_bt1886(pass, x);\n',
     '    case PL_COLOR_TRC_BT_1886:\n        break;\n'),
]
