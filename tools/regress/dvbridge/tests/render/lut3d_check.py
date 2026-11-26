#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: the LUT files of lut3d_test.c and the check of what it drew, with arithmetic of its own
# (numpy, float64): the tetrahedral interpolation of a LUT on a uniform grid (the six tetrahedra of a cube, as
# libplacebo's pl_shader_custom_lut() and as every 3D LUT player interpolates), the video levels of a LUT's input and
# output, the shaper of a .3dl of Nuke's, and a 1D LUT before the 3D table (each channel by its curve, linearly between
# its points: a .cube of LUT_1D_SIZE alone or as Resolve's shaper, ArgyllCMS's .cal).
#
#   lut3d_check.py make <folder>                 the LUT files: good ones and ones that must be refused
#   lut3d_check.py check <folder> <out> <log>    the lines of the test against the tables made here, and every picture
#                                                drawn with a LUT against that LUT applied here to the picture drawn
#                                                without it
# Prints "ok:"/"FAIL:" lines and "LUT3D-CHECK PASSED|FAILED: <n> failed".
import os
import re
import struct
import sys

import numpy as np

W, H, STRIDE = 3840, 2160, 3
# the largest difference of a picture drawn with a LUT from the LUT applied here to the picture drawn without it: the
# GPU interpolates in 32-bit floats (a few units of the last place of 1.0, 1.2e-7 each; measured on zink: 1.9e-7 at
# most), from a table of 32-bit floats (libplacebo's sh_lut() takes RGBA32F on OpenGL ES 3: the first float format that
# is not emulated); 1e-6 is 1/1000 of a code of 10 bits and 1/250 of one of 12
TOLERANCE = 1e-6


# ---------------------------------------------------------------------------------------------------- the tables
def tint(r, g, b):
    """The known LUT: nonlinear and across the channels (the interpolation between its points is not the function),
    and an obvious tint (green down, a little blue lifted)"""
    return (np.clip(0.88 * r + 0.10 * g * b + 0.01, 0, 1),
            np.clip(0.72 * g + 0.05 * r * r, 0, 1),
            np.clip(0.95 * np.power(b, 0.9) + 0.03, 0, 1))


def grid(n, positions=None):
    """the inputs of a grid of n points a side, red fastest: arrays r, g, b of n^3"""
    p = np.arange(n) / (n - 1) if positions is None else np.asarray(positions, dtype=np.float64)
    b, g, r = np.meshgrid(p, p, p, indexing="ij")
    return r.ravel(), g.ravel(), b.ravel()


def cube_text(n, values, crlf=False, extras=False, fmt="%.6f"):
    nl = "\r\n" if crlf else "\n"
    lines = []
    if extras:
        lines += ["# made by lut3d_check.py", "", "TITLE \"tinted, CRLF\"", "  # an indented comment",
                  "DOMAIN_MIN 0 0 0", "DOMAIN_MAX 1.0 1.0 1.0", "LUT_3D_INPUT_RANGE 0.0 1.0"]
    lines.append("LUT_3D_SIZE %d" % n)
    if extras:
        lines.append("")
    for i in range(n ** 3):
        row = [fmt % v for v in values[i]]
        if extras and i % 7 == 3:
            # the same number in another form: scientific notation, a leading sign
            row = ["%.5e" % float(row[0]), "+" + row[1], row[2]]
        line = " ".join(row)
        if extras and i % 5 == 1:
            line = "\t " + line + "  "
        lines.append(line)
    if extras:
        lines += ["# the end", ""]
    return (nl.join(lines) + nl).encode()


def table_of_cube(text, n):
    """the table of a .cube as the bridge reads it: each number to the nearest float32"""
    vals = []
    for line in text.decode().splitlines():
        s = line.strip()
        if not s or s[0] == "#" or s[0].isalpha() or s[0] == "_":
            continue
        vals.append([float(x) for x in s.split()])
    t = np.asarray(vals, dtype=np.float64).astype(np.float32).astype(np.float64)
    assert t.shape == (n ** 3, 3)
    return t


def nuke_shaper(n, bits=10):
    top = (1 << bits) - 1
    step = (top + 1) // (n - 1)
    return [i * step for i in range(n - 1)] + [top]


