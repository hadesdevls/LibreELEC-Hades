#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): streams made on purpose from MVC streams that decode the same in the
reference decoder (JM) and in h264_mvc, each to put one finding of the review of FFmpeg pull request 24303 to
the test. The changes are to parameter sets and their order only (and, for 'views', the view_id in the
headers of NAL units 14 and 20): the coded pictures are the same bytes. What each stream decodes to is what JM
says; where JM cannot take a stream as one, a <out>.jmparts file lists the parts whose JM decodes, one after the
other, are the reference (see views16.sh).

  scaling-default <in> <out>   F4: the subset SPS (which shares its id with the SPS) gets scaling matrices,
                               all the default ones (Default_4x4/8x8_Intra/Inter); the SPS keeps none
  scaling-custom <in> <out>    F4: as above, with an invented 4x4 intra and 8x8 inter list (the others fall
                               back to them or to the defaults)
  scaling-fallback <in> <out>  F4: the subset SPS gets scaling matrices that are all flat (16, written out),
                               which equal what the SPS has (none: flat); and the PPS gets a matrix of its own
                               with no list in it, so that every list falls back: to the defaults for the base
                               view (rule A: its SPS has no matrix), to the flat lists of the subset SPS for the
                               dependent view (rule B)
  replace <in> <out>           F5: the stream twice; in the first copy the subset SPS has no scaling matrix, in
                               the second it is as it was (a new coded video sequence, at its IDR) and the PPSs
                               are not sent again
  pps-first <in> <out>         F5: the PPSs before the subset SPS that the dependent one refers to
  views <in> <out> <view_id>   F6: the stream, then the stream again with its second view renamed to <view_id>
                               and its subset SPS (and the PPS that refers to it) given another id, one above
  levels <in> <out> <n>        F8: the operation point table of the subset SPS with <n> level values (valid:
                               1 to 64), each with one operation point for all views
  levels-bad <in> <out> <what> F8: an invalid operation point table: 'ops' (1025 operation points), 'views'
                               (1025 target views), 'cut' (the subset SPS ends inside the table)
Usage: craft.py <what> <in.264> <out.264> [<arg>]"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h264bits as hb

FLAT4 = [16] * 16
FLAT8 = [16] * 64


def load(p):
    return hb.split_annexb(open(p, "rb").read())


def save(p, nals):
    open(p, "wb").write(hb.join_annexb(nals))


def map_type(nals, t, fn):
    out, n_done = [], 0
    for n in nals:
        if hb.nal_type(n) == t:
            m = fn(n)
            if m is None:
                continue
            n_done += 1
            out.append(m)
        else:
            out.append(n)
    return out, n_done


def slice_pps_id(n):
    r = hb.Reader(hb.unescape(n), 32 if hb.nal_type(n) == 20 else 8)
    r.ue()
    r.ue()
    return r.ue()


def subset_lists(nals, lists):
    out, k = map_type(nals, 15, lambda n: hb.rewrite_sps(hb.unescape(n), lists=lists))
    assert k, "no subset SPS"
    return out, k


