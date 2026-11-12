/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * rpu_sets: decode the video track of a clip with FFmpeg and print, for every output picture,
 * the composer parameters of its Dolby Vision RPU (AV_FRAME_DATA_DOVI_METADATA) as one JSON
 * line, with the field names of libavutil/dovi_meta.h. Host test tool; it reads the clip as data.
 *
 *   rpu_sets <clip> [max_frames]
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/frame.h>

static void print_i64_list(const int64_t *v, int n)
{
    putchar('[');
    for (int i = 0; i < n; i++)
        printf("%s%" PRId64, i ? "," : "", v[i]);
    putchar(']');
}

static void print_frame(long idx, double pts, const AVDOVIMetadata *m)
{
    const AVDOVIRpuDataHeader *h = av_dovi_get_header(m);
    const AVDOVIDataMapping *map = av_dovi_get_mapping(m);

    printf("{\"frame\":%ld,\"pts\":%.6f,\"hdr\":{", idx, pts);
    printf("\"rpu_type\":%d,\"rpu_format\":%d,\"vdr_rpu_profile\":%d,\"vdr_rpu_level\":%d,",
           h->rpu_type, h->rpu_format, h->vdr_rpu_profile, h->vdr_rpu_level);
    printf("\"chroma_resampling_explicit_filter_flag\":%d,\"coef_data_type\":%d,"
           "\"coef_log2_denom\":%d,\"vdr_rpu_normalized_idc\":%d,\"bl_video_full_range_flag\":%d,",
           h->chroma_resampling_explicit_filter_flag, h->coef_data_type, h->coef_log2_denom,
           h->vdr_rpu_normalized_idc, h->bl_video_full_range_flag);
    printf("\"bl_bit_depth\":%d,\"el_bit_depth\":%d,\"vdr_bit_depth\":%d,"
           "\"spatial_resampling_filter_flag\":%d,\"el_spatial_resampling_filter_flag\":%d,"
           "\"disable_residual_flag\":%d,\"ext_mapping_idc_0_4\":%d,\"ext_mapping_idc_5_7\":%d},",
           h->bl_bit_depth, h->el_bit_depth, h->vdr_bit_depth, h->spatial_resampling_filter_flag,
           h->el_spatial_resampling_filter_flag, h->disable_residual_flag,
           h->ext_mapping_idc_0_4, h->ext_mapping_idc_5_7);
    printf("\"map\":{\"vdr_rpu_id\":%d,\"mapping_color_space\":%d,\"mapping_chroma_format_idc\":%d,"
           "\"nlq_method_idc\":%d,\"num_x_partitions\":%u,\"num_y_partitions\":%u,"
           "\"nlq_pivots\":[%u,%u],\"curves\":[",
           map->vdr_rpu_id, map->mapping_color_space, map->mapping_chroma_format_idc,
           (int)map->nlq_method_idc, map->num_x_partitions, map->num_y_partitions,
           map->nlq_pivots[0], map->nlq_pivots[1]);
    for (int c = 0; c < 3; c++) {
        const AVDOVIReshapingCurve *cv = &map->curves[c];
        int n = cv->num_pivots;
        int np = n > 1 ? n - 1 : 0;
        if (np > AV_DOVI_MAX_PIECES)
            np = AV_DOVI_MAX_PIECES;
        printf("%s{\"num_pivots\":%d,\"pivots\":[", c ? "," : "", n);
        for (int i = 0; i < n && i <= AV_DOVI_MAX_PIECES; i++)
            printf("%s%u", i ? "," : "", cv->pivots[i]);
        printf("],\"mapping_idc\":[");
        for (int i = 0; i < np; i++)
            printf("%s%d", i ? "," : "", (int)cv->mapping_idc[i]);
        printf("],\"poly_order\":[");
        for (int i = 0; i < np; i++)
            printf("%s%d", i ? "," : "", cv->poly_order[i]);
        printf("],\"poly_coef\":[");
        for (int i = 0; i < np; i++) {
            if (i)
                putchar(',');
            print_i64_list(cv->poly_coef[i], 3);
        }
        printf("],\"mmr_order\":[");
        for (int i = 0; i < np; i++)
            printf("%s%d", i ? "," : "", cv->mmr_order[i]);
        printf("],\"mmr_constant\":");
        print_i64_list(cv->mmr_constant, np);
        printf(",\"mmr_coef\":[");
        for (int i = 0; i < np; i++) {
            printf("%s[", i ? "," : "");
            for (int j = 0; j < 3; j++) {
                if (j)
                    putchar(',');
                print_i64_list(cv->mmr_coef[i][j], 7);
            }
            putchar(']');
        }
        printf("]}");
    }
    printf("],\"nlq\":[");
    for (int c = 0; c < 3; c++) {
        const AVDOVINLQParams *q = &map->nlq[c];
        printf("%s{\"nlq_offset\":%u,\"vdr_in_max\":%" PRIu64 ",\"linear_deadzone_slope\":%" PRIu64
               ",\"linear_deadzone_threshold\":%" PRIu64 "}", c ? "," : "", q->nlq_offset,
               q->vdr_in_max, q->linear_deadzone_slope, q->linear_deadzone_threshold);
    }
    printf("]}}\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <clip> [max_frames]\n", argv[0]);
        return 2;
    }
    long max_frames = argc > 2 ? atol(argv[2]) : -1;
    av_log_set_level(AV_LOG_ERROR);
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (si < 0) {
        fprintf(stderr, "no video stream\n");
        return 1;
    }
    AVStream *st = fmt->streams[si];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->thread_count = 8;
    dec->skip_loop_filter = AVDISCARD_ALL; /* pixels are not used here, only the side data */
    if (avcodec_open2(dec, codec, NULL) < 0) {
        fprintf(stderr, "cannot open decoder\n");
        return 1;
    }
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    long idx = 0, without = 0;
    int eof = 0;
    while (!eof && (max_frames < 0 || idx < max_frames)) {
        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            eof = 1;
            avcodec_send_packet(dec, NULL);
        } else {
            if (pkt->stream_index == si)
                avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
        }
        while (avcodec_receive_frame(dec, frame) >= 0) {
            const AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
            double pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? -1.0
                       : frame->best_effort_timestamp * av_q2d(st->time_base);
            if (!sd) {
                printf("{\"frame\":%ld,\"pts\":%.6f,\"no_rpu\":1}\n", idx, pts);
                without++;
            } else {
                print_frame(idx, pts, (const AVDOVIMetadata *)sd->data);
            }
            idx++;
            av_frame_unref(frame);
            if (max_frames >= 0 && idx >= max_frames)
                break;
        }
    }
    fprintf(stderr, "frames %ld, without RPU %ld\n", idx, without);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return 0;
}