def write_3dl(path, n, shaper, out_bits, func, header=True):
    top = shaper[-1]
    pos = [s / top for s in shaper]
    scale = (1 << out_bits) - 1
    lines = ["# a .3dl of lut3d_check.py"]
    if header:
        lines += ["3DMESH", "Mesh %d %d" % ((n - 1).bit_length() - 1, out_bits)]
    lines.append(" ".join(str(s) for s in shaper))
    raw = np.zeros((n, n, n, 3), dtype=np.int64)  # [r][g][b], blue fastest in the file
    for ri in range(n):
        for gi in range(n):
            for bi in range(n):
                v = func(np.float64(pos[ri]), np.float64(pos[gi]), np.float64(pos[bi]))
                q = [int(np.floor(float(c) * scale + 0.5)) for c in v]
                raw[ri, gi, bi] = q
                lines.append("%d %d %d" % tuple(q))
    open(path, "w").write("\n".join(lines) + "\n")
    # the table red fastest: [b][g][r]
    t = np.transpose(raw, (2, 1, 0, 3)).reshape(-1, 3)
    return (t / scale).astype(np.float32).astype(np.float64), pos


def write_madvr(path, bits, params, func, compression=0):
    n = 1 << bits
    p = np.arange(n) / (n - 1)
    data = bytearray()
    t = np.zeros((n, n, n, 3), dtype=np.float64)  # [b][g][r]
    for ri in range(n):
        for gi in range(n):
            for bi in range(n):
                v = func(p[ri], p[gi], p[bi])
                q = [int(np.floor(float(c) * 65535 + 0.5)) for c in v]
                # each sample blue, green, red, as Kodi's CColorManager::Load3dLut() reads it
                data += struct.pack("<3H", q[2], q[1], q[0])
                t[bi, gi, ri] = [x / 65535.0 for x in q]
    pbytes = params.encode()
    header = struct.pack("<4sI32sQ3I9I", b"3DLT", 1, b"lut3d_check.py".ljust(32, b"\0"), 1, bits, bits, bits, 0, 16, 0,
                         96, len(pbytes), 96 + len(pbytes), compression, len(data), len(data))
    assert len(header) == 96
    open(path, "wb").write(header + pbytes + bytes(data))
    return t.reshape(-1, 3).astype(np.float32).astype(np.float64)


def vcgt(x):
    """a calibration of a display's video card as dispcal writes it into a .cal: a tone and a grey balance per channel,
    its blue lifted at black and lowered at white"""
    return (np.power(x, 1.03) * 0.995, np.power(x, 0.985), 0.004 + np.power(x, 1.06) * 0.955 * 0.996)


def steep(x):
    """a curve that opens a crushed black (steep at the bottom)"""
    return (np.power(x, 0.45), np.power(x, 0.5), np.power(x, 0.42))


def write_cal(path, n, func, order=("RGB_I", "RGB_B", "RGB_R", "RGB_G"), tv=False, extra=""):
    """ArgyllCMS's .cal (CGATS), its fields in the order given (a reader that takes the columns as R, G, B by place
    reads another curve), the values with six places as dispcal writes them; the table as the bridge reads it"""
    x = np.arange(n) / (n - 1)
    v = [np.round(c, 6) for c in func(x)]
    lines = ["CAL    ", "", 'DESCRIPTOR "Argyll Device Calibration State"', 'ORIGINATOR "lut3d_check.py"',
             'CREATED "Wed Nov 25 12:00:00 2026"', 'KEYWORDS "DEVICE_CLASS"', 'DEVICE_CLASS "DISPLAY"',
             'KEYWORDS "COLOR_REP"', 'COLOR_REP "RGB"', 'KEYWORDS "VIDEO_LUT_CALIBRATION_POSSIBLE"',
             'VIDEO_LUT_CALIBRATION_POSSIBLE "YES"']
    if tv:
        lines += ['KEYWORDS "TV_OUTPUT_ENCODING"', 'TV_OUTPUT_ENCODING "YES"']
    lines += extra.splitlines()
    lines += ["", "NUMBER_OF_FIELDS 4", "BEGIN_DATA_FORMAT", " ".join(order), "END_DATA_FORMAT", "",
              "NUMBER_OF_SETS %d" % n, "BEGIN_DATA"]
    col = {"RGB_I": x, "RGB_R": v[0], "RGB_G": v[1], "RGB_B": v[2], "RGB_X": v[2]}
    for i in range(n):
        lines.append(" ".join("%.6f" % col[f][i] for f in order))
    lines += ["END_DATA", ""]
    open(path, "w").write("\n".join(lines))
    return np.stack(v, -1).astype(np.float32).astype(np.float64)


