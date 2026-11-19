# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# A mutant of the join of two tracks: a Matroska Dolby Vision group of libavformat the wrong way round (the record on
# the base track alone, or a one-track file with a second HEVC track) is refused for its size, as before that rule,
# instead of being left to the rule of the Matroska tracks that libavformat does not group (CDVLayerMerge::Find()).
FILE = 'xbmc/cores/VideoPlayer/DVDDemuxers/DVLayerMerge.cpp'
PAIRS = [
    ('        if (container == Container::MATROSKA &&\n            EnhancementSize(e->width, e->height, b->width, b->height))\n',
     '        if (false && container == Container::MATROSKA &&\n            EnhancementSize(e->width, e->height, b->width, b->height))\n'),
]
