# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): the picture taken to the LMS of the tunnel
# through the RGBtoLMS of BT.2020 whatever its RPU has, as before profile 5 went into the tunnel. A profile 5 picture is
# then off by far more than a code from the float64 conversion of its codes: ipt-p5 must fail.
PAIRS = [
    ('        lsrc[i] = src->lms_rpu[i] / 16384.0;',
     '        lsrc[i] = (int[])DVBRIDGE_LMS_2020[i] / 16384.0;'),
]