def write_cube1d(path, n, func, n3=0, func3=None, crlf=False):
    """a .cube of LUT_1D_SIZE n (its curves), alone or as Resolve's shaper before a 3D LUT of n3 points a side (its
    values after the 1D LUT's); the tables as the bridge reads them"""
    x = np.arange(n) / (n - 1)
    c = np.stack(func(x), -1)
    lines = ['TITLE "a 1D LUT of lut3d_check.py"', "LUT_1D_SIZE %d" % n, "LUT_1D_INPUT_RANGE 0.0 1.0"]
    if n3:
        lines += ["LUT_3D_SIZE %d" % n3, "LUT_3D_INPUT_RANGE 0 1"]
    lines += ["%.9f %.9f %.9f" % tuple(r) for r in c]
    t3 = None
    if n3:
        r, g, b = grid(n3)
        t3 = np.stack(func3(r, g, b), axis=1)
        lines += ["%.9f %.9f %.9f" % tuple(r) for r in t3]
        t3 = t3.astype(np.float32).astype(np.float64)
    open(path, "wb").write((("\r\n" if crlf else "\n").join(lines) + "\n").encode())
    return c.astype(np.float32).astype(np.float64), t3


def identity2():
    r, g, b = grid(2)
    return np.stack([r, g, b], axis=1)


