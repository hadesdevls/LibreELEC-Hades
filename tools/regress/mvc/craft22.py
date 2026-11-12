#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): what the tests need that craft.py does not make, with h264bits.py (its writer is checked byte for byte by bits-selftest.py).

  levels-huge <in.264> <out.264>   the operation point table of every subset SPS replaced by one level with
                                   num_applicable_ops_minus1 = 4294967294 and nothing after it (a damaged
                                   subset SPS: the NAL unit ends there). A parser that bounds the count rejects
                                   it at once; one that does not loops 2^32 times over an exhausted reader.
  grain <in.264> <out.264>         a film grain characteristics SEI (payload type 19; model 0, luma only, one
                                   interval 0-255, scale value 40, repetition period 0) before the base view
                                   of every access unit (before its prefix NAL unit): the base view must come
                                   out of h264_mvc with the grain the stock decoder gives it (a review concern:
                                   grain synthesis skipped where the view switch ends the base view picture)
  spsbits <in.264> <out.bin>       the first subset SPS of the stream for f8parse.c: [u32 LE: bits from the
                                   end of the NAL unit header to the stop bit][u32 LE: of those, the bits
                                   after the operation point table][the RBSP after the header byte]
Usage: craft22.py <what> <in> <out>"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h264bits as hb


def levels_huge(rbsp):
    s = hb.parse_sps(rbsp)
    r = hb.Reader(rbsp, 0)
    w = hb.Writer()
    w.copy(r, s["pos_ops"])           # everything up to num_level_values_signalled_minus1, as it was
    w.ue(0)                           # one level value
    w.u(8, s["levels"][0][0])         # its level_idc
    w.ue(4294967294)                  # num_applicable_ops_minus1: 2^32 - 2
    w.trailing()
    return hb.escape(w.bytes())


what, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
nals = hb.split_annexb(open(src, "rb").read())
if what == "levels-huge":
    out, k = [], 0
    for n in nals:
        if hb.nal_type(n) == 15:
            out.append(levels_huge(hb.unescape(n)))
            k += 1
        else:
            out.append(n)
    assert k, "no subset SPS"
    open(dst, "wb").write(hb.join_annexb(out))
    print("%s: %d subset SPS with num_applicable_ops_minus1 = 4294967294 and nothing after it" % (os.path.basename(dst), k))
elif what == "grain":
    w = hb.Writer()
    w.u(8, 0x06)                      # SEI NAL unit header
    w.u(8, 19)                        # payload type: film grain characteristics
    w.u(8, 7)                         # payload size: 7 bytes
    w.u(1, 0)                         # film_grain_characteristics_cancel_flag
    w.u(2, 0)                         # film_grain_model_id: frequency filtering
    w.u(1, 0)                         # separate_colour_description_present_flag
    w.u(2, 0)                         # blending_mode_id: additive
    w.u(4, 4)                         # log2_scale_factor
    w.u(1, 1); w.u(1, 0); w.u(1, 0)   # comp_model_present_flag: luma only
    w.u(8, 0)                         # num_intensity_intervals_minus1
    w.u(3, 0)                         # num_model_values_minus1
    w.u(8, 0); w.u(8, 255)            # the interval
    w.se(40)                          # comp_model_value
    w.ue(0)                           # film_grain_characteristics_repetition_period
    w.u(1, 1)                         # payload alignment: bit_equal_to_one, then zeros
    while (len(w) - 24) % 8:
        w.u(1, 0)
    assert len(w) - 24 == 56, len(w)
    w.trailing()
    sei = hb.escape(w.bytes())
    out, k = [], 0
    for i, n in enumerate(nals):
        if hb.nal_type(n) == 14 and i + 1 < len(nals) and hb.nal_type(nals[i + 1]) in (1, 5) and nals[i + 1][1] & 0x80:
            out.append(sei)
            k += 1
        out.append(n)
    assert k, "no access unit with a prefix NAL unit"
    open(dst, "wb").write(hb.join_annexb(out))
    print("%s: a film grain SEI before %d access units" % (os.path.basename(dst), k))
elif what == "spsbits":
    n = next(n for n in nals if hb.nal_type(n) == 15)
    d = hb.unescape(n)
    bits = hb.stop_bit_pos(d) - 8
    try:
        s = hb.parse_sps(d)
        after = s["stop"] - s["pos_after_ops"]
        note = "%d after its operation point table (%d level values)" % (after, len(s["levels"]))
    except EOFError:
        # a damaged table (levels-huge): h264bits cannot read it either; no expectation
        after = 0xFFFFFFFF
        note = "its operation point table cannot be read to its end (damaged)"
    open(dst, "wb").write(struct.pack("<II", bits, after) + d[1:])
    print("%s: subset SPS of %d bits after its header, %s" % (os.path.basename(dst), bits, note))
else:
    sys.exit(__doc__)
