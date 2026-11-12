# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""The chroma of the Dolby Vision transport, measured against the full 4:4:4 picture.

The bridge composes a 3840x2160 picture of PQ-coded BT.2020 R G B (the texture that its packer reads,
pack_gles300.frag) and sends it as 12-bit YCbCr 4:2:2: the luma of every pixel and, for each pair of
pixels, the chroma of its even pixel (Cb on the even column, Cr on the odd one, ETSI GS CCM 001 6.4.1;
4:2:2 over HDMI is co-sited). A display rebuilds the chroma of the odd pixels by interpolation.

Measured here, all in float64 and written apart from the bridge's code:
  ways of making the 4:2:2 chroma from the 4:4:4 picture:
    even      the chroma of the even pixel (what the bridge sends)
    annexc    ETSI GS CCM 001 annex C (informative, a guideline for clause 5.5): (1, 6, 1) / 8 centred on the
              even pixel
    mean      the mean of the two pixels of the pair (what the bridge sent before 2026-10-15; centred half a
              pixel to the right of where a display puts it)
  ways in which a display may rebuild the odd pixels:
    hold      the even pixel's chroma repeated
    linear    the mean of the two even neighbours
    cubic     4 taps (-1, 9, 9, -1) / 16: Catmull-Rom at the half pixel, and the up-sampling of annex C
against the 4:4:4 picture with each component at 12 bits (the error of the 4:2:2 chroma alone) and against
the picture as composed (floats; with the 12-bit quantization of the transport as well), in delta E ITP
(ITU-R BT.2124: 720 * sqrt(dI^2 + (dCt/2)^2 + dCp^2)); 1 is about the smallest step that can be seen.

The bridge's default transport (the source transport: As composed, videoscreen.hadesdvtransport 2) makes no
4:4:4 picture: it takes the composer's 4:2:0 chroma to 4:2:2 directly, so in the row the chroma samples of the
stream go out as they are. real2 measures that transport, as it goes out, against libplacebo's own 4:4:4
conversion of the same picture.

Usage:
  chroma_bench.py real3 <picture.f32> <composed.rgba> <ipt.rgba> <ccm_y.u16> <ccm_cb.u16> <ccm_cr.u16> [l,r,t,b]
                                                         the reference (FB_SOURCE=0 FB_FLOATS=1), the transport as
                                                         composed (FB_SOURCE=1) with the composer's planes, and the
                                                         IPT-PQ-c2 transport (FB_SOURCE=1 FB_IPT=1): first the
                                                         bytes of both against the model, then the two transports
  chroma_bench.py real2 <picture.f32> <composed.rgba>   the reference of fidbench FB_SOURCE=0 FB_FLOATS=1 and the
                                                         transport of fidbench FB_SOURCE=1, of the same picture
  chroma_bench.py real <picture.f32> [<composed.rgba>]   the composed way (FB_SOURCE=0: the transport packed from
                                                         the picture, as when menus are drawn over it); with its
                                                         transport, how far the packer is from the model above
  chroma_bench.py synth                                    the synthetic pictures
  chroma_bench.py selftest                                 the measure in its two directions
