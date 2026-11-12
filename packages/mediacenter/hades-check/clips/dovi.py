# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Dolby Vision metadata for the check clips: the RPU of each picture and the decoder configuration
record of the container.

The RPU is written as ETSI GS CCM 001 V1.1.1 lays it out, field by field in the order and widths of
FFmpeg's writer (libavcodec/dovi_rpuenc.c, ff_dovi_rpu_generate(), without metadata compression) and of
dovi_tool (MIT); both were read as data, nothing of them runs here. What the RPU says is what Kodi of
Hades makes for HDR10 sent as Dolby Vision (tools/dvbridge/dvbridge_hdr10plus.c,
dvbridge_profile81_metadata()): the header of profile 8.1 as dovi_tool makes it, an identity mapping, the
colour of BT.2020 PQ (or, for profile 5, the IPT-PQ-c2 matrices of dvbridge_ipt.h), the mastering
display as the source range, and display management of CM v2.9 with levels 1 and 6 (in that order, as
dovi_tool writes the blocks; the bridge makes no level 5 for the picture of a whole frame, and none is
written).
"""
import card


class BitWriter:
    def __init__(self):
        self.bits = []

    def u(self, n, v):
        if n == 0:
            return
        if v < 0 or v >= 1 << n:
            raise ValueError("%d does not fit in %d bits" % (v, n))
        self.bits.extend((v >> (n - 1 - i)) & 1 for i in range(n))

    def s(self, n, v):
        if v < -(1 << (n - 1)) or v >= 1 << (n - 1):
            raise ValueError("%d does not fit in %d signed bits" % (v, n))
        self.u(n, v & ((1 << n) - 1))

    def ue(self, v):
        if v < 0:
            raise ValueError("ue(v) of a negative value")
        x = v + 1
        n = x.bit_length()
        self.u(n - 1, 0)
        self.u(n, x)

    def se(self, v):
        self.ue(2 * v - 1 if v > 0 else -2 * v)

    def align(self):
        while len(self.bits) % 8:
            self.bits.append(0)

    def tobytes(self):
        if len(self.bits) % 8:
            raise ValueError("not byte aligned")
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            b = 0
            for bit in self.bits[i:i + 8]:
                b = (b << 1) | bit
            out.append(b)
        return bytes(out)


def crc32_mpeg2(data):
    """CRC-32/MPEG-2: polynomial 0x04C11DB7, initial value 0xFFFFFFFF, not reflected, no final xor"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def pq_code12(nits):
    c = card.rnd(card.pq_oetf(nits) * 4095.0, "PQ code")
    return min(max(c, 0), 4095)


# BT.2020 PQ, as dovi_tool's profile 8.1 and Kodi of Hades have it (13 and 14 fractional bits, offsets 28)
YCC_2020 = (9574, 0, 13802, 9574, -1540, -5348, 9574, 17610, 0)
OFFSETS_2020 = (16777216, 134217728, 134217728)


def metadata(profile, l1, mastering, max_cll, max_fall, scene_refresh):
    """the fields of an RPU (a dict, the names of FFmpeg's AVDOVIMetadata)"""
    min_nits, max_nits = mastering["min_nits"], mastering["max_nits"]
    ipt = profile == 5
    return {
        "header": {
            "rpu_type": 2, "rpu_format": 18, "vdr_rpu_profile": 0 if ipt else 1, "vdr_rpu_level": 0,
            "chroma_resampling_explicit_filter_flag": 0, "coef_data_type": 0, "coef_log2_denom": 23,
            "vdr_rpu_normalized_idc": 1, "bl_video_full_range_flag": 1 if ipt else 0,
            "bl_bit_depth": 10, "el_bit_depth": 10, "vdr_bit_depth": 12,
            "spatial_resampling_filter_flag": 0, "el_spatial_resampling_filter_flag": 0,
            "disable_residual_flag": 1,
        },
        "mapping": {
            "vdr_rpu_id": 0, "mapping_color_space": 0, "mapping_chroma_format_idc": 0,
            # identity: one piece over the codes of the base layer, x^1
            "curves": [{"pivots": (0, 1023), "pieces": [{"poly_order": 1, "poly_coef": (0, 1 << 23)}]}] * 3,
            "num_x_partitions": 1, "num_y_partitions": 1,
        },
        "color": {
            "dm_metadata_id": 0, "scene_refresh_flag": 1 if scene_refresh else 0,
            "ycc_to_rgb_matrix": card.IPT_YCC if ipt else YCC_2020,
            "ycc_to_rgb_offset": card.IPT_OFFSETS if ipt else OFFSETS_2020,
            "rgb_to_lms_matrix": card.IPT_LMS if ipt else card.LMS_2020,
            "signal_eotf": 65535, "signal_eotf_param0": 0, "signal_eotf_param1": 0, "signal_eotf_param2": 0,
            "signal_bit_depth": 12, "signal_color_space": 2 if ipt else 0, "signal_chroma_format": 0,
            "signal_full_range_flag": 1,
            # the mastering display as PQ codes (a minimum of at most 0.001 cd/m2 is 7, as in dovi_tool)
            "source_min_pq": 7 if min_nits <= 0.001 else pq_code12(min_nits),
            "source_max_pq": pq_code12(max_nits),
            "source_diagonal": 42,
        },
        "ext": [
            {"level": 1, "min_pq": l1[0], "max_pq": l1[1], "avg_pq": l1[2]},
            {"level": 6, "max_luminance": card.rnd(max_nits, "L6 max"),
             "min_luminance": card.rnd(min_nits * 10000.0, "L6 min"),
             "max_cll": min(int(max_cll), 65535), "max_fall": min(int(max_fall), 65535)},
        ],
    }


