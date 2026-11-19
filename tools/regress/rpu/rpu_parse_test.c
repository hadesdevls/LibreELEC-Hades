/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Hades
 *
 * rpu_parse_test: FFmpeg's Dolby Vision RPU parser (ff_dovi_rpu_parse()) on the vectors of rpu_vectors.py.
 * Built by run-rpu.sh with the dovi_rpudec.c and dovi_rpu.c under test, against the static libraries of an
 * FFmpeg host build.
 *
 * usage: rpu_parse_test <vectors> unpatched|patched
 * For each vector: the parse with err_recognition 0 (as Kodi decodes) and, for "patched", once more with
 * AV_EF_CRCCHECK | AV_EF_EXPLODE; what it did against the column of the vector; and, where the vector names
 * another, that both give the same AVDOVIMetadata, byte for byte. Last line: RPU-TEST PASSED|FAILED.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/dovi_rpu.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/mem.h"

#define MAXV 64
#define PADDING 64 /* AV_INPUT_BUFFER_PADDING_SIZE: the zero bytes a decoder's input has after its end */

struct vec {
    char name[64];
    int profile;
    char exp[3][4]; /* unpatched, patched, patched with CRCCHECK|EXPLODE */
    char same_as[64];
    uint8_t *data;
    size_t size;
    int done;
    uint8_t *md; /* the AVDOVIMetadata of the parse with err_recognition 0, or NULL */
    size_t md_size;
};

static int parse(struct vec *v, int err, uint8_t **md, size_t *md_size)
{
    DOVIContext s = { 0 };
    AVDOVIMetadata *meta = NULL;
    int ret;

    s.cfg.dv_profile = v->profile;
    ret = ff_dovi_rpu_parse(&s, v->data, v->size, err);
    if (ret >= 0 && md) {
        size_t size;
        AVDOVIMetadata *probe = av_dovi_metadata_alloc(&size);
        av_free(probe);
        if (ff_dovi_get_metadata(&s, &meta) < 0 || !meta) {
            ret = -1000; /* parsed, but no metadata to give */
        } else {
            *md = (uint8_t *) meta;
            *md_size = size;
        }
    }
    ff_dovi_ctx_unref(&s);
    return ret;
}

int main(int argc, char **argv)
{
    static struct vec v[MAXV];
    char line[8192], hex[8000];
    int n = 0, failed = 0, patched;
    FILE *f;

    if (argc != 3 || (strcmp(argv[2], "unpatched") && strcmp(argv[2], "patched"))) {
        fprintf(stderr, "usage: %s <vectors> unpatched|patched\n", argv[0]);
        return 2;
    }
    patched = !strcmp(argv[2], "patched");
    av_log_set_level(AV_LOG_ERROR);
    if (!(f = fopen(argv[1], "r"))) {
        perror(argv[1]);
        return 2;
    }
    while (fgets(line, sizeof(line), f)) {
        struct vec *x;
        size_t len;
        if (n == MAXV) {
            fprintf(stderr, "too many vectors\n");
            return 2;
        }
        x = &v[n];
        if (sscanf(line, "%63s %d %3s %3s %3s %63s %7999s", x->name, &x->profile, x->exp[0], x->exp[1],
                   x->exp[2], x->same_as, hex) != 7) {
            fprintf(stderr, "a line that is not a vector: %s", line);
            return 2;
        }
        len = strlen(hex);
        if (len % 2) {
            fprintf(stderr, "%s: odd hex\n", x->name);
            return 2;
        }
        x->size = len / 2;
        x->data = calloc(1, x->size + PADDING);
        for (size_t i = 0; i < x->size; i++) {
            unsigned b;
            sscanf(hex + 2 * i, "%2x", &b);
            x->data[i] = b;
        }
        n++;
    }
    fclose(f);

    for (int i = 0; i < n; i++) {
        struct vec *x = &v[i];
        const char *want = x->exp[patched ? 1 : 0];
        int ret = parse(x, 0, &x->md, &x->md_size);
        const char *got = ret >= 0 ? "acc" : "rej";
        int ok = !strcmp(got, want);
        printf("%-28s err 0:     %s (ret %d), want %s: %s\n", x->name, got, ret, want, ok ? "PASS" : "FAIL");
        failed += !ok;
        if (patched) {
            int r2 = parse(x, AV_EF_CRCCHECK | AV_EF_EXPLODE, NULL, NULL);
            const char *got2 = r2 >= 0 ? "acc" : "rej";
            int ok2 = !strcmp(got2, x->exp[2]);
            printf("%-28s crccheck:  %s (ret %d), want %s: %s\n", x->name, got2, r2, x->exp[2], ok2 ? "PASS" : "FAIL");
            failed += !ok2;
        }
        x->done = 1;
    }
    for (int i = 0; i < n; i++) {
        struct vec *x = &v[i], *r = NULL;
        if (!strcmp(x->same_as, "-") || !x->md)
            continue;
        for (int j = 0; j < n; j++)
            if (!strcmp(v[j].name, x->same_as))
                r = &v[j];
        if (!r || !r->md) {
            printf("%-28s metadata:  no reference %s: FAIL\n", x->name, x->same_as);
            failed++;
            continue;
        }
        int same = x->md_size == r->md_size && !memcmp(x->md, r->md, x->md_size);
        printf("%-28s metadata:  %s %s (%zu bytes): %s\n", x->name, same ? "same as" : "DIFFERENT from",
               r->name, x->md_size, same ? "PASS" : "FAIL");
        failed += !same;
    }
    for (int i = 0; i < n; i++) {
        av_free(v[i].md);
        free(v[i].data);
    }
    printf("RPU-TEST %s (%s, %d vectors, %d failures)\n", failed ? "FAILED" : "PASSED", argv[2], n, failed);
    return failed ? 1 : 0;
}