Prints one JSON line a picture and way.
"""
import json
import sys

import numpy as np

W, H = 3840, 2160
KR, KG, KB = 0.2627, 0.6780, 0.0593
M1, M2 = 2610 / 16384, 2523 / 4096 * 128
C1, C2, C3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32


def pq_eotf(e):
    p = np.clip(e, 0.0, 1.0) ** (1 / M2)
    return 10000.0 * (np.maximum(p - C1, 0.0) / (C2 - C3 * p)) ** (1 / M1)


def pq_oetf(nits):
    y = np.clip(nits / 10000.0, 0.0, 1.0) ** M1
    return ((C1 + C2 * y) / (1 + C3 * y)) ** M2


def to_ycc(rgb):
    y = rgb @ np.array([KR, KG, KB])
    return y, (rgb[..., 2] - y) / 1.8814, (rgb[..., 0] - y) / 1.4746


def to_rgb(y, cb, cr):
    r = y + 1.4746 * cr
    b = y + 1.8814 * cb
    g = (y - KR * r - KB * b) / KG
    return np.clip(np.stack([r, g, b], -1), 0.0, 1.0)


def q_y(y):
    return np.floor(256 + 3504 * y + 0.5)


def q_c(c):
    return np.floor(2048 + 3584 * c + 0.5)


def d_y(code):
    return (code - 256) / 3504


def d_c(code):
    return (code - 2048) / 3584


LMS = np.array([[1688, 2146, 262], [683, 2951, 462], [99, 309, 3688]]) / 4096


def itp(rgb_pq):
    lms = pq_oetf(pq_eotf(rgb_pq) @ LMS.T)
    i = 0.5 * lms[..., 0] + 0.5 * lms[..., 1]
    ct = (6610 * lms[..., 0] - 13613 * lms[..., 1] + 7003 * lms[..., 2]) / 4096
    cp = (17933 * lms[..., 0] - 17390 * lms[..., 1] - 543 * lms[..., 2]) / 4096
    return np.stack([i, 0.5 * ct, cp], -1)


def delta_e(a, b):
    return 720.0 * np.sqrt(((itp(a) - itp(b)) ** 2).sum(-1))


def shift(a, k):
    """a[:, x + k] with the edge pixel repeated (annex C: pixel replication)."""
    n = a.shape[1]
    idx = np.clip(np.arange(n) + k, 0, n - 1)
    return a[:, idx]


def down(c, way):
    """The chroma of each pair, at the even pixel (4:4:4 -> 4:2:2), as floats."""
    if way == "even":
        return c[:, 0::2]
    if way == "annexc":
        return ((shift(c, -1) + 6 * c + shift(c, 1)) / 8)[:, 0::2]
    if way == "mean":
        return (c[:, 0::2] + c[:, 1::2]) / 2
    raise ValueError(way)


def up(s, way):
    """4:2:2 chroma (at the even pixels) -> 4:4:4, as a display rebuilds the odd pixels."""
    out = np.empty((s.shape[0], s.shape[1] * 2))
    out[:, 0::2] = s
    if way == "hold":
        out[:, 1::2] = s
    elif way == "linear":
        out[:, 1::2] = (s + shift(s, 1)) / 2
    elif way == "cubic":
        out[:, 1::2] = (-shift(s, -1) + 9 * s + 9 * shift(s, 1) - shift(s, 2)) / 16
    else:
        raise ValueError(way)
    return out


# The transports as a display reads them, by the matrices of the DM header that goes with each (ETSI GS CCM 001 6.2):
# as composed, the YCCtoRGB and offsets of the RPU of a profile 7 stream (BT.2020, the codes over 4096); in IPT-PQ-c2,
# those of Kodi's tools/dvbridge/dvbridge_ipt.h (IPT -> L'M'S', the crosstalk as RGBtoLMS), the LMS being those
# of the RPU's RGBtoLMS of BT.2020 (DVBRIDGE_LMS_2020). Written apart from the bridge, from the same constants.
YCC_RPU = np.array([[9574, 0, 13802], [9574, -1540, -5348], [9574, 17610, 0]]) / 8192
OFF_RPU = np.array([16777216, 134217728, 134217728]) / 2 ** 28
IPT_YCC = np.array([[8192, 799, 1681], [8192, -933, 1091], [8192, 267, -5545]]) / 8192
IPT_OFF = np.array([0, 134217728, 134217728]) / 2 ** 28
IPT_XTALK = np.array([[17081, -349, -349], [-349, 17081, -349], [-349, -349, 17081]]) / 16384
LMS_2020 = np.array([[7222, 8771, 390], [2654, 12430, 1300], [0, 422, 15962]]) / 16384
TO_IPT = np.linalg.inv(IPT_YCC)  # L'M'S' -> IPT (dvbridge_source_ipt_setup(): ycc_inverse)
TO_LMS = np.linalg.inv(IPT_XTALK) @ LMS_2020  # linear RGB -> LMS without the crosstalk (lms_inverse)
FROM_LMS = np.linalg.inv(LMS_2020) @ IPT_XTALK  # its inverse (lms_forward)


def ipt_of(rgb_pq):
    """IPT-PQ-c2 of PQ BT.2020 RGB, as floats (I in [0, 1], P and T about 0), as pass 9 of the packer computes it."""
    v = np.maximum((pq_eotf(rgb_pq) / 10000.0) @ TO_LMS.T, 0.0)
    return (pq_oetf(v * 10000.0)) @ TO_IPT.T


def ipt_codes_i(i):
    return np.clip(np.floor(i * 4096 + 0.5), 0, 4095)


def ipt_codes_pt(p):
    return np.clip(np.floor(p * 4096 + 0.5) + 2048, 0, 4095)


def from_ipt(i_code, p_code, t_code):
    """PQ BT.2020 RGB of IPT-PQ-c2 codes, as a display decodes them by the DM header of the transport."""
    ipt = np.stack([i_code, p_code, t_code], -1) / 4096.0 - IPT_OFF
    lmsp = np.clip(ipt @ IPT_YCC.T, 0.0, 1.0)
    rgb = np.maximum((pq_eotf(lmsp) / 10000.0) @ FROM_LMS.T, 0.0)
    return pq_oetf(rgb * 10000.0)


def from_rpu_ycc(y_code, cb_code, cr_code):
    """PQ BT.2020 RGB of codes of the composer's YCbCr, by the YCCtoRGB of the RPU (the transport as composed)."""
    c = np.stack([y_code, cb_code, cr_code], -1) / 4096.0 - OFF_RPU
    return np.clip(c @ YCC_RPU.T, 0.0, 1.0)