def rpu_payload(m):
    """the RPU (rpu_data(), from rpu_type to the terminator 0x80, without the prefix 0x19)"""
    h, mp, c = m["header"], m["mapping"], m["color"]
    w = BitWriter()
    w.u(6, h["rpu_type"])
    w.u(11, h["rpu_format"])
    w.u(4, h["vdr_rpu_profile"])
    w.u(4, h["vdr_rpu_level"])
    w.u(1, 1)  # vdr_seq_info_present_flag
    w.u(1, h["chroma_resampling_explicit_filter_flag"])
    w.u(2, h["coef_data_type"])
    w.ue(h["coef_log2_denom"])
    w.u(2, h["vdr_rpu_normalized_idc"])
    w.u(1, h["bl_video_full_range_flag"])
    if h["rpu_format"] & 0x700 == 0:
        w.ue(h["bl_bit_depth"] - 8)
        w.ue(h["el_bit_depth"] - 8)  # (ext_mapping_idc 0 << 8)
        w.ue(h["vdr_bit_depth"] - 8)
        w.u(1, h["spatial_resampling_filter_flag"])
        w.u(3, 0)  # reserved_zero_3bits
        w.u(1, h["el_spatial_resampling_filter_flag"])
        w.u(1, h["disable_residual_flag"])
    w.u(1, 1)  # vdr_dm_metadata_present_flag
    w.u(1, 0)  # use_prev_vdr_rpu_flag
    w.ue(mp["vdr_rpu_id"])
    w.ue(mp["mapping_color_space"])
    w.ue(mp["mapping_chroma_format_idc"])
    for curve in mp["curves"]:
        w.ue(len(curve["pivots"]) - 2)
        prev = 0
        for p in curve["pivots"]:
            w.u(h["bl_bit_depth"], p - prev)  # pred_pivot_value: from the pivot before
            prev = p
    # (no NLQ: disable_residual_flag)
    w.ue(mp["num_x_partitions"] - 1)
    w.ue(mp["num_y_partitions"] - 1)
    denom = h["coef_log2_denom"]
    for curve in mp["curves"]:
        for piece in curve["pieces"]:
            w.ue(0)  # mapping_idc: polynomial
            w.ue(piece["poly_order"] - 1)
            if piece["poly_order"] == 1:
                w.u(1, 0)  # linear_interp_flag
            for k in range(piece["poly_order"] + 1):
                coef = piece["poly_coef"][k]
                w.se(coef >> denom)  # poly_coef_int
                w.u(denom, coef & ((1 << denom) - 1))  # poly_coef
    # vdr_dm_data_payload()
    w.ue(c["dm_metadata_id"])  # affected_dm_metadata_id
    w.ue(c["dm_metadata_id"])  # current_dm_metadata_id
    w.ue(c["scene_refresh_flag"])
    for v in c["ycc_to_rgb_matrix"]:
        w.s(16, v)
    for v in c["ycc_to_rgb_offset"]:
        w.u(32, v)
    for v in c["rgb_to_lms_matrix"]:
        w.s(16, v)
    w.u(16, c["signal_eotf"])
    w.u(16, c["signal_eotf_param0"])
    w.u(16, c["signal_eotf_param1"])
    w.u(32, c["signal_eotf_param2"])
    w.u(5, c["signal_bit_depth"])
    w.u(2, c["signal_color_space"])
    w.u(2, c["signal_chroma_format"])
    w.u(2, c["signal_full_range_flag"])
    w.u(12, c["source_min_pq"])
    w.u(12, c["source_max_pq"])
    w.u(10, c["source_diagonal"])
    # the extension blocks of CM v2.9
    w.ue(len(m["ext"]))  # num_ext_blocks
    w.align()  # dm_alignment_zero_bit
    for e in m["ext"]:
        if e["level"] == 1:
            length = 5
            w.ue(length)
            w.u(8, 1)
            start = len(w.bits)
            w.u(12, e["min_pq"])
            w.u(12, e["max_pq"])
            w.u(12, e["avg_pq"])
        elif e["level"] == 6:
            length = 8
            w.ue(length)
            w.u(8, 6)
            start = len(w.bits)
            w.u(16, e["max_luminance"])
            w.u(16, e["min_luminance"])
            w.u(16, e["max_cll"])
            w.u(16, e["max_fall"])
        else:
            raise ValueError("an extension block of level %d is not written here" % e["level"])
        w.u(length * 8 - (len(w.bits) - start), 0)  # ext_dm_alignment_zero_bit
    w.align()  # rpu_alignment_zero_bit
    body = w.tobytes()
    return body + crc32_mpeg2(body).to_bytes(4, "big") + b"\x80"


