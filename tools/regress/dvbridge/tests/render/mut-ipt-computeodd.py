# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# A mutant of tools/dvbridge/dvbridge_source.c for the IPT tunnel (rg_edit): the compute pass of pass 9 (9c) rounds the
# chroma of the odd pixels down (the filter's sum without its half), where the fragment pass rounds it to the nearest.
# Its codes must differ from those of the fragment passes (ipt-forms).
PAIRS = [
    ('"    ivec4 odd = clamp((acc + (1 << 13)) >> 14, ivec4(0), ivec4(4095));\\n"',
     '"    ivec4 odd = clamp(acc >> 14, ivec4(0), ivec4(4095));\\n"'),
]
