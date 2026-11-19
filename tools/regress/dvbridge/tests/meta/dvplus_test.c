// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/* Hades 2026-10-27, TEST ONLY: Dolby Vision sent as HDR10+, the decisions (tools/dvbridge/dvbridge_dvplus.c), on the host:
 *   S  the statistics of a histogram (one value, two values, an even spread), the places of 5 and 10 percent;
 *   P  samples into a picture (bins and the sum in cd/m2);
 *   C  scenes from the RPU: a change of level 1, a refresh where the picture before had none, a refresh on every
 *      picture, a picture without an RPU;
 *   L  latency: nothing before the first measurement, the scene before on the first picture of a scene, the scene's own
 *      from its first measured picture, a measurement of a scene that has ended dropped, one not presented dropped;
 *   H  within a scene: a move under the step is held, one over it is sent; a value at the edge of a code;
 *   R  the same picture again; a pts that goes back; reset;
 *   X  measurements that stop: nothing is sent after params.lost pictures, and again once one comes;
 *   U  rpu_of() on FFmpeg's metadata, and sizes that do not hold;
 *   A  the film's area of a place from level 5 (the two readings, a shown part, a place);
 *   M  the AVDynamicHDRPlus made;
 *   T  what a picture of 960 x 540 samples costs on this CPU.
 * usage: dvplus_test */
#include <libavutil/dovi_meta.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/mem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_dvplus.h"

static int g_fail, g_ok;
static void check(int ok, const char *what)
{
    if (ok)
        ++g_ok;
    else {
        ++g_fail;
        printf("FAIL: %s\n", what);
    }
}

static uint16_t code_of_nits(double nits)
{
    /* PQ (SMPTE ST 2084) inverse, in 1/65535 */
    const double y = nits / 10000, m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096,
                 c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
    const double p = pow(y, m1);
    return (uint16_t)floor(65535 * pow((c1 + c2 * p) / (1 + c3 * p), m2) + 0.5);
}

/* a picture of n samples of one value (cd/m2), and k of another */
static void picture_of(struct dvbridge_dvplus *m, struct dvbridge_dvplus_picture *p, double pts, double a, unsigned n,
                       double b, unsigned k)
{
    static uint16_t codes[1 << 20];
    memset(p, 0, sizeof(*p));
    p->pts = pts;
    for (unsigned i = 0; i < n; ++i)
        codes[i] = code_of_nits(a);
    for (unsigned i = 0; i < k; ++i)
        codes[n + i] = code_of_nits(b);
    dvbridge_dvplus_picture_add(m, p, codes, NULL, n + k);
}

static struct dvbridge_dvplus_rpu rpu(unsigned avg, int refresh)
{
    struct dvbridge_dvplus_rpu r = {true, 0, 3000, avg, refresh != 0};
    return r;
}

