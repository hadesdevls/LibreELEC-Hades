# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of CDrainRestart: a packet that comes while the decoder has been drained and has not given its end goes to
# the decoder at once (Arrived()) instead of waiting for it.
FILE = 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DrainRestart.h'
PAIRS = [
    ('        return Arrival::WAIT;\n', '        return Arrival::TAKE;\n'),
]
