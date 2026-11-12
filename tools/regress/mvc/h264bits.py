#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Test tool of Hades (tools/regress/mvc): an H.264 / MVC Annex B stream as NAL units, and the parameter sets
and slice headers in them read and written bit by bit. Only what these tests need: SPS and
subset SPS (with the MVC extension), PPS, and the slice header of base and dependent view slices.

Nothing here decodes pictures: what a changed stream decodes to is what the reference decoder (JM) says.
"""

SC4 = b"\x00\x00\x00\x01"


# ---------------------------------------------------------------- NAL units
def split_annexb(b):
    """the NAL units of an Annex B byte stream, in order; the trailing zero bytes of a NAL unit (zero_byte of
    the next start code, trailing_zero_8bits) are not part of it"""
    starts = []
    i = 0
    while True:
        j = b.find(b"\x00\x00\x01", i)
        if j < 0:
            break
        starts.append(j + 3)
        i = j + 3
    out = []
    for k, s in enumerate(starts):
        e = (starts[k + 1] - 3) if k + 1 < len(starts) else len(b)
        p = b[s:e]
        while p.endswith(b"\x00"):
            p = p[:-1]
        if p:
            out.append(p)
    return out


def join_annexb(nals):
    return b"".join(SC4 + n for n in nals)


def unescape(p):
    """emulation_prevention_three_byte removed"""
    out = bytearray()
    z = 0
    for c in p:
        if z >= 2 and c == 3:
            z = 0
            continue
        out.append(c)
        z = z + 1 if c == 0 else 0
    return bytes(out)


def escape(r):
    out = bytearray()
    z = 0
    for c in r:
        if z >= 2 and c <= 3:
            out.append(3)
            z = 0
        out.append(c)
        z = z + 1 if c == 0 else 0
    return bytes(out)


def nal_type(n):
    return n[0] & 0x1F


def mvc_header(n):
    """(non_idr_flag, view_id, anchor_pic_flag, inter_view_flag) of a NAL unit of type 14 or 20"""
    e = n[1:4]
    assert not e[0] & 0x80, "SVC extension"
    return ((e[0] >> 6) & 1, ((e[1] << 2) | (e[2] >> 6)) & 0x3FF, (e[2] >> 2) & 1, (e[2] >> 1) & 1)


def with_view_id(n, view_id):
    """the same NAL unit (type 14 or 20) with another view_id in its header"""
    r = bytearray(unescape(n))
    r[2] = (view_id >> 2) & 0xFF
    r[3] = (r[3] & 0x3F) | ((view_id & 3) << 6)
    return escape(bytes(r))


# ---------------------------------------------------------------- bits
class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def bits_left(self):
        return len(self.d) * 8 - self.p

    def u(self, n):
        v = 0
        for _ in range(n):
            if self.p >= len(self.d) * 8:
                raise EOFError("read past the end")
            v = (v << 1) | ((self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
            if z > 32:
                raise ValueError("bad exp-Golomb code")
        return (1 << z) - 1 + (self.u(z) if z else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k & 1 else -(k // 2)


def stop_bit_pos(d):
    """position of the rbsp_stop_one_bit: the last bit set in the data"""
    i = len(d) - 1
    while i >= 0 and d[i] == 0:
        i -= 1
    if i < 0:
        raise ValueError("no stop bit")
    c = d[i]
    t = 0
    while not (c >> t) & 1:
        t += 1
    return i * 8 + 7 - t


class Writer:
    def __init__(self):
        self.bits = []

    def u(self, n, v):
        assert 0 <= v < (1 << n) or n == 0 and v == 0, (n, v)
        for k in range(n - 1, -1, -1):
            self.bits.append((v >> k) & 1)

    def ue(self, v):
        assert v >= 0
        v += 1
        n = v.bit_length()
        self.u(n - 1, 0)
        self.u(n, v)

    def se(self, v):
        self.ue(2 * v - 1 if v > 0 else -2 * v)

    def copy(self, r, n):
        for _ in range(n):
            self.bits.append(r.u(1))

    def trailing(self):
        """rbsp_trailing_bits"""
        self.bits.append(1)
        while len(self.bits) % 8:
            self.bits.append(0)

    def bytes(self):
        assert len(self.bits) % 8 == 0
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            v = 0
            for b in self.bits[i:i + 8]:
                v = (v << 1) | b
            out.append(v)
        return bytes(out)

    def __len__(self):
        return len(self.bits)


# ---------------------------------------------------------------- scaling lists
ZZ4 = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]


def read_scaling_list(r, size):
    """the list as the decoder derives it (in the order the syntax gives it), and useDefaultScalingMatrixFlag"""
    last = nxt = 8
    out = []
    use_default = False
    for j in range(size):
        if nxt != 0:
            d = r.se()
            nxt = (last + d + 256) % 256
            use_default = (j == 0 and nxt == 0)
        v = last if nxt == 0 else nxt
        out.append(v)
        last = v
    return out, use_default


def write_scaling_lists(w, lists, nlists):
    """lists: {index: [values in the order of the syntax] or 'default'}; writes the present flags and the
    deltas of nlists lists"""
    for i in range(nlists):
        if i not in lists:
            w.u(1, 0)
            continue
        w.u(1, 1)
        v = lists[i]
        size = 16 if i < 6 else 64
        if v == "default":
            w.se(-8)   # nextScale 0 at j 0: useDefaultScalingMatrixFlag
            continue
        assert len(v) == size and all(1 <= x <= 255 for x in v)
        last = 8
        for j in range(size):
            w.se((v[j] - last + 128) % 256 - 128)
            last = v[j]


# ---------------------------------------------------------------- SPS and subset SPS
HIGH = (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135)


def skip_hrd(r):
    cnt = r.ue() + 1
    r.u(4)
    r.u(4)
    for _ in range(cnt):
        r.ue()
        r.ue()
        r.u(1)
    r.u(5)
    r.u(5)
    r.u(5)
    r.u(5)


def skip_vui(r):
    if r.u(1):
        if r.u(8) == 255:
            r.u(16)
            r.u(16)
    if r.u(1):
        r.u(1)
    if r.u(1):
        r.u(3)
        r.u(1)
        if r.u(1):
            r.u(8)
            r.u(8)
            r.u(8)
    if r.u(1):
        r.ue()
        r.ue()
    if r.u(1):
        r.u(32)
        r.u(32)
        r.u(1)
    nal = r.u(1)
    if nal:
        skip_hrd(r)
    vcl = r.u(1)
    if vcl:
        skip_hrd(r)
    if nal or vcl:
        r.u(1)
    r.u(1)
    if r.u(1):
        r.u(1)
        for _ in range(6):
            r.ue()


def parse_sps(rbsp):
    """rbsp: the NAL unit (header byte included) without emulation prevention. A dict with the fields, and the
    bit positions the writers need: pos_id (sps_id), pos_scaling (seq_scaling_matrix_present_flag),
    pos_after_scaling, pos_ext (start of the subset SPS part after seq_parameter_set_data), pos_views (the
    num_views_minus1 of the MVC extension), pos_ops (num_level_values_signalled_minus1), stop (the stop bit)."""
    r = Reader(rbsp, 8)
    s = {"nal_type": rbsp[0] & 0x1F, "stop": stop_bit_pos(rbsp)}
    s["profile_idc"] = r.u(8)
    s["constraint"] = r.u(8)
    s["level_idc"] = r.u(8)
    s["pos_id"] = r.p
    s["sps_id"] = r.ue()
    s["pos_after_id"] = r.p
    s["chroma_format_idc"] = 1
    s["separate_colour_plane"] = 0
    s["bit_depth_luma"] = 8
    s["scaling"] = 0
    s["lists"] = {}
    s["pos_scaling"] = None
    if s["profile_idc"] in HIGH:
        s["chroma_format_idc"] = r.ue()
        if s["chroma_format_idc"] == 3:
            s["separate_colour_plane"] = r.u(1)
        s["bit_depth_luma"] = r.ue() + 8
        s["bit_depth_chroma"] = r.ue() + 8
        s["transform_bypass"] = r.u(1)
        s["pos_scaling"] = r.p
        s["scaling"] = r.u(1)
        if s["scaling"]:
            for i in range(8 if s["chroma_format_idc"] != 3 else 12):
                if r.u(1):
                    v, d = read_scaling_list(r, 16 if i < 6 else 64)
                    s["lists"][i] = "default" if d else v
        s["pos_after_scaling"] = r.p
    s["log2_max_frame_num"] = r.ue() + 4
    s["poc_type"] = r.ue()
    s["delta_pic_order_always_zero"] = 0
    if s["poc_type"] == 0:
        s["log2_max_poc_lsb"] = r.ue() + 4
    elif s["poc_type"] == 1:
        s["delta_pic_order_always_zero"] = r.u(1)
        r.se()
        r.se()
        for _ in range(r.ue()):
            r.se()
    s["max_num_ref_frames"] = r.ue()
    s["gaps"] = r.u(1)
    s["mb_width"] = r.ue() + 1
    s["map_height"] = r.ue() + 1
    s["frame_mbs_only"] = r.u(1)
    if not s["frame_mbs_only"]:
        s["mb_aff"] = r.u(1)
    s["direct_8x8"] = r.u(1)
    if r.u(1):
        for _ in range(4):
            r.ue()
    if r.u(1):
        skip_vui(r)
    s["pos_ext"] = r.p
    if s["nal_type"] == 15 and s["profile_idc"] in (118, 128):
        assert r.u(1) == 1  # bit_equal_to_one
        s["pos_views"] = r.p
        nv = r.ue() + 1
        s["view_id"] = [r.ue() for _ in range(nv)]
        s["anchor"] = [[[], []] for _ in range(nv)]
        s["non_anchor"] = [[[], []] for _ in range(nv)]
        for i in range(1, nv):
            for l in range(2):
                s["anchor"][i][l] = [r.ue() for _ in range(r.ue())]
        for i in range(1, nv):
            for l in range(2):
                s["non_anchor"][i][l] = [r.ue() for _ in range(r.ue())]
        s["pos_ops"] = r.p
        nlev = r.ue() + 1
        s["levels"] = []
        for _ in range(nlev):
            lev = r.u(8)
            ops = []
            for _ in range(r.ue() + 1):
                tid = r.u(3)
                tv = [r.ue() for _ in range(r.ue() + 1)]
                ops.append((tid, tv, r.ue()))
            s["levels"].append((lev, ops))
        s["pos_after_ops"] = r.p
    return s


def rewrite_sps(rbsp, sps_id=None, lists="keep", views=None, levels=None):
    """the same SPS or subset SPS with, optionally: another sps_id; other scaling matrices (lists: 'keep', None
    for no matrix, or a dict for write_scaling_lists); another MVC extension (views: dict with view_id, anchor,
    non_anchor, as parse_sps gives them); another operation point table (levels, as parse_sps gives it).
    Everything else is copied bit for bit. Returns the NAL unit, escaped."""
    s = parse_sps(rbsp)
    r = Reader(rbsp, 0)
    w = Writer()
    w.copy(r, s["pos_id"])
    w.ue(s["sps_id"] if sps_id is None else sps_id)
    r.p = s["pos_after_id"]
    if lists != "keep":
        assert s["pos_scaling"] is not None, "not a profile with scaling matrices"
        w.copy(r, s["pos_scaling"] - r.p)
        if lists is None:
            w.u(1, 0)
        else:
            w.u(1, 1)
            write_scaling_lists(w, lists, 8 if s["chroma_format_idc"] != 3 else 12)
        r.p = s["pos_after_scaling"]
    if views is None and levels is None:
        w.copy(r, s["stop"] - r.p)
    else:
        assert "pos_views" in s, "not a subset SPS with an MVC extension"
        w.copy(r, s["pos_views"] - r.p)
        v = views or s
        w.ue(len(v["view_id"]) - 1)
        for x in v["view_id"]:
            w.ue(x)
        for key in ("anchor", "non_anchor"):
            for i in range(1, len(v["view_id"])):
                for l in range(2):
                    w.ue(len(v[key][i][l]))
                    for x in v[key][i][l]:
                        w.ue(x)
        lv = s["levels"] if levels is None else levels
        w.ue(len(lv) - 1)
        for lev, ops in lv:
            w.u(8, lev)
            w.ue(len(ops) - 1)
            for tid, tv, nvm1 in ops:
                w.u(3, tid)
                w.ue(len(tv) - 1)
                for x in tv:
                    w.ue(x)
                w.ue(nvm1)
        r.p = s["pos_after_ops"]
        w.copy(r, s["stop"] - r.p)
    w.trailing()
    return escape(w.bytes())


# ---------------------------------------------------------------- PPS
def parse_pps(rbsp, chroma_format_idc=1):
    r = Reader(rbsp, 8)
    p = {"stop": stop_bit_pos(rbsp)}
    p["pps_id"] = r.ue()
    p["pos_sps_id"] = r.p
    p["sps_id"] = r.ue()
    p["pos_after_sps_id"] = r.p
    p["cabac"] = r.u(1)
    p["bottom_field_pic_order"] = r.u(1)
    p["slice_groups"] = r.ue() + 1
    assert p["slice_groups"] == 1
    p["num_ref_idx"] = [r.ue() + 1, r.ue() + 1]
    p["weighted_pred"] = r.u(1)
    p["weighted_bipred"] = r.u(2)
    p["init_qp"] = r.se() + 26
    p["init_qs"] = r.se() + 26
    p["chroma_qp_offset"] = r.se()
    p["deblocking_ctrl"] = r.u(1)
    p["constrained_intra"] = r.u(1)
    p["redundant_pic_cnt"] = r.u(1)
    p["pos_ext"] = r.p
    p["has_ext"] = r.p < p["stop"]
    p["transform_8x8"] = 0
    p["pic_scaling"] = 0
    p["lists"] = {}
    p["second_chroma_qp_offset"] = p["chroma_qp_offset"]
    if p["has_ext"]:
        p["transform_8x8"] = r.u(1)
        p["pic_scaling"] = r.u(1)
        if p["pic_scaling"]:
            for i in range(6 + (2 if chroma_format_idc != 3 else 6) * p["transform_8x8"]):
                if r.u(1):
                    v, d = read_scaling_list(r, 16 if i < 6 else 64)
                    p["lists"][i] = "default" if d else v
        p["second_chroma_qp_offset"] = r.se()
    return p


def rewrite_pps(rbsp, sps_id=None, lists="keep"):
    """the same PPS with, optionally, another sps_id and another pic_scaling_matrix (lists: 'keep', None for
    none, or a dict as for write_scaling_lists; 4:2:0 only). The extension is written whenever lists is
    given, with the transform_8x8_mode_flag and second_chroma_qp_index_offset it had."""
    p = parse_pps(rbsp)
    r = Reader(rbsp, 0)
    w = Writer()
    w.copy(r, p["pos_sps_id"])
    w.ue(p["sps_id"] if sps_id is None else sps_id)
    r.p = p["pos_after_sps_id"]
    if lists == "keep":
        w.copy(r, p["stop"] - r.p)
    else:
        w.copy(r, p["pos_ext"] - r.p)
        w.u(1, p["transform_8x8"])
        if lists is None:
            w.u(1, 0)
        else:
            w.u(1, 1)
            write_scaling_lists(w, lists, 6 + 2 * p["transform_8x8"])
        w.se(p["second_chroma_qp_offset"])
    w.trailing()
    return escape(w.bytes())


# ---------------------------------------------------------------- slice header
def parse_slice_header(n, sps_of, pps_of):
    """n: an escaped NAL unit of type 1, 5 or 20. sps_of(pps, is_dependent) and pps_of(pps_id) give the parameter
    sets as parse_sps/parse_pps dicts. Returns the dict of the header and its positions (in the unescaped
    bytes): pos_mod (start of ref_pic_list_(mvc_)modification), pos_after_mod, pos_end (end of the header),
    data (the unescaped bytes)."""
    t = nal_type(n)
    d = unescape(n)
    dep = t == 20
    r = Reader(d, 32 if dep else 8)
    h = {"nal_type": t, "ref_idc": (d[0] >> 5) & 3, "data": d}
    if dep:
        h["non_idr"], h["view_id"], h["anchor"], h["inter_view"] = mvc_header(n)
        idr = not h["non_idr"]
    else:
        idr = t == 5
    h["idr"] = idr
    h["first_mb"] = r.ue()
    h["slice_type"] = r.ue() % 5
    h["pps_id"] = r.ue()
    pps = pps_of(h["pps_id"])
    sps = sps_of(pps, dep)
    if sps["separate_colour_plane"]:
        r.u(2)
    h["frame_num"] = r.u(sps["log2_max_frame_num"])
    h["field"] = 0
    if not sps["frame_mbs_only"]:
        h["field"] = r.u(1)
        if h["field"]:
            h["bottom"] = r.u(1)
    if idr:
        h["idr_pic_id"] = r.ue()
    if sps["poc_type"] == 0:
        h["poc_lsb"] = r.u(sps["log2_max_poc_lsb"])
        if pps["bottom_field_pic_order"] and not h["field"]:
            r.se()
    if sps["poc_type"] == 1 and not sps["delta_pic_order_always_zero"]:
        r.se()
        if pps["bottom_field_pic_order"] and not h["field"]:
            r.se()
    if pps["redundant_pic_cnt"]:
        r.ue()
    st = h["slice_type"]   # 0 P, 1 B, 2 I, 3 SP, 4 SI
    if st == 1:
        r.u(1)
    h["num_ref_idx"] = list(pps["num_ref_idx"])
    h["pos_override"] = r.p
    if st in (0, 1, 3):
        h["override"] = r.u(1)
        if h["override"]:
            h["num_ref_idx"][0] = r.ue() + 1
            if st == 1:
                h["num_ref_idx"][1] = r.ue() + 1
    h["pos_mod"] = r.p
    h["mods"] = [None, None]
    for l in range(2 if st == 1 else 1 if st in (0, 3) else 0):
        if r.u(1):
            ops = []
            while True:
                idc = r.ue()
                if idc == 3:
                    break
                ops.append((idc, r.ue()))
            h["mods"][l] = ops
        else:
            h["mods"][l] = []
    h["pos_after_mod"] = r.p
    if (pps["weighted_pred"] and st in (0, 3)) or (pps["weighted_bipred"] == 1 and st == 1):
        r.ue()
        chroma = sps["chroma_format_idc"] != 0 and not sps["separate_colour_plane"]
        if chroma:
            r.ue()
        for l in range(2 if st == 1 else 1):
            for _ in range(h["num_ref_idx"][l]):
                if r.u(1):
                    r.se()
                    r.se()
                if chroma and r.u(1):
                    for _ in range(4):
                        r.se()
    h["pos_marking"] = r.p
    h["mmco"] = None
    if h["ref_idc"]:
        if idr:
            h["no_output_of_prior_pics"] = r.u(1)
            h["long_term_reference"] = r.u(1)
        else:
            if r.u(1):
                mm = []
                while True:
                    op = r.ue()
                    if op == 0:
                        break
                    args = []
                    if op in (1, 3):
                        args.append(r.ue())
                    if op == 2:
                        args.append(r.ue())
                    if op in (3, 6):
                        args.append(r.ue())
                    if op == 4:
                        args.append(r.ue())
                    mm.append((op, args))
                h["mmco"] = mm
    h["pos_after_marking"] = r.p
    if pps["cabac"] and st not in (2, 4):
        r.ue()
    r.se()
    if st in (3, 4):
        if st == 3:
            r.u(1)
        r.se()
    if pps["deblocking_ctrl"]:
        if r.ue() != 1:
            r.se()
            r.se()
    h["pos_end"] = r.p
    h["cabac"] = pps["cabac"]
    return h


def rewrite_slice(n, h, mods=None, mmco="keep", num_ref_idx=None):
    """the slice of NAL unit n (h from parse_slice_header) with other modification lists (mods: [ops of l0,
    ops of l1], an op (idc, value); None: keep), another adaptive marking (mmco: 'keep', None for the sliding
    window, or a list of (op, args)), another number of active references (num_ref_idx: [l0, l1] written as an
    override; None: keep). The slice data is copied: for CABAC from its byte-aligned start after the
    cabac_alignment_one_bits, for CAVLC bit for bit. Returns the escaped NAL unit."""
    d = h["data"]
    st = h["slice_type"]
    r = Reader(d, 0)
    w = Writer()
    w.copy(r, h["pos_override"])
    if st in (0, 1, 3):
        if num_ref_idx is None:
            w.copy(r, h["pos_mod"] - h["pos_override"])
        else:
            w.u(1, 1)
            w.ue(num_ref_idx[0] - 1)
            if st == 1:
                w.ue(num_ref_idx[1] - 1)
    r.p = h["pos_mod"]
    if mods is None:
        w.copy(r, h["pos_after_mod"] - h["pos_mod"])
    else:
        for l in range(2 if st == 1 else 1 if st in (0, 3) else 0):
            ops = mods[l]
            if not ops:
                w.u(1, 0)
                continue
            w.u(1, 1)
            for idc, val in ops:
                w.ue(idc)
                w.ue(val)
            w.ue(3)
    r.p = h["pos_after_mod"]
    w.copy(r, h["pos_marking"] - h["pos_after_mod"])
    if mmco == "keep" or not h["ref_idc"]:
        w.copy(r, h["pos_after_marking"] - h["pos_marking"])
    else:
        assert not h["idr"]
        if mmco is None:
            w.u(1, 0)
        else:
            w.u(1, 1)
            for op, args in mmco:
                w.ue(op)
                for a in args:
                    w.ue(a)
            w.ue(0)
    r.p = h["pos_after_marking"]
    w.copy(r, h["pos_end"] - h["pos_after_marking"])
    if h["cabac"]:
        start = (h["pos_end"] + 7) // 8 * 8
        assert all(Reader(d, p).u(1) == 1 for p in range(h["pos_end"], start)), "cabac_alignment_one_bit"
        while len(w) % 8:
            w.bits.append(1)
        return escape(w.bytes() + d[start // 8:])
    stop = stop_bit_pos(d)
    r.p = h["pos_end"]
    w.copy(r, stop - h["pos_end"])
    w.trailing()
    return escape(w.bytes())


class Stream:
    """parameter sets as they arrive, for parsing the slice headers of a stream in order"""

    def __init__(self):
        self.sps = {}
        self.sub = {}
        self.pps = {}

    def feed(self, n):
        t = nal_type(n)
        if t == 7:
            s = parse_sps(unescape(n))
            self.sps[s["sps_id"]] = s
        elif t == 15:
            s = parse_sps(unescape(n))
            self.sub[s["sps_id"]] = s
        elif t == 8:
            p = parse_pps(unescape(n))
            self.pps[p["pps_id"]] = p
        if t in (1, 5, 20):
            return parse_slice_header(n, lambda p, dep: (self.sub if dep else self.sps)[p["sps_id"]],
                                      lambda i: self.pps[i])
        return None
