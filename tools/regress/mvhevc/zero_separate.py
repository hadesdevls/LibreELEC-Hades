#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Test-only edit (never in the image): the software stand-in of the separate base-layer decoder takes picture buffers
# that are cleared, as mvhevc_frames.c "zero" clears this decoder's, so that the parts of a damaged picture that no
# slice decodes are the same in both (FFmpeg leaves there what the buffer of the pool held).
import sys

p = sys.argv[1] + '/libavcodec/hevc/baselayer.c'
t = open(p, encoding='utf-8').read()
old = 'int ff_hevc_base_layer_alloc(HEVCContext *s)\n{'
new = '''static int zero_get_buffer(AVCodecContext *avctx, AVFrame *f, int flags)
{
    int ret = avcodec_default_get_buffer2(avctx, f, flags);
    for (int i = 0; ret >= 0 && i < AV_NUM_DATA_POINTERS && f->buf[i]; i++)
        memset(f->buf[i]->data, 0, f->buf[i]->size);
    return ret;
}

int ff_hevc_base_layer_alloc(HEVCContext *s)
{'''
old2 = '    dec->get_format       = get_format;\n'
new2 = old2 + '    if (!avctx->hw_device_ctx)\n        dec->get_buffer2 = zero_get_buffer;\n'
for a, b in ((old, new), (old2, new2)):
    if t.count(a) != 1:
        sys.exit('zero_separate: not applied')
    t = t.replace(a, b)
open(p, 'w', encoding='utf-8', newline='\n').write(t)
