#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Makes app.ico, the icon of LibreELEC-Hades-Installer.exe, from the brand source of the build
# (packages/mediacenter/kodi/brand/src/make-brand-images.py): the eclipse mark on its dark tile, computed
# once for each size (its light, its point and its tile are tuned for each, not scaled from one picture).
#
#   make-icon.py --brand <make-brand-images.py> --jost <Jost .ttf> --out app.ico [--preview <sheet.png>]
#
# Sizes 16 to 128 are stored as classic 32-bit bitmaps (every Windows reads them; the resource compiler
# of csc takes them as they are), 256 as PNG. The result is the same bytes on every run.
import argparse
import hashlib
import importlib.util
import io
import struct
import types

import numpy as np
from PIL import Image

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def load_brand(path, jost):
    spec = importlib.util.spec_from_file_location("brand", path)
    brand = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(brand)
    brand.ARGS = types.SimpleNamespace(jost=jost)
    return brand


def dib(rgba):
    """A 32-bit ICO entry: BITMAPINFOHEADER (height doubled), bottom-up BGRA rows, an all-zero AND mask."""
    h, w = rgba.shape[:2]
    bgra = rgba[..., [2, 1, 0, 3]][::-1]
    head = struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    mask = bytes(((w + 31) // 32) * 4 * h)
    return head + np.ascontiguousarray(bgra).tobytes() + mask


def png(rgba):
    out = io.BytesIO()
    Image.fromarray(rgba).save(out, "PNG", optimize=True)
    return out.getvalue()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--brand", required=True)
    p.add_argument("--jost", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--preview")
    a = p.parse_args()
    brand = load_brand(a.brand, a.jost)
    images = []
    for n in SIZES:  # fixed order: the noise of the brand script is one stream
        brand.RNG = np.random.default_rng(20261012 + n)
        images.append((n, brand.icon(n)))
    blobs = [png(im) if n >= 256 else dib(im) for n, im in images]
    data = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for (n, im), blob in zip(images, blobs):
        data += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    data += b"".join(blobs)
    with open(a.out, "wb") as f:
        f.write(data)
    print("%s %d bytes sha256 %s" % (a.out, len(data), hashlib.sha256(data).hexdigest()))
    if a.preview:
        # every size at its own pixels, on light and on dark, then each doubled-up 4x for a closer look
        pad = 12
        W = sum(n for n, _ in images) + pad * (len(images) + 1)
        sheet = np.zeros((2 * 256 + 3 * pad, W, 3), np.uint8)
        sheet[: 256 + 2 * pad] = 255
        sheet[256 + 2 * pad:] = 24
        for row, base in enumerate((255, 24)):
            x = pad
            for n, im in images:
                y = pad + row * (256 + pad) + (256 - n)
                al = im[..., 3:4] / 255.0
                reg = sheet[y:y + n, x:x + n]
                reg[:] = np.rint(im[..., :3] * al + reg * (1 - al)).astype(np.uint8)
                x += n + pad
        Image.fromarray(sheet).save(a.preview)
        big = np.zeros((4 * 48 + 2 * pad, 4 * (16 + 20 + 24 + 32 + 40 + 48) + 7 * pad, 3), np.uint8) + 128
        x = pad
        for n, im in images[:6]:
            al = im[..., 3:4] / 255.0
            comp = np.rint(im[..., :3] * al + 128 * (1 - al)).astype(np.uint8)
            comp = np.kron(comp, np.ones((4, 4, 1), np.uint8))
            big[pad:pad + 4 * n, x:x + 4 * n] = comp
            x += 4 * n + pad
        Image.fromarray(big).save(a.preview.replace(".png", "-zoom.png"))


if __name__ == "__main__":
    main()
