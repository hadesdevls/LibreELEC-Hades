# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_ipt.h for the IPT tunnel (rg_edit): one coefficient of the YCCtoRGB matrix of
# profile 5 that the tunnel inverts, -940 instead of -933. The comparison with the float64 oracle must fail.
PAIRS = [
    ('#define DVBRIDGE_IPT_YCC {8192, 799, 1681, 8192, -933, 1091, 8192, 267, -5545}',
     '#define DVBRIDGE_IPT_YCC {8192, 799, 1681, 8192, -940, 1091, 8192, 267, -5545}'),
]
