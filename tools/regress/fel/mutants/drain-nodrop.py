# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of CDrainRestart: no picture made from a packet given again is left out (Drop()), so the pictures that went
# out before the drain go out twice.
FILE = 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DrainRestart.h'
PAIRS = [
    ('      if (*it == time)\n', '      if (false && *it == time)\n'),
]
