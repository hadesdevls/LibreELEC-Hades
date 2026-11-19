// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (profile 8.4): the fields of a Dolby Vision RPU (an HEVC unspec62 NAL
// unit as dovi_tool's test assets keep it: a 4-byte start code, then the escaped RPU from its 0x19 prefix), parsed
// by FFmpeg's own RPU parser (ff_dovi_rpu_parse(), linked from the static libavcodec of FFmpeg's source tree), written as the
// picture directory keys that the composer tests read (meta.txt) and printed.
// Usage: p84_rpuinfo <rpu file> [<meta.txt to write> <bl width> <bl height>]
#include "libavcodec/dovi_rpu.h"
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 5)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    static uint8_t raw[1 << 16], rbsp[1 << 16];
    size_t n = fread(raw, 1, sizeof raw, f);
    fclose(f);
    size_t at = 0;
    while (at + 1 < n && raw[at] != 0x19)
        at++;
    // emulation prevention removed (00 00 03 -> 00 00), as the HEVC decoder hands the RPU to the parser
    size_t m = 0;
    int zeros = 0;
    for (size_t i = at; i < n; i++) {
        if (zeros >= 2 && raw[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = raw[i] == 0 ? zeros + 1 : 0;
        rbsp[m++] = raw[i];
    }
    DOVIContext s;
    memset(&s, 0, sizeof s);
    s.cfg.dv_profile = 8;
    int ret = ff_dovi_rpu_parse(&s, rbsp, m, 0);
    if (ret < 0 || !s.mapping || !s.color) {
        fprintf(stderr, "parse failed %d\n", ret);
        return 1;
    }
    const AVDOVIRpuDataHeader *h = &s.header;
    const AVDOVIDataMapping *map = s.mapping;
    const AVDOVIColorMetadata *c = s.color;
    FILE *o = argc == 5 ? fopen(argv[2], "w") : stdout;
    if (!o)
        return 1;
    if (argc == 5) {
        fprintf(o, "bl.width %s\nbl.height %s\nbl.chroma_location 2 top\nel.width 0\nel.height 0\n"
                   "el.chroma_location 2 top\n", argv[3], argv[4]);
    }
    fprintf(o, "hdr.rpu_type %d\nhdr.rpu_format %d\nhdr.vdr_rpu_profile %d\nhdr.vdr_rpu_level %d\n",
            h->rpu_type, h->rpu_format, h->vdr_rpu_profile, h->vdr_rpu_level);
    fprintf(o, "hdr.coef_data_type %d\nhdr.coef_log2_denom %d\nhdr.bl_video_full_range_flag %d\n",
            h->coef_data_type, h->coef_log2_denom, h->bl_video_full_range_flag);
    fprintf(o, "hdr.bl_bit_depth %d\nhdr.el_bit_depth %d\nhdr.vdr_bit_depth %d\nhdr.disable_residual_flag %d\n",
            h->bl_bit_depth, h->el_bit_depth, h->vdr_bit_depth, h->disable_residual_flag);
    fprintf(o, "map.mapping_color_space %d\nmap.mapping_chroma_format_idc %d\nmap.nlq_method_idc %d\n",
            map->mapping_color_space, map->mapping_chroma_format_idc, (int)map->nlq_method_idc);
    for (int k = 0; k < 3; k++) {
        const AVDOVIReshapingCurve *cv = &map->curves[k];
        fprintf(o, "curve%d.num_pivots %d\ncurve%d.pivots", k, cv->num_pivots, k);
        for (int i = 0; i < cv->num_pivots; i++)
            fprintf(o, " %d", cv->pivots[i]);
        fprintf(o, "\n");
        for (int i = 0; i + 1 < cv->num_pivots; i++) {
            fprintf(o, "curve%d.piece%d.mapping_idc %d\n", k, i, (int)cv->mapping_idc[i]);
            if (cv->mapping_idc[i] == AV_DOVI_MAPPING_POLYNOMIAL) {
                fprintf(o, "curve%d.piece%d.poly %d %lld %lld %lld\n", k, i, cv->poly_order[i],
                        (long long)cv->poly_coef[i][0], (long long)cv->poly_coef[i][1],
                        (long long)cv->poly_coef[i][2]);
            } else {
                fprintf(o, "curve%d.piece%d.mmr_order %d\ncurve%d.piece%d.mmr_constant %lld\n", k, i,
                        cv->mmr_order[i], k, i, (long long)cv->mmr_constant[i]);
                for (int j = 0; j < cv->mmr_order[i]; j++) {
                    fprintf(o, "curve%d.piece%d.mmr%d", k, i, j + 1);
                    for (int t = 0; t < 7; t++)
                        fprintf(o, " %lld", (long long)cv->mmr_coef[i][j][t]);
                    fprintf(o, "\n");
                }
            }
        }
        fprintf(o, "nlq%d %d %llu %llu %llu\n", k, map->nlq[k].nlq_offset,
                (unsigned long long)map->nlq[k].vdr_in_max,
                (unsigned long long)map->nlq[k].linear_deadzone_slope,
                (unsigned long long)map->nlq[k].linear_deadzone_threshold);
    }
    fprintf(o, "color.ycc_to_rgb_matrix");
    for (int i = 0; i < 9; i++)
        fprintf(o, " %d/%d", c->ycc_to_rgb_matrix[i].num, c->ycc_to_rgb_matrix[i].den);
    fprintf(o, "\ncolor.ycc_to_rgb_offset");
    for (int i = 0; i < 3; i++)
        fprintf(o, " %d/%d", c->ycc_to_rgb_offset[i].num, c->ycc_to_rgb_offset[i].den);
    fprintf(o, "\ncolor.rgb_to_lms_matrix");
    for (int i = 0; i < 9; i++)
        fprintf(o, " %d/%d", c->rgb_to_lms_matrix[i].num, c->rgb_to_lms_matrix[i].den);
    fprintf(o, "\ncolor.signal_eotf %u %u %u %u\n", c->signal_eotf, c->signal_eotf_param0,
            c->signal_eotf_param1, c->signal_eotf_param2);
    fprintf(o, "color.signal_bit_depth %d\ncolor.signal_color_space %d\ncolor.signal_chroma_format %d\n"
               "color.signal_full_range_flag %d\ncolor.source_min_pq %d\ncolor.source_max_pq %d\n",
            c->signal_bit_depth, c->signal_color_space, c->signal_chroma_format, c->signal_full_range_flag,
            c->source_min_pq, c->source_max_pq);
    fprintf(o, "guessed_profile %d\n", ff_dovi_guess_profile_hevc(h));
    if (o != stdout)
        fclose(o);
    ff_dovi_ctx_unref(&s);
    return 0;
}