int main(void)
{
    struct dvbridge_dvplus *m = dvbridge_dvplus_create(NULL);
    static struct dvbridge_dvplus_picture pic;
    struct dvbridge_dvplus_stats s;

    /* S */
    {
        static uint64_t h[DVBRIDGE_DVPLUS_BINS];
        memset(h, 0, sizeof(h));
        const unsigned b = 2048; /* PQ 0.5: about 92 cd/m2 */
        h[b] = 1000;
        dvbridge_dvplus_stats_of(h, DVBRIDGE_DVPLUS_BINS, 1000, 1000 * 92.25, &s);
        const double lo = dvbridge_dvplus_nits(2048.0 / 4096) * 10, hi = dvbridge_dvplus_nits(2049.0 / 4096) * 10;
        int in = 1;
        for (int i = 0; i < 9; ++i)
            in &= s.percentiles[i] >= lo - 1 && s.percentiles[i] <= hi + 1;
        check(in, "S1 one bin: every percentile within it");
        check(s.average == 923, "S1 the average from the sum (92.3 cd/m2)");
        {
            int order = 1;
            for (int i = 0; i + 1 < 9; ++i)
                order &= s.percentiles[i] <= s.percentiles[i + 1];
            check(order, "S1 every place measured (5 and 10 percent too), the distribution never decreases");
        }
        check(s.percentiles[0] <= s.percentiles[3] && s.percentiles[3] <= s.percentiles[8], "S1 in order");
        /* two values: 99 % at bin 1000, 1 % at bin 3000: p1 is in the low bin, p99.98 in the high one */
        memset(h, 0, sizeof(h));
        h[1000] = 9900;
        h[3000] = 100;
        dvbridge_dvplus_stats_of(h, DVBRIDGE_DVPLUS_BINS, 10000, 0, &s);
        check(s.percentiles[8] >= dvbridge_dvplus_nits(3000.0 / 4096) * 10 - 1, "S2 p99.98 in the bright bin");
        check(s.percentiles[7] <= dvbridge_dvplus_nits(1001.0 / 4096) * 10 + 1, "S2 p95 in the dark bin");
        /* an even spread over 4096 bins: p50 at PQ 0.5 */
        for (unsigned i = 0; i < DVBRIDGE_DVPLUS_BINS; ++i)
            h[i] = 10;
        dvbridge_dvplus_stats_of(h, DVBRIDGE_DVPLUS_BINS, 40960, 0, &s);
        check(fabs(s.percentiles[4] / 10.0 - dvbridge_dvplus_nits(0.5)) < 0.2, "S3 p50 of an even spread at PQ 0.5");
        check(fabs(s.percentiles[6] / 10.0 - dvbridge_dvplus_nits(0.9)) < 2, "S3 p90 at PQ 0.9");
        dvbridge_dvplus_stats_of(h, DVBRIDGE_DVPLUS_BINS, 0, 0, &s);
        check(s.average == 0 && s.percentiles[8] == 0, "S4 nothing measured: zeros");
    }
    /* P */
    {
        picture_of(m, &pic, 1, 100, 300, 1000, 100);
        const uint16_t c100 = code_of_nits(100), c1000 = code_of_nits(1000);
        check(pic.count == 400 && pic.hist[c100 >> 4] == 300 && pic.hist[c1000 >> 4] == 100, "P1 bins of the codes");
        check(fabs(pic.sum - (300 * dvbridge_dvplus_nits(c100 / 65535.0) + 100 * dvbridge_dvplus_nits(c1000 / 65535.0))) < 1e-6,
              "P1 the sum in cd/m2 of the codes");
    }
    /* C, L */
    {
        dvbridge_dvplus_reset(m);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1), b = rpu(2000, 0);
        bool refresh = false;
        check(!dvbridge_dvplus_present(m, 0, &a, &s, &refresh) && refresh, "L1 the first picture: nothing, a scene");
        picture_of(m, &pic, 0, 50, 1000, 0, 0);
        check(dvbridge_dvplus_measured(m, &pic), "L1 its measurement taken");
        a.refresh = false;
        check(dvbridge_dvplus_present(m, 1, &a, &s, &refresh) && !refresh, "L2 the second picture: its scene's own");
        const unsigned scene_a = s.average;
        check(abs((int)scene_a - 500) <= 1, "L2 ... the average of the scene (50 cd/m2)");
        /* (picture 1 is not measured before the cut) */
        check(dvbridge_dvplus_present(m, 2, &b, &s, &refresh) && refresh && s.average == scene_a,
              "L3 a cut (level 1 changes, no refresh flag): the scene before goes out on its first picture");
        picture_of(m, &pic, 1, 999, 1000, 0, 0);
        check(!dvbridge_dvplus_measured(m, &pic), "L4 a measurement of the scene before, after the cut: dropped");
        picture_of(m, &pic, 2, 400, 1000, 0, 0);
        check(dvbridge_dvplus_measured(m, &pic), "L5 the first measurement of the new scene: taken");
        check(dvbridge_dvplus_present(m, 3, &b, &s, &refresh) && !refresh && abs((int)s.average - 4000) <= 2,
              "L5 ... and what goes out from the next picture on is its own");
        picture_of(m, &pic, 77, 400, 1000, 0, 0);
        check(!dvbridge_dvplus_measured(m, &pic), "L6 a measurement of a picture not presented: dropped");
        /* a refresh where the picture before had none, the same level 1 */
        b.refresh = true;
        check(dvbridge_dvplus_present(m, 4, &b, &s, &refresh) && refresh, "C1 a refresh after none: a cut");
        /* a refresh on every picture: one scene */
        dvbridge_dvplus_present(m, 5, &b, &s, &refresh);
        check(!refresh, "C2 a refresh on every picture, the same level 1: no cut");
        /* no RPU */
        check(dvbridge_dvplus_present(m, 6, NULL, &s, &refresh) && refresh, "C3 a picture without an RPU: a cut");
        dvbridge_dvplus_present(m, 7, NULL, &s, &refresh);
        check(!refresh, "C3 ... and the next one without: the same scene");
    }
    /* H */
    {
        dvbridge_dvplus_reset(m);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1);
        double pts = 0;
        dvbridge_dvplus_present(m, pts, &a, &s, NULL);
        a.refresh = false;
        picture_of(m, &pic, pts, 1000, 1000, 0, 0);
        dvbridge_dvplus_measured(m, &pic);
        dvbridge_dvplus_present(m, ++pts, &a, &s, NULL);
        const unsigned first = s.percentiles[8];
        /* a picture a little brighter: the pooled p99.98 moves by less than the step in PQ */
        picture_of(m, &pic, pts, 1010, 1000, 0, 0);
        dvbridge_dvplus_measured(m, &pic);
        dvbridge_dvplus_present(m, ++pts, &a, &s, NULL);
        check(s.percentiles[8] == first, "H1 a move under the step in PQ: held");
        struct dvbridge_dvplus_counts c;
        dvbridge_dvplus_get_counts(m, &c);
        check(c.changes == 0, "H1 ... no change counted");
        /* many much brighter pictures: the pool moves far */
        for (int i = 0; i < 4; ++i) {
            picture_of(m, &pic, pts, 2000, 1000, 0, 0);
            dvbridge_dvplus_measured(m, &pic);
            dvbridge_dvplus_present(m, ++pts, &a, &s, NULL);
        }
        check(s.percentiles[8] > first + 100, "H2 a move over the step: sent");
        dvbridge_dvplus_get_counts(m, &c);
        check(c.changes >= 1, "H2 ... a change counted");
        /* a value at the edge of a code (39.9 / 40.1 cd/m2: codes 2 and 3) never flips with the step in PQ */
        dvbridge_dvplus_reset(m);
        a.refresh = true;
        pts = 100;
        dvbridge_dvplus_present(m, pts, &a, &s, NULL);
        a.refresh = false;
        unsigned flips = 0, last = 0;
        for (int i = 0; i < 40; ++i) {
            picture_of(m, &pic, pts, (i & 1) ? 39.4 : 40.6, 1000, 0, 0);
            dvbridge_dvplus_measured(m, &pic);
            if (dvbridge_dvplus_present(m, ++pts, &a, &s, NULL)) {
                const unsigned code = dvbridge_dvplus_vsif_code(s.average);
                flips += i > 0 && code != last;
                last = code;
            }
        }
        check(flips == 0, "H3 an average wavering at the edge of a code: no flip");
    }
    /* H4 */
    {
        dvbridge_dvplus_reset(m);
        struct dvbridge_dvplus_counts before, after;
        dvbridge_dvplus_get_counts(m, &before);
        struct dvbridge_dvplus_rpu a = rpu(1500, 1);
        double pts = 500;
        dvbridge_dvplus_present(m, pts, &a, &s, NULL);
        a.refresh = false;
        for (int i = 0; i < 60; ++i) {
            picture_of(m, &pic, pts, (i & 1) ? 1000 : 1002, 1000, 120, 10);
            dvbridge_dvplus_measured(m, &pic);
            dvbridge_dvplus_present(m, ++pts, &a, &s, NULL);
        }
        dvbridge_dvplus_get_counts(m, &after);
        check(after.changes == before.changes, "H4 sixty pictures of a scene that does not move: no change");
    }
    /* R */
    {
        dvbridge_dvplus_reset(m);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1);
        dvbridge_dvplus_present(m, 10, &a, &s, NULL);
        picture_of(m, &pic, 10, 300, 100, 0, 0);
        dvbridge_dvplus_measured(m, &pic);
        a.refresh = false;
        bool r1 = false, r2 = true;
        const bool s1 = dvbridge_dvplus_present(m, 11, &a, &s, &r1);
        struct dvbridge_dvplus_stats t;
        picture_of(m, &pic, 11, 3000, 100, 0, 0);
        dvbridge_dvplus_measured(m, &pic);
        const bool s2 = dvbridge_dvplus_present(m, 11, &a, &t, &r2);
        check(s1 && s2 && !r2 && !memcmp(&s, &t, sizeof(s)), "R1 the same picture again: the same, until the next one");
        dvbridge_dvplus_present(m, 5, &a, &s, &r1);
        check(r1, "R2 a pts that goes back: a new scene");
        dvbridge_dvplus_reset(m);
        check(!dvbridge_dvplus_present(m, 6, &a, &s, NULL), "R3 after a reset: nothing until measured");
    }
    /* X */
    {
        const struct dvbridge_dvplus_params p = {0, 32, 5, 0, 0};
        struct dvbridge_dvplus *x = dvbridge_dvplus_create(&p);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1);
        dvbridge_dvplus_present(x, 0, &a, &s, NULL);
        a.refresh = false;
        picture_of(x, &pic, 0, 100, 100, 0, 0);
        dvbridge_dvplus_measured(x, &pic);
        int sent = 0;
        for (int i = 1; i <= 8; ++i)
            sent += dvbridge_dvplus_present(x, i, &a, &s, NULL);
        check(sent == 5, "X1 measurements stop: sent for params.lost pictures, then nothing");
        picture_of(x, &pic, 8, 100, 100, 0, 0);
        dvbridge_dvplus_measured(x, &pic);
        check(dvbridge_dvplus_present(x, 9, &a, &s, NULL), "X2 ... and again once one comes");
        dvbridge_dvplus_destroy(x);
    }
    /* U */
    {
        size_t size = 0;
        AVDOVIMetadata *md = av_dovi_metadata_alloc(&size);
        md->num_ext_blocks = 2;
        av_dovi_get_color(md)->scene_refresh_flag = 1;
        AVDOVIDmData *e = av_dovi_get_ext(md, 0);
        e->level = 1;
        e->l1.min_pq = 7;
        e->l1.max_pq = 3000;
        e->l1.avg_pq = 1500;
        av_dovi_get_ext(md, 1)->level = 5;
        struct dvbridge_dvplus_rpu r;
        check(dvbridge_dvplus_rpu_of(md, size, &r) && r.level1 && r.refresh && r.min_pq == 7 && r.max_pq == 3000 &&
                  r.avg_pq == 1500,
              "U1 rpu_of: level 1 and refresh");
        check(!dvbridge_dvplus_rpu_of(md, sizeof(*md) - 1, &r) && !r.level1, "U2 a size that does not hold");
        e->level = 2;
        check(dvbridge_dvplus_rpu_of(md, size, &r) && !r.level1, "U3 no level 1 block");
        /* A */
        AVDOVIDmData *l5 = av_dovi_get_ext(md, 1);
        const float full[4] = {0, 0, 3840, 2160};
        const int place[4] = {0, 0, 3840, 2160};
        int area[4];
        l5->l5.top_offset = 276;
        l5->l5.bottom_offset = 276;
        dvbridge_dvplus_area(md, size, 3840, 2160, full, place, area);
        check(area[0] == 0 && area[1] == 276 && area[2] == 3840 && area[3] == 1608, "A1 a 2.39:1 film in 3840x2160");
        /* 1920x1080 with the offsets of a 3840x2160 master: the smaller margin, 138 */
        const float hd[4] = {0, 0, 1920, 1080};
        dvbridge_dvplus_area(md, size, 1920, 1080, hd, place, area);
        check(area[1] == 276 && area[3] == 1608, "A2 1920x1080 scaled from the master, shown at 3840x2160");
        /* 3840x1600 cropped from the master (the offsets count bars it has not): the whole place */
        const float crop[4] = {0, 0, 3840, 1600};
        const int at[4] = {0, 280, 3840, 1600};
        l5->l5.top_offset = 280;
        l5->l5.bottom_offset = 280;
        dvbridge_dvplus_area(md, size, 3840, 1600, crop, at, area);
        check(area[0] == 0 && area[1] == 280 && area[2] == 3840 && area[3] == 1600, "A3 a crop of the master: the whole place");
        /* a part shown (zoom: the middle half of a 3840x2160 film with bars of 276) at 1920x1080 */
        l5->l5.top_offset = 276;
        l5->l5.bottom_offset = 276;
        const float mid[4] = {960, 540, 2880, 1620};
        const int small[4] = {0, 0, 1920, 1080};
        dvbridge_dvplus_area(md, size, 3840, 2160, mid, small, area);
        check(area[0] == 0 && area[1] == 0 && area[2] == 1920 && area[3] == 1080, "A4 a zoom inside the film: all of it");
        const float top[4] = {0, 0, 3840, 1080};
        dvbridge_dvplus_area(md, size, 3840, 2160, top, small, area);
        check(area[1] == 276 && area[3] == 1080 - 276, "A5 the top half shown: its bar left out");
        md->num_ext_blocks = 1;
        dvbridge_dvplus_area(md, size, 3840, 2160, full, place, area);
        check(area[1] == 0 && area[3] == 2160, "A6 no level 5 block: the whole place");
        md->num_ext_blocks = 2;
        l5->l5.top_offset = 1200;
        l5->l5.bottom_offset = 1200;
        dvbridge_dvplus_area(md, size, 3840, 2160, full, place, area);
        check(area[1] == 0 && area[3] == 2160, "A7 offsets that fit no reading: the whole place");
        av_free(md);
    }
    /* M */
    {
        struct dvbridge_dvplus_stats t = {0};
        t.average = 523;
        unsigned v[9] = {3, 9, 31, 56, 219, 1036, 2714, 4668, 14445};
        memcpy(t.percentiles, v, sizeof(v));
        t.maxrgb = 17830;
        t.y99 = 9876;
        t.y100nit = 91;
        AVDynamicHDRPlus h;
        dvbridge_dvplus_metadata(&t, &h);
        int ok = h.application_version == 1 && h.num_windows == 1 &&
                 h.targeted_system_display_maximum_luminance.num == 0 && !h.params[0].tone_mapping_flag &&
                 h.params[0].num_distribution_maxrgb_percentiles == 9 && h.params[0].average_maxrgb.num == 523 &&
                 h.params[0].average_maxrgb.den == 100000;
        for (int i = 0; i < 9; ++i)
            ok &= h.params[0].distribution_maxrgb[i].percentage == dvbridge_dvplus_percentages[i] &&
                  h.params[0].distribution_maxrgb[i].percentile.num == (int)(i == 1 ? t.y99 : i == 2 ? t.y100nit : v[i]);
        check(ok, "M1 version 1, one window, profile A, the distribution in its places");
    }
    /* W */
    {
        const int64_t P = 41708333, M = DVBRIDGE_DVPLUS_WAIT_MARGIN, V = 5000000000LL;
        check(dvbridge_dvplus_wait_budget(V + 10000000, V, P, M) == P - M - 10000000,
              "W1 10 ms after a vblank: until the margin before the next one");
        check(dvbridge_dvplus_wait_budget(V + 100000000, V, P, M) == 3 * P - M - 100000000,
              "W2 some vblanks later: until the margin before the next one after now");
        check(dvbridge_dvplus_wait_budget(V + P - 1000000, V, P, M) < 0, "W3 within the margin of the next: no wait");
        check(dvbridge_dvplus_wait_budget(V, 0, P, M) == 0 && dvbridge_dvplus_wait_budget(V + 2000000000LL, V, P, M) == 0 &&
                  dvbridge_dvplus_wait_budget(V + 1000, V, 0, M) == 0 && dvbridge_dvplus_wait_budget(V - 1, V, P, M) == 0,
              "W4 no vblank, one of more than a second ago, no period, a clock before it: no wait");
        /* the first picture of a scene, its own measurement taken after it is presented */
        dvbridge_dvplus_reset(m);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1), b = rpu(2000, 0);
        dvbridge_dvplus_present(m, 0, &a, &s, NULL);
        picture_of(m, &pic, 0, 50, 1000, 0, 0);
        dvbridge_dvplus_measured(m, &pic);
        a.refresh = false;
        dvbridge_dvplus_present(m, 1, &a, &s, NULL);
        const unsigned held = s.average;
        bool refresh = false;
        dvbridge_dvplus_present(m, 2, &b, &s, &refresh);
        check(refresh && s.average == held, "W5 the first picture of a scene: the scene before, until measured");
        picture_of(m, &pic, 2, 800, 1000, 0, 0);
        dvbridge_dvplus_measured(m, &pic);
        struct dvbridge_dvplus_stats again;
        check(dvbridge_dvplus_present_again(m, 2, &again) && abs((int)again.average - 8000) <= 4,
              "W5 ... waited for: its own");
        check(dvbridge_dvplus_present(m, 2, &b, &s, NULL) && s.average == again.average,
              "W5 ... and shown again: its own, not the scene before");
        check(!dvbridge_dvplus_present_again(m, 1, &again), "W6 not the picture presented last: nothing");
    }
    /* K */
    {
        const struct dvbridge_dvplus_params p = {0, 32, 48, 3, 200};
        struct dvbridge_dvplus *k = dvbridge_dvplus_create(&p);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1);
        dvbridge_dvplus_present(k, 0, &a, &s, NULL);
        a.refresh = false;
        picture_of(k, &pic, 0, 100, 1000, 0, 0);
        dvbridge_dvplus_measured(k, &pic);
        dvbridge_dvplus_present(k, 1, &a, &s, NULL);
        const unsigned first = s.percentiles[8];
        /* 150 cd/m2: a move over the step (32) and under the bypass (200) */
        picture_of(k, &pic, 1, 150, 1000, 0, 0);
        dvbridge_dvplus_measured(k, &pic);
        dvbridge_dvplus_present(k, 2, &a, &s, NULL);
        check(s.percentiles[8] == first, "K1 a move over the step before the interval: held");
        for (int i = 2; i < 4; ++i) {
            picture_of(k, &pic, i, 150, 1000, 0, 0);
            dvbridge_dvplus_measured(k, &pic);
            dvbridge_dvplus_present(k, i + 1, &a, &s, NULL);
        }
        check(s.percentiles[8] > first, "K2 ... sent once the interval has passed");
        const unsigned second = s.percentiles[8];
        /* 4000 cd/m2 at once: over the bypass */
        picture_of(k, &pic, 4, 4000, 4000, 0, 0);
        dvbridge_dvplus_measured(k, &pic);
        dvbridge_dvplus_present(k, 5, &a, &s, NULL);
        check(s.percentiles[8] > second, "K3 a move over the bypass: sent before the interval");
        dvbridge_dvplus_destroy(k);
    }
    /* T */
    {
        static uint16_t codes[960 * 540];
        srand(7);
        for (int i = 0; i < 960 * 540; ++i)
            codes[i] = (uint16_t)(rand() & 0xffff);
        const int rounds = 200;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < rounds; ++r) {
            memset(&pic, 0, sizeof(pic));
            dvbridge_dvplus_picture_add(m, &pic, codes, NULL, 960 * 540);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / rounds;
        dvbridge_dvplus_reset(m);
        struct dvbridge_dvplus_rpu a = rpu(1000, 1);
        dvbridge_dvplus_present(m, 0, &a, &s, NULL);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < rounds; ++r) {
            pic.pts = r;
            dvbridge_dvplus_measured(m, &pic);
            a.refresh = false;
            dvbridge_dvplus_present(m, r + 1, &a, &s, NULL);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        const double ms2 = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / rounds;
        printf("T: a picture of 960 x 540 samples into its histogram: %.3f ms; into its scene and the statistics: %.3f ms\n",
               ms, ms2);
    }
    dvbridge_dvplus_destroy(m);
    printf("%s: %d of %d\n", g_fail ? "FAILED" : "ALL PASSED", g_ok, g_ok + g_fail);
    return g_fail != 0;
}