def make(folder):
    os.makedirs(folder, exist_ok=True)
    tables = {}
    for n in (17, 33, 65):
        r, g, b = grid(n)
        vals = np.stack([r, g, b], axis=1)
        text = cube_text(n, vals, fmt="%.9f")
        open(os.path.join(folder, "id%d.cube" % n), "wb").write(text)
    r, g, b = grid(17)
    vals = np.stack(tint(r, g, b), axis=1)
    open(os.path.join(folder, "tint17.cube"), "wb").write(cube_text(17, vals))
    open(os.path.join(folder, "tint17-crlf.cube"), "wb").write(cube_text(17, vals, crlf=True, extras=True))
    open(os.path.join(folder, "video17.cube"), "wb").write(cube_text(17, vals))
    write_3dl(os.path.join(folder, "nuke17.3dl"), 17, nuke_shaper(17), 12, tint)
    write_3dl(os.path.join(folder, "unif9.3dl"), 9, [int(np.floor(i * 1023 / 8 + 0.5)) for i in range(9)], 10, tint,
              header=False)
    write_madvr(os.path.join(folder, "madvr16.3dlut"), 4,
                "Input_Range 16 235\r\nOutput_Range 16 235\r\n"
                "Input_Primaries 0.64 0.33 0.30 0.60 0.15 0.06 0.3127 0.3290\r\nInput_Transfer_Function BT.1886\r\n",
                tint)
    write_madvr(os.path.join(folder, "madvrpq8.3dlut"), 3,
                "Input_Primaries 0.708 0.292 0.170 0.797 0.131 0.046 0.3127 0.3290\nInput_Transfer_Function PQ\n"
                "Input_Range 0 255\nOutput_Range 0 255\n", tint)
    # 1D LUTs: ArgyllCMS's .cal (its fields in another order; one that says it was loaded in video levels), a .cube of
    # LUT_1D_SIZE alone (a steep curve; one of 5000 points, more than a row of the texture of Kodi's shaders; the
    # identity), and Resolve's .cube with a shaper before its 3D LUT
    write_cal(os.path.join(folder, "vcgt256.cal"), 256, vcgt)
    write_cal(os.path.join(folder, "vcgt-tv.cal"), 256, vcgt, order=("RGB_I", "RGB_R", "RGB_G", "RGB_B"), tv=True)
    write_cube1d(os.path.join(folder, "steep1d.cube"), 1024, steep, crlf=True)
    write_cube1d(os.path.join(folder, "long1d.cube"), 5000, vcgt)
    write_cube1d(os.path.join(folder, "id1d256.cube"), 256, lambda x: (x, x, x))
    write_cube1d(os.path.join(folder, "shaper.cube"), 64, lambda x: (np.power(x, 0.8), np.power(x, 0.75),
                                                                     np.power(x, 0.85)), 17, tint)
    # ... and files that are refused
    two = "\n".join(["0 0 0", "1 0 0", "0 1 0", "1 1 0", "0 0 1", "1 0 1", "0 1 1", "1 1 1"]) + "\n"
    bad = {
        "bad-1drange.cube": "LUT_1D_SIZE 2\nLUT_1D_INPUT_RANGE 0 2\n0 0 0\n1 1 1\n",
        "bad-1dcount.cube": "LUT_1D_SIZE 3\n0 0 0\n1 1 1\n",
        "bad-1d3dcount.cube": "LUT_1D_SIZE 2\nLUT_3D_SIZE 2\n0 0 0\n1 1 1\n" + "\n".join(two.splitlines()[:7]) + "\n",
        "bad-domain.cube": "LUT_3D_SIZE 2\nDOMAIN_MAX 2 2 2\n" + two,
        "bad-keyword.cube": "LUT_3D_SIZE 2\nLUT_3D_SHAPER 1\n" + two,
        "bad-count.cube": "LUT_3D_SIZE 2\n" + "\n".join(two.splitlines()[:7]) + "\n",
        "bad-inputrange.cube": "LUT_3D_SIZE 2\nLUT_3D_INPUT_RANGE 0 1.5\n" + two,
        "bad-value.cube": "LUT_3D_SIZE 2\n" + two.replace("1 1 1", "1 1 5"),
        "bad-name.txt": "LUT_3D_SIZE 2\n" + two,
        "bad-shaper.3dl": "0 100 300 1023\n" + "0 0 0\n" * 64,
        "bad-header.3dlut": "LUT_3D_SIZE 2\n" + two,
        "empty.cube": "",
    }
    for name, text in bad.items():
        open(os.path.join(folder, name), "w").write(text)
    write_madvr(os.path.join(folder, "bad-compressed.3dlut"), 2, "", tint, compression=1)
    write_cal(os.path.join(folder, "bad-printer.cal"), 16, vcgt, extra='KEYWORDS "X"\nDEVICE_CLASS "OUTPUT"')
    write_cal(os.path.join(folder, "bad-cmyk.cal"), 16, vcgt, extra='COLOR_REP "CMYK"')
    write_cal(os.path.join(folder, "bad-fields.cal"), 16, vcgt, order=("RGB_I", "RGB_R", "RGB_G", "RGB_X"))
    text = open(os.path.join(folder, "vcgt256.cal")).read()
    open(os.path.join(folder, "bad-grid.cal"), "w").write(text.replace("\n0.501961 ", "\n0.503000 ", 1))
    open(os.path.join(folder, "bad-header.cal"), "w").write(text.replace("CAL    ", "CTI3   ", 1))
    write_cal(os.path.join(folder, "bad-value.cal"), 16, lambda x: (x * 1.3, x, x))
    print("made %d files in %s" % (len(os.listdir(folder)), folder))


