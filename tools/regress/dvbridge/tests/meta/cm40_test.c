// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the serializer of the tunnel's metadata (dvbridge_metadata.h of a tree) with
// the default content mapping v4.0 part (dvbridge_dv::cm40_default). Switched off, the payload of the frame is
// printed (OFF), for the runner's byte-identity check with a bridge without the part (ref/meta/cm40-off.txt).
// Usage: cm40_test <a .meta of FFmpeg's AVDOVIMetadata, CM v2.9 only>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_metadata.h"

static void *load(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = aligned_alloc(64, (*n + 63) / 64 * 64 + 4096);
    if (!p || fread(p, 1, *n, f) != *n)
        return NULL;
    fclose(f);
    return p;
}

static struct dvbridge_dv g_d;

static int run(const AVDOVIMetadata *m, int cm40, int cm29, uint8_t *out, unsigned *size, unsigned *added,
               unsigned *count_at)
{
    memset(&g_d, 0, sizeof(g_d));
    g_d.cm40_default = cm40;
    g_d.cm29_only = cm29;
    if (!dvbridge_dv_geometry_view(&g_d, 3840, 2160, 0, 0, 3840, 2160, NULL)) {
        printf("FAIL: geometry: %s\n", g_d.refused ? g_d.refused : "?");
        return 0;
    }
    if (!dvbridge_dv_metadata(&g_d, m, 1.0, false, 0)) {
        printf("FAIL: metadata: %s\n", g_d.refused ? g_d.refused : "?");
        return 0;
    }
    memcpy(out, g_d.payload, g_d.size);
    *size = g_d.size;
    *count_at = g_d.blocks_at;
    *added = g_d.reading.cm40_added;
    return 1;
}

static void hex(const char *tag, const uint8_t *p, unsigned n)
{
    printf("%s", tag);
    for (unsigned i = 0; i < n; i++)
        printf("%02x", p[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    size_t n = 0;
    AVDOVIMetadata *m = load(argv[1], &n);
    if (!m)
        return 1;
    static uint8_t p0[DVBRIDGE_DV_MAX_PAYLOAD], p1[DVBRIDGE_DV_MAX_PAYLOAD], p2[DVBRIDGE_DV_MAX_PAYLOAD];
    unsigned s0, s1, s2, a0, a1, a2, c0, c1, c2;
    if (!run(m, 0, 0, p0, &s0, &a0, &c0))
        return 1;
    hex("OFF ", p0, s0);
    int fails = 0;
#define CHECK(cond, what) do { const int ok_ = (cond); printf("%s %s\n", ok_ ? "PASS" : "FAIL", what); fails += !ok_; } while (0)
    CHECK(a0 == 0, "off: nothing added");
    if (!run(m, 1, 0, p1, &s1, &a1, &c1))
        return 1;
    hex("ON  ", p1, s1);
    static const uint8_t tail[] = {0, 0, 0, 6, 3, 0x08, 0x00, 0x08, 0x00, 0x08, 0x00, /* level 3: 2048 x 3 */
                                   0, 0, 0, 1, 9, 0x00,                                /* level 9: DCI-P3 D65 */
                                   0, 0, 0, 4, 11, 1, 0, 0, 0,                         /* level 11: cinema */
                                   0, 0, 0, 2, 254, 0, 2};                             /* level 254: CM v4.0.2 */
    CHECK(a1 == 4, "on: 4 blocks added");
    CHECK(s1 == s0 + sizeof(tail), "on: the payload is 33 bytes longer");
    CHECK(c1 == c0 && p1[c1] == p0[c0] + 4, "on: the count of blocks is 4 more");
    int same_head = s1 >= s0;
    for (unsigned i = 0; same_head && i < s0; i++)
        same_head = i == c0 ? 1 : p0[i] == p1[i];
    CHECK(same_head, "on: the payload as before up to its end");
    CHECK(s1 == s0 + sizeof(tail) && !memcmp(p1 + s0, tail, sizeof(tail)), "on: the 4 blocks as expected, at the end");
    if (!run(m, 1, 1, p2, &s2, &a2, &c2))
        return 1;
    CHECK(a2 == 0 && s2 == s0 && !memcmp(p2, p0, s0), "on, display management 2 or 3: nothing added, the same bytes");
    // an RPU that has a v4.0 part (a level 254 block, its bytes kept as the decoder keeps them): nothing added
    if (m->num_ext_blocks >= AV_DOVI_MAX_EXT_BLOCKS ||
        m->ext_block_offset + (size_t)(m->num_ext_blocks + 1) * m->ext_block_size > n + 4096) {
        printf("FAIL: no room for a level 254 block\n");
        return 1;
    }
    AVDOVIDmData *e = av_dovi_get_ext(m, m->num_ext_blocks++);
    memset(e, 0, m->ext_block_size);
    e->level = 254;
    e->l254.dm_mode = 0;
    e->l254.dm_version_index = 2;
    e->dvbridge_raw_magic = 0x41424456;
    e->dvbridge_original_length = 2;
    e->dvbridge_original_bytes[0] = 0;
    e->dvbridge_original_bytes[1] = 2;
    if (!run(m, 0, 0, p0, &s0, &a0, &c0) || !run(m, 1, 0, p1, &s1, &a1, &c1))
        return 1;
    CHECK(a1 == 0 && s1 == s0 && !memcmp(p1, p0, s0), "RPU with a v4.0 part: nothing added, the same bytes");
    CHECK(s0 > 0 && p0[s0 - 3] == 254, "RPU with a v4.0 part: its level 254 block is sent");
    // kept over the restart of a stream, as no_trims
    memset(&g_d, 0, sizeof(g_d));
    g_d.cm40_default = 1;
    g_d.no_trims = 1;
    dvbridge_dv_restart(&g_d);
    CHECK(g_d.cm40_default == 1 && g_d.no_trims == 1, "the switch is kept over dvbridge_dv_restart()");
    printf("CM40-TEST %s (%d failed)\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
