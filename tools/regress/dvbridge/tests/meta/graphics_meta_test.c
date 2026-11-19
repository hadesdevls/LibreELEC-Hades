// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/* LibreELEC Hades, TEST ONLY (host): the metadata of a frame of graphics alone (dvbridge_graphics.c), as the
 * serializer of the tunnel writes them (dvbridge_core.c, dvbridge_metadata.h), field by field against what ETSI GS
 * CCM 001 V1.1.1 6.2 (tables 3 to 5) and dovi_tool say, computed here on their own; and that the first video frame
 * after frames of the menus carries the same display-management payload as without them (only the metadata id goes
 * on, as 6.3.1 asks of changed metadata).
 * Usage: graphics_meta_test */
#include "dvbridge_core.h"
#include "dvbridge_graphics.h"
#include "dvbridge_hdr10plus.h"

#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mem.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c, ...)                                                                                       \
    do {                                                                                                    \
        checks++;                                                                                           \
        if (!(c)) {                                                                                         \
            failures++;                                                                                     \
            printf("FAIL: " __VA_ARGS__);                                                                   \
            printf("\n");                                                                                   \
        }                                                                                                   \
    } while (0)

/* ST 2084 in long double, written again here (not the bridge's) */
static unsigned pq12(long double nits)
{
    const long double m1 = 2610.0L / 16384, m2 = 2523.0L / 4096 * 128;
    const long double c1 = 3424.0L / 4096, c2 = 2413.0L / 4096 * 32, c3 = 2392.0L / 4096 * 32;
    const long double y = powl(nits / 10000.0L, m1);
    return (unsigned)floorl(powl((c1 + c2 * y) / (1 + c3 * y), m2) * 4095.0L + 0.5L);
}

/* the payload of a candidate, from its packets (four bytes to a word, the first in the high bits; the first
 * packet carries the payload from byte 5, the others from byte 3: dvbridge_dv_metadata()) */
