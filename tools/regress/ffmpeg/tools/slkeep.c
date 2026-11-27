// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// slkeep.c <sw|vaapi> <file.hevc> [<threads>]: FFmpeg's HEVC decoder on a stream, for the stage slkeep of
// run-ffmpeg.sh. In software it writes the MD5 of each picture (every plane, its visible part); with vaapi it decodes on
// the display of fakeva.c (a VA-API driver that decodes nothing and writes what it is given to $HADES_FAKEVA_LOG), made
// here without a device. Lines:
//   FORMAT <n> offered <formats> -> <chosen>     each call of get_format()
//   LAVC <line>                                  FFmpeg's lines of AV_LOG_VERBOSE and above from the HEVC decoder
//   FRAME <n> pts <pts> <format> <w>x<h> [md5 <hex>]
//   SUMMARY get_format <n> frames <n> kept <n>   kept: the lines that say an SPS changed only in its scaling lists
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <va/va.h>
#include <va/va_backend.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/md5.h>
#include <libavutil/pixdesc.h>

static int n_format, n_kept, hw;

static enum AVPixelFormat get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmts)
{
    enum AVPixelFormat chosen = AV_PIX_FMT_NONE;
    (void)avctx;
    printf("FORMAT %d offered", ++n_format);
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        printf(" %s", av_get_pix_fmt_name(*p));
        if (hw && *p == AV_PIX_FMT_VAAPI)
            chosen = *p;
    }
    if (chosen == AV_PIX_FMT_NONE) {
        // the software format: the last one offered
        for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++)
            chosen = *p;
    }
    printf(" -> %s\n", av_get_pix_fmt_name(chosen));
    return chosen;
}

static void log_cb(void *avcl, int level, const char *fmt, va_list vl)
{
    char line[1024];
    int pfx = 1;
    if (level > AV_LOG_VERBOSE)
        return;
    av_log_format_line(avcl, level, fmt, vl, line, sizeof(line), &pfx);
    if (strstr(line, "changed only in its scaling lists"))
        n_kept++;
    line[strcspn(line, "\n")] = 0;
    printf("LAVC %s\n", line);
}

// the display: libva's display context made by hand, its driver named hadesfake (fakeva.c)
static VAStatus driver_names(VADisplayContextP d, char **names, unsigned *n)
{
    (void)d;
    names[0] = strdup("hadesfake");
    *n = 1;
    return names[0] ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_ALLOCATION_FAILED;
}
static int is_valid(VADisplayContextP d) { return d && d->pDriverContext; }
static void destroy(VADisplayContextP d)
{
    if (!d)
        return;
    free(d->pDriverContext);
    free(d);
}
static VADisplay fake_display(void)
{
    VADisplayContextP d = calloc(1, sizeof(*d));
    VADriverContextP c = calloc(1, sizeof(*c));
    if (!d || !c) {
        free(d);
        free(c);
        return NULL;
    }
    d->vadpy_magic = VA_DISPLAY_MAGIC;
    d->pDriverContext = c;
    d->vaIsValid = is_valid;
    d->vaDestroy = destroy;
    d->vaGetDriverNames = driver_names;
    c->pDisplayContext = d;
    return (VADisplay)d;
}

int main(int argc, char **argv)
{
    AVFormatContext *fmt = NULL;
    AVCodecContext *avctx = NULL;
    AVBufferRef *dev = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    VADisplay dpy = NULL;
    const AVCodec *codec;
    int st, ret, n_frames = 0, threads = argc > 3 ? atoi(argv[3]) : 1;

    if (argc < 3 || (strcmp(argv[1], "sw") && strcmp(argv[1], "vaapi"))) {
        fprintf(stderr, "usage: %s sw|vaapi <file.hevc> [<threads>]\n", argv[0]);
        return 2;
    }
    hw = !strcmp(argv[1], "vaapi");
    setvbuf(stdout, NULL, _IOLBF, 0);
    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback(log_cb);

    if (avformat_open_input(&fmt, argv[2], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        printf("FAIL: cannot open %s\n", argv[2]);
        return 1;
    }
    st = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (st < 0 || !(avctx = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(avctx, fmt->streams[st]->codecpar) < 0) {
        printf("FAIL: no video stream\n");
        return 1;
    }
    avctx->get_format = get_format;
    avctx->thread_count = threads;
    avctx->pkt_timebase = fmt->streams[st]->time_base;
    if (hw) {
        int major, minor;
        AVVAAPIDeviceContext *hwctx;
        dpy = fake_display();
        if (!dpy || vaInitialize(dpy, &major, &minor) != VA_STATUS_SUCCESS) {
            printf("FAIL: vaInitialize on the display of the fake driver\n");
            return 1;
        }
        printf("VA-API %d.%d: %s\n", major, minor, vaQueryVendorString(dpy));
        dev = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
        if (!dev) {
            printf("FAIL: av_hwdevice_ctx_alloc\n");
            return 1;
        }
        hwctx = ((AVHWDeviceContext *)dev->data)->hwctx;
        hwctx->display = dpy;
        if ((ret = av_hwdevice_ctx_init(dev)) < 0) {
            printf("FAIL: av_hwdevice_ctx_init: %s\n", av_err2str(ret));
            return 1;
        }
        avctx->hw_device_ctx = av_buffer_ref(dev);
    }
    if ((ret = avcodec_open2(avctx, codec, NULL)) < 0) {
        printf("FAIL: avcodec_open2: %s\n", av_err2str(ret));
        return 1;
    }

    for (int eof = 0; !eof;) {
        ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            eof = 1;
            ret = avcodec_send_packet(avctx, NULL);
        } else {
            if (pkt->stream_index == st)
                ret = avcodec_send_packet(avctx, pkt);
            av_packet_unref(pkt);
        }
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            printf("FAIL: avcodec_send_packet: %s\n", av_err2str(ret));
            return 1;
        }
        while ((ret = avcodec_receive_frame(avctx, frame)) >= 0) {
            printf("FRAME %d pts %lld %s %dx%d", n_frames++, (long long)frame->pts,
                   av_get_pix_fmt_name(frame->format), frame->width, frame->height);
            if (frame->format != AV_PIX_FMT_VAAPI) {
                const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(frame->format);
                struct AVMD5 *md5 = av_md5_alloc();
                uint8_t sum[16];
                av_md5_init(md5);
                for (int p = 0; p < 4 && frame->data[p]; p++) {
                    int w = p ? AV_CEIL_RSHIFT(frame->width, d->log2_chroma_w) : frame->width;
                    int h = p ? AV_CEIL_RSHIFT(frame->height, d->log2_chroma_h) : frame->height;
                    int bytes = (d->comp[0].depth + 7) / 8;
                    for (int y = 0; y < h; y++)
                        av_md5_update(md5, frame->data[p] + (size_t)y * frame->linesize[p], (size_t)w * bytes);
                }
                av_md5_final(md5, sum);
                av_free(md5);
                printf(" md5 ");
                for (int i = 0; i < 16; i++)
                    printf("%02x", sum[i]);
            }
            printf("\n");
            av_frame_unref(frame);
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            printf("FAIL: avcodec_receive_frame: %s\n", av_err2str(ret));
            return 1;
        }
    }
    printf("SUMMARY get_format %d frames %d kept %d\n", n_format, n_frames, n_kept);
    avcodec_free_context(&avctx);
    av_buffer_unref(&dev);
    avformat_close_input(&fmt);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (dpy)
        vaTerminate(dpy);
    return 0;
}