# ---------------------------------------------------------------------------------------------------- the reference
class Lut:
    def __init__(self, table, n, lo_in=0.0, hi_in=255.0, lo_out=0.0, hi_out=255.0, knee=None, curve=None):
        self.t = table.reshape(n, n, n, 3)  # [b][g][r]
        self.n = n
        self.lo_in, self.hi_in, self.lo_out, self.hi_out = lo_in, hi_in, lo_out, hi_out
        self.knee = knee
        self.curve = curve  # (m, 3): a 1D LUT before the table, or None

    def point(self, r, g, b):
        return self.t[b, g, r]

    def apply(self, x):
        """x: (..., 3) the signal in full range"""
        n = self.n
        u = (self.lo_in + (self.hi_in - self.lo_in) * x) / 255.0
        u = np.clip(u, 0.0, 1.0)
        if self.curve is not None:
            # each channel by its curve, linearly between the two points around it
            m = self.curve.shape[0]
            q = u * (m - 1)
            i = np.minimum(np.floor(q).astype(np.int64), m - 2)
            f = q - i
            u = np.stack([(1 - f[..., c]) * self.curve[i[..., c], c] + f[..., c] * self.curve[i[..., c] + 1, c]
                          for c in range(3)], -1)
        if self.knee is not None:
            k = self.knee
            gpos = np.where(u > k, (n - 2) + (u - k) / (1.0 - k), u * (n - 2) / k)
            p = np.clip(gpos / (n - 1), 0.0, 1.0) * (n - 1)
        else:
            p = u * (n - 1)
        base = np.minimum(np.floor(p).astype(np.int64), n - 1)
        f = p - base
        # the six tetrahedra: the steps along the axes in the order of the fractions, largest first
        order = np.argsort(-f, axis=-1, kind="stable")
        s = np.take_along_axis(f, order, axis=-1)
        v = [base.copy()]
        cur = base.copy()
        for k in range(3):
            step = np.zeros_like(cur)
            np.put_along_axis(step, order[..., k:k + 1], 1, axis=-1)
            cur = np.minimum(cur + step, n - 1)
            v.append(cur.copy())

        def at(idx):
            return self.t[idx[..., 2], idx[..., 1], idx[..., 0]]

        out = ((1.0 - s[..., 0:1]) * at(v[0]) + (s[..., 0:1] - s[..., 1:2]) * at(v[1]) +
               (s[..., 1:2] - s[..., 2:3]) * at(v[2]) + s[..., 2:3] * at(v[3]))
        return np.clip((out * 255.0 - self.lo_out) / (self.hi_out - self.lo_out), 0.0, 1.0)


def tables(folder):
    """the LUTs of lut3d_test.c's ways, as the bridge reads them"""
    def cube(name, n):
        return table_of_cube(open(os.path.join(folder, name), "rb").read(), n)
    L = {}
    L["tint"] = Lut(cube("tint17.cube", 17), 17)
    L["video"] = Lut(cube("video17.cube", 17), 17, 16, 235, 16, 235)
    for name, n in (("id17", 17), ("id33", 33), ("id65", 65)):
        L[name] = Lut(cube(name + ".cube", n), n)
    t, pos = write_3dl(os.path.join(folder, ".nuke17.check"), 17, nuke_shaper(17), 12, tint)
    os.remove(os.path.join(folder, ".nuke17.check"))
    L["nuke"] = Lut(t, 17, knee=float(np.float32(960.0 / 1023.0)))
    t, _ = write_3dl(os.path.join(folder, ".unif9.check"), 9, [int(np.floor(i * 1023 / 8 + 0.5)) for i in range(9)],
                     10, tint, header=False)
    os.remove(os.path.join(folder, ".unif9.check"))
    # (a uniform shaper of rounded codes: the grid is taken as uniform, as the bridge takes it)
    L["unif"] = Lut(t, 9)
    t = write_madvr(os.path.join(folder, ".madvr.check"), 4, "", tint)
    os.remove(os.path.join(folder, ".madvr.check"))
    L["madvr"] = Lut(t, 16, 16, 235, 16, 235)
    t = write_madvr(os.path.join(folder, ".madvrpq.check"), 3, "", tint)
    os.remove(os.path.join(folder, ".madvrpq.check"))
    L["madvrpq"] = Lut(t, 8)
    # the 1D LUTs (made again to a hidden file, as above)
    tmp = os.path.join(folder, ".check1d")
    L["cal"] = Lut(identity2(), 2, curve=write_cal(tmp, 256, vcgt))
    L["caltv"] = Lut(identity2(), 2, curve=write_cal(tmp, 256, vcgt, order=("RGB_I", "RGB_R", "RGB_G", "RGB_B"),
                                                     tv=True))
    L["steep1d"] = Lut(identity2(), 2, curve=write_cube1d(tmp, 1024, steep)[0])
    L["long1d"] = Lut(identity2(), 2, curve=write_cube1d(tmp, 5000, vcgt)[0])
    L["id1d256"] = Lut(identity2(), 2, curve=write_cube1d(tmp, 256, lambda x: (x, x, x))[0])
    c, t3 = write_cube1d(tmp, 64, lambda x: (np.power(x, 0.8), np.power(x, 0.75), np.power(x, 0.85)), 17, tint)
    L["shaper"] = Lut(t3, 17, curve=c)
    os.remove(tmp)
    return L