static int payload_of(const struct dvbridge_candidate *c, uint8_t *out, unsigned *id, unsigned *crc0)
{
    unsigned count = 0;
    const uint32_t *w = dvbridge_packets(c, &count);
    if (!w || !count)
        return -1;
    uint8_t p[128];
    int size = -1, off = 0;
    for (unsigned k = 0; k < count; k++) {
        for (unsigned j = 0; j < 32; j++) {
            const uint32_t v = w[k * 32 + j];
            p[4 * j] = v >> 24;
            p[4 * j + 1] = v >> 16;
            p[4 * j + 2] = v >> 8;
            p[4 * j + 3] = v;
        }
        if (k == 0) {
            *id = p[1] & 15;
            *crc0 = (unsigned)p[124] << 24 | (unsigned)p[125] << 16 | (unsigned)p[126] << 8 | p[127];
            if ((p[1] >> 4) != (p[1] & 15))
                return -1;
            size = p[3] << 8 | p[4];
        }
        const int start = k ? 3 : 5, room = k ? 121 : 119;
        int take = size - off;
        if (take > room)
            take = room;
        memcpy(out + off, p + start, take);
        off += take;
    }
    return off == size ? size : -1;
}

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static int s16(const uint8_t *p) { return (int16_t)u16(p); }
static unsigned u32(const uint8_t *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

static const struct dvbridge_geometry WHOLE = {.source_width = 3840, .source_height = 2160, .width = 3840,
                                               .height = 2160};

static void check_white(double white)
{
    struct dvbridge_graphics_range r;
    CHECK(dvbridge_graphics_range_of(white, &r), "white %.2f: no range", white);
    /* expected, from the specification and dovi_tool, on their own */
    const unsigned code = pq12(white);
    const unsigned smax = code > 2081 ? code : 2081;
    unsigned avg = (7 + smax) / 2;
    if (avg < 819)
        avg = 819;
    if (avg > smax - 1)
        avg = smax - 1;
    CHECK(r.white == code, "white %.2f: PQ code %u, expected %u", white, r.white, code);
    CHECK(r.source_min == 7 && r.source_max == smax, "white %.2f: source %u..%u, expected 7..%u", white,
          r.source_min, r.source_max, smax);
    CHECK(r.min == 7 && r.max == smax && r.avg == avg, "white %.2f: L1 %u %u %u, expected 7 %u %u", white, r.min,
          r.max, r.avg, smax, avg);

    size_t size = 0;
    AVDOVIMetadata *m = dvbridge_graphics_metadata(white, true, &size);
    CHECK(m != NULL, "white %.2f: no metadata", white);
    if (!m)
        return;
    struct dvbridge_context *ctx = dvbridge_create();
    struct dvbridge_candidate *c = dvbridge_prepare(ctx, m, size, 1000.0, WHOLE, false);
    CHECK(c != NULL, "white %.2f: the serializer refused the metadata", white);
    uint8_t pl[512];
    unsigned id = 99, crc = 0;
    const int n = c ? payload_of(c, pl, &id, &crc) : -1;
    /* table 3: 2 + 18 + 12 + 18 + 2 + 8 + 4 + 4 + 2 + 1 = 71 bytes; L1 block 4 + 1 + 6, L5 block 4 + 1 + 8 */
    CHECK(n == 71 + 11 + 13, "white %.2f: payload of %d bytes, expected 95", white, n);
    if (n == 95) {
        CHECK(pl[0] == 0, "reserved byte 0");
        CHECK(pl[1] == 1, "white %.2f: scene_refresh_flag %u on the first frame", white, pl[1]);
        static const int ycc[9] = {9574, 0, 13802, 9574, -1540, -5348, 9574, 17610, 0};
        static const int lms[9] = {7222, 8771, 390, 2654, 12430, 1300, 0, 422, 15962};
        static const unsigned off[3] = {16777216, 134217728, 134217728};
        for (int i = 0; i < 9; i++) {
            CHECK(s16(pl + 2 + 2 * i) == ycc[i], "YCCtoRGB[%d] %d", i, s16(pl + 2 + 2 * i));
            CHECK(s16(pl + 32 + 2 * i) == lms[i], "RGBtoLMS[%d] %d", i, s16(pl + 32 + 2 * i));
        }
        for (int i = 0; i < 3; i++)
            CHECK(u32(pl + 20 + 4 * i) == off[i], "YCCtoRGB offset[%d] %u", i, u32(pl + 20 + 4 * i));
        /* BT.2020 YCbCr in limited range at 12 bits, as the converted packer writes it (pack_gles300.frag):
         * Y = 256 + 3504 Y', C = 2048 + 3584 C': the matrix is 4095/3504 and the offsets 256/4096, 2048/4096 */
        CHECK(fabs(9574 / 8192.0 - 4095.0 / 3504.0) < 1e-4, "luma scale of the header against the packer's");
        CHECK(fabs(16777216 / 268435456.0 - 256.0 / 4096.0) < 1e-9 &&
                  fabs(134217728 / 268435456.0 - 2048.0 / 4096.0) < 1e-9,
              "offsets of the header against the packer's");
        CHECK(fabs(13802 / 8192.0 * 3584.0 / 4095.0 - 2 * (1 - 0.2627)) < 2e-4 &&
                  fabs(17610 / 8192.0 * 3584.0 / 4095.0 - 2 * (1 - 0.0593)) < 2e-4,
              "Cr and Cb scales of the header against BT.2020's (and the packer's 1.4746, 1.8814)");
        CHECK(pl[50] == 0xFF && pl[51] == 0xFF, "signal_eotf (reserved 0xFFFF)");
        for (int i = 0; i < 8; i++)
            CHECK(pl[52 + i] == 0, "reserved 0x00 [%d]", i);
        CHECK(pl[60] == 12 && pl[61] == 0, "signal_bit_depth %u, signal_color_space %u", pl[60], pl[61]);
        CHECK(pl[62] == 1 && pl[63] == 1, "reserved 0x01 0x01");
        CHECK(u16(pl + 64) == 7 && u16(pl + 66) == smax, "source_min_PQ %u, source_max_PQ %u", u16(pl + 64),
              u16(pl + 66));
        CHECK(pl[68] == 0 && pl[69] == 0x2A, "reserved 0x00 0x2A");
        CHECK(pl[70] == 2, "num_ext_blocks %u", pl[70]);
        CHECK(u32(pl + 71) == 6 && pl[75] == 1, "first block: length %u level %u", u32(pl + 71), pl[75]);
        CHECK(u16(pl + 76) == 7 && u16(pl + 78) == smax && u16(pl + 80) == avg, "L1 %u %u %u", u16(pl + 76),
              u16(pl + 78), u16(pl + 80));
        CHECK(u32(pl + 82) == 8 && pl[86] == 5, "second block: length %u level %u", u32(pl + 82), pl[86]);
        CHECK(!u16(pl + 87) && !u16(pl + 89) && !u16(pl + 91) && !u16(pl + 93), "L5 of the whole raster");
    }
    /* the frames after it: the same bytes, without the scene refresh (one id change for that), then nothing */
    CHECK(c && dvbridge_commit(ctx, c), "white %.2f: commit", white);
    size_t size2 = 0;
    AVDOVIMetadata *m2 = dvbridge_graphics_metadata(white, false, &size2);
    unsigned ids[4] = {id, 0, 0, 0};
    for (int f = 1; f < 4 && m2; f++) {
        struct dvbridge_candidate *k = dvbridge_prepare(ctx, m2, size2, 1000.0 + 1000.0 * f, WHOLE, false);
        uint8_t q[512];
        unsigned crc2 = 0;
        const int n2 = k ? payload_of(k, q, &ids[f], &crc2) : -1;
        CHECK(n2 == n && q[1] == 0 && !memcmp(q + 2, pl + 2, n - 2),
              "white %.2f, frame %d: not the same payload without the scene refresh", white, f);
        CHECK(k && dvbridge_commit(ctx, k), "commit %d", f);
        dvbridge_candidate_destroy(k);
    }
    CHECK(ids[1] == ((ids[0] + 1) & 15) && ids[2] == ids[1] && ids[3] == ids[1],
          "white %.2f: metadata ids %u %u %u %u (one change for the end of the scene refresh, then none)", white,
          ids[0], ids[1], ids[2], ids[3]);
    av_free(m2);
    dvbridge_candidate_destroy(c);
    dvbridge_destroy(ctx);
    av_free(m);
    printf("white %7.2f cd/m2: PQ %4u, source %u..%u, L1 min %u max %u avg %u\n", white, r.white, r.source_min,
           r.source_max, r.min, r.max, r.avg);
}

/* a video frame: a profile 8.1 RPU with a level 1 and a level 5 block (letterbox), as dvbridge_hdr10plus.c makes one */
static AVDOVIMetadata *video_rpu(unsigned avg, bool refresh, size_t *size)
{
    AVMasteringDisplayMetadata md = {0};
    md.has_luminance = 1;
    md.min_luminance = (AVRational){50, 10000};
    md.max_luminance = (AVRational){1000, 1};
    AVContentLightMetadata light = {.MaxCLL = 1000, .MaxFALL = 400};
    const unsigned area[4] = {0, 0, 276, 276};
    return dvbridge_profile81_metadata(10, 3079, avg, area, &md, &light, refresh, size);
}

static void check_video_after_menus(void)
{
    /* A: a fresh context (1.0 RC4: no menus before the video). B: 40 frames of the menus, then the same video. */
    struct dvbridge_context *a = dvbridge_create(), *b = dvbridge_create();
    size_t gs = 0;
    AVDOVIMetadata *g0 = dvbridge_graphics_metadata(203.0, true, &gs), *g1 = dvbridge_graphics_metadata(203.0, false, &gs);
    for (int f = 0; f < 40; f++) {
        struct dvbridge_candidate *k = dvbridge_prepare(b, f ? g1 : g0, gs, 1000.0 * (f + 1), WHOLE, false);
        CHECK(k && dvbridge_commit(b, k), "menu frame %d", f);
        dvbridge_candidate_destroy(k);
    }
    /* the hand-over to the video: the bridge's stream starts again as with any new stream (the window system calls
     * no reset at a hand-over: as here, the video's times are after the menus') */
    const struct dvbridge_geometry film = {.source_width = 3840, .source_height = 2160, .width = 3840, .height = 2160};
    unsigned same = 0, frames = 0, idA[6], idB[6];
    for (int f = 0; f < 6; f++) {
        size_t vs = 0;
        AVDOVIMetadata *v = video_rpu(1200 + (f >= 3 ? 40 : 0), f == 0 || f == 3, &vs);
        const double pts = 5.0e6 + 41708.0 * f;
        struct dvbridge_candidate *ka = dvbridge_prepare(a, v, vs, pts, film, false);
        struct dvbridge_candidate *kb = dvbridge_prepare(b, v, vs, pts, film, false);
        uint8_t pa[512], pb[512];
        unsigned ca = 0, cb = 0;
        const int na = ka ? payload_of(ka, pa, &idA[f], &ca) : -1, nb = kb ? payload_of(kb, pb, &idB[f], &cb) : -2;
        frames++;
        if (na > 0 && na == nb && !memcmp(pa, pb, na))
            same++;
        CHECK(ka && dvbridge_commit(a, ka) && kb && dvbridge_commit(b, kb), "video frame %d commit", f);
        dvbridge_candidate_destroy(ka);
        dvbridge_candidate_destroy(kb);
        av_free(v);
    }
    CHECK(same == frames, "video frames after the menus: %u of %u payloads the same as without them", same, frames);
    /* the id after the menus goes on from theirs; it changes where it changes without them */
    unsigned changesA = 0, changesB = 0;
    for (int f = 1; f < 6; f++) {
        changesA += idA[f] != idA[f - 1];
        changesB += idB[f] != idB[f - 1];
    }
    CHECK(changesA == changesB, "the metadata id changes %u times without the menus and %u with them", changesA,
          changesB);
    printf("video after the menus: %u of %u payloads the same bytes as without them; metadata id %u -> %u without, "
           "%u -> %u with (the id goes on), %u changes in both\n",
           same, frames, idA[0], idA[5], idB[0], idB[5], changesA);
    av_free(g0);
    av_free(g1);
    dvbridge_destroy(a);
    dvbridge_destroy(b);
}

int main(void)
{
    const double whites[] = {50.75, 76.125, 101.5, 152.25, 203.0, 304.5, 406.0, 507.5, 609.0};
    for (unsigned i = 0; i < sizeof(whites) / sizeof(whites[0]); i++)
        check_white(whites[i]);
    struct dvbridge_graphics_range r;
    size_t s = 0;
    CHECK(!dvbridge_graphics_range_of(50.0, &r) && !dvbridge_graphics_range_of(610.0, &r) &&
              !dvbridge_graphics_range_of(NAN, &r) && !dvbridge_graphics_metadata(1000.0, true, &s),
          "a white outside the GUI's is refused");
    /* every white of the setting (25 to 300 % in steps of 5) gives metadata the serializer takes */
    unsigned taken = 0, asked = 0;
    for (int pct = 25; pct <= 300; pct += 5) {
        asked++;
        AVDOVIMetadata *m = dvbridge_graphics_metadata(203.0 * pct / 100.0, true, &s);
        struct dvbridge_context *ctx = dvbridge_create();
        struct dvbridge_candidate *c = m ? dvbridge_prepare(ctx, m, s, 1000.0, WHOLE, false) : NULL;
        taken += c != NULL;
        dvbridge_candidate_destroy(c);
        dvbridge_destroy(ctx);
        av_free(m);
    }
    CHECK(taken == asked, "%u of %u whites of the setting taken by the serializer", taken, asked);
    check_video_after_menus();
    printf("%s: %d checks, %d failed\n", failures ? "FAILED" : "ALL PASSED", checks, failures);
    return failures ? 1 : 0;
}
