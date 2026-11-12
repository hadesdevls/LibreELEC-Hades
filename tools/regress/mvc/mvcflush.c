/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Test tool of Hades (tools/regress/mvc): feeds a file of packets ([u32 little-endian length][bytes]...)
 * to a decoder of the host FFmpeg by its name, one packet per call, as a player does, and flushes the
 * decoder (avcodec_flush_buffers, what a player does at a seek) before each packet whose number is in the
 * list. Prints, for every picture that comes out, the packet time, the view id the decoder names (or "-"),
 * its size, its cropping and a hash of its samples; a line "flush before N" at each flush.
 * Usage: mvcflush <decoder> <threads> <frame|slice> <all|base> <file> [<n>,<n>...]   */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

static uint64_t hash_frame(const AVFrame *f)
{
    uint64_t h = 1469598103934665603ULL;
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(f->format);
    for (int p = 0; p < 3 && f->data[p]; p++) {
        int w = p ? AV_CEIL_RSHIFT(f->width, d->log2_chroma_w) : f->width;
        int hgt = p ? AV_CEIL_RSHIFT(f->height, d->log2_chroma_h) : f->height;
        for (int y = 0; y < hgt; y++) {
            const uint8_t *row = f->data[p] + (ptrdiff_t)y * f->linesize[p];
            for (int x = 0; x < w; x++) {
                h ^= row[x];
                h *= 1099511628211ULL;
            }
        }
    }
    return h;
}

static void drain(AVCodecContext *c, AVFrame *f, int *count)
{
    for (;;) {
        int r = avcodec_receive_frame(c, f);
        if (r < 0)
            return;
        const AVFrameSideData *sd = av_frame_get_side_data(f, AV_FRAME_DATA_VIEW_ID);
        char view[16] = "-";
        if (sd && sd->size >= sizeof(int)) {
            int id;
            memcpy(&id, sd->data, sizeof(id));
            snprintf(view, sizeof(view), "%d", id);
        }
        printf("pts %" PRId64 " view %s %dx%d crop %zu,%zu,%zu,%zu %016" PRIx64 "%s\n", f->pts, view,
               f->width, f->height, f->crop_left, f->crop_right, f->crop_top, f->crop_bottom, hash_frame(f),
               (f->decode_error_flags || (f->flags & AV_FRAME_FLAG_CORRUPT)) ? " ERRORS" : "");
        (*count)++;
        av_frame_unref(f);
    }
}

static int in_list(const char *list, int64_t n)
{
    if (!list)
        return 0;
    const char *p = list;
    while (*p) {
        char *end;
        long long v = strtoll(p, &end, 10);
        if (end == p)
            return 0;
        if (v == n)
            return 1;
        p = *end == ',' ? end + 1 : end;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7)
        return 2;
    const char *flushes = argc == 7 ? argv[6] : NULL;
    const AVCodec *codec = avcodec_find_decoder_by_name(argv[1]);
    if (!codec) {
        fprintf(stderr, "no decoder %s\n", argv[1]);
        return 2;
    }
    AVCodecContext *c = avcodec_alloc_context3(codec);
    c->thread_count = atoi(argv[2]);
    c->thread_type = !strcmp(argv[3], "slice") ? FF_THREAD_SLICE : FF_THREAD_FRAME;
    if (!strcmp(argv[4], "all") && av_opt_set(c, "view_ids", "-1", AV_OPT_SEARCH_CHILDREN) < 0) {
        fprintf(stderr, "the decoder does not take view_ids\n");
        return 2;
    }
    if (avcodec_open2(c, codec, NULL) < 0)
        return 2;
    FILE *in = fopen(argv[5], "rb");
    if (!in)
        return 2;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *f = av_frame_alloc();
    int count = 0, refused = 0, nflush = 0;
    int64_t n = 0;
    for (;;) {
        uint8_t l[4];
        if (fread(l, 1, 4, in) != 4)
            break;
        uint32_t len = l[0] | l[1] << 8 | l[2] << 16 | (uint32_t)l[3] << 24;
        if (av_new_packet(pkt, len) < 0 || fread(pkt->data, 1, len, in) != len)
            return 2;
        if (in_list(flushes, n)) {
            avcodec_flush_buffers(c);
            printf("flush before %" PRId64 "\n", n);
            nflush++;
        }
        pkt->pts = pkt->dts = n++;
        int r = avcodec_send_packet(c, pkt);
        if (r < 0)
            refused++;
        av_packet_unref(pkt);
        drain(c, f, &count);
    }
    avcodec_send_packet(c, NULL);
    drain(c, f, &count);
    fprintf(stderr, "%s threads %s %s views %s: packets %" PRId64 ", flushes %d, refused %d, pictures %d\n",
            argv[1], argv[2], argv[3], argv[4], n, nflush, refused, count);
    fclose(in);
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    return 0;
}
