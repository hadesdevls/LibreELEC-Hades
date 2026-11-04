#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Draws the images under which LibreELEC Hades shows itself, in place of the logos and splash screens of
# Kodi and of LibreELEC (Kodi's trademark policy: a modified build that the XBMC Foundation has not
# approved does not use Kodi's name and logos as its own; and an unofficial build does not show the logo
# of LibreELEC as if it were LibreELEC's own).
#
# The mark is an eclipse at the instant of its first light: a dark disc, the thin rim of light around
# it, and the one point where the light breaks through. It is the picture of what this build is for,
# light at its brightest next to black at its deepest; and of its name, which has been read since
# antiquity as "the unseen": what could not be seen, coming into sight. The name is set in Jost
# (indestructible type*, SIL Open Font License 1.1), a typeface of circles and straight lines like
# the mark. Everything is computed, nothing is traced from another picture.
#
#   make-brand-images.py --jost Jost-VariableFont_wght.ttf --out <folder>
#
# writes four folders: kodi/ (what the recipe of Kodi installs), boot/ (the splash screen of the
# start of the system, distributions/LibreELEC/splash), settings/ (the two pictures of the settings
# add-on of LibreELEC that carried its logo) and notice/ (the notice that the start shows where several
# displays are connected, packages/mediacenter/kodi/brand/notice, which kodi-display-notice draws).
#
# Needs Python 3 with Pillow (FreeType) and NumPy. The light is computed in floating point and brought
# to 8 bits with noise (dither): a soft glow on black shows bands on a good display otherwise. Where
# there is no light the picture is exactly black, so that an OLED panel stays off.
import argparse
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ARGS = None
RNG = np.random.default_rng(20261012)  # fixed: the same files every time

SILVER = np.array([0.86, 0.90, 1.00])  # the light
WARM = np.array([1.00, 0.87, 0.70])  # its first point
NAME_TOP = np.array([1.00, 1.00, 1.00])
NAME_BOTTOM = np.array([0.78, 0.81, 0.87])
SMALL = np.array([0.60, 0.63, 0.69])
TILE = np.array([8.0, 8.0, 12.0]) / 255.0  # whole steps: a tile without light is one flat colour
FIRST_LIGHT = -0.72  # radians, where the light breaks through: upper right
REACH = 3.7  # no light of the mark farther than this from the centre of the disc, in radii
# The mark and the name stand this much higher than the middle of the screen (in lines of 1080), in
# every splash screen: the notice "one display" lays its card under them, and nothing moves when it
# shows (2026-11-03; before, they stood in the middle).
RAISE = 70
NOTICE_EN = ("LibreELEC Hades shows on one display: normally the TV connected over HDMI.",
             "The other displays turn off in a few seconds.")
NOTICE_ES = ("LibreELEC Hades se muestra en una sola pantalla: normalmente la TV conectada por HDMI.",
             "Las demás pantallas se apagan en unos segundos.")
NOTICE_TEXT = np.array([0.90, 0.92, 0.96])
NOTICE_SOFT = np.array([0.58, 0.61, 0.67])
PLATE = np.array([0.05, 0.055, 0.07])

