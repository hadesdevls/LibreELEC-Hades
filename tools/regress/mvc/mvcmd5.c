/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Test tool of Hades (tools/regress/mvc): feeds a file of packets ([u32 little-endian
 * length][bytes]..., as mvcpack.py writes it) to h264_mvc of the host FFmpeg it is linked with, one packet per
 * call as a player does, all views asked for, and writes the MD5 of each picture of view index 0 and of view
 * index 1 to <out>.v0.md5 and <out>.v1.md5, in the order they come out, as picmd5.py computes it from a raw
 * 8-bit 4:2:0 file (the rows of Y, then of U, then of V, without padding): the lists that views22.sh compares
 * with the reference decoder. The view of the first picture (AV_FRAME_DATA_VIEW_ID) is the base view, index 0;
 * every other view id is the second view, index 1.
 * Usage: mvcmd5 <threads> <frame|slice> <packets> <out>   */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>

static int ids[1] = { -1 };
static FILE *out[2];

static void put(const AVFrame *f)
{
    const AVFrameSideData *sd = av_frame_get_side_data(f, AV_FRAME_DATA_VIEW_ID);
    int id = 0, idx;
    uint8_t d[16];
    struct AVMD5 *m = av_md5_alloc();

    if (sd && sd->size >= sizeof(int))
        memcpy(&id, sd->data, sizeof(id));
    /* the base view is the view of the first picture; every other view is the second (a stream can rename its
     * second view, as f6-views does) */
    if (ids[0] == -1)
        ids[0] = id;
    idx = id == ids[0] ? 0 : 1;
    av_md5_init(m);
    for (int p = 0; p < 3; p++) {
        int w = p ? (f->width + 1) >> 1 : f->width, h = p ? (f->height + 1) >> 1 : f->height;
        for (int y = 0; y < h; y++)
            av_md5_update(m, f->data[p] + (ptrdiff_t)y * f->linesize[p], w);
    }
    av_md5_final(m, d);
    av_free(m);
    for (int i = 0; i < 16; i++)
        fprintf(out[idx], "%02x", d[i]);
    fprintf(out[idx], "\n");
}

static void drain(AVCodecContext *c, AVFrame *f)
{
    while (avcodec_receive_frame(c, f) >= 0) {
        if (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P)
            put(f);
        av_frame_unref(f);
    }
}

int main(int argc, char **argv)
{
    char name[4096];
    if (argc != 5)
        return 2;
    const AVCodec *codec = avcodec_find_decoder_by_name("h264_mvc");
    AVCodecContext *c = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!c)
        return 2;
    c->thread_count = atoi(argv[1]);
    c->thread_type = !strcmp(argv[2], "slice") ? FF_THREAD_SLICE : FF_THREAD_FRAME;
    /* A player opens the decoder with the reorder depth that probing the stream found (the ffmpeg command
     * line does); a decoder opened cold finds it picture by picture and drops the pictures that came out
     * too early meanwhile. The largest depth H.264 allows: every picture comes out, in output order. */
    c->has_b_frames = 16;
    if (av_opt_set(c, "view_ids", "-1", AV_OPT_SEARCH_CHILDREN) < 0 || avcodec_open2(c, codec, NULL) < 0)
        return 2;
    for (int v = 0; v < 2; v++) {
        snprintf(name, sizeof(name), "%s.v%d.md5", argv[4], v);
        if (!(out[v] = fopen(name, "w")))
            return 2;
    }
    FILE *in = fopen(argv[3], "rb");
    AVPacket *pkt = av_packet_alloc();
    AVFrame *f = av_frame_alloc();
    int64_t n = 0;
    if (!in)
        return 2;
    for (;;) {
        uint8_t l[4];
        if (fread(l, 1, 4, in) != 4)
            break;
        uint32_t len = l[0] | l[1] << 8 | l[2] << 16 | (uint32_t)l[3] << 24;
        if (av_new_packet(pkt, len) < 0 || fread(pkt->data, 1, len, in) != len)
            return 2;
        pkt->pts = pkt->dts = n++;
        avcodec_send_packet(c, pkt);
        av_packet_unref(pkt);
        drain(c, f);
    }
    avcodec_send_packet(c, NULL);
    drain(c, f);
    fclose(in);
    fclose(out[0]);
    fclose(out[1]);
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    return 0;
}
