# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of CDrainRestart: no packet is a random-access point (RandomAccess(), in both framings), so nothing is kept
# and the decoder starts again after a drain at the packet that came, as before CDrainRestart.
FILE = 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DrainRestart.h'
PAIRS = [
    ('\n          return type >= 16 && type <= 23;\n', '\n          return false;\n'),
    ('\n        return type >= 16 && type <= 23;\n', '\n        return false;\n'),
]
