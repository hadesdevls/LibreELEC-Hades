# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_ipt.h for the IPT tunnel (rg_edit): one coefficient of the YCCtoRGB matrix of
# the tunnel (those of a licensed core) that the tunnel inverts, -940 instead of -933. The comparison with the float64 oracle must fail.
PAIRS = [
    ('#define DVBRIDGE_IPT_YCC {8194, 799, 1681, 8194, -933, 1091, 8194, 267, -5545}',
     '#define DVBRIDGE_IPT_YCC {8194, 799, 1681, 8194, -940, 1091, 8194, 267, -5545}'),
]