def emulation_prevent(data):
    """an RBSP -> the bytes of a NAL unit's payload (an 0x03 before a byte 0-3 after two zeros)"""
    out = bytearray()
    zeros = 0
    for b in data:
        if zeros >= 2 and b <= 3:
            out.append(3)
            zeros = 0
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def rpu_nal(m):
    """the RPU as an HEVC NAL unit of type 62 (header 0x7C01) with the prefix 0x19, emulation prevented,
    without start code"""
    return b"\x7c\x01" + emulation_prevent(b"\x19" + rpu_payload(m))


# ---------------------------------------------------------------------------------------------------
# the decoder configuration record (Dolby Vision streams within the ISO base media file format; FFmpeg
# libavformat/dovi_isom.c ff_isom_put_dvcc_dvvc()); the level as FFmpeg picks it (dovi_rpuenc.c,
# dovi_configure_ext(): the lowest level whose pixel rate and width hold the stream)

DV_LEVELS = {1: (1280 * 720 * 24, 1280), 2: (1280 * 720 * 30, 1280), 3: (1920 * 1080 * 24, 1920),
             4: (1920 * 1080 * 30, 2560), 5: (1920 * 1080 * 60, 3840), 6: (3840 * 2160 * 24, 3840),
             7: (3840 * 2160 * 30, 3840), 8: (3840 * 2160 * 48, 3840), 9: (3840 * 2160 * 60, 3840),
             10: (3840 * 2160 * 120, 3840), 11: (3840 * 2160 * 120, 7680), 12: (7680 * 4320 * 60, 7680),
             13: (7680 * 4320 * 120, 7680)}


def dv_level(width, height, fps):
    pps = width * height * fps[0] // fps[1]
    for level in sorted(DV_LEVELS):
        max_pps, max_w = DV_LEVELS[level]
        if pps <= max_pps and width <= max_w:
            return level
    raise ValueError("no Dolby Vision level holds %dx%d at %d/%d" % (width, height, fps[0], fps[1]))


def config_record(profile, level, compatibility):
    """dvcC / dvvC: 24 bytes"""
    w = BitWriter()
    w.u(8, 1)  # dv_version_major
    w.u(8, 0)  # dv_version_minor
    w.u(7, profile)
    w.u(6, level)
    w.u(1, 1)  # rpu_present_flag
    w.u(1, 0)  # el_present_flag
    w.u(1, 1)  # bl_present_flag
    w.u(4, compatibility)  # dv_bl_signal_compatibility_id
    w.u(2, 0)  # dv_md_compression: none
    w.u(26, 0)
    for _ in range(4):
        w.u(32, 0)
    return w.tobytes()


def config_box_type(profile):
    """'dvcC' up to profile 7, 'dvvC' for profiles 8 to 10 (as FFmpeg's muxers name them)"""
    return b"dvvC" if profile > 7 else b"dvcC"
