#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Test-only edit (never in the image): a fault of the separate base-layer decoder, as a graphics card's decoder could
# have one, at the n-th picture it gives (counted over the process):
#   flip <n>   one sample of the picture changed by one (a wrong picture)
#   flipfrom <n> the same in that picture and in every one after it (a decoder that goes wrong and stays so: with frame
#              threads, pictures set up while the one that finds it is still being decoded are in flight; the comparison
#              that finds it waits half a second before it gives up, so that they are, whatever the load of the host)
#   drop <n>   the picture not given (a picture missing)
#   stale <n>  the picture before given again, with its own POC, in its place (pictures out of step)
#   fillfail <n> the n-th copy of a picture of the separate decoder into a frame fails (ff_hevc_base_layer_fill(), after
#              setup: the picture is filled with grey and can only be marked when it is returned)
#   inter      (n unused) every base picture taken for inter-coded by the check of the layer above, so that the
#              second view's temporal prediction from the base picture of its access unit counts as needing a
#              motion field the separate decoder does not give (no stream at hand codes that)
# usage: fault_separate.py <ffmpeg tree> <kind> <n>
import sys

root, kind, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
p = root + '/libavcodec/hevc/baselayer.c'
t = open(p, encoding='utf-8').read()

if kind == 'inter':
    old = '    if (s->cur_layer > 0 && col == s->layers[0].cur_frame && col->intra)\n'
    new = '    if (s->cur_layer > 0 && col == s->layers[0].cur_frame && col->intra && 0)\n'
elif kind == 'fillfail':
    old = '''    if (s->base_pic < 0 ||
        ff_hevc_base_layer_copy('''
    new = '''    static atomic_int fault_fills;
    if (s->base_pic < 0 || atomic_fetch_add(&fault_fills, 1) + 1 == %d ||
        ff_hevc_base_layer_copy(''' % n
else:
    old = '''        av_frame_unref(f);
    }
    u->transfer_ns'''
    act = {
        'flip': '''if (p->frame && av_frame_make_writable(p->frame) >= 0)
                p->frame->data[0][0] ^= p->frame->format == AV_PIX_FMT_P010 ? 0x40 : 1;''',
        'flipfrom': '''if (p->frame && av_frame_make_writable(p->frame) >= 0)
                p->frame->data[0][0] ^= p->frame->format == AV_PIX_FMT_P010 ? 0x40 : 1;''',
        'drop': 'av_frame_free(&p->frame);',
        'stale': '''if (fault_prev && p->frame) {
                AVFrame *x = p->frame; int xp = p->poc;
                p->frame = fault_prev; p->poc = fault_prev_poc;
                fault_prev = x; fault_prev_poc = xp;
                goto faulted;
            }''',
    }[kind]
    new = '''        {
            static int fault_given;
            static AVFrame *fault_prev;
            static int fault_prev_poc;
            if (++fault_given %s %d) {
                %s
            }
            if (p->frame) {
                av_frame_free(&fault_prev);
                fault_prev = av_frame_clone(p->frame);
                fault_prev_poc = p->poc;
            }
        }
faulted:
        av_frame_unref(f);
    }
    u->transfer_ns''' % ('>=' if kind == 'flipfrom' else '==', n, act)
if t.count(old) != 1:
    sys.exit('fault_separate %s: not applied' % kind)
t = t.replace(old, new)
if kind == 'flipfrom':
    old = '    if (plane >= 0) {\n        give_up('
    if t.count(old) != 1:
        sys.exit('fault_separate flipfrom: the wait not applied')
    t = t.replace(old, '    if (plane >= 0) {\n        av_usleep(500000);\n        give_up(')
open(p, 'w', encoding='utf-8', newline='\n').write(t)
