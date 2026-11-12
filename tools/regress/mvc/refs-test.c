/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Test tool of Hades (tools/regress/mvc): the reference picture lists of a dependent
 * view slice as ff_h264_build_ref_list() of a twin tree of h264_mvc builds them, against what H.8.2.1 and
 * H.8.2.2 give, for two findings of the review of FFmpeg pull request 24303 whose effect a stream rewritten
 * from an existing one cannot show (they only change list entries that its coded macroblocks do not use):
 *   F2  a B slice with one temporal reference T and one inter-view reference V: both lists are [T, V]; the
 *       swap of 8.2.4.2.4 is for the temporal entries alone, which are one here (H.8.2.1 appends the
 *       inter-view ones after it)
 *   F3  a P slice whose temporal reference of PicNum 0 is put at index 0, and again at 1: the second search
 *       for a duplicate of it meets V, whose pic_id is 0 too (a picture that no list has held yet), and
 *       equations H-4/H-5 only count an entry of the same view: [T0, T0, T1, V]
 * Compiled against the headers of the twin (h264mvc/) and linked with its library.
 * Usage: refs-test (exit 0: both as the norm says) */
#include <stdio.h>
#include <string.h>

#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "h264dec.h"
#include "h264_ps.h"

static H264Picture pics[8];

static H264Picture *mkpic(int i, int frame_num, int poc, int view_id)
{
    H264Picture *p = &pics[i];
    p->f = av_frame_alloc();
    p->f->width  = 64;
    p->f->height = 64;
    p->f->format = AV_PIX_FMT_YUV420P;
    if (av_frame_get_buffer(p->f, 0) < 0)
        return NULL;
    p->reference    = PICT_FRAME;
    p->frame_num    = frame_num;
    p->poc          = poc;
    p->field_poc[0] = p->field_poc[1] = poc;
    p->view_id      = view_id;
    return p;
}

static const char *name(const H264Picture *p)
{
    static const char *names[8] = { "cur", "V", "T0", "T1", "?", "?", "?", "?" };
    return p ? names[p - pics] : "-";
}

static int check(const char *what, H264SliceContext *sl, int list, H264Picture **want, int n)
{
    int ok = 1;
    printf("%s, list %d:", what, list);
    for (int i = 0; i < n; i++) {
        const H264Picture *got = sl->ref_list[list][i].parent;
        printf(" %s", name(got));
        if (got != want[i])
            ok = 0;
    }
    printf(" (the norm: ");
    for (int i = 0; i < n; i++)
        printf("%s%s", i ? " " : "", name(want[i]));
    printf(") %s\n", ok ? "same" : "DIFFERENT");
    return ok;
}

int main(void)
{
    static H264Context h;
    static H264SliceContext sl;
    static SPS sps;
    int ok = 1;

    h.avctx = avcodec_alloc_context3(NULL);
    h.picture_structure = PICT_FRAME;
    h.cur_view = 1;
    h.nb_views = 2;
    /* the subset SPS: views 0 and 1, view 1 predicted from view 0 in both lists */
    sps.is_subset          = 1;
    sps.mvc.num_views      = 2;
    sps.mvc.view_id[0]     = 0;
    sps.mvc.view_id[1]     = 1;
    for (int l = 0; l < 2; l++) {
        sps.mvc.num_anchor_refs[l][1]     = 1;
        sps.mvc.num_non_anchor_refs[l][1] = 1;
    }
    sps.log2_max_frame_num = 4;
    h.ps.sps = &sps;
    h.ps.subset_sps_list[0] = &sps;   /* where the older ff_h264_view_idx() looks */

    H264Picture *cur = mkpic(0, 2, 8, 1), *V = mkpic(1, 2, 8, 0);
    H264Picture *T0 = mkpic(2, 0, 0, 1), *T1 = mkpic(3, 1, 4, 1);
    if (!cur || !V || !T0 || !T1)
        return 2;
    h.cur_pic_ptr = cur;
    h.views[0].cur_pic_ptr = V;

    /* F2: B slice, current POC 8 in view 1, one temporal reference T0 (POC 0) */
    h.short_ref[0] = T0;
    h.short_ref_count = 1;
    sl.slice_type_nos = AV_PICTURE_TYPE_B;
    sl.list_count = 2;
    sl.ref_count[0] = sl.ref_count[1] = 2;
    sl.curr_pic_num = 2;
    sl.max_pic_num = 16;
    sl.anchor_pic_flag = 0;
    if (ff_h264_build_ref_list(&h, &sl) < 0) {
        printf("F2: the lists could not be built\n");
        ok = 0;
    } else {
        H264Picture *want[2] = { T0, V };
        ok &= check("F2 B slice, one temporal reference", &sl, 0, want, 2);
        ok &= check("F2 B slice, one temporal reference", &sl, 1, want, 2);
    }

    /* F3: P slice, frame_num 2 in view 1, temporal references T1 (frame_num 1) and T0 (frame_num 0) */
    memset(&sl, 0, sizeof(sl));
    h.short_ref[0] = T1;
    h.short_ref[1] = T0;
    h.short_ref_count = 2;
    sl.slice_type_nos = AV_PICTURE_TYPE_P;
    sl.list_count = 1;
    sl.ref_count[0] = 4;
    sl.curr_pic_num = 2;
    sl.max_pic_num = 16;
    sl.nb_ref_modifications[0] = 2;
    sl.ref_modifications[0][0].op  = 0;   /* PicNum 2 - 2 = 0: T0, at index 0 */
    sl.ref_modifications[0][0].val = 1;
    sl.ref_modifications[0][1].op  = 0;   /* 0 - 16, modulo 16: T0 again, at index 1 */
    sl.ref_modifications[0][1].val = 15;
    if (ff_h264_build_ref_list(&h, &sl) < 0) {
        printf("F3: the list could not be built\n");
        ok = 0;
    } else {
        H264Picture *want[4] = { T0, T0, T1, V };
        ok &= check("F3 P slice, PicNum 0 put twice", &sl, 0, want, 4);
    }

    printf("%s\n", ok ? "REFS-TEST PASSED" : "REFS-TEST FAILED");
    return ok ? 0 : 1;
}
