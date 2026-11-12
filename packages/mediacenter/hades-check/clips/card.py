# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""The Hades check card: its layout, the signal of each region in each format, and its planes.

One composition in five formats (SDR, HDR10, HLG, Dolby Vision 8.1, Dolby Vision 5), so that a viewer
learns it once. Everything here is plain arithmetic of the Python standard library: the values are
defined in light on the display and coded in the signal of each format, and every code that lands too
close to a rounding tie for the result to be sure on every machine stops the build (rnd()).

Layout (a grid of 1920x1080; at 3840x2160 every coordinate is doubled). Region edges are multiples of
8 pixels of the grid, so every region covers whole 2x2 chroma samples of 4:2:0 and has a flat chroma:
  1. the label, top left (x 96, y 64, 56 high): a 5x7 bitmap font of this file, every dot 8x8 pixels,
     grey at half the reference white in the signal;
  2. eight grey bars (y 176-496, x 96-1824, 216 wide): bar k is black + k/8 (reference white - black)
     in the signal (equal steps in the perceptual scale of each format);
  3. six colour squares (y 560-800, 240 wide, 48 apart, from x 120): red, green, blue, cyan, magenta,
     yellow at 75 % as the EBU bars (R'G'B' 0 or 0.75, BT.709); in HDR the same light on the display as
     those colours give in SDR with the reference white at 203 cd/m2 (ITU-R BT.2408): linear BT.709
     0.75^2.4 x 203 cd/m2 per lit component, to BT.2020, then the format's transfer;
  4. a dark strip (y 864-1008, x 120-936) of black, with four squares of 96 (y 888-984; x 168, 384,
     600, 816) at 2, 4, 6 and 8 % of the code range above black;
  5. a light strip (y 864-1008, x 984-1800): in SDR white with four squares at 2, 4, 6 and 8 % of the
     range below it; in HDR the reference white (203 cd/m2) with four brighter squares, 400, 600, 800
     and 1000 cd/m2 (x 1032, 1248, 1464, 1680).
The background is the black of the signal.
"""
import math
import sys
from array import array

GRID_W, GRID_H = 1920, 1080

# ---------------------------------------------------------------------------------------------------
# rounding: half up, and never near a tie (a code that one ulp could move is not deterministic)


class TieError(ValueError):
    pass


def rnd(x, what=""):
    f = math.floor(x)
    frac = x - f
    if frac != 0.5 and abs(frac - 0.5) < 1e-7:
        raise TieError("%r is too close to a rounding tie (%s)" % (x, what))
    return int(f + 1) if frac >= 0.5 else int(f)


# ---------------------------------------------------------------------------------------------------
# transfer functions

PQ_M1 = 2610.0 / 16384.0
PQ_M2 = 2523.0 / 4096.0 * 128.0
PQ_C1 = 3424.0 / 4096.0
PQ_C2 = 2413.0 / 4096.0 * 32.0
PQ_C3 = 2392.0 / 4096.0 * 32.0


def pq_oetf(nits):
    """SMPTE ST 2084 inverse EOTF: light on the display in cd/m2 -> E' in [0, 1]"""
    y = min(max(nits / 10000.0, 0.0), 1.0)
    p = y ** PQ_M1
    return ((PQ_C1 + PQ_C2 * p) / (1.0 + PQ_C3 * p)) ** PQ_M2


def pq_eotf(e):
    """SMPTE ST 2084 EOTF: E' -> cd/m2"""
    e = min(max(e, 0.0), 1.0)
    p = e ** (1.0 / PQ_M2)
    n = max(p - PQ_C1, 0.0)
    return 10000.0 * (n / (PQ_C2 - PQ_C3 * p)) ** (1.0 / PQ_M1)


HLG_A = 0.17883277
HLG_B = 1.0 - 4.0 * HLG_A
HLG_C = 0.5 - HLG_A * math.log(4.0 * HLG_A)
HLG_LW = 1000.0  # nominal peak of the display (ITU-R BT.2100, Lb = 0)
HLG_GAMMA = 1.2  # the system gamma at 1000 cd/m2


def hlg_oetf(e):
    """ITU-R BT.2100 HLG OETF: scene light in [0, 1] -> E'"""
    e = max(e, 0.0)
    return math.sqrt(3.0 * e) if e <= 1.0 / 12.0 else HLG_A * math.log(12.0 * e - HLG_B) + HLG_C


def hlg_inverse_oetf(v):
    return v * v / 3.0 if v <= 0.5 else (math.exp((v - HLG_C) / HLG_A) + HLG_B) / 12.0


def hlg_from_display(rgb):
    """The inverse of the HLG EOTF (BT.2100, Lw 1000, Lb 0, gamma 1.2): display light of BT.2020 RGB in
    cd/m2 -> R'G'B'. F_D = Lw Y_S^(gamma - 1) E_S, Y_D = Lw Y_S^gamma."""
    yd = 0.2627 * rgb[0] + 0.6780 * rgb[1] + 0.0593 * rgb[2]
    if yd <= 0.0:
        return (0.0, 0.0, 0.0)
    ys = (yd / HLG_LW) ** (1.0 / HLG_GAMMA)
    scale = 1.0 / (HLG_LW * ys ** (HLG_GAMMA - 1.0))
    return tuple(hlg_oetf(c * scale) for c in rgb)


def hlg_to_display(rgbp):
    """the HLG EOTF (BT.2100, as above): R'G'B' -> display light of BT.2020 RGB in cd/m2"""
    es = [hlg_inverse_oetf(v) for v in rgbp]
    ys = 0.2627 * es[0] + 0.6780 * es[1] + 0.0593 * es[2]
    if ys <= 0.0:
        return (0.0, 0.0, 0.0)
    return tuple(HLG_LW * ys ** (HLG_GAMMA - 1.0) * e for e in es)


# ---------------------------------------------------------------------------------------------------
# colour: BT.709 -> BT.2020 from the primaries (ITU-R BT.2087), and Y'CbCr


def _solve3(m, v):
    a = [row[:] + [v[i]] for i, row in enumerate(m)]
    for c in range(3):
        p = max(range(c, 3), key=lambda r: abs(a[r][c]))
        a[c], a[p] = a[p], a[c]
        for r in range(3):
            if r != c:
                f = a[r][c] / a[c][c]
                for k in range(c, 4):
                    a[r][k] -= f * a[c][k]
    return [a[i][3] / a[i][i] for i in range(3)]


def inverse3(m):
    cols = [_solve3(m, [1.0 if i == j else 0.0 for i in range(3)]) for j in range(3)]
    return [[cols[j][i] for j in range(3)] for i in range(3)]


def mul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def apply3(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


def npm(primaries, white):
    """the normalised primary matrix, RGB -> XYZ (SMPTE RP 177)"""
    cols = [[x / y, 1.0, (1.0 - x - y) / y] for x, y in primaries]
    m = [[cols[j][i] for j in range(3)] for i in range(3)]
    xw, yw = white
    s = _solve3(m, [xw / yw, 1.0, (1.0 - xw - yw) / yw])
    return [[m[i][j] * s[j] for j in range(3)] for i in range(3)]


D65 = (0.3127, 0.3290)
BT709 = ((0.640, 0.330), (0.300, 0.600), (0.150, 0.060))
BT2020 = ((0.708, 0.292), (0.170, 0.797), (0.131, 0.046))
BT709_TO_BT2020 = mul3(inverse3(npm(BT2020, D65)), npm(BT709, D65))

KR_KB = {"bt709": (0.2126, 0.0722), "bt2020": (0.2627, 0.0593)}


def ycbcr(rgbp, matrix):
    kr, kb = KR_KB[matrix]
    r, g, b = rgbp
    y = kr * r + (1.0 - kr - kb) * g + kb * b
    return y, (b - y) / (2.0 * (1.0 - kb)), (r - y) / (2.0 * (1.0 - kr))


def rgb_from_ycbcr(ycc, matrix):
    kr, kb = KR_KB[matrix]
    y, cb, cr = ycc
    r = y + 2.0 * (1.0 - kr) * cr
    b = y + 2.0 * (1.0 - kb) * cb
    g = (y - kr * r - kb * b) / (1.0 - kr - kb)
    return r, g, b


def limited_codes(ycc, bits, what):
    """Y'CbCr -> codes of the limited range (ITU-R BT.709 / BT.2020): (219 Y' + 16) 2^(n-8), ..."""
    s = 1 << (bits - 8)
    y, cb, cr = ycc
    return (rnd((219.0 * y + 16.0) * s, what + " Y"), rnd((224.0 * cb + 128.0) * s, what + " Cb"),
            rnd((224.0 * cr + 128.0) * s, what + " Cr"))


def limited_values(codes, bits):
    s = float(1 << (bits - 8))
    return ((codes[0] / s - 16.0) / 219.0, (codes[1] / s - 128.0) / 224.0, (codes[2] / s - 128.0) / 224.0)


# ---------------------------------------------------------------------------------------------------
# IPT-PQ-c2 (Dolby Vision profile 5): the matrices that the RPUs of profile 5 carry (as dovi_tool writes
# them; Kodi of Hades, tools/dvbridge/dvbridge_ipt.h): YCCtoRGB (IPT -> L'M'S', 13 fractional bits), its
# offsets (28) and RGBtoLMS (the crosstalk undone, 14). The linear BT.2020 RGB of an LMS is
# RGBtoLMS_2020^-1 LMS, RGBtoLMS_2020 being that of the RPUs of profiles 7 and 8.

IPT_YCC = (8192, 799, 1681, 8192, -933, 1091, 8192, 267, -5545)
IPT_OFFSETS = (0, 134217728, 134217728)
IPT_LMS = (17081, -349, -349, -349, 17081, -349, -349, -349, 17081)
LMS_2020 = (7222, 8771, 390, 2654, 12430, 1300, 0, 422, 15962)


def _m(coefs, denom):
    return [[coefs[3 * i + j] / denom for j in range(3)] for i in range(3)]


IPT_YCC_M = _m(IPT_YCC, 8192.0)
IPT_LMS_M = _m(IPT_LMS, 16384.0)
LMS_2020_M = _m(LMS_2020, 16384.0)
IPT_OFF = tuple(o / float(1 << 28) for o in IPT_OFFSETS)
# encode: RGB -> LMS of BT.2020 -> the crosstalk of profile 5 (the inverse of RGBtoLMS) -> PQ -> IPT
_TO_LMS_C = mul3(inverse3(IPT_LMS_M), LMS_2020_M)
_TO_IPT = inverse3(IPT_YCC_M)
# decode: IPT -> L'M'S' -> PQ EOTF -> RGBtoLMS -> RGBtoLMS_2020^-1
_FROM_LMS_C = mul3(inverse3(LMS_2020_M), IPT_LMS_M)

IPT_SCALE = 1023.0  # the codes of the base layer read as code / (2^10 - 1), as libplacebo maps them


def ipt_codes(rgb_nits, neutral, what):
    """display light of BT.2020 RGB in cd/m2 -> I, P, T codes (10 bits, full range). For a grey
    (neutral) P and T are 512 by rule: the chain gives a grey a P and T a hair from 511.5, which side of
    it depending on the last bits of the matrices, and 512 is the code of the offset (0.5) in the
    units of the DM header (code / 2^bits)."""
    lms = apply3(_TO_LMS_C, tuple(c / 10000.0 for c in rgb_nits))
    lmsp = tuple(pq_oetf(max(v, 0.0) * 10000.0) for v in lms)
    i, p, t = apply3(_TO_IPT, lmsp)
    ci = rnd(i * IPT_SCALE, what + " I")
    if neutral:
        return (min(max(ci, 0), 1023), 512, 512)
    cp = rnd((p + IPT_OFF[1]) * IPT_SCALE, what + " P")
    ct = rnd((t + IPT_OFF[2]) * IPT_SCALE, what + " T")
    return tuple(min(max(c, 0), 1023) for c in (ci, cp, ct))


def ipt_to_display(codes, scale=IPT_SCALE):
    """I, P, T codes -> display light of BT.2020 RGB in cd/m2 (the decoder's chain, for the tests)"""
    v = (codes[0] / scale - IPT_OFF[0], codes[1] / scale - IPT_OFF[1], codes[2] / scale - IPT_OFF[2])
    lmsp = apply3(IPT_YCC_M, v)
    lms = tuple(pq_eotf(x) / 10000.0 for x in lmsp)
    return tuple(10000.0 * c for c in apply3(_FROM_LMS_C, lms))


# ---------------------------------------------------------------------------------------------------
# formats

REF_WHITE = 203.0  # cd/m2 (ITU-R BT.2408)
HIGHLIGHTS = (400.0, 600.0, 800.0, 1000.0)
COLOURS = (("red", (1, 0, 0)), ("green", (0, 1, 0)), ("blue", (0, 0, 1)),
           ("cyan", (0, 1, 1)), ("magenta", (1, 0, 1)), ("yellow", (1, 1, 0)))
MASTERING = {"primaries": "P3-D65", "max_nits": 1000.0, "min_nits": 0.005}

FORMATS = (
    {"id": "sdr", "label": "SDR", "file": "hades-check-sdr.mkv", "width": 1920, "height": 1080,
     "fps": (24000, 1001), "frames": 360, "bits": 8, "signal": "sdr", "range": "limited",
     "primaries": 1, "transfer": 1, "matrix": 1},
    {"id": "hdr10", "label": "HDR10", "file": "hades-check-hdr10.mkv", "width": 3840, "height": 2160,
     "fps": (24000, 1001), "frames": 360, "bits": 10, "signal": "pq", "range": "limited",
     "primaries": 9, "transfer": 16, "matrix": 9},
    {"id": "hlg", "label": "HLG", "file": "hades-check-hlg.mkv", "width": 3840, "height": 2160,
     "fps": (50, 1), "frames": 750, "bits": 10, "signal": "hlg", "range": "limited",
     "primaries": 9, "transfer": 18, "matrix": 9},
    # (profile 8.1: the base layer is an HDR10 picture, the card of HDR10 coded the same way; only its
    # label is its own, so that what is on the screen says what is playing)
    {"id": "dv81", "label": "DOLBY VISION 8.1", "file": "hades-check-dv81.mkv", "width": 3840,
     "height": 2160, "fps": (24000, 1001), "frames": 360, "bits": 10, "signal": "pq", "range": "limited",
     "primaries": 9, "transfer": 16, "matrix": 9, "dovi_profile": 8},
    {"id": "dv5", "label": "DOLBY VISION 5", "file": "hades-check-dv5.mkv", "width": 3840,
     "height": 2160, "fps": (24000, 1001), "frames": 360, "bits": 10, "signal": "ipt", "range": "full",
     # (the VUI of profile 5 as x265 4.2 writes it, its "Dolby Vision P5 VUI defaults": BT.2020, PQ and
     # ITU-T H.273 MatrixCoefficients 15, IPT-C2, full range)
     "primaries": 9, "transfer": 16, "matrix": 15, "dovi_profile": 5},
)


def fmt(fid):
    for f in FORMATS:
        if f["id"] == fid:
            return f
    raise KeyError(fid)


def _hdr_colour_light(lit):
    """display light (BT.2020 RGB, cd/m2) of an EBU 75 % colour shown in SDR with white at 203 cd/m2"""
    lin = tuple((0.75 ** 2.4) * REF_WHITE * c for c in lit)
    return apply3(BT709_TO_BT2020, lin)


class Value:
    """the signal of a region: codes (Y, Cb, Cr) or (I, P, T), the non-linear R'G'B' they carry (None
    for IPT) and, in HDR, the intended display light (BT.2020 RGB, cd/m2)"""

    def __init__(self, codes, light=None, nonlinear=None):
        self.codes = tuple(codes)
        self.light = None if light is None else tuple(light)
        self.nonlinear = None if nonlinear is None else tuple(nonlinear)


def _grey_signal(f, ep, light, what):
    """a grey of non-linear value E' in a Y'CbCr format"""
    return Value(limited_codes((ep, 0.0, 0.0), f["bits"], what), light, (ep, ep, ep))


def region_values(f):
    """{region name: Value} of the card in format f"""
    sig, bits, out = f["signal"], f["bits"], {}
    matrix = "bt709" if sig == "sdr" else "bt2020"

    if sig == "ipt":
        # the same light as the HDR10 card: every region's intended light, coded in IPT-PQ-c2
        for name, v in region_values(fmt("hdr10")).items():
            neutral = v.light[0] == v.light[1] == v.light[2]
            out[name] = Value(ipt_codes(v.light, neutral, name), v.light)
        return out

    if sig == "sdr":
        white_ep, black_light = 1.0, None
        grey_light = lambda ep: None
    elif sig == "pq":
        white_ep = pq_oetf(REF_WHITE)
        grey_light = lambda ep: (pq_eotf(ep),) * 3
    else:
        white_ep = 0.75  # BT.2408: 203 cd/m2 at 75 % of the HLG signal
        grey_light = lambda ep: hlg_to_display((ep,) * 3)

    out["label"] = _grey_signal(f, 0.5 * white_ep, grey_light(0.5 * white_ep), "label")
    out["background"] = _grey_signal(f, 0.0, grey_light(0.0), "background")
    for k in range(1, 9):
        ep = k / 8.0 * white_ep
        out["bar%d" % k] = _grey_signal(f, ep, grey_light(ep), "bar%d" % k)
    for name, lit in COLOURS:
        if sig == "sdr":
            rgbp = tuple(0.75 * c for c in lit)
            light = None
        else:
            light = _hdr_colour_light(lit)
            rgbp = tuple(pq_oetf(c) for c in light) if sig == "pq" else hlg_from_display(light)
        out[name] = Value(limited_codes(ycbcr(rgbp, matrix), bits, name), light, rgbp)
    for n, pct in enumerate((2, 4, 6, 8), 1):
        ep = pct / 100.0
        out["dark%d" % n] = _grey_signal(f, ep, grey_light(ep), "dark%d" % n)
    if sig == "sdr":
        out["light_strip"] = _grey_signal(f, 1.0, None, "light_strip")
        for n, pct in enumerate((2, 4, 6, 8), 1):
            out["light%d" % n] = _grey_signal(f, 1.0 - pct / 100.0, None, "light%d" % n)
    else:
        def grey_nits(nits, what):
            ep = pq_oetf(nits) if sig == "pq" else hlg_from_display((nits,) * 3)[0]
            return _grey_signal(f, ep, (nits,) * 3, what)
        out["light_strip"] = grey_nits(REF_WHITE, "light_strip")
        for n, nits in enumerate(HIGHLIGHTS, 1):
            out["light%d" % n] = grey_nits(nits, "light%d" % n)
    return out


# ---------------------------------------------------------------------------------------------------
# geometry (grid of 1920x1080; rectangles are x0, y0, x1, y1, end exclusive)

def regions():
    """[(name, rect)] painted in this order over the background; the label is drawn separately"""
    out = []
    for k in range(8):
        out.append(("bar%d" % (k + 1), (96 + 216 * k, 176, 96 + 216 * (k + 1), 496)))
    for k, (name, _) in enumerate(COLOURS):
        x = 120 + 288 * k
        out.append((name, (x, 560, x + 240, 800)))
    for k in range(4):
        x = 168 + 216 * k
        out.append(("dark%d" % (k + 1), (x, 888, x + 96, 984)))
    out.append(("light_strip", (984, 864, 1800, 1008)))
    for k in range(4):
        x = 1032 + 216 * k
        out.append(("light%d" % (k + 1), (x, 888, x + 96, 984)))
    return out


# A 5x7 font of this project (a dot is an 8x8 block of the grid; a character advances 6 dots)
FONT = {
    " ": ("00000",) * 7,
    ".": ("00000", "00000", "00000", "00000", "00000", "01100", "01100"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "G": ("01110", "10001", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
}
LABEL_X, LABEL_Y, DOT, ADVANCE = 96, 64, 8, 6


def label_dots(text):
    """the dots of a label as rectangles of the grid"""
    out = []
    for n, ch in enumerate(text):
        glyph = FONT[ch]
        for row in range(7):
            for col in range(5):
                if glyph[row][col] == "1":
                    x = LABEL_X + (n * ADVANCE + col) * DOT
                    y = LABEL_Y + row * DOT
                    out.append((x, y, x + DOT, y + DOT))
    return out


def layout(f):
    """[(rect in pixels of the format, region name)], painted in order over the background"""
    s = f["width"] // GRID_W
    items = [(tuple(c * s for c in r), name) for name, r in regions()]
    items += [(tuple(c * s for c in r), "label") for r in label_dots(f["label"])]
    return items


def spans(f):
    """The picture as bands of rows: [(y0, y1, [(x0, x1, name), ...])], the row's runs left to right,
    covering the whole width; built from layout() so that a later rectangle paints over an earlier one."""
    w, h = f["width"], f["height"]
    items = layout(f)
    ys = sorted({0, h} | {r[1] for r, _ in items} | {r[3] for r, _ in items})
    bands = []
    for y0, y1 in zip(ys, ys[1:]):
        cuts = {0, w}
        active = [(r, n) for r, n in items if r[1] <= y0 and r[3] >= y1]
        for r, _ in active:
            cuts |= {r[0], r[2]}
        xs = sorted(cuts)
        runs = []
        for x0, x1 in zip(xs, xs[1:]):
            name = "background"
            for r, n in active:
                if r[0] <= x0 and r[2] >= x1:
                    name = n
            if runs and runs[-1][2] == name:
                runs[-1] = (runs[-1][0], x1, name)
            else:
                runs.append((x0, x1, name))
        if bands and bands[-1][2] == runs:
            bands[-1] = (bands[-1][0], y1, runs)
        else:
            bands.append((y0, y1, runs))
    return bands


def areas(f):
    """{region name: pixels} of the picture"""
    out = {}
    for y0, y1, runs in spans(f):
        for x0, x1, name in runs:
            out[name] = out.get(name, 0) + (x1 - x0) * (y1 - y0)
    return out


def planes(f, values=None):
    """The picture as three planes of codes: bytes of 8-bit samples, or of 16-bit little-endian samples
    (as x265 reads an input of more than 8 bits), Y then Cb then Cr (or I, P, T), 4:2:0."""
    if values is None:
        values = region_values(f)
    w, h, bits = f["width"], f["height"], f["bits"]
    out = []
    for c in range(3):
        sub = 1 if c == 0 else 2
        cw = w // sub
        typecode = "B" if bits == 8 else "H"
        plane = array(typecode)
        for y0, y1, runs in spans(f):
            row = array(typecode)
            for x0, x1, name in runs:
                if x0 % sub or x1 % sub:
                    raise ValueError("a run that does not cover whole chroma samples")
                row.extend(array(typecode, [values[name].codes[c]]) * ((x1 - x0) // sub))
            if len(row) != cw or y0 % sub or y1 % sub:
                raise ValueError("a band that does not cover whole chroma samples")
            plane.extend(row * ((y1 - y0) // sub))
        if typecode == "H" and sys.byteorder != "little":
            plane.byteswap()
        out.append(plane.tobytes())
    return out


# ---------------------------------------------------------------------------------------------------
# statistics of the card


def light_stats(f):
    """MaxCLL and MaxFALL of the card from its intended light (cd/m2): the highest component of any
    pixel, and the mean over the picture of the highest component of each pixel"""
    vals, ar = region_values(f), areas(f)
    total = sum(ar.values())
    peak = max(max(vals[n].light) for n in ar)
    mean = sum(max(vals[n].light) * a for n, a in ar.items()) / total
    return peak, mean


def level1(f):
    """Level 1 of the card (ST 2094-10): min, max and mean over the picture of PQ(max(R,G,B)) of what
    the signal carries (its codes, decoded), as 12-bit codes; and clamped as dovi_tool clamps a level 1
    block for CM v2.9 (min at most 12, max at least 2081, the mean from 819 to max - 1), as Kodi of
    Hades does too (tools/dvbridge/dvbridge_hdr10plus.c). For IPT (profile 5) it is measured on the same
    picture coded as HDR10: the same light, its label included."""
    if f["signal"] == "ipt":
        f = dict(fmt("hdr10"), label=f["label"])
    vals, ar = region_values(f), areas(f)
    total = sum(ar.values())

    def max_e(n):
        rgbp = rgb_from_ycbcr(limited_values(vals[n].codes, f["bits"]), "bt2020")
        return min(max(max(rgbp), 0.0), 1.0)

    def min_e(n):
        rgbp = rgb_from_ycbcr(limited_values(vals[n].codes, f["bits"]), "bt2020")
        return min(max(min(rgbp), 0.0), 1.0)

    mn = rnd(min(min_e(n) for n in ar) * 4095.0, "L1 min")
    mx = rnd(max(max_e(n) for n in ar) * 4095.0, "L1 max")
    avg = rnd(sum(max_e(n) * a for n, a in ar.items()) / total * 4095.0, "L1 avg")
    measured = (mn, mx, avg)
    cmn = min(mn, 12)
    cmx = max(mx, 2081)
    cavg = min(max(avg, 819), cmx - 1)
    return measured, (cmn, cmx, cavg)
