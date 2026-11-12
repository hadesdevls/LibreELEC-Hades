# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""HEVC elementary streams (ITU-T H.265 Annex B) for the check clips: the NAL units, the access units,
the few fields of the sequence parameter set that the container needs, and the decoder configuration
record of ISO/IEC 14496-15 (hvcC)."""

VPS, SPS, PPS, AUD, EOS, EOB, FD, SEI_PREFIX, SEI_SUFFIX = 32, 33, 34, 35, 36, 37, 38, 39, 40
UNSPEC62 = 62  # Dolby Vision RPU


def nal_type(nal):
    return (nal[0] >> 1) & 0x3F


def split_annexb(data):
    """the NAL units of an Annex B stream (without start codes and without the zero bytes before one)"""
    starts = []
    i, n = 0, len(data)
    while True:
        i = data.find(b"\x00\x00\x01", i)
        if i < 0:
            break
        starts.append(i + 3)
        i += 3
    nals = []
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else n
        while e > s and data[e - 1] == 0:  # the zero_byte of the next start code, trailing_zero_8bits
            e -= 1
        nals.append(bytes(data[s:e]))
    if not nals or starts[0] > 4 or any(data[:starts[0] - 3]):
        raise ValueError("not an Annex B stream")
    return nals


def access_units(nals):
    """the NAL units grouped into access units (ITU-T H.265 7.4.2.4.4: after the last VCL unit of a
    picture, the first of an AUD, VPS, SPS, PPS, prefix SEI, types 41-44 or 48-55, or the first slice
    segment of a picture, starts the next access unit)"""
    aus, cur, vcl = [], [], False
    for nal in nals:
        t = nal_type(nal)
        first_slice = t < 32 and len(nal) > 2 and nal[2] & 0x80
        starts = t in (VPS, SPS, PPS, AUD, SEI_PREFIX) or 41 <= t <= 44 or 48 <= t <= 55 or first_slice
        if starts and vcl:
            aus.append(cur)
            cur, vcl = [], False
        cur.append(nal)
        if t < 32:
            vcl = True
    if cur:
        aus.append(cur)
    return aus


def unescape(nal):
    """the RBSP of a NAL unit's payload (emulation prevention bytes removed), header included"""
    out = bytearray()
    zeros = 0
    for b in nal:
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


class BitReader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def u(self, n):
        v = 0
        for _ in range(n):
            byte = self.data[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def ue(self):
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
            if zeros > 31:
                raise ValueError("bad exp-Golomb code")
        return (1 << zeros) - 1 + self.u(zeros)


def parse_sps(nal):
    """the fields of an SPS that hvcC carries"""
    rbsp = unescape(nal)
    r = BitReader(rbsp, 16)
    out = {"vps_id": r.u(4), "max_sub_layers_minus1": r.u(3), "temporal_id_nesting": r.u(1)}
    ptl_start = r.pos
    r.u(96)  # general profile, tier, level
    n = out["max_sub_layers_minus1"]
    present = [(r.u(1), r.u(1)) for _ in range(n)]
    if n > 0:
        r.u(2 * (8 - n))
    for p, l in present:
        r.u(88 if p else 0)
        r.u(8 if l else 0)
    out["ptl"] = rbsp[ptl_start // 8: ptl_start // 8 + 12]
    out["sps_id"] = r.ue()
    out["chroma_format_idc"] = r.ue()
    if out["chroma_format_idc"] == 3:
        r.u(1)
    out["width"] = r.ue()
    out["height"] = r.ue()
    if r.u(1):  # conformance_window_flag
        out["conformance_window"] = (r.ue(), r.ue(), r.ue(), r.ue())
    out["bit_depth_luma"] = r.ue() + 8
    out["bit_depth_chroma"] = r.ue() + 8
    return out


def hvcc(nals):
    """HEVCDecoderConfigurationRecord (ISO/IEC 14496-15 8.3.3.1) of the parameter sets of a stream, as
    FFmpeg writes it for Matroska (ff_isom_write_hvcc(), array_completeness 0: the parameter sets are in
    the stream as well): no spatial segmentation, parallelism unknown, frame rate not given, lengths of
    4 bytes, arrays of VPS, SPS and PPS"""
    ps = {VPS: [], SPS: [], PPS: []}
    for nal in nals:
        t = nal_type(nal)
        if t in ps and nal not in ps[t]:
            ps[t].append(nal)
    if not all(ps.values()):
        raise ValueError("a parameter set is missing")
    sps = parse_sps(ps[SPS][0])
    out = bytearray()
    out.append(1)  # configurationVersion
    out += sps["ptl"][:1]  # general_profile_space, general_tier_flag, general_profile_idc
    out += sps["ptl"][1:5]  # general_profile_compatibility_flags
    out += sps["ptl"][5:11]  # general_constraint_indicator_flags
    out += sps["ptl"][11:12]  # general_level_idc
    out += (0xF000).to_bytes(2, "big")  # reserved, min_spatial_segmentation_idc 0
    out.append(0xFC)  # reserved, parallelismType 0
    out.append(0xFC | sps["chroma_format_idc"])
    out.append(0xF8 | (sps["bit_depth_luma"] - 8))
    out.append(0xF8 | (sps["bit_depth_chroma"] - 8))
    out += (0).to_bytes(2, "big")  # avgFrameRate
    out.append(((sps["max_sub_layers_minus1"] + 1) << 3) | (sps["temporal_id_nesting"] << 2) | 3)
    arrays = [(t, ps[t]) for t in (VPS, SPS, PPS)]
    out.append(len(arrays))
    for t, units in arrays:
        out.append(t)  # array_completeness 0, reserved 0, NAL_unit_type
        out += len(units).to_bytes(2, "big")
        for u in units:
            out += len(u).to_bytes(2, "big") + u
    return bytes(out), sps


def length_prefixed(nals):
    """an access unit as Matroska and MP4 store it: each NAL unit after its length in 4 bytes"""
    return b"".join(len(n).to_bytes(4, "big") + n for n in nals)


def annexb(nals):
    return b"".join(b"\x00\x00\x00\x01" + n for n in nals)
