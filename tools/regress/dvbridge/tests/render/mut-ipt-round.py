# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Mutant of tools/dvbridge/dvbridge_source.c (stage ipt, ipt-mutants): pass 9 rounds the I of a pixel up from
# 0.49999 of a code in place of 0.5, so a few codes of each picture move by one. ipt-bytes sees every one of them (on
# pictures 51 and 960, where the mutants run, the shares of the codes off that the oracle counts move as well).
PAIRS = [
    ('"    return clamp(floor(i * 4096.0 + 0.5), 0.0, 4095.0);\\n"',
     '"    return clamp(floor(i * 4096.0 + 0.49999), 0.0, 4095.0);\\n"'),
]
