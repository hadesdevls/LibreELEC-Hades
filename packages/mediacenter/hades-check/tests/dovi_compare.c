/*
 * Copyright (C) 2026 Hades
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host test of the check clips (packages/mediacenter/hades-check): the Dolby Vision metadata of a clip
 * as FFmpeg and Kodi of Hades read them, compared field by field with what Kodi of Hades makes:
 *  - the configuration record of the Matroska track (dvcC/dvvC), as FFmpeg's demuxer exports it and as
 *    Kodi's demuxer takes it (CDVDDemuxFFmpeg::DetermineHdrType() and AddStream(): the record is the
 *    stream's only when it is at least as large as AVDOVIDecoderConfigurationRecord);
 *  - every access unit as the bridge reads it before decoding (dvbridge_rpu_probe_packet() and the
 *    residual probe of tools/dvbridge/dvbridge_probe.c, compiled in from the Kodi tree): one RPU of
 *    type 2, its header, the profile that FFmpeg guesses from it, no enhancement layer, no residual;
 *  - the RPU of every picture as the HEVC decoder exports it (AV_FRAME_DATA_DOVI_METADATA), against
 *    dvbridge_profile81_metadata() (tools/dvbridge/dvbridge_hdr10plus.c, compiled in) for the same
 *    level 1, mastering display and content light; for profile 5 that RPU as dovi_tool makes profile 5
 *    of it (profile5_config(): the header of profile 8.1 with vdr_rpu_profile 0 and the base layer in
 *    full range, the same identity mapping, the IPT-PQ-c2 matrices and signal_color_space 2), the
 *    matrices taken from tools/dvbridge/dvbridge_ipt.h and checked against dovi_tool's.
 * FFmpeg exports the extension blocks of an RPU static ones first (dovi_rpudec.c, ff_dovi_get_metadata()),
 * so they are matched by level; each level once.
 *
 * usage: dovi_compare <clip.mkv> <profile> <level> <compat> <l1 min> <l1 max> <l1 avg>
 *                     <mastering max cd/m2> <mastering min x10000> <MaxCLL> <MaxFALL> <frames>
 * Prints every failed check as "FAIL ..."; the last line is "PASS: n checks, 0 failed" or "FAIL: ...";
 * exit 0 only when every check passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mastering_display_metadata.h>

#include "dvbridge_hdr10plus.h"
#include "dvbridge_ipt.h"
#include "dvbridge_probe.h"

static int checks, failures, logged;

static void check(int ok, const char *what, long long got, long long want)
{
    ++checks;
    if (!ok) {
        ++failures;
        if (failures <= 40)
            printf("FAIL %s: got %lld, want %lld\n", what, got, want);
    }
}
#define EQ(what, got, want) check((long long)(got) == (long long)(want), what, (long long)(got), (long long)(want))
#define EQQ(what, got, want)                                                                              \
    check(av_cmp_q(got, want) == 0, what, (long long)((got).num * 1000000LL / ((got).den ? (got).den : 1)), \
          (long long)((want).num * 1000000LL / ((want).den ? (want).den : 1)))

static void log_cb(void *avcl, int level, const char *fmt, va_list vl)
{
    (void)avcl;
    if (level <= AV_LOG_WARNING) {
        ++logged;
        vfprintf(stdout, fmt, vl);
    }
}

/* dovi_tool 2.3.4 (dolby_vision 3.4.0, commit 614c816b6446dcd1dbaf433403d499a6026fbb5a, MIT, read as data):
 * dolby_vision/src/rpu/profiles/profile5.rs, Profile5::dm_data() */
static const int DOVI_TOOL_P5_YCC[9] = {8192, 799, 1681, 8192, -933, 1091, 8192, 267, -5545};
static const unsigned DOVI_TOOL_P5_OFFSETS[3] = {0, 134217728, 134217728};
static const int DOVI_TOOL_P5_LMS[9] = {17081, -349, -349, -349, 17081, -349, -349, -349, 17081};