def stats(de):
    de = de.ravel()
    return {"mean": round(float(de.mean()), 4), "p99": round(float(np.percentile(de, 99)), 3),
            "p999": round(float(np.percentile(de, 99.9)), 3), "max": round(float(de.max()), 3),
            "over1": round(float((de > 1).mean() * 100), 4)}


def measure(rgb, name, ways=("even", "annexc", "mean"), displays=("hold", "linear", "cubic"),
            act=(slice(None), slice(None))):
    """rgb: H x W x 3 PQ-coded BT.2020, floats in [0, 1]; act: the rows and columns the statistics go over."""
    rgb = np.clip(rgb.astype(np.float64), 0.0, 1.0)
    y, cb, cr = to_ycc(rgb)
    yq = d_y(q_y(y))
    ref12 = to_rgb(yq, d_c(q_c(cb)), d_c(q_c(cr)))  # 4:4:4, each component at 12 bits
    itp12, itpc = itp(ref12), itp(rgb)
    out = []
    for way in ways:
        # the 12-bit chroma code of each pair, as the transport carries it
        sb, sr = d_c(q_c(down(cb, way))), d_c(q_c(down(cr, way)))
        for disp in displays:
            got = itp(to_rgb(yq, up(sb, disp), up(sr, disp)))
            out.append({"picture": name, "chroma": way, "display": disp,
                        "vs_444_12bit": stats(720.0 * np.sqrt(((got - itp12)[act] ** 2).sum(-1))),
                        "vs_composed": stats(720.0 * np.sqrt(((got - itpc)[act] ** 2).sum(-1)))})
    out.append({"picture": name, "chroma": "444", "display": "-", "vs_444_12bit": stats(np.zeros(1)),
                "vs_composed": stats(720.0 * np.sqrt(((itp12 - itpc)[act] ** 2).sum(-1)))})
    return out


def transport_planes(rgba, ref_luma):
    """The 12-bit luma of each pixel and the chroma of each pair of a transport (fidbench composed.rgba: RGBA8 3840x2160,
    red = chroma bits 11..4, green = luma bits 11..4, blue = chroma bits 3..0 above luma bits 3..0), with its rows in
    the order of the reference (the read back may be bottom-up: the order whose luma is nearer the reference's)."""
    t = np.frombuffer(rgba, np.uint8).reshape(H, W, 4).astype(np.int64)
    luma = (t[..., 1] << 4) | (t[..., 2] & 0xF)
    chroma = (t[..., 0] << 4) | (t[..., 2] >> 4)
    if np.abs(luma[::-1] - ref_luma).mean() < np.abs(luma - ref_luma).mean():
        luma, chroma = luma[::-1], chroma[::-1]
    return luma, chroma[:, 0::2], chroma[:, 1::2]  # Cb on the even column, Cr on the odd one (CCM 001 6.4.1)


