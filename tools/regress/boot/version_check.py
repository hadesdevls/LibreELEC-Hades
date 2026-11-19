#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# version_check.py <picture> <Jost-VariableFont_wght.ttf> <notice folder> <text>
#
# The line of the version that hades-picture writes on the card of the notice that Kodi could not start, made of the
# letters of an atlas (picture.c, picture_text(); notice/font-1080), against the same line set by the typeface itself:
# the whole line with Pillow's basic layout, four times as large and brought down by averaging, as
# make-brand-images.py sets the other lines of the card, in the colour and at the place that font-1080.txt gives.
# Passes where the two differ only by what placing each letter to a quarter of a pixel can make: a mean difference of
# at most 2 levels over the line and 99 % of its pixels within 40 levels. Exit 0 when it passes.
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

pic, jost, notice, text = sys.argv[1:5]
shot = np.asarray(Image.open(pic).convert("RGB")).astype(float)
h = shot.shape[0]
strip = Image.open("%s/failed-off-%d.png" % (notice, h)).size[1]
top = h - strip
words = open("%s/font-%d.txt" % (notice, h)).read().split()
cw, ch, base = int(words[3]), int(words[4]), int(words[6])
i = words.index("line")
x256, line, right = int(words[i + 1]), int(words[i + 2]), int(words[i + 3])
colour = np.array([int(words[i + 5]), int(words[i + 6]), int(words[i + 7])], float)
plate = shot[top + line, right - 2]  # (the plate, at the end of the line, which the text never reaches)
ss = 4
f = ImageFont.truetype(jost, int(round(17 * h / 1080.0 * ss)), layout_engine=ImageFont.Layout.BASIC)
f.set_variation_by_axes([340])
y0, y1 = top + line - base, top + line - base + ch
x0, x1 = x256 // 256 - 4, right
img = Image.new("L", ((x1 - x0) * ss, (y1 - y0) * ss), 0)
ImageDraw.Draw(img).text(((x256 / 256.0 - x0) * ss, (line - (line - base) - 0) * ss + 0), text, font=f, fill=255,
                         anchor="ls")
a = np.asarray(img, float).reshape(y1 - y0, ss, x1 - x0, ss).mean(axis=(1, 3)) / 255.0
want = plate * (1 - a[..., None]) + colour * a[..., None]
got = shot[y0:y1, x0:x1]
d = np.abs(got - want).max(axis=2)
ink = a > 0.02
mean, p99 = d[ink].mean(), np.percentile(d[ink], 99)
print("the line of the version, %d pixels of ink: mean difference %.2f levels, 99 %% within %.1f, largest %.0f" %
      (ink.sum(), mean, p99, d.max()))
ok = ink.sum() > 100 and mean <= 2.0 and p99 <= 40
print("version-check: %s" % ("PASSED" if ok else "FAILED"))
sys.exit(0 if ok else 1)