static const AVDOVIDmData *ext_of_level(const AVDOVIMetadata *m, int level, int *count)
{
    const AVDOVIDmData *found = NULL;
    *count = 0;
    for (int i = 0; i < m->num_ext_blocks; i++) {
        const AVDOVIDmData *e = av_dovi_get_ext(m, i);
        if (e->level == level) {
            if (!found)
                found = e;
            ++*count;
        }
    }
    return found;
}

static void compare(const AVDOVIMetadata *got, const AVDOVIMetadata *want, long frame)
{
    char w[128];
    const AVDOVIRpuDataHeader *gh = av_dovi_get_header(got), *wh = av_dovi_get_header(want);
#define H(f) snprintf(w, sizeof(w), "frame %ld header." #f, frame), EQ(w, gh->f, wh->f)
    H(rpu_type); H(rpu_format); H(vdr_rpu_profile); H(vdr_rpu_level); H(chroma_resampling_explicit_filter_flag);
    H(coef_data_type); H(coef_log2_denom); H(vdr_rpu_normalized_idc); H(bl_video_full_range_flag);
    H(bl_bit_depth); H(el_bit_depth); H(vdr_bit_depth); H(spatial_resampling_filter_flag);
    H(el_spatial_resampling_filter_flag); H(disable_residual_flag); H(ext_mapping_idc_0_4); H(ext_mapping_idc_5_7);
#undef H
    const AVDOVIDataMapping *gm = av_dovi_get_mapping(got), *wm = av_dovi_get_mapping(want);
#define M(f) snprintf(w, sizeof(w), "frame %ld mapping." #f, frame), EQ(w, gm->f, wm->f)
    M(vdr_rpu_id); M(mapping_color_space); M(mapping_chroma_format_idc); M(nlq_method_idc);
    M(num_x_partitions); M(num_y_partitions);
#undef M
    for (int c = 0; c < 3; c++) {
        const AVDOVIReshapingCurve *g = &gm->curves[c], *x = &wm->curves[c];
        snprintf(w, sizeof(w), "frame %ld curve %d num_pivots", frame, c), EQ(w, g->num_pivots, x->num_pivots);
        for (int i = 0; i < x->num_pivots && i < AV_DOVI_MAX_PIECES + 1; i++)
            snprintf(w, sizeof(w), "frame %ld curve %d pivot %d", frame, c, i), EQ(w, g->pivots[i], x->pivots[i]);
        for (int i = 0; i + 1 < x->num_pivots && i < AV_DOVI_MAX_PIECES; i++) {
            snprintf(w, sizeof(w), "frame %ld curve %d piece %d mapping_idc", frame, c, i);
            EQ(w, g->mapping_idc[i], x->mapping_idc[i]);
            snprintf(w, sizeof(w), "frame %ld curve %d piece %d poly_order", frame, c, i);
            EQ(w, g->poly_order[i], x->poly_order[i]);
            for (int k = 0; k < 3; k++)
                snprintf(w, sizeof(w), "frame %ld curve %d piece %d poly_coef %d", frame, c, i, k),
                    EQ(w, g->poly_coef[i][k], x->poly_coef[i][k]);
        }
    }
    const AVDOVIColorMetadata *gc = av_dovi_get_color(got), *wc = av_dovi_get_color(want);
    for (int i = 0; i < 9; i++) {
        snprintf(w, sizeof(w), "frame %ld color.ycc_to_rgb_matrix[%d] (x1e6)", frame, i);
        EQQ(w, gc->ycc_to_rgb_matrix[i], wc->ycc_to_rgb_matrix[i]);
        snprintf(w, sizeof(w), "frame %ld color.rgb_to_lms_matrix[%d] (x1e6)", frame, i);
        EQQ(w, gc->rgb_to_lms_matrix[i], wc->rgb_to_lms_matrix[i]);
    }
    for (int i = 0; i < 3; i++) {
        snprintf(w, sizeof(w), "frame %ld color.ycc_to_rgb_offset[%d] (x1e6)", frame, i);
        EQQ(w, gc->ycc_to_rgb_offset[i], wc->ycc_to_rgb_offset[i]);
    }
#define C(f) snprintf(w, sizeof(w), "frame %ld color." #f, frame), EQ(w, gc->f, wc->f)
    C(dm_metadata_id); C(scene_refresh_flag); C(signal_eotf); C(signal_eotf_param0); C(signal_eotf_param1);
    C(signal_eotf_param2); C(signal_bit_depth); C(signal_color_space); C(signal_chroma_format);
    C(signal_full_range_flag); C(source_min_pq); C(source_max_pq); C(source_diagonal);
#undef C
    snprintf(w, sizeof(w), "frame %ld num_ext_blocks", frame), EQ(w, got->num_ext_blocks, want->num_ext_blocks);
    /* every level of the reference once in what was read, and nothing else */
    for (int i = 0; i < want->num_ext_blocks; i++) {
        const AVDOVIDmData *x = av_dovi_get_ext(want, i);
        int n = 0;
        const AVDOVIDmData *g = ext_of_level(got, x->level, &n);
        snprintf(w, sizeof(w), "frame %ld blocks of level %d", frame, x->level), EQ(w, n, 1);
        if (!g)
            continue;
        if (x->level == 1) {
            snprintf(w, sizeof(w), "frame %ld L1 min_pq", frame), EQ(w, g->l1.min_pq, x->l1.min_pq);
            snprintf(w, sizeof(w), "frame %ld L1 max_pq", frame), EQ(w, g->l1.max_pq, x->l1.max_pq);
            snprintf(w, sizeof(w), "frame %ld L1 avg_pq", frame), EQ(w, g->l1.avg_pq, x->l1.avg_pq);
        } else if (x->level == 6) {
            snprintf(w, sizeof(w), "frame %ld L6 max_luminance", frame), EQ(w, g->l6.max_luminance, x->l6.max_luminance);
            snprintf(w, sizeof(w), "frame %ld L6 min_luminance", frame), EQ(w, g->l6.min_luminance, x->l6.min_luminance);
            snprintf(w, sizeof(w), "frame %ld L6 max_cll", frame), EQ(w, g->l6.max_cll, x->l6.max_cll);
            snprintf(w, sizeof(w), "frame %ld L6 max_fall", frame), EQ(w, g->l6.max_fall, x->l6.max_fall);
        } else {
            snprintf(w, sizeof(w), "frame %ld ext block of level %d: not compared here", frame, x->level);
            EQ(w, x->level, -1);
        }
    }
    for (int i = 0; i < got->num_ext_blocks; i++) {
        int n = 0;
        const AVDOVIDmData *g = av_dovi_get_ext(got, i);
        ext_of_level(want, g->level, &n);
        snprintf(w, sizeof(w), "frame %ld a block of level %d that the reference does not have", frame, g->level);
        check(n == 1, w, g->level, 0);
    }
}

