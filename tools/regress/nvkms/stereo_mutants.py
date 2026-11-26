# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# The mutants of stereo_cta_vic_test.c (run-nvkms.sh, stage stereocta): each one changes one rule of
# nvidia-open-kernel-modules-9086 (and of nvidia-legacy-open-9083, the same functions) in a copy of
# src/nvidia-modeset/include/nvkms-hades.h, and the test must fail on it with exactly that many failed checks.
#   stereo_mutants.py list               the names, one a line, with their counts ("<name> <count>")
#   stereo_mutants.py <name> <out.py>    the edit of that mutant, as rg_edit takes it (PAIRS)
import sys

MUTANTS = {
    # whatever the EDID declares
    'declared-not-asked': (23, [('    if (!nvHadesEdidDeclares3D(pEdid, ctaVic, half)) {',
                                '    if (!nvHadesEdidDeclares3D(pEdid, ctaVic, half) && 0) {')]),
    # 3D_MASK ignored
    'mask-ignored': (5, [('            if (mask & (1u << i)) {', '            if ((mask | 0xFFFF) & (1u << i)) {')]),
    # side by side without 3D_Detail 1
    'sbs-detail-ignored': (2, [('            if ((db[9 + offset + i] >> 4) == 1) {', '            if (1) {')]),
    # any 3D_Ext_Data for side by side (half)
    'sbs-ext-any': (2, [('((size >= 6) && ((payload[5] >> 4) == 0))', '(size >= 6)')]),
    # the I_latency fields not skipped
    'i-latency-not-skipped': (2, [('offset += (db[8] & 0x40) ? 4 : 2;', 'offset += 2;')]),
    # VIC 0 and 128 taken for VICs
    'unknown-vic-kept': (2, [('((vic >= 1) && (vic <= 127))', '((vic != 1000) && (vic <= 128))')]),
    # an extension with a wrong checksum read
    'checksum-not-checked': (2, [('((sum & 0xFF) != 0)', '(((sum & 0xFF) != 0) && (b[0] == 0xEE))')]),
    # a CTA extension of revision 2 read
    'revision-2-read': (2, [('(b[1] < 3)', '(b[1] < 2)')]),
    # an HDMI_VIC vendor InfoFrame taken for a 3D one
    'vsif-format-not-3d': (2, [('((payload[3] >> 5) != 2)', '((payload[3] >> 5) == 0)')]),
    # the seventeenth SVD taken for the first
    'past-sixteen': (5, [('if (p->svds < 16) {\n                                p->vic[p->svds] =',
                             'if (p->svds < 17) {\n                                p->vic[p->svds % 16] =')]),
    # only the first HDMI VSDB read
    'first-hdmi-vsdb-only': (2, [('nvHadesEdidHdmiVsdb3D(db, len, p);\n',
                                     'nvHadesEdidHdmiVsdb3D(db, len, p);\n                    break;\n')]),
    # byte 2 of 128 read
    'collection-past-127': (2, [('(b[2] > 127)', '(b[2] > 128)')]),
    # the CTA VIC of another HDMI_VIC
    'cta-vic-of-hdmi-vic': (14, [('    case 3:\n        return 93;', '    case 3:\n        return 94;')]),
    # the formats of a Y420VDB taken for SVDs of the video data blocks: (p)
    'y420vdb-as-vdb': (4, [('                    if (tag == 2) {', '                    if ((tag == 2) || ((tag == 7) && (db[1] == 14))) {')]),
    # the length tests of the 3D part
    '3d-len-not-checked': (2, [('    if ((len < (8 + offset + len3D - 1)) || (len3D < multiLen)) {',
                                   '    if (len3D < multiLen) {')]),
}

if __name__ == '__main__':
    if sys.argv[1] == 'list':
        for name, (count, _) in MUTANTS.items():
            print(name, '-' if count is None else count)
    else:
        with open(sys.argv[2], 'w', encoding='utf-8') as out:
            out.write('PAIRS = %r\n' % (MUTANTS[sys.argv[1]][1],))
