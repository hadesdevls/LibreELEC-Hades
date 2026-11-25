/*
 * Copyright (C) 2026 Hades
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host test of the clip of the A/B comparison of "Check this setup" (packages/mediacenter/hades-check,
 * clips/abclip.py): the extension blocks of the RPU of every picture as FFmpeg's HEVC decoder exports them
 * (AV_FRAME_DATA_DOVI_METADATA), one line a picture, for check_clips_tests.py to compare with clips.json:
 *   frame <n> refresh <scene_refresh_flag> blocks <count>
 *     [L1 <min> <max> <avg>] [L2 <target> <slope> <offset> <power> <chroma> <saturation> <ms_weight>]...
 *     [L5 <left> <right> <top> <bottom>] [L6 <max> <min> <max_cll> <max_fall>] [L<n> (another level)]
 * The blocks in the order FFmpeg gives them. Nothing of the generator is used.
 *
 * usage: dovi_blocks <clip.mkv>
 */
#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>

static void print_blocks(long n, const AVDOVIMetadata *m)
{
    const AVDOVIColorMetadata *c = av_dovi_get_color(m);
    printf("frame %ld refresh %d blocks %d", n, c->scene_refresh_flag, m->num_ext_blocks);
    for (int i = 0; i < m->num_ext_blocks; i++) {
        const AVDOVIDmData *e = av_dovi_get_ext(m, i);
        switch (e->level) {
        case 1:
            printf(" L1 %u %u %u", e->l1.min_pq, e->l1.max_pq, e->l1.avg_pq);
            break;
        case 2:
            printf(" L2 %u %u %u %u %u %u %d", e->l2.target_max_pq, e->l2.trim_slope, e->l2.trim_offset,
                   e->l2.trim_power, e->l2.trim_chroma_weight, e->l2.trim_saturation_gain, e->l2.ms_weight);
            break;
        case 5:
            printf(" L5 %u %u %u %u", e->l5.left_offset, e->l5.right_offset, e->l5.top_offset, e->l5.bottom_offset);
            break;
        case 6:
            printf(" L6 %u %u %u %u", e->l6.max_luminance, e->l6.min_luminance, e->l6.max_cll, e->l6.max_fall);
            break;
        default:
            printf(" L%d", e->level);
        }
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: dovi_blocks <clip.mkv>\n");
        return 2;
    }
    av_log_set_level(AV_LOG_ERROR);
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        printf("FAIL cannot open %s\n", argv[1]);
        return 1;
    }
    AVStream *st = fmt->streams[0];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->thread_count = 1;
    dec->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_EXPLODE;
    if (avcodec_open2(dec, codec, NULL) < 0) {
        printf("FAIL cannot open the decoder\n");
        return 1;
    }
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    long n = 0;
    int flushing = 0, status = 0;
    for (;;) {
        int ret;
        if (!flushing) {
            ret = av_read_frame(fmt, pkt);
            if (ret < 0) {
                flushing = 1;
                avcodec_send_packet(dec, NULL);
            } else {
                ret = avcodec_send_packet(dec, pkt);
                av_packet_unref(pkt);
                if (ret < 0) {
                    printf("FAIL the decoder refused a packet (%d)\n", ret);
                    status = 1;
                    break;
                }
            }
        }
        while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
            const AVFrameSideData *fd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
            if (fd)
                print_blocks(n, (const AVDOVIMetadata *)fd->data);
            else
                printf("frame %ld no RPU\n", n);
            ++n;
            av_frame_unref(frame);
        }
        if (flushing && ret == AVERROR_EOF)
            break;
    }
    printf("pictures %ld\n", n);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return status;
}
