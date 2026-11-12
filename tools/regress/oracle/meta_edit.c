/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * meta_edit: change the composing metadata of a picture that ref_dump wrote (meta.bin, the
 * AVDOVIMetadata side data as FFmpeg gave it), for pictures made on purpose: a reading of the clause
 * that the real RPUs never exercise (a sample on an inner pivot, above the last one, an NLQ clamp or
 * threshold, an MMR mapping of order 1, the identity without a residual).
 *
 *   meta_edit <in meta.bin> <out meta.bin> <edit>...
 *     cN.pivots=a,b,...            pivots of component N (sets num_pivots)
 *     cN.pK.poly=order,c0,c1,c2    piece K of component N a polynomial
 *     cN.pK.mmr=order,const,c...   piece K an MMR mapping (7 coefficients per order, order-major)
 *     nlqN=offset,vdr_in_max,slope,threshold
 *     disable_residual=0|1         hdr.disable_residual_flag
 *     nlq_method=0|-1              map.nlq_method_idc
 * Prints the lines of meta.txt (ref_dump's format) that describe the new mapping.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/dovi_meta.h>

static int parse_list(const char *s, long long *v, int max)
{
    int n = 0;
    while (*s && n < max) {
        char *end;
        v[n++] = strtoll(s, &end, 10);
        if (end == s)
            return -1;
        s = *end == ',' ? end + 1 : end;
    }
    return *s ? -1 : n;
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = aligned_alloc(64, (size_t)(size + 63) / 64 * 64);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size)
        return 1;
    fclose(f);
    AVDOVIMetadata *m = buf;
    AVDOVIRpuDataHeader *h = av_dovi_get_header(m);
    AVDOVIDataMapping *map = av_dovi_get_mapping(m);
    for (int a = 3; a < argc; a++) {
        const char *e = argv[a];
        const char *eq = strchr(e, '=');
        long long v[64];
        int c, k, n;
        if (!eq || (n = parse_list(eq + 1, v, 64)) < 1) {
            fprintf(stderr, "meta_edit: bad edit %s\n", e);
            return 2;
        }
        if (sscanf(e, "c%d.pivots=", &c) == 1 && c >= 0 && c < 3 && strstr(e, ".pivots=")) {
            if (n < 2 || n > AV_DOVI_MAX_PIECES + 1)
                return 2;
            map->curves[c].num_pivots = (uint8_t)n;
            for (int i = 0; i < n; i++)
                map->curves[c].pivots[i] = (uint16_t)v[i];
        } else if (sscanf(e, "c%d.p%d.", &c, &k) == 2 && c >= 0 && c < 3 && k >= 0 && k < AV_DOVI_MAX_PIECES) {
            AVDOVIReshapingCurve *cv = &map->curves[c];
            if (strstr(e, ".poly=")) {
                if (n != 4)
                    return 2;
                cv->mapping_idc[k] = AV_DOVI_MAPPING_POLYNOMIAL;
                cv->poly_order[k] = (uint8_t)v[0];
                for (int i = 0; i < 3; i++)
                    cv->poly_coef[k][i] = v[1 + i];
            } else if (strstr(e, ".mmr=")) {
                if (v[0] < 1 || v[0] > 3 || n != 2 + 7 * v[0])
                    return 2;
                cv->mapping_idc[k] = AV_DOVI_MAPPING_MMR;
                cv->mmr_order[k] = (uint8_t)v[0];
                cv->mmr_constant[k] = v[1];
                memset(cv->mmr_coef[k], 0, sizeof(cv->mmr_coef[k]));
                for (int o = 0; o < v[0]; o++)
                    for (int j = 0; j < 7; j++)
                        cv->mmr_coef[k][o][j] = v[2 + 7 * o + j];
            } else {
                return 2;
            }
        } else if (sscanf(e, "nlq%d=", &c) == 1 && c >= 0 && c < 3) {
            if (n != 4)
                return 2;
            map->nlq[c].nlq_offset = (uint16_t)v[0];
            map->nlq[c].vdr_in_max = (uint64_t)v[1];
            map->nlq[c].linear_deadzone_slope = (uint64_t)v[2];
            map->nlq[c].linear_deadzone_threshold = (uint64_t)v[3];
        } else if (!strncmp(e, "disable_residual=", 17)) {
            h->disable_residual_flag = (uint8_t)v[0];
        } else if (!strncmp(e, "nlq_method=", 11)) {
            map->nlq_method_idc = (enum AVDOVINLQMethod)v[0];
        } else {
            fprintf(stderr, "meta_edit: unknown edit %s\n", e);
            return 2;
        }
    }
    f = fopen(argv[2], "wb");
    if (!f || fwrite(buf, 1, (size_t)size, f) != (size_t)size || fclose(f))
        return 1;
    /* the lines of ref_dump's WriteMeta() for what can change */
    printf("hdr.disable_residual_flag %u\n", h->disable_residual_flag);
    for (int c = 0; c < 3; c++) {
        const AVDOVIReshapingCurve *cv = &map->curves[c];
        printf("curve%d.num_pivots %u\ncurve%d.pivots", c, cv->num_pivots, c);
        for (int i = 0; i < cv->num_pivots; i++)
            printf(" %u", cv->pivots[i]);
        printf("\n");
        for (int i = 0; i + 1 < cv->num_pivots; i++) {
            printf("curve%d.piece%d.mapping_idc %d\n", c, i, (int)cv->mapping_idc[i]);
            if (cv->mapping_idc[i] == AV_DOVI_MAPPING_POLYNOMIAL) {
                printf("curve%d.piece%d.poly %u %" PRId64 " %" PRId64 " %" PRId64 "\n", c, i, cv->poly_order[i],
                       cv->poly_coef[i][0], cv->poly_coef[i][1], cv->poly_coef[i][2]);
            } else {
                printf("curve%d.piece%d.mmr_order %u\ncurve%d.piece%d.mmr_constant %" PRId64 "\n", c, i,
                       cv->mmr_order[i], c, i, cv->mmr_constant[i]);
                for (int j = 0; j < cv->mmr_order[i]; j++) {
                    printf("curve%d.piece%d.mmr%d", c, i, j + 1);
                    for (int t = 0; t < 7; t++)
                        printf(" %" PRId64, cv->mmr_coef[i][j][t]);
                    printf("\n");
                }
            }
        }
    }
    printf("map.nlq_method_idc %d\n", (int)map->nlq_method_idc);
    for (int c = 0; c < 3; c++)
        printf("nlq%d %u %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", c, map->nlq[c].nlq_offset, map->nlq[c].vdr_in_max,
               map->nlq[c].linear_deadzone_slope, map->nlq[c].linear_deadzone_threshold);
    return 0;
}
