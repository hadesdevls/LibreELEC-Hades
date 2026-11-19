# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_render.c for the profile 8.4 render test (rg_edit): a base layer in HLG is
# not said to libplacebo (the transfer left unknown), so it is composed as if it were in SMPTE ST 2084 whatever its
# RPU declares, as the bridge did before profile 8.4 was taken. The refusal of an RPU that declares another signal
# must then be gone.
PAIRS = [
    ('           : r->base_transfer == DVBRIDGE_BASE_HLG ? PL_COLOR_TRC_HLG\n',
     '           : r->base_transfer == DVBRIDGE_BASE_HLG ? PL_COLOR_TRC_UNKNOWN\n'),
]