# The heights of screen that have a picture of their own for the start of the system. Another height
# gets the one of 1080 lines, made larger or smaller by the program that draws it.
BOOT_HEIGHTS = (720, 768, 800, 900, 1024, 1050, 1080, 1200, 1440, 1600, 2160)
# The side of the first-run wizard is drawn into a place of 270 by 676 of a window of 1080 lines,
# whatever the proportions of the picture: it has those proportions, at twice the size.
WIZARD = (540, 1352)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def text_mask(text, px, weight, tracking=0.0, ss=4):
    """The ink of a line of text as numbers from 0 to 1; its last row is the baseline.

    Drawn letter by letter, four times as large, and brought down: the spacing is then exact to a
    quarter of a pixel. Only for text with nothing under the baseline. The basic layout of Pillow is
    asked for by name: the other one (Raqm) is there only where its library is, and places the
    letters elsewhere.
    """
    font = ImageFont.truetype(ARGS.jost, int(round(px * ss)), layout_engine=ImageFont.Layout.BASIC)
    font.set_variation_by_axes([weight])
    track = tracking * px * ss
    at, x = [], 0.0
    for i, ch in enumerate(text):
        at.append(x)
        x += font.getlength(ch) + (track if i < len(text) - 1 else 0.0)
    ascent, descent = font.getmetrics()
    pad = 4 * ss
    img = Image.new("L", (int(np.ceil(x)) + 2 * pad, ascent + descent + 2 * pad), 0)
    draw = ImageDraw.Draw(img)
    for ch, cx in zip(text, at):
        draw.text((pad + int(round(cx)), pad + ascent), ch, font=font, fill=255, anchor="ls")
    img = img.crop(img.getbbox())
    w, h = img.size
    w2, h2 = -(-w // ss) * ss, -(-h // ss) * ss
    whole = Image.new("L", (w2, h2), 0)
    whole.paste(img, (0, h2 - h))
    # (brought down by plain averaging: the same on every machine)
    a = np.asarray(whole, dtype=np.float64).reshape(h2 // ss, ss, w2 // ss, ss).mean(axis=(1, 3))
    return a / 255.0


def mark(w, h, cx, cy, radius, wide=True, streak=(1.8, 3.2), floor=0.10, ss=2):
    """The light of the mark, in colour, on a canvas of w by h: (h, w, 3).

    (cx, cy) is the centre of the dark disc and radius its radius. A sharp rim just outside the disc,
    a close glow and a wide one, all of them strong on the side of the first light and nearly gone on
    the far side; the point where the light breaks through; and, through that point, the thin line
    that a camera lens draws from a light so much brighter than the rest. streak: between which two
    distances from the point, in radii, that line fades out (None: no line). On a tile it has to end
    inside the tile: a line that the edge cuts reads as a scratch.
    """
    W, H, R = w * ss, h * ss, radius * ss
    y, x = np.mgrid[0:H, 0:W].astype(np.float64)
    x += 0.5 - cx * ss
    y += 0.5 - cy * ss
    r = np.hypot(x, y)
    d = r - R
    out = np.clip(d, 0.0, None)
    side = (1.0 + np.cos(np.arctan2(y, x) - FIRST_LIGHT)) / 2.0
    edge_in = max(0.006 * R, 0.55 * ss)  # the edge of the disc, never under half a pixel
    edge_out = max(0.020 * R, 0.90 * ss)
    rim = np.where(d >= 0.0, np.exp(-((d / edge_out) ** 2)), np.exp(-((d / edge_in) ** 2)))
    near = 0.60 * np.exp(-out / (0.075 * R)) * (d >= 0.0)
    light = (floor + (1.0 - floor) * side**1.7) * (1.6 * rim + near)
    if wide:
        far = 0.22 * np.exp(-out / (0.40 * R)) * (d >= 0.0)
        light += (0.30 + 0.70 * side) * far * (1.0 - smoothstep(2.0 * R, 3.6 * R, r))
    px = x - R * np.cos(FIRST_LIGHT)
    py = y - R * np.sin(FIRST_LIGHT)
    q = np.hypot(px, py)
    behind = np.where(d >= 0.0, 1.0, np.exp(-((d / (0.03 * R)) ** 2)))  # the disc hides it
    # (the point is never smaller than a pixel can show: at the smallest sizes it is what tells the
    # mark from a plain ring)
    bead = max(0.055 * R, 0.85 * ss)
    halo = max(0.22 * R, 2.2 * ss)
    light += (2.8 * np.exp(-((q / bead) ** 2)) + 0.75 * np.exp(-q / halo)) * behind
    if streak:
        thin = max(0.011 * R, 0.5 * ss)
        line = np.exp(-((py / thin) ** 2)) * np.exp(-np.abs(px) / (0.85 * R))
        light += 0.40 * line * (1.0 - smoothstep(streak[0] * R, streak[1] * R, np.abs(px))) * behind
    k = (0.8 * np.exp(-q / (0.6 * R)))[..., None]
    tint = SILVER * (1.0 - k) + WARM * k
    img = 1.0 - np.exp(-1.6 * light[..., None] * tint)
    img = np.clip(img - 0.4 / 255.0, 0.0, None) / (1.0 - 0.4 / 255.0)  # ends in exact black
    return img.reshape(h, ss, w, ss, 3).mean(axis=(1, 3))


def ring(w, h, cx, cy, radius, glow=False, thin=0.010, ss=4):
    """The mark as a mask, for where it is drawn in one colour over any background: (h, w).

    A ring that is thick at the point of the first light and thins away from it to a hair on the far
    side, and that point as a dot on it. Its outer edge is a circle: it thins from the inside. thin:
    half the width of the ring on the far side, in radii (more where the picture is drawn much
    smaller than it is: the hair would be lost).
    """
    W, H, R = w * ss, h * ss, radius * ss
    y, x = np.mgrid[0:H, 0:W].astype(np.float64)
    x += 0.5 - cx * ss
    y += 0.5 - cy * ss
    r = np.hypot(x, y)
    side = (1.0 + np.cos(np.arctan2(y, x) - FIRST_LIGHT)) / 2.0
    half = np.maximum(R * (thin + (0.068 - thin) * side**1.6), 0.5 * ss)  # half the width of the ring there
    mask = np.clip((half - np.abs(r - (R - half))) / ss + 0.5, 0.0, 1.0)
    q = np.hypot(x - R * np.cos(FIRST_LIGHT), y - R * np.sin(FIRST_LIGHT))
    dot = max(0.135 * R, 1.0 * ss)
    mask = np.maximum(mask, np.clip((dot - q) / ss + 0.5, 0.0, 1.0))
    if glow:
        mask = np.maximum(mask, 0.30 * np.exp(-q / (0.30 * R)))
    return mask.reshape(h, ss, w, ss).mean(axis=(1, 3))


def mark_on(img, cx, cy, radius):
    """The mark in colour, laid into a picture: computed only where it has light."""
    h, w = img.shape[:2]
    half = int(np.ceil(REACH * radius)) + 2
    x0, x1 = max(0, int(cx) - half), min(w, int(cx) + half)
    y0, y1 = max(0, int(cy) - half), min(h, int(cy) + half)
    img[y0:y1, x0:x1] = mark(x1 - x0, y1 - y0, cx - x0, cy - y0, radius)


def put(img, mask, x, y, top, bottom=None):
    """Lays ink over the picture: a mask in a colour, or in two that run from its top to its bottom."""
    x, y = int(round(x)), int(round(y))
    h, w = mask.shape
    part = img[y:y + h, x:x + w]
    if bottom is None:
        col = top[None, None, :]
    else:
        t = np.linspace(0.0, 1.0, h)[:, None, None]
        col = top * (1.0 - t) + bottom * t
    part[:] = part * (1.0 - mask[..., None]) + col * mask[..., None]


def to8(values, base=None):
    """Floating point to 8 bits with noise, so that the mean of many pixels is the value itself.

    Over most of the range the noise is the sum of two, each of one step: its strength is then the
    same whatever the value, and a slow change of light shows no bands of grain. Within half a step
    of the darkest value of the picture (black, or the colour given as base: whole steps) and of
    white, where that noise would cross the end, it is noise of one step: a value between two steps
    becomes the upper one as often as it is near it. Its strength is the same at half a step and
    falls to nothing at the end, so what has no light is exactly the base, what is nearly without
    light is a few lit pixels among dark ones and not a raised floor, and white is white.

    The value is rounded to a millionth of a step first, so that the last digits of the arithmetic,
    which differ between processors, do not decide a pixel. The noise is the same for the three
    colours: it has no colour.
    """
    v = np.round(np.clip(values, 0.0, 1.0) * 255.0, 6)
    low = 0.0 if base is None else np.round(np.asarray(base) * 255.0, 6)
    shape = v.shape[:2] + (1,) * (v.ndim - 2)
    one, other = RNG.random(shape), RNG.random(shape)
    middle = np.rint(v + one + other - 1.0)
    ends = np.floor(v + one)
    out = np.where((v >= low + 0.5) & (v <= 254.5), middle, ends)
    return np.clip(out, 0, 255).astype(np.uint8)


def save(name, array, **options):
    path = os.path.join(ARGS.out, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(array).save(path, **options)
    print("%-32s %4dx%-4d %7d bytes" % (name, array.shape[1], array.shape[0], os.path.getsize(path)))


def white(alpha):
    """A white picture whose transparency is the mask."""
    h, w = alpha.shape
    out = np.empty((h, w, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = to8(alpha)
    return out


def tall(w, h, s=None, middle=None):
    """The mark over the name, on black: the splash screens. s is the size (1 for 1080 lines)."""
    s = h / 1080.0 if s is None else s
    middle = h / 2.0 - RAISE * s if middle is None else middle
    img = np.zeros((h, w, 3))
    mark_on(img, w / 2.0, middle - 150 * s, 118 * s)
    small = text_mask("LibreELEC", 38 * s, 400, tracking=0.16)
    name = text_mask("HADES", 104 * s, 300, tracking=0.46)
    put(img, name, (w - name.shape[1]) / 2.0, middle + 300 * s - name.shape[0], NAME_TOP, NAME_BOTTOM)
    put(img, small, (w - small.shape[1]) / 2.0, middle + 180 * s - small.shape[0], SMALL)
    return img


def wide(w, h):
    """The mark with the name at its right, as a mask: white on nothing, over any background."""
    alpha = ring(w, h, 0.48 * h, 0.52 * h, 0.36 * h, glow=h >= 200)
    small = text_mask("LibreELEC", 0.21 * h, 450, tracking=0.10)
    name = text_mask("HADES", 0.52 * h, 400, tracking=0.30)
    left = int(round(1.06 * h))
    for mask, base, strength in ((small, 0.345 * h, 0.74), (name, 0.845 * h, 1.0)):
        y = int(round(base)) - mask.shape[0]
        part = alpha[y:y + mask.shape[0], left:left + mask.shape[1]]
        part[:] = np.maximum(part, mask * strength)
    return alpha


def small_mark(n):
    """The mark alone, as a mask: the small mark that the skin tints, in the corner of every dialog.

    The skin draws it at a third of its size and less: the far side of its ring is thicker than in
    the logo.
    """
    return ring(n, n, 0.48 * n, 0.52 * n, 0.385 * n, thin=0.024)


def rounded(w, h, radius, ss=4):
    """A rectangle with round corners, as a mask."""
    W, H, c = w * ss, h * ss, radius * ss
    y, x = np.mgrid[0:H, 0:W].astype(np.float64) + 0.5
    qx = np.clip(np.abs(x - W / 2.0) - (W / 2.0 - c), 0.0, None)
    qy = np.clip(np.abs(y - H / 2.0) - (H / 2.0 - c), 0.0, None)
    inside = np.clip(c - np.hypot(qx, qy) + 0.5, 0.0, 1.0)  # distance to the round edge, in fine pixels
    return inside.reshape(h, ss, w, ss).mean(axis=(1, 3))


def icon(n):
    """The mark on a dark tile with round corners: the icon of the program."""
    small = n <= 32
    light = mark(n, n, 0.49 * n, 0.51 * n, (0.30 if small else 0.27) * n, wide=not small,
                 streak=(0.35, 1.05) if n >= 80 else None,
                 floor=0.42 if n <= 16 else 0.24 if small else 0.14, ss=4)
    img = TILE[None, None, :] + light * (1.0 - TILE[None, None, :])
    out = np.empty((n, n, 4), np.uint8)
    out[..., :3] = to8(img, TILE)
    out[..., 3] = np.clip(np.rint(rounded(n, n, 0.22 * n) * 255.0), 0, 255).astype(np.uint8)
    return out


def panel(w, h):
    """The mark over the name on a dark panel with round corners: the side of the first-run wizard."""
    s = w / 662.0
    img = tall(w, h, s, h / 2.0 - 62 * s)
    img = TILE[None, None, :] + img * (1.0 - TILE[None, None, :])
    out = np.empty((h, w, 4), np.uint8)
    out[..., :3] = to8(img, TILE)
    out[..., 3] = np.clip(np.rint(rounded(w, h, 0.05 * w) * 255.0), 0, 255).astype(np.uint8)
    return out


def boot(h, splash):
    """The picture of the start of the system for a screen of h lines: (h, w, 3), in 8 bits.

    It is the picture of the splash screen of the interface at the size that the interface gives it
    on a screen of that height (it makes its picture of 1080 lines as high as the screen), cut to
    the width of the narrowest screen there is of that height: five to four. The program that draws
    it puts it in the middle of the screen, on black. The one of 1080 lines is that very picture.
    """
    w = 2 * int(0.625 * h)
    if h == 1080:
        left = (splash.shape[1] - w) // 2
        if splash[:, :left].any() or splash[:, left + w:].any():
            raise SystemExit("the splash screen has light outside the part that the start of the system shows")
        return splash[:, left:left + w]
    return to8(tall(w, h))


def line_mask(text, px, weight, ss=4):
    """A line of text with descenders as ink from 0 to 1, and the row of its baseline in it."""
    font = ImageFont.truetype(ARGS.jost, int(round(px * ss)), layout_engine=ImageFont.Layout.BASIC)
    font.set_variation_by_axes([weight])
    ascent, descent = font.getmetrics()
    w = -(-(int(np.ceil(font.getlength(text))) + 4 * ss) // ss) * ss
    base = -(-(ascent + 2 * ss) // ss) * ss
    h = -(-(base + descent + 2 * ss) // ss) * ss
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((2 * ss, base), text, font=font, fill=255, anchor="ls")
    a = np.asarray(img, dtype=np.float64).reshape(h // ss, ss, w // ss, ss).mean(axis=(1, 3)) / 255.0
    return a, base // ss


def notice(h):
    """The splash screen of a screen of h lines with the notice "one display" on a card under the name:
    a picture of a screen at its left, whose corner has the point of first light, and the notice in
    English and in Spanish. The rest is the splash screen as it is, so that only the card appears."""
    w = 2 * int(0.625 * h)
    s = h / 1080.0
    img = tall(w, h)
    x0, y0, pw, ph = 70 * s, 830 * s, w - 140 * s, 190 * s
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64) + 0.5
    r = 22 * s
    qx = np.clip(np.abs(xx - (x0 + pw / 2)) - (pw / 2 - r), 0, None)
    qy = np.clip(np.abs(yy - (y0 + ph / 2)) - (ph / 2 - r), 0, None)
    d = np.hypot(qx, qy)
    inside = np.clip(r - d + 0.5, 0, 1)
    img = img * (1 - inside[..., None]) + inside[..., None] * PLATE
    # its border, lit from the upper right as the rim of the mark is
    rim = np.clip(1 - np.abs(d - (r - 0.75)) / 0.9, 0, 1) * (inside > 0)
    lit = np.clip((xx - x0) / pw, 0, 1) * np.clip(1 - (yy - y0) / ph, 0, 1)
    a = rim * (0.12 + 0.45 * lit)
    img = img * (1 - a[..., None]) + SILVER[None, None, :] * a[..., None]
    # a screen: its frame, its stand, and the point of first light at its upper right corner
    cx, cy, fw, fh = x0 + 92 * s, y0 + ph / 2 - 6 * s, 92 * s, 58 * s
    fq = np.hypot(np.clip(np.abs(xx - cx) - (fw / 2 - 6 * s), 0, None), np.clip(np.abs(yy - cy) - (fh / 2 - 6 * s), 0, None))
    frame = np.clip(1 - np.abs(fq - 6 * s) / (1.6 * s), 0, 1)
    stand = ((np.abs(xx - cx) < 20 * s) & (np.abs(yy - (cy + fh / 2 + 12 * s)) < 1.4 * s)).astype(float)
    neck = ((np.abs(xx - cx) < 1.4 * s) & (yy > cy + fh / 2) & (yy < cy + fh / 2 + 12 * s)).astype(float)
    m = np.maximum(frame, np.maximum(stand, neck)) * 0.85
    img = img * (1 - m[..., None]) + SILVER[None, None, :] * m[..., None]
    q = np.hypot(xx - (cx + fw / 2 - 6 * s), yy - (cy - fh / 2 + 6 * s))
    dot = np.clip(np.clip(4.5 * s - q + 0.5, 0, 1) + 0.5 * np.exp(-q / (6 * s)) * (q > 4.5 * s), 0, 1)
    img = img * (1 - dot[..., None]) + WARM[None, None, :] * dot[..., None]
    y = y0 + 58 * s
    lines = ((NOTICE_EN[0], 23, NOTICE_TEXT, 400), (NOTICE_EN[1], 23, NOTICE_TEXT, 400),
             (NOTICE_ES[0], 19, NOTICE_SOFT, 340), (NOTICE_ES[1], 19, NOTICE_SOFT, 340))
    for i, (text, px, colour, weight) in enumerate(lines):
        ink, base = line_mask(text, px * s, weight)
        x, top = int(round(x0 + 180 * s)), int(round(y)) - base
        if x + ink.shape[1] > x0 + pw:
            raise SystemExit("the notice does not fit its card at %d lines" % h)
        part = img[top:top + ink.shape[0], x:x + ink.shape[1]]
        part[:] = part * (1 - ink[..., None]) + colour[None, None, :] * ink[..., None]
        y += (32 if i != 1 else 44) * s
    return to8(img)


def smaller(path):
    """A PNG written again by zopflipng, only where it is smaller and every pixel is the same, which is
    checked here (the notices: they go into the system, 5 MB as Pillow writes them)."""
    tmp = path + ".z.png"
    subprocess.run([ARGS.zopflipng, "-y", path, tmp], check=True, stdout=subprocess.DEVNULL)
    a = np.asarray(Image.open(path).convert("RGBA"))
    b = np.asarray(Image.open(tmp).convert("RGBA"))
    if a.shape != b.shape or not (a == b).all():
        raise SystemExit("zopflipng changed a pixel of " + path)
    if os.path.getsize(tmp) < os.path.getsize(path):
        os.replace(tmp, path)
    else:
        os.remove(tmp)


def main():
    global ARGS
    p = argparse.ArgumentParser()
    p.add_argument("--jost", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--zopflipng", help="zopflipng, to make the notices smaller without changing a pixel")
    ARGS = p.parse_args()

    # what Kodi installs in its media folder
    splash = to8(tall(1920, 1080))
    save("kodi/splash.jpg", splash, quality=98, subsampling=0, optimize=True)
    save("kodi/applaunch_screen.png", splash, optimize=True)
    banner = np.zeros((180, 320, 3))
    put(banner, wide(298, 82), 11, 49, NAME_TOP)
    save("kodi/banner.png", to8(banner), optimize=True)
    save("kodi/vendor_logo.png", white(wide(465, 128)), optimize=True)
    save("kodi/about/hades.png", white(wide(1308, 360)), optimize=True)
    save("kodi/vendor_icon.png", white(small_mark(128)), optimize=True)
    for n in (16, 32, 48, 80, 120, 256):
        save("kodi/icon%dx%d.png" % (n, n), icon(n), optimize=True)

    # the splash screen of the start of the system, one for each height of screen in the list
    for h in BOOT_HEIGHTS:
        save("boot/splash-%d.png" % h, boot(h, splash), optimize=True)

    # the settings add-on of LibreELEC: the logo at the foot of its window, and the side of its wizard
    save("settings/logo.png", white(wide(440, 128)), optimize=True)
    save("settings/wizard.png", panel(*WIZARD), optimize=True)

    # the notice "one display", for each height of the start (last: the pictures above are the same
    # whether or not these are made)
    for h in BOOT_HEIGHTS:
        save("notice/notice-%d.png" % h, notice(h), optimize=True)
        if ARGS.zopflipng:
            smaller(os.path.join(ARGS.out, "notice/notice-%d.png" % h))


if __name__ == "__main__":
    main()
