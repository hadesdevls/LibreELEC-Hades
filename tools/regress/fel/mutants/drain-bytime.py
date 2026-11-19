# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of CDrainRestart: the pictures left out after a restart are known by the times of the packets given again
# (GiveAgain()), as before the mark, instead of by the mark of the packet each picture was made from (Decoded()): a
# picture of a packet that came after the drain with the time of one given again is left out too.
FILE = 'xbmc/cores/VideoPlayer/DVDCodecs/Video/DrainRestart.h'
PAIRS = [
    ('      k.packet->opaque_ref = m_mark ? av_buffer_ref(m_mark) : nullptr;\n',
     '      k.packet->opaque_ref = m_mark ? av_buffer_ref(m_mark) : nullptr;\n      m_made.push_back(k.time);\n'),
    ('    if (mark && m_mark && mark->data == m_mark->data)\n      m_made.push_back(time);\n',
     '    (void)mark;\n    (void)time;\n'),
]