def measure_transport(ref, transport, name, displays=("hold", "linear", "cubic")):
    """ref: the 4:4:4 picture of libplacebo's own conversion (fidbench FB_SOURCE=0, picture.f32), PQ BT.2020 RGB;
    transport: the bridge's default transport of the same picture (fidbench FB_SOURCE=1, composed.rgba): the
    composer's 4:2:0 chroma taken to 4:2:2 directly, with no 4:4:4 picture between. The ways of measure() are made
    from ref; this one is the transport as it goes out, its own luma included."""
    ref = np.clip(ref.astype(np.float64), 0.0, 1.0)
    y, cb, cr = to_ycc(ref)
    ref12 = to_rgb(d_y(q_y(y)), d_c(q_c(cb)), d_c(q_c(cr)))
    itp12, itpc = itp(ref12), itp(ref)
    luma, tcb, tcr = transport_planes(transport, q_y(y))
    rows = slice(8, H)  # the rows of the metadata packets left out (their chroma carry the bits in their LSB)
    out = [{"picture": name, "chroma": "transport", "display": "luma",
            "luma_codes_off": round(float((np.abs(luma[rows] - q_y(y)[rows]) > 1).mean() * 100), 4)}]
    for disp in displays:
        got = itp(to_rgb(d_y(luma.astype(np.float64)), up(d_c(tcb.astype(np.float64)), disp),
                         up(d_c(tcr.astype(np.float64)), disp)))
        out.append({"picture": name, "chroma": "transport", "display": disp,
                    "vs_444_12bit": stats(720.0 * np.sqrt(((got - itp12)[rows] ** 2).sum(-1))),
                    "vs_composed": stats(720.0 * np.sqrt(((got - itpc)[rows] ** 2).sum(-1)))})
    return out + measure(ref, name)


def measure_ipt(rgb, name, ways=("even", "annexc", "mean"), displays=("hold", "linear", "cubic"), act=(slice(None), slice(None))):
    """As measure(), with the 4:2:2 signal in IPT-PQ-c2: I of every pixel, P and T of each pair made each way, all at
    12 bits as the packer writes them; the display rebuilds P and T of the odd pixel and decodes by the DM header."""
    rgb = np.clip(rgb.astype(np.float64), 0.0, 1.0)
    ipt = ipt_of(rgb)
    ic = ipt_codes_i(ipt[..., 0])
    itpc = itp(rgb)
    floor = itp(from_ipt(ic, ipt_codes_pt(ipt[..., 1]), ipt_codes_pt(ipt[..., 2])))
    out = [{"picture": name, "signal": "ipt", "chroma": "444", "display": "-",
            "vs_composed": stats(720.0 * np.sqrt(((floor - itpc)[act] ** 2).sum(-1)))}]
    for way in ways:
        p, t = ipt_codes_pt(down(ipt[..., 1], way)), ipt_codes_pt(down(ipt[..., 2], way))
        for disp in displays:
            got = itp(from_ipt(ic, up(p, disp), up(t, disp)))
            out.append({"picture": name, "signal": "ipt", "chroma": way, "display": disp,
                        "vs_composed": stats(720.0 * np.sqrt(((got - itpc)[act] ** 2).sum(-1)))})
    return out


def oriented(planes, want):
    """The planes of a transport read back with their rows in the order of want (the composer's luma plane): the
    order in which more luma codes are those of the plane."""
    luma = planes[0]
    if (luma[::-1] == want).mean() > (luma == want).mean():
        return tuple(p[::-1] for p in planes)
    return planes


def split(rgba):
    """luma of each pixel and chroma of each pair (even column: Cb or P; odd column: Cr or T) of a transport."""
    t = np.frombuffer(rgba, np.uint8).reshape(H, W, 4).astype(np.int64)
    luma = (t[..., 1] << 4) | (t[..., 2] & 0xF)
    chroma = (t[..., 0] << 4) | (t[..., 2] >> 4)
    return luma, chroma[:, 0::2], chroma[:, 1::2]


