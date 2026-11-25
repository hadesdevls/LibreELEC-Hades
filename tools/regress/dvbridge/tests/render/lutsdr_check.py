#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: the check of what lutsdr_test.c drew with the video shaders of Kodi's own renderer and a
# 3D LUT of the display: every picture drawn with a LUT against that LUT applied here, in float64, to the picture drawn
# without it (lut3d_check.py's tables and its tetrahedral interpolation, the reference of the bridge's LUT too), the
# signal taken to full range and back where the output is of limited range (64 to 940 of 1023, as Kodi's
# CConvertMatrix makes it), and held within [0, 1] first, as the shader holds it.
#
#   lutsdr_check.py <LUT folder> <out folder> <log of lutsdr_test>
# Prints "ok:"/"FAIL:" lines and "LUTSDR-CHECK PASSED|FAILED: <n> failed".
import importlib.util
import os
import re
import sys

import numpy as np

STRIDE = 2
# the largest difference allowed: a shader in highp interpolates in 32-bit floats from a table of 32-bit floats (as the
# bridge: lut3d_check.py's 1e-6, 1/1000 of a code of 10 bits); Kodi's YUV shader of GLES 2.0 is mediump, and its
# variable of the output takes the LUT's result in that precision, which a GPU may keep in 16-bit floats (Mesa's
# software renderer does): half a step of a 16-bit float below 1.0 (2^-12, 1/16 of a code of 8 bits) more
TOLERANCE = {"highp": 1e-6, "mediump": 2.0 ** -12 + 1e-6}
LIMITED = ((940.0 - 64.0) / 1023.0, 64.0 / 1023.0)


def lut3d_check():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("lut3d_check", os.path.join(here, "lut3d_check.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load(out, name, w, h):
    a = np.fromfile(os.path.join(out, name + ".f32"), dtype=np.float32)
    rows, cols = (h + STRIDE - 1) // STRIDE, (w + STRIDE - 1) // STRIDE
    return a.reshape(rows, cols, 3).astype(np.float64)


def main(folder, out, logpath):
    log = open(logpath, errors="replace").read()
    failed = 0

    def say(ok, what):
        nonlocal failed
        print(("ok:   " if ok else "FAIL: ") + what)
        failed += not ok

    tables = lut3d_check().tables(folder)
    pairs = re.findall(r"^pair (\S+) (\S+) (\S+) (\S+) (\d+) (\d+) (\d) (\S+)$", log, re.M)
    say(len(pairs) >= 11, "%d pictures drawn with a LUT to check" % len(pairs))
    for way, key, off_name, lut_name, w, h, limited, precision in pairs:
        w, h, limited = int(w), int(h), int(limited)
        off, got = load(out, off_name, w, h), load(out, lut_name, w, h)
        scale, offset = LIMITED if limited else (1.0, 0.0)
        # (a picture that is no number, as Kodi's tone mapping makes of some colours, is black to the shader's clamp
        # (the IEEE minimum and maximum take the number), and to a fixed-point output; the LUT then gives its black)
        nan = int(np.isnan(off).any(axis=-1).sum())
        full = (np.nan_to_num(off, nan=0.0) - offset) / scale
        inside = np.clip(full, 0.0, 1.0)
        # (an output of limited range carries what is below its black and above its white beside the LUT)
        want = (tables[key].apply(inside) + ((full - inside) if limited else 0.0)) * scale + offset
        d = np.abs(got - want)
        change = np.abs(want - np.clip(np.nan_to_num(off, nan=0.0), 0.0, 1.0)).mean()
        tol = TOLERANCE[precision]
        beyond = int((np.abs(full - inside) > 0).any(axis=-1).sum()) if limited else 0
        say(np.isfinite(got).all() and d.max() <= tol and change > 0.005,
            "%s (%s): the picture with the LUT is the LUT applied here to the picture without it: at most %.3g apart "
            "(99.99 %%: %.3g; tolerance %.3g, %s), over %d pixels that it changes by %.4f on average%s%s"
            % (way, key, d.max(), np.percentile(d, 99.99), tol, precision, d.shape[0] * d.shape[1], change,
               ", the output of limited range (%d pixels beyond its black or white)" % beyond if limited else "",
               ", %d pixels no number without it" % nan if nan else ""))
    print("LUTSDR-CHECK %s: %d failed" % ("PASSED" if not failed else "FAILED", failed))
    return 0 if not failed else 1


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: lutsdr_check.py <LUT folder> <out folder> <log>")
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3]))