int main(int argc, char **argv)
{
    if (argc != 13) {
        fprintf(stderr, "usage: see the source\n");
        return 2;
    }
    const char *path = argv[1];
    const int profile = atoi(argv[2]), level = atoi(argv[3]), compat = atoi(argv[4]);
    const unsigned l1min = atoi(argv[5]), l1max = atoi(argv[6]), l1avg = atoi(argv[7]);
    const int mmax = atoi(argv[8]), mmin = atoi(argv[9]), cll = atoi(argv[10]), fall = atoi(argv[11]);
    const long frames = atol(argv[12]);
    av_log_set_callback(log_cb);

    /* the matrices of the bridge's IPT tunnel are those of dovi_tool's profile 5 */
    {
        static const int ycc[9] = DVBRIDGE_IPT_YCC, lms[9] = DVBRIDGE_IPT_LMS;
        static const unsigned off[3] = DVBRIDGE_IPT_OFFSETS;
        char w[96];
        for (int i = 0; i < 9; i++) {
            snprintf(w, sizeof(w), "dvbridge_ipt.h YCC[%d] as dovi_tool's profile 5", i), EQ(w, ycc[i], DOVI_TOOL_P5_YCC[i]);
            snprintf(w, sizeof(w), "dvbridge_ipt.h LMS[%d] as dovi_tool's profile 5", i), EQ(w, lms[i], DOVI_TOOL_P5_LMS[i]);
        }
        for (int i = 0; i < 3; i++)
            snprintf(w, sizeof(w), "dvbridge_ipt.h OFFSETS[%d] as dovi_tool's profile 5", i),
                EQ(w, off[i], DOVI_TOOL_P5_OFFSETS[i]);
    }

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        printf("FAIL cannot open %s\n", path);
        return 1;
    }
    EQ("streams in the file", fmt->nb_streams, 1);
    AVStream *st = fmt->streams[0];
    const AVPacketSideData *sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                                         st->codecpar->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
    check(sd != NULL, "the track has a Dolby Vision configuration record", sd != NULL, 1);
    /* Kodi: CDVDDemuxFFmpeg::AddStream() copies the record only when it is this large */
    check(sd && sd->size >= sizeof(AVDOVIDecoderConfigurationRecord),
          "the record is as large as Kodi's demuxer takes it", sd ? (long long)sd->size : -1,
          (long long)sizeof(AVDOVIDecoderConfigurationRecord));
    if (sd) {
        const AVDOVIDecoderConfigurationRecord *cfg = (const void *)sd->data;
        EQ("config dv_version_major", cfg->dv_version_major, 1);
        EQ("config dv_version_minor", cfg->dv_version_minor, 0);
        EQ("config dv_profile", cfg->dv_profile, profile);
        EQ("config dv_level", cfg->dv_level, level);
        EQ("config rpu_present_flag", cfg->rpu_present_flag, 1);
        EQ("config el_present_flag", cfg->el_present_flag, 0);
        EQ("config bl_present_flag", cfg->bl_present_flag, 1);
        EQ("config dv_bl_signal_compatibility_id", cfg->dv_bl_signal_compatibility_id, compat);
        EQ("config dv_md_compression", cfg->dv_md_compression, 0);
    }
    /* (Kodi, CDVDDemuxFFmpeg::DetermineHdrType(): the record makes the stream Dolby Vision whatever its
     * transfer; the transfer is still that of the base layer: PQ) */
    EQ("color_trc of the track (PQ)", st->codecpar->color_trc, AVCOL_TRC_SMPTE2084);
    /* the length of the units' lengths, as Kodi's CDVLayerMerge::LengthSize() reads it from hvcC */
    const AVCodecParameters *par = st->codecpar;
    const int length_size = par->extradata && par->extradata_size >= 23 && par->extradata[0] == 1
                                ? (par->extradata[21] & 3) + 1
                                : 0;
    EQ("hvcC: units after 4-byte lengths", length_size, 4);

    /* the reference: what Kodi of Hades makes for these values */
    AVMasteringDisplayMetadata mastering = {0};
    mastering.has_luminance = 1;
    mastering.max_luminance = av_make_q(mmax, 1);
    mastering.min_luminance = av_make_q(mmin, 10000);
    AVContentLightMetadata light = {.MaxCLL = cll, .MaxFALL = fall};
    size_t size = 0;
    AVDOVIMetadata *want[2];
    for (int k = 0; k < 2; k++) {
        want[k] = dvbridge_profile81_metadata(l1min, l1max, l1avg, NULL, &mastering, &light, k == 0, &size);
        if (!want[k]) {
            printf("FAIL dvbridge_profile81_metadata() made nothing\n");
            return 1;
        }
        if (profile == 5) {
            /* dovi_tool's DoviRpu::profile5_config(): RpuDataHeader::p5_default() (that of profile 8.1
             * with vdr_rpu_profile 0 and bl_video_full_range_flag 1), Profile81::rpu_data_mapping() (the
             * identity), Profile5::dm_data() (the IPT-PQ-c2 matrices, signal_color_space 2, the rest as
             * profile 8.1's VdrDmData::default_pq()) */
            static const int ycc[9] = DVBRIDGE_IPT_YCC, lms[9] = DVBRIDGE_IPT_LMS;
            static const unsigned off[3] = DVBRIDGE_IPT_OFFSETS;
            AVDOVIRpuDataHeader *h = av_dovi_get_header(want[k]);
            AVDOVIColorMetadata *c = av_dovi_get_color(want[k]);
            h->vdr_rpu_profile = 0;
            h->bl_video_full_range_flag = 1;
            for (int i = 0; i < 9; i++) {
                c->ycc_to_rgb_matrix[i] = av_make_q(ycc[i], 8192);
                c->rgb_to_lms_matrix[i] = av_make_q(lms[i], 16384);
            }
            for (int i = 0; i < 3; i++)
                c->ycc_to_rgb_offset[i] = av_make_q(off[i], 1 << 28);
            c->signal_color_space = 2;
        }
    }

    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->thread_count = 1;
    dec->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_EXPLODE;
    if (avcodec_open2(dec, codec, NULL) < 0) {
        printf("FAIL cannot open the decoder\n");
        return 1;
    }
    struct dvbridge_residual_probe *residual = dvbridge_residual_probe_create(par->extradata, par->extradata_size);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    long n = 0, with = 0, packets = 0;
    int flushing = 0;
    for (;;) {
        int ret;
        if (!flushing) {
            ret = av_read_frame(fmt, pkt);
            if (ret < 0) {
                flushing = 1;
                avcodec_send_packet(dec, NULL);
            } else {
                /* the access unit as the bridge reads it before decoding */
                struct dvbridge_rpu_probe probe;
                char w[96];
                dvbridge_rpu_probe_packet(pkt->data, pkt->size, length_size, &probe);
                snprintf(w, sizeof(w), "packet %ld: the bridge finds an RPU", packets), EQ(w, probe.rpu, 1);
                snprintf(w, sizeof(w), "packet %ld: no enhancement layer", packets), EQ(w, probe.layer, 0);
                snprintf(w, sizeof(w), "packet %ld: vdr_rpu_profile", packets), EQ(w, probe.rpu_profile, profile == 5 ? 0 : 1);
                snprintf(w, sizeof(w), "packet %ld: vdr_bit_depth", packets), EQ(w, probe.vdr_bit_depth, 12);
                snprintf(w, sizeof(w), "packet %ld: bl_video_full_range_flag", packets), EQ(w, probe.bl_full_range, profile == 5);
                snprintf(w, sizeof(w), "packet %ld: el_spatial_resampling_filter_flag", packets), EQ(w, probe.el_resampling, 0);
                snprintf(w, sizeof(w), "packet %ld: disable_residual_flag", packets), EQ(w, probe.residual_disabled, 1);
                snprintf(w, sizeof(w), "packet %ld: the profile FFmpeg guesses from the RPU", packets), EQ(w, probe.profile, profile);
                dvbridge_residual_probe_packet(residual, pkt->data, pkt->size);
                ++packets;
                ret = avcodec_send_packet(dec, pkt);
                av_packet_unref(pkt);
                if (ret < 0) {
                    printf("FAIL the decoder refused a packet (%d)\n", ret);
                    ++failures;
                    break;
                }
            }
        }
        while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
            const AVFrameSideData *fd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
            char w[96];
            snprintf(w, sizeof(w), "frame %ld has an RPU", n);
            check(fd != NULL, w, fd != NULL, 1);
            if (fd) {
                ++with;
                compare((const AVDOVIMetadata *)fd->data, want[n == 0 ? 0 : 1], n);
            }
            ++n;
            av_frame_unref(frame);
        }
        if (ret == AVERROR_EOF)
            break;
        if (ret != AVERROR(EAGAIN)) {
            printf("FAIL decoding (%d)\n", ret);
            ++failures;
            break;
        }
    }
    const struct dvbridge_residual_counts counts = dvbridge_residual_probe_counts(residual);
    EQ("packets read", packets, frames);
    EQ("residual probe: packets", counts.packets, frames);
    EQ("residual probe: RPUs without a residual", counts.no_residual, frames);
    EQ("residual probe: RPUs with one", counts.residual, 0);
    EQ("residual probe: RPUs not read", counts.unread, 0);
    EQ("pictures decoded", n, frames);
    EQ("pictures with an RPU", with, frames);
    EQ("warnings or errors logged by FFmpeg", logged, 0);
    printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures);
    dvbridge_residual_probe_destroy(residual);
    av_free(want[0]);
    av_free(want[1]);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return failures ? 1 : 0;
}