def measure3(ref, composed, ipt_transport, ccm, name, margins=(0, 0, 0, 0), displays=("hold", "linear", "cubic")):
    """ref: libplacebo's own 4:4:4 conversion (fidbench FB_SOURCE=0, picture.f32); composed and ipt_transport: the
    bridge's two transports of the same picture (FB_SOURCE=1, composed.rgba, without and with FB_IPT=1); ccm: the
    composer's 4:2:0 planes of FB_SOURCE=1 (ccm_y, ccm_cb, ccm_cr). First the bytes: the transport as composed against
    the composer's planes, and the IPT one against IPT-PQ-c2 computed here from the codes of the one as composed (the
    rows that carry the metadata packets left out: their chroma carry its bits). Then each transport as it goes out,
    against ref, with each way in which a display rebuilds the odd pixel. Everything over the active picture of the
    RPU's level 5 (margins: left, right, top, bottom): the transport is black outside it."""
    ref = np.clip(ref.astype(np.float64), 0.0, 1.0)
    ccm_y, ccm_cb, ccm_cr = ccm
    yl, ycb, ycr = oriented(split(composed), ccm_y)
    il, ip, it = split(ipt_transport)
    left, right, top, bottom = margins
    top = max(top, 8)
    top += top & 1
    rows = slice(top, H - bottom)
    act = (rows, slice(left, W - right))
    pairs = (rows, slice(left // 2, (W - right) // 2))
    out = []
    # the transport as composed: luma = the composer's; chroma of the even rows = the composer's 4:2:0 samples
    luma_eq = float((yl[act] == ccm_y[act]).mean() * 100)
    even = (slice(top, H - bottom, 2), pairs[1])
    half = (slice(top // 2, (H - bottom + 1) // 2), pairs[1])
    cb_eq = float((ycb[even] == ccm_cb[half]).mean() * 100)
    cr_eq = float((ycr[even] == ccm_cr[half]).mean() * 100)
    # IPT-PQ-c2 of the even pixels, from the codes of the transport as composed (pass 9: luma of the even pixel and the
    # chroma of its pair; the I of an odd pixel takes a chroma that pass 9a interpolates, not modelled here)
    model = ipt_of(from_rpu_ycc(yl[:, 0::2].astype(np.float64), ycb.astype(np.float64), ycr.astype(np.float64)))
    mi, mp, mt = ipt_codes_i(model[..., 0]), ipt_codes_pt(model[..., 1]), ipt_codes_pt(model[..., 2])
    if (np.abs(il[::-1][:, 0::2][pairs] - mi[pairs]).mean() < np.abs(il[:, 0::2][pairs] - mi[pairs]).mean()):
        il, ip, it = il[::-1], ip[::-1], it[::-1]
    def agree(got, want):
        d = np.abs(got[pairs] - want[pairs])
        return {"same": round(float((d == 0).mean() * 100), 4), "one": round(float((d == 1).mean() * 100), 4),
                "max": int(d.max())}
    out.append({"picture": name, "bytes": {
        "composed_luma_vs_composer_%": round(luma_eq, 4), "composed_cb_even_rows_vs_composer_%": round(cb_eq, 4),
        "composed_cr_even_rows_vs_composer_%": round(cr_eq, 4),
        "ipt_I_even_vs_model": agree(il[:, 0::2], mi), "ipt_P_vs_model": agree(ip, mp), "ipt_T_vs_model": agree(it, mt)}})
    itpc = itp(ref)
    for disp in displays:
        got = itp(from_rpu_ycc(yl.astype(np.float64), up(ycb.astype(np.float64), disp), up(ycr.astype(np.float64), disp)))
        out.append({"picture": name, "signal": "ycc", "chroma": "transport", "display": disp,
                    "vs_composed": stats(720.0 * np.sqrt(((got - itpc)[act] ** 2).sum(-1)))})
        got = itp(from_ipt(il.astype(np.float64), up(ip.astype(np.float64), disp), up(it.astype(np.float64), disp)))
        out.append({"picture": name, "signal": "ipt", "chroma": "transport", "display": disp,
                    "vs_composed": stats(720.0 * np.sqrt(((got - itpc)[act] ** 2).sum(-1)))})
    ycc = measure(ref, name, act=act)
    for r in ycc:
        r["signal"] = "ycc"
    out[0]["bytes"]["margins"] = [left, right, top, bottom]
    return out + ycc + measure_ipt(ref, name, act=act)


def transport_check(rgb, rgba):
    """The bridge's own transport (fidbench composed.rgba) against the even-pixel model above: luma and
    chroma codes, outside the rows that carry the metadata packets."""
    t = np.frombuffer(rgba, np.uint8).reshape(H, W, 4).astype(np.int64)
    luma = (t[..., 1] << 4) | (t[..., 2] & 0xF)
    chroma = (t[..., 0] << 4) | (t[..., 2] >> 4)
    y, cb, cr = to_ycc(np.clip(rgb.astype(np.float64), 0, 1))
    want_l = q_y(y)
    want_c = np.empty_like(want_l)
    want_c[:, 0::2] = q_c(cb[:, 0::2])
    want_c[:, 1::2] = q_c(cr[:, 0::2])
    best = None
    for flip in (False, True):
        ll, cc = (luma[::-1], chroma[::-1]) if flip else (luma, chroma)
        rows = slice(8, H)  # the metadata packets are in the first rows (16 packets of 1024 bits at most)
        dl = np.abs(ll[rows] - want_l[rows])
        dc = np.abs((cc[rows] >> 1) - (want_c[rows].astype(np.int64) >> 1))  # bit 0 may carry metadata
        r = {"flip": flip, "luma_max": int(dl.max()), "luma_off": float((dl > 0).mean()),
             "chroma_max": int(dc.max()), "chroma_off": float((dc > 0).mean())}
        if best is None or r["luma_off"] < best["luma_off"]:
            best = r
    return best


def synth():
    pics = {}
    x = np.arange(W)
    yy = np.arange(H)[:, None]
    # 1. the worst a stream can carry: 4:2:0 chroma at its own Nyquist frequency (every chroma sample of the
    # stream the opposite of its neighbour), saturated, on a mid grey; up-sampled to 4:4:4 at the left siting of
    # HEVC with Catmull-Rom (interpolating: the even pixels keep the stream's samples)
    cw, chh = W // 2, H // 2
    s = np.where((np.arange(cw)[None, :] + np.arange(chh)[:, None]) % 2 == 0, 1.0, -1.0)
    cb4 = 0.08 * s
    cr4 = -0.06 * s
    def up420(c):
        c = np.repeat(c, 2, axis=0)  # rows: the chroma rows twice (the vertical step does not matter here)
        return up(c, "cubic")
    y = np.full((H, W), 0.5)
    pics["synth-420-nyquist"] = to_rgb(y, up420(cb4), up420(cr4))
    # 2. chroma of full resolution that no stream has but the menus and subtitles drawn over the picture have:
    # one-pixel columns of saturated red and blue, and white text-like strokes edged in colour
    rb = np.where(x % 2 == 0, 1, 0)[None, :].repeat(H, 0)
    red = np.stack([np.full((H, W), 0.58), np.full((H, W), 0.0), np.full((H, W), 0.0)], -1)
    blue = np.stack([np.full((H, W), 0.0), np.full((H, W), 0.0), np.full((H, W), 0.58)], -1)
    pics["synth-444-columns"] = np.where(rb[..., None] == 1, red, blue)
    strokes = np.zeros((H, W, 3))
    band = (x % 24 < 3)[None, :] & (yy % 48 < 40)
    edge = ((x % 24 == 3) | (x % 24 == 23))[None, :] & (yy % 48 < 40)
    strokes[band] = 0.58
    strokes[edge] = [0.0, 0.0, 0.5]
    pics["synth-444-text"] = strokes
    # 3. what most pictures are: smooth gradients of saturated colour
    g = (x / (W - 1))[None, :].repeat(H, 0)
    v = (np.arange(H) / (H - 1))[:, None].repeat(W, 1)
    pics["synth-gradients"] = np.stack([0.2 + 0.5 * g, 0.2 + 0.4 * v, 0.6 - 0.4 * g], -1)
    return pics


def selftest():
    """The measure in its two directions: a flat picture gives 0 for every way; a chroma shifted by one pixel,
    which a display would show as colour fringes, gives clearly more than any way above."""
    flat = np.full((64, 128, 3), 0.4)
    ok = True
    for r in measure(flat, "flat"):
        if r["vs_444_12bit"]["max"] != 0.0:
            print("FAIL flat", r)
            ok = False
    pics = synth()
    pic = pics["synth-420-nyquist"][:64, :256]
    # the transport with a display that rebuilds the odd pixels as the picture was up-sampled (4 taps): the
    # chroma of the stream comes back as it was, so the error is the 12-bit rounding alone
    base = [r["vs_444_12bit"]["mean"] for r in measure(pic, "x") if r["chroma"] == "even" and r["display"] == "cubic"][0]
    y, cb, cr = to_ycc(pic)
    shifted = to_rgb(d_y(q_y(y)), d_c(q_c(shift(cb, 1))), d_c(q_c(shift(cr, 1))))
    y2, cb2, cr2 = to_ycc(pic)
    ref12 = to_rgb(d_y(q_y(y2)), d_c(q_c(cb2)), d_c(q_c(cr2)))
    mutant = float(delta_e(shifted, ref12).mean())
    print(json.dumps({"selftest": "shift", "even_cubic_mean": base, "shifted_mean": round(mutant, 4)}))
    if not (base < 1.0 and mutant > 1.0 and mutant > 10 * base):
        print("FAIL: a one-pixel chroma shift is not told apart")
        ok = False
    # IPT-PQ-c2: codes of 12 bits decoded as a display decodes them come back within the rounding; a decoder that
    # leaves the crosstalk out (RGBtoLMS of the DM header not applied) does not
    rng = np.random.default_rng(7)
    rgb = rng.uniform(0.05, 0.75, (64, 64, 3))
    ipt = ipt_of(rgb)
    i, pp, tt = ipt_codes_i(ipt[..., 0]), ipt_codes_pt(ipt[..., 1]), ipt_codes_pt(ipt[..., 2])
    back = float(delta_e(from_ipt(i, pp, tt), rgb).mean())
    lmsp = np.clip((np.stack([i, pp, tt], -1) / 4096.0 - IPT_OFF) @ IPT_YCC.T, 0.0, 1.0)
    wrong = pq_oetf(np.maximum((pq_eotf(lmsp) / 10000.0) @ np.linalg.inv(LMS_2020).T, 0.0) * 10000.0)
    no_xtalk = float(delta_e(wrong, rgb).mean())
    print(json.dumps({"selftest": "ipt", "round_trip_mean": round(back, 4), "without_crosstalk_mean": round(no_xtalk, 4)}))
    if not (back < 0.5 and no_xtalk > 1.0):
        print("FAIL: the IPT-PQ-c2 chain")
        ok = False
    print("SELFTEST", "PASS" if ok else "FAIL")
    return ok


def main():
    mode = sys.argv[1]
    if mode == "selftest":
        sys.exit(0 if selftest() else 1)
    if mode == "synth":
        for name, rgb in synth().items():
            for r in measure(rgb, name):
                r["signal"] = "ycc"
                print(json.dumps(r), flush=True)
            for r in measure_ipt(rgb, name):
                print(json.dumps(r), flush=True)
        return
    if mode == "real3":
        raw = np.fromfile(sys.argv[2], np.float32)
        ref = raw.reshape(H, W, raw.size // (W * H))[..., :3]
        ccm = (np.fromfile(sys.argv[5], np.uint16).astype(np.int64).reshape(H, W),
               np.fromfile(sys.argv[6], np.uint16).astype(np.int64).reshape(H // 2, W // 2),
               np.fromfile(sys.argv[7], np.uint16).astype(np.int64).reshape(H // 2, W // 2))
        name = sys.argv[2].rsplit("/", 1)[-1].replace("-picture.f32", "")
        margins = tuple(int(v) for v in sys.argv[8].split(",")) if len(sys.argv) > 8 and sys.argv[8] else (0, 0, 0, 0)
        for r in measure3(ref, open(sys.argv[3], "rb").read(), open(sys.argv[4], "rb").read(), ccm, name, margins):
            print(json.dumps(r), flush=True)
        return
    if mode == "real2":
        raw = np.fromfile(sys.argv[2], np.float32)
        ref = raw.reshape(H, W, raw.size // (W * H))[..., :3]
        name = sys.argv[2].rsplit("/", 1)[-1].replace("-picture.f32", "")
        for r in measure_transport(ref, open(sys.argv[3], "rb").read(), name):
            print(json.dumps(r), flush=True)
        return
    if mode == "real":
        raw = np.fromfile(sys.argv[2], np.float32)
        ch = raw.size // (W * H)
        rgb = raw.reshape(H, W, ch)[..., :3]
        name = sys.argv[2].rsplit("/", 1)[-1].replace("-picture.f32", "")
        if len(sys.argv) > 3:
            print(json.dumps({"picture": name, "transport_check": transport_check(rgb, open(sys.argv[3], "rb").read())}))
        for r in measure(rgb, name):
            print(json.dumps(r), flush=True)
        return
    sys.exit("usage: chroma_bench.py real|real2|real3|synth|selftest")


main()