def main():
    what, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    arg = sys.argv[4] if len(sys.argv) > 4 else None
    nals = load(src)
    note = ""
    if what == "scaling-default":
        nals, k = subset_lists(nals, {i: "default" for i in range(8)})
        note = "%d subset SPS with the default matrices" % k
    elif what == "scaling-custom":
        intra4 = [6 + 3 * j for j in range(16)]          # 6 .. 51, rising along the zigzag
        inter8 = [40 - (j * 3) // 8 for j in range(64)]  # 40 .. 17, falling
        nals, k = subset_lists(nals, {0: intra4, 7: inter8})
        note = "%d subset SPS with an invented intra 4x4 and inter 8x8 list" % k
    elif what == "scaling-fallback":
        flat = {i: (FLAT4 if i < 6 else FLAT8) for i in range(8)}
        nals, k = subset_lists(nals, flat)
        nals, kp = map_type(nals, 8, lambda n: hb.rewrite_pps(hb.unescape(n), lists={}))
        note = "%d subset SPS with flat lists written out, %d PPS with a matrix that has no list" % (k, kp)
    elif what == "replace":
        first, k = subset_lists(nals, None)
        second = [n for n in nals if hb.nal_type(n) != 8]
        assert hb.nal_type(second[0]) in (7, 9, 15, 6, 14) and any(hb.nal_type(n) == 5 for n in second[:12])
        # JM asserts when a subset SPS changes in a stream (InterpretSubsetSPS()), so the reference is its
        # decode of the first copy alone, then of the stream as it was: the second coded video sequence
        # decodes as that does, its PPSs being the same bytes, read against the subset SPS it activates
        base = os.path.splitext(dst)[0]
        save(base + ".part1.264", first)
        save(base + ".part2.264", nals)
        open(base + ".jmparts", "w").write("%s\n%s\n" % (os.path.basename(base) + ".part1.264", os.path.basename(base) + ".part2.264"))
        nals = first + second
        note = "first copy: %d subset SPS without a matrix; second copy as it was, without its %d PPS" % (
            k, len(load(src)) - len(second))
    elif what == "pps-first":
        ps = [n for n in nals if hb.nal_type(n) in (7, 8, 15)]
        order = [hb.nal_type(n) for n in nals[:len(ps) + 2]]
        rest = [n for n in nals if hb.nal_type(n) not in (7, 8, 15)]
        new_ps = [n for n in ps if hb.nal_type(n) == 7] + [n for n in ps if hb.nal_type(n) == 8] + \
                 [n for n in ps if hb.nal_type(n) == 15]
        assert len(ps) == 4, "made for one SPS, one subset SPS and two PPS at the head"
        nals = new_ps + rest
        note = "parameter sets in the order SPS, PPS, PPS, subset SPS (were %s)" % order[:4]
    elif what == "views":
        new_view = int(arg)
        subs = [hb.parse_sps(hb.unescape(n)) for n in nals if hb.nal_type(n) == 15]
        assert len(subs) == 1 and len(subs[0]["view_id"]) == 2
        s = subs[0]
        old_view = s["view_id"][1]
        sub_id = s["sps_id"]
        new_id = sub_id + 1
        ren = lambda v: new_view if v == old_view else v
        views = {"view_id": [ren(v) for v in s["view_id"]],
                 "anchor": [[[ren(v) for v in l] for l in a] for a in s["anchor"]],
                 "non_anchor": [[[ren(v) for v in l] for l in a] for a in s["non_anchor"]]}
        levels = [(lev, [(tid, [ren(v) for v in tv], nv) for tid, tv, nv in ops]) for lev, ops in s["levels"]]
        second = []
        for n in nals:
            t = hb.nal_type(n)
            if t == 15:
                second.append(hb.rewrite_sps(hb.unescape(n), sps_id=new_id, views=views, levels=levels))
            elif t == 8:
                p = hb.parse_pps(hb.unescape(n))
                second.append(hb.rewrite_pps(hb.unescape(n), sps_id=new_id) if p["sps_id"] == sub_id else n)
            elif t in (14, 20) and hb.mvc_header(n)[1] == old_view:
                second.append(hb.with_view_id(n, new_view))
            else:
                second.append(n)
        base_pps = {slice_pps_id(n) for n in nals if hb.nal_type(n) in (1, 5)}
        assert not any(hb.parse_pps(hb.unescape(n))["sps_id"] == sub_id and hb.parse_pps(hb.unescape(n))["pps_id"] in base_pps
                       for n in nals if hb.nal_type(n) == 8), "a PPS of the base view refers to the id of the subset SPS"
        base = os.path.splitext(dst)[0]
        save(base + ".part2.264", second)
        save(base + ".part1.264", nals)
        open(base + ".jmparts", "w").write("%s\n%s\n" % (os.path.basename(base) + ".part1.264", os.path.basename(base) + ".part2.264"))
        nals = nals + second
        note = "second copy: view %d is view %d, subset SPS %d is %d" % (old_view, new_view, sub_id, new_id)
    elif what in ("levels", "levels-bad"):
        s = [hb.parse_sps(hb.unescape(n)) for n in nals if hb.nal_type(n) == 15][0]
        allv = list(s["view_id"])
        if what == "levels":
            k = int(arg)
            levels = [(s["levels"][0][0], [(0, allv, len(allv) - 1)]) for _ in range(k)]
            nals, c = map_type(nals, 15, lambda n: hb.rewrite_sps(hb.unescape(n), levels=levels))
            note = "%d subset SPS with %d level values" % (c, k)
        elif arg == "ops":
            levels = [(s["levels"][0][0], [(0, allv, len(allv) - 1)] * 1025)]
            nals, c = map_type(nals, 15, lambda n: hb.rewrite_sps(hb.unescape(n), levels=levels))
            note = "1025 operation points"
        elif arg == "views":
            levels = [(s["levels"][0][0], [(0, [allv[0]] * 1025, len(allv) - 1)])]
            nals, c = map_type(nals, 15, lambda n: hb.rewrite_sps(hb.unescape(n), levels=levels))
            note = "1025 target views"
        elif arg == "cut":
            def cut(n):
                d = hb.unescape(n)
                p = hb.parse_sps(d)
                keep = (p["pos_ops"] + 12) // 8
                return hb.escape(d[:keep] + b"\x80")
            nals, c = map_type(nals, 15, cut)
            note = "subset SPS cut in its operation point table"
        else:
            sys.exit("levels-bad: ops, views or cut")
    elif what in ("viewmod", "viewmod-one"):
        # viewmod <in> <out> <idc>,<value>: the l0 modification list of dependent view P and B slices
        # replaced by that one inter-view modification. 'viewmod': every such slice whose inter-view
        # dependency list for l0 is not empty (a valid stream when value < its length); 'viewmod-one': the
        # first non-anchor one after the fourth access unit only (for values that are not valid)
        idc, val = (int(x) for x in arg.split(","))
        st = hb.Stream()
        out, k, seen_base = [], 0, 0
        for n in nals:
            h = st.feed(n)
            t = hb.nal_type(n)
            if t in (1, 5) and h and h["first_mb"] == 0:
                seen_base += 1
            if h is None or t != 20 or h["slice_type"] not in (0, 1):
                out.append(n)
                continue
            sub = st.sub[st.pps[h["pps_id"]]["sps_id"]]
            refs = (sub["anchor"] if h["anchor"] else sub["non_anchor"])[1][0]
            if what == "viewmod" and refs or what == "viewmod-one" and not k and not h["anchor"] and seen_base > 4:
                out.append(hb.rewrite_slice(n, h, mods=[[(idc, val)], h["mods"][1]]))
                k += 1
            else:
                out.append(n)
        assert k, "no slice to change"
        nals = out
        note = "%d dependent view slices with l0 modification (%d, %d)" % (k, idc, val)
    else:
        sys.exit(__doc__)
    save(dst, nals)
    print("%s: %s (%d NAL units)" % (os.path.basename(dst), note, len(nals)))


main()
