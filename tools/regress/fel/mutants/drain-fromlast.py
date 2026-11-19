# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of CDrainRestart: every packet taken counts as a random-access point (Taken()), so only the last packet is
# kept and given again after a drain.
FILE = 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DrainRestart.h'
PAIRS = [
    ('    if (RandomAccess(packet.data, packet.size))\n', '    if (true)\n'),
]
