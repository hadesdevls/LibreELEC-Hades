# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Mutants for the render test with the CCM 001 reader (src/dvrender_oracle.c), the measured test (src/measured_oracle.c)
# and the pass times (mutants.py reads it).
#  - Defects of the transport that the reader of ETSI GS CCM 001 (src/ccm001_oracle.h) must find ("reader": the test's
#    check "every transport reads as ETSI GS CCM 001 says it is sent" must fail). Two kinds: packer defects, put into
#    every packer that one test draws with, so that the ways it draws agree and its own comparison of them cannot see
#    the defect (for the render test, the converted transport: pack_gles300.frag, its composed way, and the two hooks
#    of dvbridge_render.c that its direct way packs with, pack_active_area() and, for the bars, skip_inactive_area();
#    for the measured test, the source transport: the packer of dvbridge_source.c, "src-"); and serializer defects
#    (dvbridge_metadata.h), which reach every packer.
#  - The pass times taken out of the bridge, and the clamp of the colour of the float picture.
DIRS = ['tools/dvbridge']
S, F, R, M = ('tools/dvbridge/' + f for f in ('dvbridge_source.c', 'pack_gles300.frag', 'dvbridge_render.c',
                                               'dvbridge_metadata.h'))
F_BIT = 'uint bit = (metadata_words[w / 4u][w % 4u] >> (31u - b % 32u)) & 1u;'
P_BIT = 'uint pk_bit = (dv_words[pk_w / 4u][pk_w % 4u] >> (31u - pk_b % 32u)) & 1u;'
K_BIT = 'sk_bit = (dv_words[sk_w / 4u][sk_w % 4u] >> (31u - sk_b % 32u)) & 1u;'
MUTANTS = [
    # ------------------------------------------------------------------ the converted transport's packers (render test)
    ('parity-no-luma', None,
     [(F, 'uint s = bit ^ parity((chroma >> 1u) & 0x7FFu) ^ parity(luma & 0xFFFu);', 'uint s = bit ^ parity((chroma >> 1u) & 0x7FFu);'),
      (R, 'uint pk_s = ((pk_chroma >> 1u) & 0x7FFu) ^ (pk_luma & 0xFFFu);', 'uint pk_s = ((pk_chroma >> 1u) & 0x7FFu);'),
      (R, K_BIT, 'sk_bit = ((dv_words[sk_w / 4u][sk_w % 4u] >> (31u - sk_b % 32u)) & 1u) ^ 1u;')],
     'oracle-render', 'reader', 'the scrambling without the luma term (6.4.2); in the bars of the early hook, where the '
     'parities of 256 and of 1024 cancel, the bit flipped'),
    ('second-copy', None,
     [(F, F_BIT, 'uint bit = ((metadata_words[w / 4u][w % 4u] >> (31u - b % 32u)) & 1u) ^ uint((i / 1024u) % 3u == 1u);'),
      (R, P_BIT, 'uint pk_bit = ((dv_words[pk_w / 4u][pk_w % 4u] >> (31u - pk_b % 32u)) & 1u) ^ uint((pk_i / 1024u) % 3u == 1u);'),
      (R, K_BIT, 'sk_bit = ((dv_words[sk_w / 4u][sk_w % 4u] >> (31u - sk_b % 32u)) & 1u) ^ uint((sk_i / 1024u) % 3u == 1u);')],
     'oracle-render', 'reader', 'the second copy of every packet with every bit flipped (6.4.3)'),
    ('two-copies', None,
     [(F, 'if (i < 3072u * packet_count) {', 'if (i < 3072u * packet_count && i % 3072u < 2048u) {'),
      (R, 'if (pk_i < 3072u * dv_count) {', 'if (pk_i < 3072u * dv_count && pk_i % 3072u < 2048u) {'),
      (R, 'if (sk_i < 3072u * dv_count) {', 'if (sk_i < 3072u * dv_count && sk_i % 3072u < 2048u) {')],
     'oracle-render', 'reader', 'two copies only: the third is the picture\'s own LSB (6.4.3)'),
    ('bar-luma-257', None,
     [(F, 'uint luma = uint(floor(256.0 + 3504.0 * y + .5));', 'uint luma = uint(floor(256.0 + 3504.0 * y + .5)) + uint(y <= 0.0);'),
      (R, 'uint pk_luma = uint(floor(256.0 + 3504.0 * pk_y + .5));', 'uint pk_luma = uint(floor(256.0 + 3504.0 * pk_y + .5)) + uint(pk_y <= 0.0);'),
      (R, 'return vec4(128.0, 16.0, float(sk_bit * 16u), 255.0) / 255.0;', 'return vec4(128.0, 16.0, float(sk_bit * 16u + 1u), 255.0) / 255.0;')],
     'oracle-render', 'reader', 'the luma of the bars one code above black'),
    # ------------------------------------------------------------------ the source transport's packer (measured test)
    ('src-parity-no-luma', S,
     [('md_bit ^ ccm_parity((c >> 1u) & 0x7FFu) ^ ccm_parity(y & 0xFFFu);', 'md_bit ^ ccm_parity((c >> 1u) & 0x7FFu);')],
     'oracle-measured', 'reader', 'the scrambling of the source transport without the luma term (6.4.2)'),
    ('src-second-copy', S,
     [('highp uint md_bit = ccm_packet_bit(packet, b);', 'highp uint md_bit = ccm_packet_bit(packet, b) ^ uint((i / 1024u) % 3u == 1u);')],
     'oracle-measured', 'reader', 'the source transport\'s second copy of every packet with every bit flipped (6.4.3)'),
    ('src-two-copies', S,
     [('if (i >= count * 3072u)', 'if (i >= count * 3072u || i % 3072u >= 2048u)')],
     'oracle-measured', 'reader', 'the source transport with two copies only (6.4.3)'),
    ('src-bar-luma-257', S,
     [('return inside(p.x, p.y) ? v : uint(black.x);', 'return inside(p.x, p.y) ? v : uint(black.x) + 1u;'),
      ('code_scale + 0.5)) : uint(black.x);', 'code_scale + 0.5)) : uint(black.x) + 1u;')],
     'oracle-measured', 'reader', 'the luma of the source transport\'s bars one code above black'),
    # ------------------------------------------------------------------ the serializer
    ('crc-init-0', M,
     [('uint32_t crc = 0xFFFFFFFFu;\n    for (size_t i = 0; i < n; i++) {', 'uint32_t crc = 0;\n    for (size_t i = 0; i < n; i++) {')],
     'oracle-render', 'reader', 'the CRC from an initial value of 0: its remainder over the packet is still 0 (6.3.4)'),
    ('eos', M,
     [('p[1] = (uint8_t)((d->id & 15) << 4 | (d->id & 15));', 'p[1] = (uint8_t)((d->id & 15) << 4 | (d->id & 15)); p[2] = 1;')],
     'oracle-render', 'reader', 'EOS on every packet (6.3.2)'),
    ('id-second-packet', M,
     [('p[1] = (uint8_t)((d->id & 15) << 4 | (d->id & 15));',
       'p[1] = (uint8_t)(((d->id + (i > 0)) & 15) << 4 | ((d->id + (i > 0)) & 15));')],
     'oracle-render', 'reader', 'another metadata id in the packets after the first (6.3.1)'),
    ('length-plus-1', M,
     [('p[3] = (uint8_t)(d->size >> 8);', 'p[3] = (uint8_t)((d->size + 1) >> 8);'),
      ('p[4] = (uint8_t)(d->size & 0xFF);', 'p[4] = (uint8_t)((d->size + 1) & 0xFF);')],
     'oracle-render', 'reader', 'the length one more than the structure (6.3.3)'),
    ('trailing-byte', M,
     [('memcpy(p + at, d->payload + from, left < room ? left : room);',
       'memcpy(p + at, d->payload + from, left < room ? left : room); if (at + (left < room ? left : room) < 124) p[at + (left < room ? left : room)] = 0x5a;')],
     'oracle-render', 'reader', 'a trailing byte that is not 0x00 (6.3.3)'),
    ('reserved-2a', M,
     [('dvbridge_dv_u16(d, 42);', 'dvbridge_dv_u16(d, 43);')],
     'oracle-render', 'reader', 'reserved_0x2a written 0x2b (table 3)'),
    ('reserved-ff', M,
     [('dvbridge_dv_u16(d, 65535); dvbridge_dv_u32(d, 0);', 'dvbridge_dv_u16(d, 65279); dvbridge_dv_u32(d, 0);')],
     'oracle-render', 'reader', 'reserved_0xff written 0xfe 0xff (table 3)'),
    ('l2-after-l5', M,
     [('if(e->level==2) rpu_l2=true;', 'if(e->level==2) {rpu_l2=true; rank+=256;}'),
      ('DV_REFUSE(!area, "a level 2 block after the level 5 one"); n = 7;', 'n = 7;')],
     'oracle-render', 'reader', 'level 2 blocks after the level 5 block (CCM l.1177-1178), with the serializer\'s guard '
     'of that order taken out'),
    ('body-offset', M,
     [('unsigned at = 3, room = 121, from = 0;', 'unsigned at = 4, room = 120, from = 0;'),
      ('from = 119 + (i - 1) * 121;', 'from = 119 + (i - 1) * 120;')],
     'oracle-render', 'reader', 'the body of the packets after the first one byte later (6.3.3.3), the payload going on '
     'from where the packet before stopped'),
    ('type-no-last', M,
     [('i + 1 == d->count ? 3 : 2;', 'i + 1 == d->count ? 2 : 2;')],
     'oracle-render', 'reader', 'no last packet of type 0b11 (6.3.2)'),
    # ------------------------------------------------------------------ the pass times, the clamp of the float picture
    ('passtimes-no-merge', 'tools/dvbridge/dvbridge_passtime.h',
     [('            merged[j].count += p->count;', '            merged[j].count = p->count;')],
     'passtimes', '-', 'passes of one name are not merged: the count of the last one only'),
    ('passtimes-taken-out', R,
     [('void dvbridge_render_time_passes(struct dvbridge_renderer *r, bool on)',
       'void dvbridge_render_time_passes_taken_out(struct dvbridge_renderer *r, bool on)')],
     'passtime', 'bridge', 'the bridge has no dvbridge_render_time_passes(): the passes are not timed'),
    ('sample-never-pauses', R,
     [('    pl_opengl_pause_timers(r->gpu, !now);', '    pl_opengl_pause_timers(r->gpu, false);'),
      ('    r->passtime_now = now;', '    r->passtime_now = true;')],
     'passtime-sampled', 'sampled', 'a frame that is not sampled is timed all the same: its passes begin their queries'),
    ('identity-planes-never', R,
     [('    r->identity_taken = identity_planes(r, source, metadata, bytes, geometry);',
       '    r->identity_taken = false && identity_planes(r, source, metadata, bytes, geometry);')],
     'passtime-measured', 'placebo', 'the source transport never takes the decoder\'s planes: libplacebo composes the '
     'identity mapping again'),
    ('k24-noclamp', R,
     [('"color = vec4(clamp(color.rgb, 0.0, 1.0), " PICTURE_ALPHA ");",', '"color = vec4(color.rgb, " PICTURE_ALPHA ");",')],
     'k24', 'clamp', 'the colour of the float picture without its clamp to [0, 1] (mask_active_area())'),
]
