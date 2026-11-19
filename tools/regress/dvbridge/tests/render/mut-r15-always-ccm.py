# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for the render test (rg_edit): render() says that every picture was
# composed by the composer of CCM 001, also those composed by libplacebo's reshaping. Its R15 check must see it.
PAIRS = [
    ('    r->composed = !r->ready      ? DVBRIDGE_COMPOSED_NONE\n'
     '                  : ccm_composed ? DVBRIDGE_COMPOSED_CCM\n'
     '                                 : DVBRIDGE_COMPOSED_RESHAPING;',
     '    r->composed = !r->ready ? DVBRIDGE_COMPOSED_NONE : DVBRIDGE_COMPOSED_CCM;'),
]
