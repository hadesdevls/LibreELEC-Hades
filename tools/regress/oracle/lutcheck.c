/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * lutcheck: the CPU part of libplacebo's composer of ETSI GS CCM 001 (pl_dovi_ccm_prepare() and
 * pl_dovi_ccm_mmr_table() of src/dovi_ccm.c) for RPUs given on stdin, its tables written to stdout
 * for oracle_luts.py to compare with the oracle. Each RPU goes the way it goes in Kodi's bridge:
 * an AVDOVIMetadata (FFmpeg) mapped by pl_map_avdovi_metadata() (libplacebo/utils/libav.h).
 *
 * stdin, one RPU a line (numbers separated by spaces):
 *   bl el vdr denom coef_data_type disable_residual nlq_method
 *   then for each of the 3 components: num_pivots, the pivots, and for each piece:
 *     mapping_idc poly_order c0 c1 c2 mmr_order mmr_constant and 21 mmr coefficients (order-major)
 *   then for each component: nlq_offset vdr_in_max slope threshold
 * stdout, for each line: "LUTCHECK <n> <ok> <reason or ->\n", then (binary, little endian) when ok:
 *   float lut[6][2048]; int32 range[3][2];
 *   for each chroma component: int32 num_pieces, pivots[7], mmr_mask, terms;
 *     uint32 q[8][24]; float r[8][24]; float f[8][24];
 *   uint32 mmr table: 48 rows x 2^bl x 4 (pl_dovi_ccm_mmr_table()).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libplacebo/utils/libav.h>

#include "dovi_ccm.h"

static int rd(long long *v)
{
    return scanf("%lld", v) == 1;
}

int main(void)
{
    long n = 0;
    struct pl_dovi_ccm_prep *prep = calloc(1, sizeof(*prep));
    uint32_t *table = malloc((size_t)PL_DOVI_CCM_MMRT_ROWS * 1024 * 4 * sizeof(uint32_t));
    if (!prep || !table)
        return 1;
    for (;;) {
        long long v[7];
        int ok = 1;
        for (int i = 0; i < 7 && ok; i++)
            ok = rd(&v[i]);
        if (!ok)
            break;
        size_t size = 0;
        AVDOVIMetadata *m = av_dovi_metadata_alloc(&size);
        if (!m)
            return 1;
        AVDOVIRpuDataHeader *h = av_dovi_get_header(m);
        AVDOVIDataMapping *map = av_dovi_get_mapping(m);
        AVDOVIColorMetadata *col = av_dovi_get_color(m);
        h->rpu_type = 2;
        h->rpu_format = 18;
        h->vdr_rpu_profile = 1;
        h->bl_bit_depth = (uint8_t)v[0];
        h->el_bit_depth = (uint8_t)v[1];
        h->vdr_bit_depth = (uint8_t)v[2];
        h->coef_log2_denom = (uint8_t)v[3];
        h->coef_data_type = (uint8_t)v[4];
        h->disable_residual_flag = (uint8_t)v[5];
        map->nlq_method_idc = (enum AVDOVINLQMethod)v[6];
        map->num_x_partitions = map->num_y_partitions = 1;
        for (int c = 0; c < 3 && ok; c++) {
            AVDOVIReshapingCurve *cv = &map->curves[c];
            long long np;
            ok = rd(&np) && np >= 2 && np <= AV_DOVI_MAX_PIECES + 1;
            if (!ok)
                break;
            cv->num_pivots = (uint8_t)np;
            for (int i = 0; i < np && ok; i++) {
                long long p;
                ok = rd(&p);
                cv->pivots[i] = (uint16_t)p;
            }
            for (int k = 0; k < np - 1 && ok; k++) {
                long long f[28];
                for (int i = 0; i < 28 && ok; i++)
                    ok = rd(&f[i]);
                cv->mapping_idc[k] = (enum AVDOVIMappingMethod)f[0];
                cv->poly_order[k] = (uint8_t)f[1];
                for (int i = 0; i < 3; i++)
                    cv->poly_coef[k][i] = f[2 + i];
                cv->mmr_order[k] = (uint8_t)f[5];
                cv->mmr_constant[k] = f[6];
                for (int o = 0; o < 3; o++)
                    for (int j = 0; j < 7; j++)
                        cv->mmr_coef[k][o][j] = f[7 + 7 * o + j];
            }
        }
        for (int c = 0; c < 3 && ok; c++) {
            long long q[4];
            for (int i = 0; i < 4 && ok; i++)
                ok = rd(&q[i]);
            map->nlq[c].nlq_offset = (uint16_t)q[0];
            map->nlq[c].vdr_in_max = (uint64_t)q[1];
            map->nlq[c].linear_deadzone_slope = (uint64_t)q[2];
            map->nlq[c].linear_deadzone_threshold = (uint64_t)q[3];
        }
        if (!ok) {
            fprintf(stderr, "lutcheck: line %ld is short\n", n);
            return 1;
        }
        for (int i = 0; i < 9; i++) {
            col->ycc_to_rgb_matrix[i] = (AVRational){i % 4 == 0, 1};
            col->rgb_to_lms_matrix[i] = (AVRational){i % 4 == 0, 1};
        }
        for (int i = 0; i < 3; i++)
            col->ycc_to_rgb_offset[i] = (AVRational){0, 1};
        col->signal_eotf = 0xFFFF;
        col->signal_bit_depth = (uint8_t)v[2];

        struct pl_color_space color = {0};
        struct pl_color_repr repr = {0};
        struct pl_dovi_metadata dovi;
        memset(&dovi, 0, sizeof(dovi));
        pl_map_avdovi_metadata(&color, &repr, &dovi, m);
        memset(prep, 0, sizeof(*prep));
        const bool good = pl_dovi_ccm_prepare(prep, &dovi.ccm);
        printf("LUTCHECK %ld %d %s\n", n, good ? 1 : 0, good ? "-" : (prep->reason ? prep->reason : "?"));
        if (good) {
            fwrite(prep->lut, sizeof(float), 6 * PL_DOVI_CCM_LUT_W, stdout);
            int32_t range[6];
            for (int c = 0; c < 3; c++) {
                range[2 * c] = prep->range[c][0];
                range[2 * c + 1] = prep->range[c][1];
            }
            fwrite(range, sizeof(range), 1, stdout);
            for (int ci = 0; ci < 2; ci++) {
                const struct pl_dovi_ccm_chroma *ch = &prep->chroma[ci];
                int32_t hd[10] = {ch->num_pieces};
                for (int i = 0; i < 7; i++)
                    hd[1 + i] = ch->pivots[i];
                hd[8] = ch->mmr_mask;
                hd[9] = ch->terms;
                fwrite(hd, sizeof(hd), 1, stdout);
                fwrite(ch->q, sizeof(ch->q), 1, stdout);
                fwrite(ch->r, sizeof(ch->r), 1, stdout);
                fwrite(ch->f, sizeof(ch->f), 1, stdout);
            }
            const size_t w = (size_t)1 << prep->bl_depth;
            pl_dovi_ccm_mmr_table(prep, table);
            fwrite(table, sizeof(uint32_t), PL_DOVI_CCM_MMRT_ROWS * w * 4, stdout);
        }
        fflush(stdout);
        av_free(m);
        n++;
    }
    fprintf(stderr, "lutcheck: %ld RPUs\n", n);
    return 0;
}