FILES = {"id17.cube": "id17", "id33.cube": "id33", "id65.cube": "id65", "tint17.cube": "tint",
         "tint17-crlf.cube": "tint", "video17.cube": "video", "nuke17.3dl": "nuke", "unif9.3dl": "unif",
         "madvr16.3dlut": "madvr", "madvrpq8.3dlut": "madvrpq", "vcgt256.cal": "cal", "vcgt-tv.cal": "caltv",
         "steep1d.cube": "steep1d", "long1d.cube": "long1d", "id1d256.cube": "id1d256", "shaper.cube": "shaper"}
# (the 1D LUTs of these files: their points that lut3d_test.c prints, "curve" lines)
CURVES = {"vcgt256.cal": "cal", "vcgt-tv.cal": "caltv", "steep1d.cube": "steep1d", "long1d.cube": "long1d",
          "id1d256.cube": "id1d256", "shaper.cube": "shaper"}
REFUSED = ["bad-1drange.cube", "bad-1dcount.cube", "bad-1d3dcount.cube", "bad-printer.cal", "bad-cmyk.cal",
           "bad-fields.cal", "bad-grid.cal", "bad-header.cal", "bad-value.cal", "bad-domain.cube", "bad-keyword.cube",
           "bad-count.cube", "bad-inputrange.cube",
           "bad-shaper.3dl", "bad-compressed.3dlut", "bad-header.3dlut", "empty.cube", "bad-value.cube",
           "bad-name.txt"]
# the ways of lut3d_test.c and the LUT of each
WAYS = {"sdr": "tint", "sdr-video": "video", "sdr-nuke": "nuke", "sdr-unif": "unif", "sdr-madvr": "madvr",
        "proj": "tint", "projblack": "tint", "asis": "tint", "codes": "tint", "ll": "tint", "llblack": "tint",
        "sdr-cal": "cal", "sdr-steep1d": "steep1d", "sdr-long1d": "long1d", "proj-shaper": "shaper"}


def load(out, name):
    a = np.fromfile(os.path.join(out, name + ".f32"), dtype=np.float32)
    h, w = (H + STRIDE - 1) // STRIDE, (W + STRIDE - 1) // STRIDE
    return a.reshape(h, w, 3).astype(np.float64)


