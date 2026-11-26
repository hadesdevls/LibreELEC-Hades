# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): the EOTF of pass 9 reads, from 2^-6 on,
# the cell of its table after the right one at the place in the right one (the EOTF of e + 1/4096). The comparison
# with the float64 oracle must fail (more than one code).
PAIRS = [
    ('"    int j = uni ? ju : zero ? 0 : int(b >> 15u) - 22784;\\n"',
     '"    int j = uni ? min(ju + 1, 4095) : zero ? 0 : int(b >> 15u) - 22784;\\n"'),
]
