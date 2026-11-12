#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): one mutation of the shipped h264_mvc that puts back
the defect of one finding of the automatic review of FFmpeg pull request 24303, in a copy of the twin tree, by
exact string replacements that must each match once. A test of that finding must fail on the mutant and pass
on the shipped code: that is how each test is proven in both directions.
Usage: mut.py <h264mvc folder> <mutation>; prints what it did. mut.py list: the mutations."""
import os
import sys

M = {
    # F1: ff_h264_view_reset() as the series of August had it (h264_refs.c): the references, their counts,
    # cur_pic_ptr and the POC state are cleared, the field-pair state is not (the POC values after it stay
    # those of the shipped code, so that only the field-pair state differs)
    "F1-reset-keeps-fields": [("h264_refs.c",
        "        memset(v, 0, sizeof(*v));\n        v->poc.prev_poc_msb   = 1 << 16;",
        "        memset(v->short_ref, 0, sizeof(v->short_ref));\n"
        "        memset(v->long_ref,  0, sizeof(v->long_ref));\n"
        "        v->short_ref_count = 0;\n"
        "        v->long_ref_count  = 0;\n"
        "        v->cur_pic_ptr     = NULL;\n"
        "        memset(&v->poc, 0, sizeof(v->poc));\n"
        "        v->poc.prev_poc_msb   = 1 << 16;")],
    # F2: the inter-view references appended to each B list before the comparison and swap of 8.2.4.2.4
    "F2-append-before-swap": [
        ("h264_refs.c",
         "            lens[list] = len;\n",
         "            len += build_inter_view_list(h, sl->ref_list[list] + len,\n"
         "                                         FF_ARRAY_ELEMS(sl->ref_list[0]) - len,\n"
         "                                         list, anchor);\n"
         "            lens[list] = len;\n"),
        ("h264_refs.c",
         "            len  = lens[list];\n"
         "            len += build_inter_view_list(h, sl->ref_list[list] + len,\n"
         "                                         FF_ARRAY_ELEMS(sl->ref_list[0]) - len,\n"
         "                                         list, anchor);\n",
         "            len  = lens[list];\n")],
    # F3: the duplicate search of a temporal modification matches an inter-view entry by pic_id
    "F3-temporal-matches-inter-view": [("h264_refs.c",
        "                        !sl->ref_list[list][i].inter_view &&\n                        ref->long_ref",
        "                        ref->long_ref")],
    # F4: a dependent view keeps the PPS read against the SPS of the base view (fix 9006)
    "F4-no-view-pps": [("h264_slice.c",
        "        if (h->ps.pps->sps != mvc_sps) {\n            ret = view_pps(h, mvc_sps);",
        "        if (0 && h->ps.pps->sps != mvc_sps) {\n            ret = view_pps(h, mvc_sps);")],
    # F5: the PPS read again for a dependent view is never read again, whatever replaced its subset SPS
    #     (fix 9006)
    "F5-view-pps-never-renewed": [("h264_slice.c",
        "    if (!h->view_pps || h->view_pps->src != src || h->view_pps->sps != sps) {",
        "    if (!h->view_pps) {")],
    # F5: the PPS read again for a dependent view kept while the subset SPS it was read against is replaced
    #     (the cache of fix 9006 keyed by the PPS only)
    "F5-view-pps-keyed-by-pps-only": [("h264_slice.c",
        "    if (!h->view_pps || h->view_pps->src != src || h->view_pps->sps != sps) {",
        "    if (!h->view_pps || h->view_pps->src != src) {")],
    # F6: the views of the first subset SPS in the list, not of the one the dependent view activates
    #     (fix 9007: the views of a packet)
    "F6-first-subset-sps": [("h264_slice.c",
        "    if (dep && (found = slice_views_sps(ps, dep, 4)))",
        "    if (0 && dep && (found = slice_views_sps(ps, dep, 4)))")],
    # F6: a coded slice extension placed in its view by the views of the list, not of its own subset SPS
    #     (fix 9007)
    "F6-slice-view-by-list": [("h264_slice.c",
        "        const SPS *own = slice_views_sps(&h->ps, nal, 4);",
        "        const SPS *own = NULL;")],
    # F7: the slices of a dependent view are never waited for while only the base view is decoded (fix 9005)
    "F7-no-wait-for-new-view": [("h264dec.c",
        "            if (h->views_active_decode == 1 && !dependent_view_may_start(h))",
        "            if (h->views_active_decode == 1)")],
    # F8: the level count read with get_ue_golomb_31(), the counts of the operation point table unbounded and
    #     the overread checked only once the table is done (as the series of August had it)
    "F8-levels-golomb31": [("h264_ps.c",
        "    num_level_values = get_ue_golomb_long(gb) + 1;",
        "    num_level_values = get_ue_golomb_31(gb) + 1;")],
    "F8-ops-unbounded": [
        ("h264_ps.c", "        if (num_ops > 1024)\n            goto invalid_ops;\n", ""),
        ("h264_ps.c", "            if (num_target_views > 1024)\n                goto invalid_ops;\n", ""),
        ("h264_ps.c", "            get_ue_golomb_long(gb);              // applicable_op_num_views_minus1\n\n"
                      "            if (get_bits_left(gb) < 0)\n                goto overread;\n",
                      "            get_ue_golomb_long(gb);              // applicable_op_num_views_minus1\n")],
    # F9: abs_diff_view_idx_minus1 narrowed to int and used before it is checked against the dependency list
    "F9-no-range-check": [("h264_refs.c",
        "                if (val >= (unsigned)nb_refs) {",
        "                if (0) {")],
    # N1 (fix 9009, where the tree has it): idr() again before the picture of the dependent view is finished
    "N1-idr-before-the-dependent-view-ends": [("h264dec.c",
        "                err = finish_dependent_view(h);\n",
        "                err = 0;\n")],
    # a concern of the review: film grain synthesis skipped where a view switch ends the base view picture
    "C1-grain-not-in-setup": [("h264_picture.c",
        "    } else if (cur->needs_fg && (!FIELD_PICTURE(h) || !h->first_field)) {",
        "    } else if (!in_setup && cur->needs_fg && (!FIELD_PICTURE(h) || !h->first_field)) {")],
    # F10: the extension header read through gb, which get_bit_length() left one bit short for a prefix NAL
    #      unit of four bytes
    "F10-header-through-gb": [("h2645_parse.c",
        "        if (nal->size < 4)\n            return AVERROR_INVALIDDATA;",
        "        if (get_bits_left(gb) < 24)\n            return AVERROR_INVALIDDATA;")],
}

if sys.argv[1] == "list":
    print(" ".join(M))
    sys.exit(0)
folder, name = sys.argv[1], sys.argv[2]
for f, old, new in M[name]:
    p = os.path.join(folder, f)
    t = open(p, encoding="utf-8").read()
    if t.count(old) != 1:
        sys.exit("%s: %s: the text to replace is found %d times" % (name, f, t.count(old)))
    open(p, "w", encoding="utf-8").write(t.replace(old, new))
    print("%s: %s changed" % (name, f))