def check(folder, out, logpath):
    log = open(logpath, errors="replace").read()
    failed = 0

    def say(ok, what):
        nonlocal failed
        print(("ok:   " if ok else "FAIL: ") + what)
        failed += not ok

    L = tables(folder)
    # --- what was read and what refused
    for name, key in FILES.items():
        say(re.search(r"^parse %s: read: " % re.escape(name), log, re.M) is not None, "%s read" % name)
    for name in REFUSED:
        m = re.search(r"^parse %s: refused: (.+)$" % re.escape(name), log, re.M)
        say(m is not None and len(m.group(1)) > 10, "%s refused (%s)" % (name, m.group(1) if m else "not refused"))
    # --- the grid points: the orientation of each table
    bad_points = 0
    points = 0
    for m in re.finditer(r"^point (\S+) (\d+) (\d+) (\d+): (\S+) (\S+) (\S+)$", log, re.M):
        name, r, g, b = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        got = np.array([float(m.group(k)) for k in (5, 6, 7)])
        points += 1
        # (a file that should have been refused and was read: no table here, a point that is not its own)
        if name not in FILES:
            bad_points += 1
            continue
        want = L[FILES[name]].point(r, g, b)
        if np.max(np.abs(got - want)) > 1e-7:
            bad_points += 1
            print("   point %s (%d, %d, %d): %s, the table here %s" % (name, r, g, b, got, want))
    say(points == 5 * len(FILES) and bad_points == 0,
        "the grid points of every LUT read are those of its file, red fastest (%d points, %d not)" % (points, bad_points))
    # --- the points of the 1D LUTs, each channel its own
    bad_curve, curve_points = 0, 0
    for m in re.finditer(r"^curve (\S+) (\d+) of (\d+): (\S+) (\S+) (\S+)$", log, re.M):
        name, i, n = m.group(1), int(m.group(2)), int(m.group(3))
        got = np.array([float(m.group(k)) for k in (4, 5, 6)])
        curve_points += 1
        if name not in CURVES:
            bad_curve += 1
            continue
        want = L[CURVES[name]].curve
        if n != want.shape[0] or np.max(np.abs(got - want[i])) > 1e-7:
            bad_curve += 1
            print("   curve %s point %d of %d: %s, here %s" % (name, i, n, got, want[min(i, want.shape[0] - 1)]))
    say(curve_points == 3 * len(CURVES) and bad_curve == 0,
        "the points of every 1D LUT read are those of its file, each channel its own (%d points, %d not)"
        % (curve_points, bad_curve))
    # --- the pictures
    for way, key in WAYS.items():
        if not os.path.exists(os.path.join(out, way + "-off.f32")):
            say(False, "%s: no picture" % way)
            continue
        off, got = load(out, way + "-off"), load(out, way + "-lut")
        # (a picture kept as 10-bit codes: the LUT on the signal before it is quantized, the same picture drawn in
        # floats with the same mapping)
        signal = load(out, "sdr-off") if way == "codes" else off
        want = L[key].apply(signal)
        mask = np.ones(off.shape[:2], dtype=bool)
        if way.startswith("ll"):
            m = re.search(r"^active %s: (\d+) (\d+) (\d+) (\d+)$" % way, log, re.M)
            left, right, top, bottom = (int(m.group(k)) for k in range(1, 5)) if m else (0, 0, 0, 0)
            ys = np.arange(off.shape[0]) * STRIDE
            xs = np.arange(off.shape[1]) * STRIDE
            mask = ((ys[:, None] >= top) & (ys[:, None] < H - bottom) & (xs[None, :] >= left) &
                    (xs[None, :] < W - right))
            bars = got[~mask]
            say(m is not None and (mask.sum() < mask.size) and np.all(bars == 0.0) and np.all(off[~mask] == 0.0),
                "%s: the bars (%d of %d pixels) stay at code 0 with the LUT (which maps black to %s)"
                % (way, (~mask).sum(), mask.size, np.round(L[key].apply(np.zeros(3)), 4)))
        if way == "codes":
            # 10-bit codes: the LUT on the signal, then quantized once; and the picture without the LUT is the signal
            # quantized (the same mapping: the reference of the input is right)
            for tag, pic, ref in (("with the LUT", got, want), ("without it", off, signal)):
                wc = np.floor(ref * 1023.0 + 0.5)
                diff = np.abs(wc - np.round(pic * 1023.0))
                frac = ref * 1023.0 - np.floor(ref * 1023.0)
                near = np.abs(frac - 0.5) < 1e-4
                say(diff.max() <= 1 and np.all(near[diff > 0]),
                    "codes %s: every 10-bit code that of the signal %squantized once, but where the value is within "
                    "1/10000 of a code of a half (%d such of %d)"
                    % (tag, "with the LUT applied here, " if pic is got else "", int((diff > 0).sum()), diff.size))
            continue
        d = np.abs(got - want)[mask]
        change = np.abs(want - off)[mask].mean()
        say(d.max() <= TOLERANCE and change > 0.005,
            "%s (%s): the picture with the LUT is the LUT applied here to the picture without it: at most %.3g apart "
            "(99.99 %%: %.3g; tolerance %.0e), over %d pixels that it changes by %.4f on average"
            % (way, key, d.max(), np.percentile(d, 99.99), TOLERANCE, mask.sum(), change))
    print("LUT3D-CHECK %s: %d failed" % ("PASSED" if not failed else "FAILED", failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "make":
        make(sys.argv[2])
    elif len(sys.argv) == 5 and sys.argv[1] == "check":
        sys.exit(check(sys.argv[2], sys.argv[3], sys.argv[4]))
    else:
        sys.exit("usage: lut3d_check.py make <folder> | check <folder> <out> <log>")
