/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Test tool of Hades (tools/regress/mvc): finding 1 of the automatic
 * review of FFmpeg pull request 24303 ("flush leaves saved field-pair state behind"), at the level of the
 * decoder's own functions, compiled against the headers of a twin tree of h264_mvc and linked with its
 * library.
 *
 * The state that ff_h264_view_switch_fields() saves for a view that is left half way through a
 * complementary field pair (pending_field_pic, first_field, picture_structure, droppable, missing_fields)
 * is put in both view slots, pointing at a live picture of the DPB, as a field-coded stream leaves it.
 * Then the decoder is flushed as a player does at a seek (avcodec_flush_buffers(): h264_decode_flush()
 * unreferences every picture of the DPB and calls ff_h264_view_reset() through ff_h264_flush_change()).
 *   check 1: no view slot keeps any of that state;
 *   check 2: what the first slice of the dependent view after the flush does with it: the switch to
 *            view 1 (ff_h264_view_switch_fields(), as a coded slice extension calls it), run in a child
 *            process: it must neither abort (the unconditional av_assert0(src->f->buf[0]) of
 *            ff_h264_ref_picture() on a picture the flush emptied) nor bind a picture.
 * Usage: f1reset   (exit 0: both hold; prints F1-RESET PASSED or F1-RESET FAILED) */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "h264dec.h"

int main(void)
{
    const AVCodec *codec = avcodec_find_decoder_by_name("h264_mvc");
    AVCodecContext *c;
    H264Context *h;
    H264Picture *pend;
    int ok = 1, status;
    pid_t pid;

    if (!codec || !(c = avcodec_alloc_context3(codec)))
        return 2;
    c->thread_count = 1;
    if (av_opt_set(c, "view_ids", "-1", AV_OPT_SEARCH_CHILDREN) < 0 || avcodec_open2(c, codec, NULL) < 0)
        return 2;
    h = c->priv_data;

    /* a live picture of the DPB, as h264_field_start() leaves the first field of a pair */
    pend = &h->DPB[3];
    pend->f->width  = 64;
    pend->f->height = 64;
    pend->f->format = AV_PIX_FMT_YUV420P;
    if (av_frame_get_buffer(pend->f, 0) < 0)
        return 2;
    pend->tf.f = pend->f;
    pend->reference = PICT_TOP_FIELD;
    for (int v = 0; v < 2; v++) {
        h->views[v].pending_field_pic = pend;
        h->views[v].first_field       = 1;
        h->views[v].picture_structure = PICT_TOP_FIELD;
        h->views[v].droppable         = 1;
        h->views[v].missing_fields    = 1;
    }

    avcodec_flush_buffers(c);

    printf("after the flush: DPB picture %s\n", pend->f->buf[0] ? "still referenced" : "unreferenced");
    for (int v = 0; v < (int)FF_ARRAY_ELEMS(h->views); v++) {
        const H264ViewContext *s = &h->views[v];
        int left = s->pending_field_pic || s->first_field || s->picture_structure || s->droppable ||
                   s->missing_fields;
        if (left) {
            printf("FAIL: view slot %d keeps field-pair state: pending_field_pic %s, first_field %d, "
                   "picture_structure %d, droppable %d, missing_fields %d\n", v,
                   s->pending_field_pic ? "set" : "NULL", s->first_field, s->picture_structure,
                   s->droppable, s->missing_fields);
            ok = 0;
        }
    }
    if (ok)
        printf("check 1: no view slot keeps field-pair state\n");

    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        /* the first coded slice extension after the flush: the decoder is in view 0 (cur_view is 0) */
        int r = ff_h264_view_switch_fields(h, 1);
        _exit(r < 0 ? 3 : h->cur_pic_ptr ? 4 : 0);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return 2;
    if (WIFSIGNALED(status)) {
        printf("FAIL: check 2: the switch to view 1 after the flush died of signal %d%s\n", WTERMSIG(status),
               WTERMSIG(status) == SIGABRT ? " (an assertion)" : "");
        ok = 0;
    } else if (WEXITSTATUS(status) == 4) {
        printf("FAIL: check 2: the switch to view 1 after the flush bound a picture of the emptied DPB\n");
        ok = 0;
    } else if (WEXITSTATUS(status) != 0) {
        printf("FAIL: check 2: the switch to view 1 after the flush returned an error (%d)\n", WEXITSTATUS(status));
        ok = 0;
    } else {
        printf("check 2: the switch to view 1 after the flush binds nothing and does not abort\n");
    }

    avcodec_free_context(&c);
    printf("%s\n", ok ? "F1-RESET PASSED" : "F1-RESET FAILED");
    return ok ? 0 : 1;
}
