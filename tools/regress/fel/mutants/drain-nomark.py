# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of CDrainRestart: every picture the decoder makes counts as one made from a packet given again
# (Decoded() without the mark), so a picture is left out by its time alone, as before the mark.
FILE = 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DrainRestart.h'
PAIRS = [
    ('    if (mark && m_mark && mark->data == m_mark->data)\n', '    if (true)\n'),
]
