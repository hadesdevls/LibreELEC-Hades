#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): the bit writers of h264bits.py give back, byte for byte, every parameter set
and every slice header of the given streams when asked to write them as they were (with the scaling lists,
modifications and markings that were read, written again). This is what the crafted streams rest on.
Usage: bits-selftest.py <stream.264>..."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h264bits as hb

def norm(n):
    return hb.escape(hb.unescape(n))

total = {"sps": 0, "pps": 0, "slices": 0, "fail": 0, "mods": 0, "mmco": 0}
for f in sys.argv[1:]:
    st = hb.Stream()
    for k, n in enumerate(hb.split_annexb(open(f, "rb").read())):
        t = hb.nal_type(n)
        try:
            if t in (7, 15):
                d = hb.unescape(n)
                s = hb.parse_sps(d)
                a = hb.rewrite_sps(d)
                b = hb.rewrite_sps(d, sps_id=s["sps_id"], lists=(s["lists"] if s["scaling"] else None) if s["pos_scaling"] is not None else "keep",
                                   views=s if "view_id" in s else None, levels=s.get("levels"))
                s2 = hb.parse_sps(hb.unescape(b))
                same = {k2: v for k2, v in s.items() if not k2.startswith("pos") and k2 != "stop"} == \
                       {k2: v for k2, v in s2.items() if not k2.startswith("pos") and k2 != "stop"}
                if a != norm(n) or not same:
                    total["fail"] += 1
                    print("FAIL sps %s #%d: copy %s, rewritten fields %s" % (os.path.basename(f), k, a == norm(n), same))
                total["sps"] += 1
            elif t == 8:
                d = hb.unescape(n)
                p = hb.parse_pps(d)
                a = hb.rewrite_pps(d)
                b = hb.rewrite_pps(d, lists=(p["lists"] if p["pic_scaling"] else None)) if p["has_ext"] else a
                p2 = hb.parse_pps(hb.unescape(b))
                same = {k2: v for k2, v in p.items() if not k2.startswith("pos") and k2 != "stop"} == \
                       {k2: v for k2, v in p2.items() if not k2.startswith("pos") and k2 != "stop"}
                if a != norm(n) or not same:
                    total["fail"] += 1
                    print("FAIL pps %s #%d: copy %s, rewritten fields %s" % (os.path.basename(f), k, a == norm(n), same))
                total["pps"] += 1
            h = st.feed(n)
            if h is not None:
                a = hb.rewrite_slice(n, h)
                b = hb.rewrite_slice(n, h, mods=h["mods"], mmco=h["mmco"] if h["ref_idc"] and not h["idr"] else "keep",
                                     num_ref_idx=h["num_ref_idx"] if h.get("override") else None)
                if a != norm(n) or b != norm(n):
                    total["fail"] += 1
                    print("FAIL slice %s #%d type %d: copy %s, rewritten %s" % (os.path.basename(f), k, t, a == norm(n), b == norm(n)))
                total["slices"] += 1
                total["mods"] += sum(1 for m in h["mods"] if m)
                total["mmco"] += 1 if h["mmco"] else 0
        except Exception as e:
            total["fail"] += 1
            print("FAIL %s #%d type %d: %s" % (os.path.basename(f), k, t, e))
print("streams %d; SPS and subset SPS %d, PPS %d, slices %d (with a modification list: %d, with adaptive marking: %d); failures %d"
      % (len(sys.argv) - 1, total["sps"], total["pps"], total["slices"], total["mods"], total["mmco"], total["fail"]))
