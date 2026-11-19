// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/* LibreELEC Hades, TEST ONLY: what Hades' measured HDR10-as-DV module (tools/dvbridge/dvbridge_measured.c,
 * default parameters; EM_AVG_FLOOR=<n> sets its average floor) sends for every picture, played through
 * per-picture measurements of a stream (the output of a frame statistics tool, one line a picture),
 * printed picture by picture:
 *   "H idx sent_refresh min max avg rpu_refresh rpu_min rpu_max rpu_avg meas_min meas_avg meas_max"
 * usage: hades_dump <framestats output> <look-ahead> */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_measured.h"

#define MAXF 20000

struct frame {
    int refresh, rpu[3];
    double min, avg, max;
    float grid[DVBRIDGE_MEASURED_GRID];
    int active[4];
};
static struct frame g_f[MAXF];
static int g_n, g_bw, g_bh;

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    const int ahead = atoi(argv[2]);
    struct dvbridge_measured_params p = dvbridge_measured_default_params;
    if (getenv("EM_AVG_FLOOR"))
        p.avg_floor = (unsigned)atoi(getenv("EM_AVG_FLOOR"));
    FILE *in = fopen(argv[1], "r");
    if (!in)
        return 2;
    char tag[4];
    int a[6];
    if (fscanf(in, "%3s %d %d %d %d %d %d", tag, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 7 || strcmp(tag, "A"))
        return 2;
    g_bw = a[4];
    g_bh = a[5];
    while (g_n < MAXF) {
        struct frame *f = &g_f[g_n];
        int idx;
        long long pts;
        if (fscanf(in, "%3s %d %lld %d %d %d %d %lf %lf %lf %d %d %d %d", tag, &idx, &pts, &f->refresh, &f->rpu[0],
                   &f->rpu[1], &f->rpu[2], &f->min, &f->avg, &f->max, &f->active[0], &f->active[1], &f->active[2],
                   &f->active[3]) != 14)
            break;
        for (int i = 0; i < DVBRIDGE_MEASURED_GRID; ++i)
            if (fscanf(in, "%f", &f->grid[i]) != 1)
                return 2;
        ++g_n;
    }
    fclose(in);
    fprintf(stderr, "params: cut %.3f motion %.2f step %.2f steps %u/%u/%u window %u\n", p.cut, p.motion, p.step,
            p.avg_step, p.max_step, p.min_step, p.avg_window);
    struct dvbridge_measured *m = dvbridge_measured_create(&p);
    int added = 0;
    for (int i = 0; i < g_n; ++i) {
        while (added < g_n && added <= i + ahead) {
            const struct frame *q = &g_f[added];
            struct dvbridge_measured_picture pic = {0};
            pic.pts = added;
            pic.content = q->active[0] >= 0;
            pic.min = (float)(q->min / 4095);
            pic.avg = (float)(q->avg / 4095);
            pic.max = (float)(q->max / 4095);
            for (int k = 0; k < DVBRIDGE_MEASURED_GRID; ++k)
                pic.grid[k] = q->grid[k] / 4095;
            memcpy(pic.active, q->active, sizeof(pic.active));
            pic.blocks_w = g_bw;
            pic.blocks_h = g_bh;
            dvbridge_measured_add(m, &pic);
            ++added;
        }
        struct dvbridge_measured_l1 l1;
        dvbridge_measured_take(m, i, &l1);
        const struct frame *f = &g_f[i];
        printf("H %d %d %u %u %u %d %d %d %d %.3f %.3f %.3f\n", i, l1.refresh, l1.min_pq, l1.max_pq, l1.avg_pq,
               f->refresh, f->rpu[0], f->rpu[1], f->rpu[2], f->min, f->avg, f->max);
    }
    dvbridge_measured_destroy(m);
    return 0;
}
