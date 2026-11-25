# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutants of the serializer, the HDR10+ InfoFrame, the source transport's pass 2 and the pack (mutants.py reads it):
# each takes out one thing that a test of run-dvbridge-meta.sh protects. The expected numbers of failed checks were
# measured on the bridge of the tree they were written for (Kodi with the level 5 rule of DVBRIDGE_SENT_NO_L5), and
# count the test's lines that start with FAIL (its last line, FAILED, among them); "-" is any number above 0.
DIRS = ['tools/dvbridge', 'xbmc/utils', 'xbmc/windowing/gbm']
D = 'tools/dvbridge/'
V = 'xbmc/utils/HDR10PlusVsif.h'
O = 'xbmc/windowing/gbm/HDR10PlusOutput.h'
MUTANTS = [
    # ------------------------------------------------------------------ the serializer (serializer_test.c)
    ('ser-no-reason', D + 'dvbridge_metadata.h',
     [('#define DV_REFUSE(x, why) do { if (!(x)) { d->refused = (why); return false; } } while (0)',
       '#define DV_REFUSE(x, why) do { if (!(x)) { d->refused = ""; return false; } } while (0)')],
     'serializer', '24', 'a refused frame says no reason (an earlier bridge gave none)'),
    ('ser-cm40-not-kept', D + 'dvbridge_metadata.h',
     [('                d->cm40_dropped = true;\n', '')],
     'serializer', '17', 'after a frame whose v4.0 part could not be carried, the next frames of the stream carry '
     'theirs again (switching between the two mappings from frame to frame)'),
    ('ser-cm40-no-why', D + 'dvbridge_metadata.h',
     [('                d->cm40_why = why;', '                d->cm40_why = NULL;')],
     'serializer', '10', 'the reading does not say why the v4.0 part was left out'),
    ('ser-l5-picture-not-read', D + 'dvbridge_metadata.h',
     [('    const bool picture = fits && d->l5_picture && d->l5_picture_video == d->cm40_video;',
       '    const bool picture = false;')],
     'serializer', '3', 'the level 5 offsets of an HD video measured as its own are read as before: the display is '
     'sent half of its bars'),
    ('ser-refresh-all-ignored', D + 'dvbridge_metadata.h',
     [('(uint8_t)(repeat ? d->previous[1] : refresh || d->refresh_all ? 1 : c->scene_refresh_flag);',
       '(uint8_t)(repeat ? d->previous[1] : refresh ? 1 : c->scene_refresh_flag);')],
     'serializer', '6', 'the switch that sends every frame as the first picture of a scene '
     '(video/dvscenerefreshall) changes nothing: the flag of the RPU goes out'),
    ('ser-refresh-all-not-kept', D + 'dvbridge_metadata.h',
     [('    d->refresh_all = refresh_all;\n', '')],
     'serializer', '2', 'a new stream forgets that switch: its frames go out with the flag of the RPU'),
    # (the form of the metadata, dvbridge_set_form(), and the matrices of the IPT tunnel)
    ('ser-form-not-by-level', D + 'dvbridge_metadata.h',
     [('    if (d->level_order)\n        DV_REFUSE(dvbridge_dv_order_by_level(d, count_pos),',
       '    if (false)\n        DV_REFUSE(dvbridge_dv_order_by_level(d, count_pos),')],
     'serializer', '4', 'the blocks are never put in the order of their level'),
    ('ser-form-l5-sent', D + 'dvbridge_metadata.h',
     [('            if (d->l5_form == DVBRIDGE_DV_L5_NONE) {\n                d->reading.l5_left_out = true;\n                continue;\n            }',
       '            if (d->l5_form == DVBRIDGE_DV_L5_NONE)\n                d->reading.l5_left_out = true;')],
     'serializer', '4', 'a level 5 block of the RPU is sent where none is to be'),
    ('ser-form-l5-made', D + 'dvbridge_metadata.h',
     [('    if (d->l5_form != DVBRIDGE_DV_L5_PLACED) {\n        d->reading.l5_left_out = true;',
       '    if (d->l5_form == DVBRIDGE_DV_L5_NONE) {\n        d->reading.l5_left_out = true;')],
     'serializer', '3', 'a level 5 block of the place is made where the RPU has none and the form makes none'),
    ('ser-form-l5-placed', D + 'dvbridge_metadata.h',
     [('            if (d->l5_form == DVBRIDGE_DV_L5_AS_GIVEN) {\n                v[0] = e->l5.left_offset;',
       '            if (false) {\n                v[0] = e->l5.left_offset;')],
     'serializer', '2', 'the level 5 block "as in the video" goes out as Hades places it'),
    ('ser-form-l254-kept', D + 'dvbridge_metadata.h',
     [('    wire[1] = 0;\n    return true;',
       '    return true;')],
     'serializer', '4', 'level 254 keeps its dm_version_index where 0 is asked for'),
    ('ser-form-l3-for-v40', D + 'dvbridge_metadata.h',
     [('d->l3_neutral && !rpu_v40 && !d->cm29_only',
       'd->l3_neutral && !d->cm29_only')],
     'serializer', '3', 'the neutral level 3 is added to an RPU of content mapping v4.0 too'),
    ('ser-form-no-l3', D + 'dvbridge_metadata.h',
     [('    if (d->l3_neutral && !rpu_v40',
       '    if (false && d->l3_neutral && !rpu_v40')],
     'serializer', '3', 'the neutral level 3 is never added'),
    ('ser-form-not-kept', D + 'dvbridge_metadata.h',
     [('    d->l5_form = l5_form;\n',
       '')],
     'serializer', '4', 'the form of the level 5 block is lost at a reset (dvbridge_reset())'),
    ('ser-ipt-rpu-matrices', D + 'dvbridge_ipt.h',
     [('#define DVBRIDGE_IPT_YCC {8194, 799, 1681, 8194, -933, 1091, 8194, 267, -5545}',
       '#define DVBRIDGE_IPT_YCC {8192, 799, 1681, 8192, -933, 1091, 8192, 267, -5545}'),
      ('#define DVBRIDGE_IPT_LMS {17080, -348, -348, -348, 17080, -348, -348, -348, 17080}',
       '#define DVBRIDGE_IPT_LMS {17081, -349, -349, -349, 17081, -349, -349, -349, 17081}')],
     'serializer', '3', "the IPT tunnel in the matrices of the RPUs of profile 5 (as before 2026-11), not the licensed core's"),
    # ------------------------------------------------------------------ HDR10+ (hdr10plus_vsif_test.cpp, hdr10plus_test.cpp, hdr10plus_gap_test.cpp)
    ('vsif-by-place', V,
     [('    if (w.distribution_maxrgb[entry].percentage != VSIF_PERCENTAGES[i])\n      return std::nullopt;',
       '    if (false)\n      return std::nullopt;')],
     'vsif', '13', 'the entries of the distribution of maxRGB taken by their places, not by their percentages'),
    ('vsif-sent-last', O,
     [('      if (m_recent[at] == *kernel)\n        return m_recentPayloads[at];',
       '      if (m_recent[at] == *kernel)\n        return m_queued;')],
     'vsif', '2', 'the payload said to be sent is the last one queued, not the one the kernel keeps'),
    ('h10-nothing-queued', O,
     [('    m_queued = payload;\n    m_recent[m_next] = value;', '    m_queued = std::nullopt;\n    m_recent[m_next] = value;')],
     'h10', '7', 'nothing is queued for a frame'),
    ('gap-no-hold', V,
     [('      if (++m_missing < MISSING_MAX)\n        return m_last;', '      if (++m_missing < MISSING_MAX)\n        return std::nullopt;')],
     'gap', '7', 'a picture without metadata in a stream with HDR10+ gets no InfoFrame (the last one is not held)'),
    ('gap-no-overlay', V,
     [('  p[26] = static_cast<uint8_t>(overlay ? (p[26] | 0x80) : (p[26] & ~0x80));',
       '  (void)overlay;\n  p[26] = static_cast<uint8_t>(p[26] & ~0x80);')],
     'gap', '1', 'the graphics overlay flag (PB27) is never set'),
    # ------------------------------------------------------------------ pass 2 of the source transport (rows_test.c)
    ('rows-odd-rounding', D + 'dvbridge_source.c',
     [('    "    a1 = (a1 + (1 << (chroma_shift - 1))) >> chroma_shift;\\n"',
       '    "    a1 = (a1 + (1 << (chroma_shift - 2))) >> chroma_shift;\\n"')],
     'rows', '127', 'the odd rows of 4:2:2 rounded otherwise than the integer filter of dvbridge_source.h'),
    # ------------------------------------------------------------------ what is drawn over the bars (cover_test_direct.c)
    ('cover-no-edges', D + 'dvbridge_source.c',
     [('            *edges |= (p->cover_read[t + i] >= 128 ? 1u : 0u) << i;',
       '            *edges |= 0u * (unsigned)p->cover_read[t + i] * (unsigned)i;')],
     'cover', '3', 'the measurement of what is drawn over the bars sees nothing: the level 5 block keeps the bars '
     'that the GUI covers'),
    # ------------------------------------------------------------------ the pack (pack_state_test.c)
    ('pack-no-restore', D + 'dvbridge_gl_pack.c',
     [('        glActiveTexture(GL_TEXTURE0);\n    }\n    glDrawArrays(GL_TRIANGLES, 0, 3);\n'
       '    ok = glGetError() == GL_NO_ERROR;\nrestore:\n    dvbridge_gl_state_restore(&state);\n',
       '        glActiveTexture(GL_TEXTURE0);\n    }\n    glDrawArrays(GL_TRIANGLES, 0, 3);\n'
       '    ok = glGetError() == GL_NO_ERROR;\nrestore:\n')],
     'pack', '7', 'the pack leaves its own GL state to the caller'),
    ('pack-no-packets', D + 'dvbridge_gl_pack.c',
     [('    glUniform1ui(program->count, count);\n', '    glUniform1ui(program->count, 0);\n')],
     'packbytes', '-', 'the metadata packets are not written into the transport'),
]
