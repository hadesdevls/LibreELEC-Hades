/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Test tool of Hades (tools/regress/mvc): finding 8 at the level of the
 * parser. A subset SPS (from craft22.py spsbits) is given to ff_h264_decode_subset_seq_parameter_set() of a
 * twin tree of h264_mvc, compiled against its headers and linked with its library. The parser reads up to the
 * end of the operation point table and leaves what follows it (mvc_vui_parameters_present_flag,
 * additional_extension2_flag...) unread; so a parser that reads the table as H.7.3.2.1.4 has it leaves exactly
 * the bits that h264bits.py finds after the table. More left = part of the table not read (a count misread).
 * Usage: f8parse <file>...   (exit 0 when every file is read so; prints F8-PARSE PASSED or F8-PARSE FAILED) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/get_bits.h"
#include "h264_ps.h"

int main(int argc, char **argv)
{
    int ok = 1;
    AVCodecContext *avctx = avcodec_alloc_context3(NULL);

    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        static uint8_t buf[1 << 16];
        size_t n;
        uint32_t bits, after;
        H264ParamSets ps;
        GetBitContext gb;
        int ret, left;

        if (!f)
            return 2;
        n = fread(buf, 1, sizeof(buf) - AV_INPUT_BUFFER_PADDING_SIZE, f);
        fclose(f);
        if (n < 8)
            return 2;
        memcpy(&bits, buf, 4);
        memcpy(&after, buf + 4, 4);
        memset(buf + n, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        memset(&ps, 0, sizeof(ps));
        if (init_get_bits(&gb, buf + 8, bits) < 0)
            return 2;
        ret = ff_h264_decode_subset_seq_parameter_set(&gb, avctx, &ps);
        left = get_bits_left(&gb);
        printf("%s: parser returned %d; bits left %d, after the operation point table %u: %s\n", argv[a], ret, left,
               after, ret >= 0 && left == (int)after ? "read to its end" : "NOT read as the norm has it");
        if (ret < 0 || left != (int)after)
            ok = 0;
        ff_h264_ps_uninit(&ps);
    }
    avcodec_free_context(&avctx);
    printf("%s\n", ok ? "F8-PARSE PASSED" : "F8-PARSE FAILED");
    return ok ? 0 : 1;
}
