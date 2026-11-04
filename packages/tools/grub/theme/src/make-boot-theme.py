#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Draws the boot menu of LibreELEC Hades on a UEFI computer: a theme of GRUB (gfxmenu) with the mark of
# the build (packages/mediacenter/kodi/brand/src/make-brand-images.py, whose drawing it uses), its type
# (Jost) and each entry of the menu explained in a line.
#
#   make-boot-theme.py --jost <Jost-VariableFont_wght.ttf> --brand <make-brand-images.py> --out <folder>
#                      --bios-out <folder> [--zopflipng <zopflipng>]
#
# writes into <folder> what GRUB reads from EFI/BOOT/hades: theme.cfg (which grub.cfg reads), fonts/
# (the fonts that GRUB writes its own text with, .pf2, and the licence of Jost), and a folder for each
# mode that has a theme of its own (SETS; the folder is named WIDTHxHEIGHT), with
# theme.txt, the background, one picture per entry (icons/<class>.png) and the pieces of the bar of the
# chosen entry and of the line of the countdown.
#
# What GRUB can do, and why the theme is made so:
#   - GRUB draws text only with fonts of one bit per pixel (grub-core/font/font.c), without smoothing.
#     The names and the explanations of the entries are therefore drawn here, smooth, as one picture per
#     entry, which GRUB puts in its row (the "icon" of the class of the entry, as wide as the row); the
#     title that GRUB would write itself has a transparent colour. GRUB writes only the countdown, and
#     its console, with fonts made here from Jost at sizes where one bit per pixel reads well.
#   - The sizes of a theme are pixels. Each mode in SETS has its own set, and the menu is drawn in the
#     mode that the firmware is in where it has one (write_cfg): in the common case nothing switches.
#
# Needs Python 3 with Pillow (FreeType) and NumPy, as the brand script does. The same files every time.
import argparse
import importlib.util
import os
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.dont_write_bytecode = True  # (the brand script is imported: no cache next to it)

ARGS = None
B = None  # the brand script, as a module

# The entries, as scripts/mkimage writes them, in its order: the class names the picture; the title is
# the one that the init, internal-disks and the installers know, and it is not changed here. What each
# line says is what the code does:
#   Run: initcall_blacklist=nvme_init,ahci_pci_driver_init from a USB drive, hades.otherdisks=hide on
#     an internal disk (scripts/mkimage, the init): the NVMe and SATA disks other than its own
#   Run (internal disks read-only): those disks mounted read-only (the init)
#   Live: /storage in memory (grub_live: LIVE=yes in the init)
#   Installer: two questions, "no" chosen first, before the disk is wiped (installer: do_install_quick)
ENTRIES = (
    ("hades-run", "Run", "The normal start. This computer's other NVMe and SATA disks stay hidden.", "Recommended"),
    ("hades-internal", "Run (internal disks read-only)", "Like Run, and shows the internal disks, mounted read-only.", None),
    ("hades-live", "Live", "Settings and add-ons stay in memory only, gone at power off.", None),
    ("hades-installer", "Installer", "Installs LibreELEC Hades on a disk you choose, erasing it. Asks twice first.", None),
)
COUNTDOWN = "The highlighted option starts in %d s"
HINT = "Arrow keys to choose, Enter to start"

# The modes that have a theme of their own: the ones that a firmware is commonly in when it starts a
# boot loader (the panel of a laptop or a monitor, a TV), so that the menu is drawn in the mode that the
# screen already has and nothing switches; and, first, the three that are asked for where the mode of
# the firmware has none (1920x1080, 1280x720, 1024x768).
FALLBACK = ((1920, 1080), (1280, 720), (1024, 768))
SETS = FALLBACK + ((1280, 800), (1280, 1024), (1360, 768), (1366, 768), (1440, 900), (1600, 900),
                   (1600, 1200), (1680, 1050), (1920, 1200), (2560, 1080), (2560, 1440), (2560, 1600),
                   (3840, 2160))


def geometry(W, H):
    """How the design for 1920x1080 is laid on a screen: s, the scale (the height, or the width where the
    screen is narrower than 16:9 allows: the column and the eclipse must both fit), and dy, the space
    above it (the design in the middle of the height)."""
    s = min(H / 1080.0, W / 1650.0)
    return s, (H - 1080.0 * s) / 2.0

TITLE = np.array([0.96, 0.97, 1.00])
DESC = np.array([0.62, 0.65, 0.71])
DIM = np.array([0.44, 0.46, 0.51])


def load_brand(path, jost):
    spec = importlib.util.spec_from_file_location("brand", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.ARGS = argparse.Namespace(jost=jost)
    return mod


def font(px, weight, ss=1):
    f = ImageFont.truetype(ARGS.jost, int(round(px * ss)), layout_engine=ImageFont.Layout.BASIC)
    f.set_variation_by_axes([weight])
    return f


def ink(text, px, weight, tracking=0.0, ss=4):
    """A line of text as ink from 0 to 1, drawn four times as large and brought down.

    Returns (mask, baseline row): the baseline is a row of the mask, so that text with descenders stands
    on it. Letter by letter, so that the tracking is exact to a quarter of a pixel; Pillow's basic layout,
    asked for by name (the other one is there only where its library is, and places letters elsewhere).
    """
    f = font(px, weight, ss)
    ascent, descent = f.getmetrics()
    track = tracking * px * ss
    at, x = [], 0.0
    for i, ch in enumerate(text):
        at.append(x)
        x += f.getlength(ch) + (track if i < len(text) - 1 else 0.0)
    pad = 2 * ss
    w = -(-(int(np.ceil(x)) + 2 * pad) // ss) * ss
    base = -(-(ascent + pad) // ss) * ss
    h = -(-(base + descent + pad) // ss) * ss
    img = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(img)
    for ch, cx in zip(text, at):
        d.text((pad + int(round(cx)), base), ch, font=f, fill=255, anchor="ls")
    a = np.asarray(img, dtype=np.float64).reshape(h // ss, ss, w // ss, ss).mean(axis=(1, 3)) / 255.0
    return a, base // ss


def lay(img, alpha, mask, x, base_y, base_row, colour):
    """Lays ink of one colour with its baseline at base_y: into an RGB picture (alpha None), or into a
    picture with alpha, whose colour is then kept premultiplied."""
    x, y = int(round(x)), int(round(base_y)) - base_row
    h, w = mask.shape
    H, W = img.shape[:2]
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
    if x0 >= x1 or y0 >= y1:
        return
    m = mask[y0 - y:y1 - y, x0 - x:x1 - x]
    img[y0:y1, x0:x1] = img[y0:y1, x0:x1] * (1.0 - m[..., None]) + colour[None, None, :] * m[..., None]
    if alpha is not None:
        alpha[y0:y1, x0:x1] = alpha[y0:y1, x0:x1] * (1.0 - m) + m


def rgba(premul, alpha):
    """Premultiplied colour and alpha to straight RGBA in 8 bits (what PNG keeps)."""
    out = np.zeros(premul.shape[:2] + (4,), np.uint8)
    a = np.clip(alpha, 0.0, 1.0)
    col = np.where(a[..., None] > 0, premul / np.maximum(a[..., None], 1e-9), 0.0)
    out[..., :3] = np.clip(np.rint(np.clip(col, 0, 1) * 255.0), 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(np.rint(a * 255.0), 0, 255).astype(np.uint8)
    return out


def save(rel, array, out=None):
    path = os.path.join(out or ARGS.out, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(array).save(path, optimize=True)
    smaller(path)


def smaller(path):
    """The PNG written again by zopflipng (--zopflipng), only where it is smaller and every pixel is the
    same, which is checked here. Without zopflipng the file stays as Pillow wrote it."""
    if not ARGS.zopflipng:
        return
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


# ---- the fonts that GRUB writes with (PFF2: grub-core/font/font.c reads them)

def pf2(name, px, weight, path, chars=range(32, 127)):
    """A font of GRUB: one bit per pixel, drawn by FreeType without smoothing (Pillow's mode "1").

    Sections FILE, NAME, FAMI, WEIG, SLAN, PTSZ, MAXW, MAXH, ASCE, DESC, CHIX (code, flags, offset of the
    glyph in the file, ascending codes) and DATA (to the end of the file). A glyph: width, height, x
    offset, y offset of its bottom row from the baseline, advance, then its rows one after the other,
    one bit per pixel, padded only at the end.
    """
    f = font(px, weight)
    ascent, descent = f.getmetrics()
    glyphs = []
    for code in chars:
        ch = chr(code)
        adv = int(round(f.getlength(ch)))
        pad = px
        img = Image.new("1", (adv + 3 * pad, ascent + descent + 2 * pad), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        base = pad + ascent
        d.text((pad, base), ch, font=f, fill=1, anchor="ls")
        box = img.getbbox()
        if box is None:
            glyphs.append((code, 0, 0, 0, 0, adv, b""))
            continue
        bits = np.asarray(img.crop(box), dtype=np.uint8).reshape(-1)
        bits = np.concatenate([bits, np.zeros((-len(bits)) % 8, np.uint8)])
        glyphs.append((code, box[2] - box[0], box[3] - box[1], box[0] - pad, base - box[3], adv,
                       np.packbits(bits).tobytes()))

    def section(tag, payload):
        return tag + struct.pack(">I", len(payload)) + payload

    head = b"".join([
        section(b"FILE", b"PFF2"),
        section(b"NAME", name.encode() + b"\0"),
        section(b"FAMI", b"Jost\0"),
        section(b"WEIG", (b"bold" if weight >= 600 else b"normal") + b"\0"),
        section(b"SLAN", b"normal\0"),
        section(b"PTSZ", struct.pack(">H", px)),
        section(b"MAXW", struct.pack(">H", max(g[1] for g in glyphs))),
        section(b"MAXH", struct.pack(">H", max(g[2] for g in glyphs))),
        section(b"ASCE", struct.pack(">H", ascent)),
        section(b"DESC", struct.pack(">H", descent)),
    ])
    at = len(head) + 8 + 9 * len(glyphs) + 8
    index, blob = b"", b""
    for code, w, h, xo, yo, adv, data in glyphs:
        index += struct.pack(">IBI", code, 0, at)
        g = struct.pack(">HHhhh", w, h, xo, yo, adv) + data
        blob += g
        at += len(g)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(head + section(b"CHIX", index) + b"DATA" + struct.pack(">I", 0xFFFFFFFF) + blob)


# ---- the bar of the chosen entry

def rounded_end(h, r, style, s, dot, left):
    """One end of the bar, as high as the row (GRUB never scales it in height): a rounded rectangle,
    its border lit from the upper right as the rim of the mark is, and at the left end the point of
    first light. Returns premultiplied colour and alpha, (h, w)."""
    ss = 4
    W = left + 8 + r if dot else r
    yy, xx = np.mgrid[0:h * ss, 0:W * ss].astype(np.float64) + 0.5
    xx /= ss
    yy /= ss
    if not dot:
        xx = r - xx  # the right end: mirrored
    qx = np.clip(r - xx, 0, None)
    qy = np.clip(np.abs(yy - h / 2.0) - (h / 2.0 - r), 0, None)
    dist = np.hypot(qx, qy)
    inside = np.clip(r - dist + 0.5, 0, 1)
    fill_col, fill_a = style["fill"]
    border_col, border_a = style["border"]
    col = inside[..., None] * fill_col[None, None, :] * fill_a
    a = inside * fill_a
    top_light = np.clip(1.0 - yy / h, 0, 1)
    ring = np.clip(1.0 - np.abs(dist - (r - 0.75)) / 0.75, 0, 1) * (inside > 0)
    strength = 1.0 if dot else style.get("right_light", 1.6)
    bw = np.clip(ring * border_a * (0.45 + 0.55 * top_light) * strength, 0, 1)
    col = col * (1 - bw[..., None]) + border_col[None, None, :] * bw[..., None]
    a = a * (1 - bw) + bw
    if dot:
        cx, cy, rad = left * 0.52, h / 2.0, max(2.2, 4.2 * s)
        q = np.hypot(xx - cx, yy - cy)
        for m in (0.33 * np.exp(-np.clip(q - rad, 0, None) / (3.0 * s)) * (q > rad), np.clip(rad - q + 0.5, 0, 1)):
            col = col * (1 - m[..., None]) + B.WARM[None, None, :] * m[..., None]
            a = a * (1 - m) + m
    return col.reshape(h, ss, W, ss, 3).mean(axis=(1, 3)), a.reshape(h, ss, W, ss).mean(axis=(1, 3))


def plate(h, s, style):
    """The pieces of the bar: select_w (the left end, with the point), select_c (a column of its middle,
    which GRUB makes as wide as the row) and select_e (the right end). Only the three: GRUB leaves out
    the pieces that are not there, so the bar adds nothing above or below the row."""
    r = max(2, int(round(style["radius"] * s)))
    left = int(round(style["left"] * s))
    wc, wa = rounded_end(h, r, style, s, True, left)
    ec, ea = rounded_end(h, r, style, s, False, left)
    return rgba(wc, wa), rgba(wc[:, -1:], wa[:, -1:]), rgba(ec, ea)


def row_picture(entry, w, h, s, d):
    """The picture of one entry: its name and the line that explains it, smooth, on nothing."""
    cls, title, desc, tag = entry
    img = np.zeros((h, w, 3))
    al = np.zeros((h, w))
    tm, tb = ink(title, d["title_px"] * s, 500, 0.01)
    dm, db = ink(desc, d["desc_px"] * s, 340, 0.005)
    if d["two_lines"]:
        tx, ty, dx, dy = d["pad"] * s, d["title_base"] * s, d["pad"] * s, d["desc_base"] * s
    else:
        tx = d["pad"] * s
        ty = dy = h / 2.0 + d["title_px"] * s * 0.36
        dx = d["desc_x"] * s
    if dx + dm.shape[1] > w or tx + tm.shape[1] > (dx if not d["two_lines"] else w):
        raise SystemExit("%s: the text of %s does not fit its row" % (ARGS.out, cls))
    lay(img, al, tm, tx, ty, tb, TITLE)
    lay(img, al, dm, dx, dy, db, DESC)
    if tag:
        gm, gb = ink(tag.upper(), d["tag_px"] * s, 520, 0.16)
        lay(img, al, gm, tx + tm.shape[1] + 18 * s, ty - (d["title_px"] - d["tag_px"]) * s * 0.30, gb, B.WARM * 0.92)
    return rgba(img, al)


def background(W, H, s, dy, d):
    """The picture behind the menu, in 8 bits: black where there is no light (an OLED panel stays off)."""
    img = np.zeros((H, W, 3))
    # the eclipse large at the right (on a narrow screen, as far right as it must be to leave the column
    # free), the name at the upper left, the heading over the entries
    # (on a screen narrower than 16:10 the column leaves it less room: it is drawn smaller, whole, with
    # its point of first light clear of the edge)
    end = (d["menu_x"] + d["menu_w"]) * s
    r = min(d["mark_r"] * s, (W - end - 30 * s) / 2.12)
    cx = max(W - d["mark_right"] * s, end + 1.12 * r)
    B.mark_on(img, cx, d["mark_y"] * s + dy, r)
    wm = B.wide(int(round(330 * s)), int(round(92 * s)))
    x0, y0 = int(round(d["col_x"] * s)), int(round(84 * s + dy))
    part = img[y0:y0 + wm.shape[0], x0:x0 + wm.shape[1]]
    part[:] = part * (1 - wm[..., None]) + B.NAME_TOP[None, None, :] * wm[..., None] * 0.92
    head, hb = ink("Choose how to start", 54 * s, 300, 0.01)
    lay(img, None, head, d["col_x"] * s, d["head_base"] * s + dy, hb, TITLE)
    if d.get("hint"):
        hm, hbr = ink(HINT, d["hint_px"] * s, 360, 0.02)
        hx = (W - hm.shape[1]) / 2.0 if d.get("hint_center", True) else d["col_x"] * s
        lay(img, None, hm, hx, d["hint_base"] * s + dy, hbr, DIM)
    return B.to8(img)


# "Horizon", the look chosen on 2026-11-03 out of three (the other two: the composition of the splash
# screen with a table under it, and the splash screen with a short chooser under a line of light): a
# title card, the eclipse large at the right, its light falling on a column at the left with a heading
# and the entries in two lines each. Sizes for 1080 lines.
LOOK = dict(mark_right=420, mark_y=500, mark_r=250, col_x=150, head_base=300,
            menu_x=118, menu_y=360, menu_w=1030, row_h=112, row_gap=12, two_lines=True,
            title_px=36, desc_px=25, tag_px=15, pad=16, title_base=48, desc_base=88,
            plate=dict(fill=(np.array([0.62, 0.66, 0.76]), 0.075), border=(np.array([0.86, 0.90, 1.0]), 0.36),
                       radius=16, left=44, right_light=2.2),
            count_y=890, count_px=26, count_left=162, bar=(162, 944, 420, 2),
            hint=True, hint_px=21, hint_base=1010, hint_center=False)


def theme_set(name, W, H, d):
    """The folder of one size of screen; returns the name of its font."""
    s, dy = geometry(W, H)
    save(name + "/background.png", background(W, H, s, dy, d))
    row_h = int(round(d["row_h"] * s))
    wpc, cpc, epc = plate(row_h, s, d["plate"])
    save(name + "/select_w.png", wpc)
    save(name + "/select_c.png", cpc)
    save(name + "/select_e.png", epc)
    mw = int(round(d["menu_w"] * s))
    mx = int(round(d["menu_x"] * s)) if "menu_x" in d else (W - mw) // 2
    if mx < 0 or mx + mw > W:
        raise SystemExit("%s: the menu is wider than the screen" % name)
    # (GRUB puts the picture of an entry after the left end of the bar, and before its right end; and
    # makes a menu at least as wide as its pictures and "Typical OS" in its font (gui_list.c), which
    # the picture leaves out, so that the menu is as wide as drawn)
    count_px = max(16, int(round(d["count_px"] * s)))
    f = font(count_px, 400)
    typical = sum(int(round(f.getlength(ch))) for ch in "Typical OS")
    iw = mw - wpc.shape[1] - epc.shape[1] - typical
    for e in ENTRIES:
        save("%s/icons/%s.png" % (name, e[0]), row_picture(e, iw, row_h, s, d))
    gap = int(round(d["row_gap"] * s))
    fname = "Jost Regular %d" % count_px
    lines = [
        "# LibreELEC Hades: the boot menu on a screen of %dx%d (written by make-boot-theme.py)" % (W, H),
        'title-text: ""',
        'desktop-image: "background.png"',
        'desktop-color: "#000000"',
        'terminal-font: "%s"' % fname,
        "+ boot_menu {",
        "  left = %d" % mx, "  top = %d" % int(round(d["menu_y"] * s + dy)),
        "  width = %d" % mw, "  height = %d" % (len(ENTRIES) * (row_h + gap)),
        '  item_font = "%s"' % fname, '  selected_item_font = "%s"' % fname,
        "  # (the title is in the picture of the entry: GRUB's own writing of it is transparent, and the",
        "  # picture is narrower by what GRUB adds to the width of a menu, \"Typical OS\" in its font)",
        '  item_color = "#00000000"', '  selected_item_color = "#00000000"',
        "  icon_width = %d" % iw, "  icon_height = %d" % row_h,
        "  item_icon_space = 0", "  item_height = %d" % row_h, "  item_padding = 0",
        "  item_spacing = %d" % gap,
        '  selected_item_pixmap_style = "select_*.png"',
        "  scrollbar = false",
        "}",
    ]
    if d.get("count_left"):
        cl, cw, align = int(round(d["count_left"] * s)), int(round(900 * s)), "left"
    else:
        cl, cw, align = 0, W, "center"
    lines += [
        "+ label {",
        '  id = "__timeout__"', "  left = %d" % cl, "  top = %d" % int(round(d["count_y"] * s + dy)),
        "  width = %d" % cw, "  height = %d" % int(count_px * 1.6), '  align = "%s"' % align,
        '  color = "#9ea4b0"', '  font = "%s"' % fname, '  text = "%s"' % COUNTDOWN,
        "}",
    ]
    if d.get("bar"):
        # GRUB draws a progress bar at least 28 pixels high (gui_progress_bar.c): the line of light is
        # a thin line in the middle of a column of that height, with nothing around it
        bx, by, bw, bh = d["bar"]
        bx = (W - bw * s) / 2.0 if align == "center" else bx * s
        th = max(1, int(round(bh * s)))
        for piece, col, a in (("bar_c.png", (0.86, 0.90, 1.0), 0.16), ("glow_c.png", (0.92, 0.95, 1.0), 0.95)):
            column = np.zeros((28, 1, 4), np.uint8)
            column[14 - th // 2:14 - th // 2 + th, 0, :3] = [int(round(c * 255)) for c in col]
            column[14 - th // 2:14 - th // 2 + th, 0, 3] = int(round(a * 255))
            save(name + "/" + piece, column)
        lines += [
            "+ progress_bar {",
            '  id = "__timeout__"', "  left = %d" % int(round(bx)), "  top = %d" % (int(round(by * s + dy)) - 14),
            "  width = %d" % int(round(bw * s)), "  height = 28",
            '  bar_style = "bar_*.png"', '  highlight_style = "glow_*.png"',
            "  # (no text: with one, even empty, GRUB makes the bar as high as its font at least, and",
            "  # draws the picture of the line that much lower and thicker (gui_progress_bar.c))",
            "}",
        ]
    with open(os.path.join(ARGS.out, name, "theme.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    pf2(fname, count_px, 400, os.path.join(ARGS.out, "fonts", "jost-%d.pf2" % count_px))
    return fname


# The menu of syslinux on a BIOS computer: vesamenu.c32 at 1024x768, the mode that the VESA BIOS of every
# graphics card has. It writes with the font of the card (8 by 16 pixels: a cell of 8 pixels has no room for
# Jost), the entries in the column of the GRUB menu, and under them the line of the chosen entry (TEXT HELP
# in syslinux.cfg). In vesamenu the row of the help is shifted by VSHIFT and its last row is not (menumain.c:
# HELPMSG_ROW, HELPMSGEND_ROW), so the last row is given as it is on the screen.
BIOS_VSHIFT = 13      # the first entry on row 4 + 13 = 17: 272 pixels, where the GRUB menu starts
BIOS_HSHIFT = 13      # its text in column 16: 128 pixels, as the titles of the GRUB menu
BIOS_HELP_ROW = 22    # on the screen; the menu has four rows, 17 to 20
BIOS_HELP_INDENT = 16  # columns before the help text (vesamenu writes it from the first column)


def write_bios(d):
    W, H = 1024, 768
    s, dy = geometry(W, H)
    look = dict(d)
    look["hint"] = False
    save("bios-1024x768.png", background(W, H, s, dy, look), out=ARGS.bios_out)
    lines = [
        "# LibreELEC Hades: the look of the boot menu on a BIOS computer (vesamenu.c32; written by",
        "# packages/tools/grub/theme/src/make-boot-theme.py). syslinux.cfg includes it; without it, or",
        "# without a mode of 1024x768, the menu is the text that syslinux.cfg writes.",
        "PATH /hades/",
        "UI vesamenu.c32",
        "MENU RESOLUTION 1024 768",
        "MENU BACKGROUND /hades/bios-1024x768.png",
        "MENU TITLE",
        "MENU WIDTH 56",
        "MENU MARGIN 0",
        "MENU ROWS 4",
        "MENU VSHIFT %d" % BIOS_VSHIFT,
        "MENU HSHIFT %d" % BIOS_HSHIFT,
        "MENU HELPMSGROW %d" % (BIOS_HELP_ROW - BIOS_VSHIFT),
        "MENU HELPMSGENDROW %d" % BIOS_HELP_ROW,
        "MENU TIMEOUTROW %d" % (BIOS_HELP_ROW + 3 - BIOS_VSHIFT),
        "MENU TABMSGROW %d" % (BIOS_HELP_ROW + 6 - BIOS_VSHIFT),
        "MENU CMDLINEROW %d" % (BIOS_HELP_ROW + 6 - BIOS_VSHIFT),
        "MENU TABMSG " + HINT,
        "MENU AUTOBOOT " + COUNTDOWN.replace("%d", "#"),
        "MENU COLOR screen 0 #00000000 #00000000 none",
        "MENU COLOR border 0 #00000000 #00000000 none",
        "MENU COLOR title 0 #00000000 #00000000 none",
        "MENU COLOR unsel 0 #ffe6e8ee #00000000 none",
        "MENU COLOR hotkey 0 #ffe6e8ee #00000000 none",
        "MENU COLOR sel 0 #ffffffff #26aab6d0 none",
        "MENU COLOR hotsel 0 #ffffffff #26aab6d0 none",
        "MENU COLOR help 0 #ffa0a6b2 #00000000 none",
        "MENU COLOR timeout_msg 0 #ff9ea4b0 #00000000 none",
        "MENU COLOR timeout 0 #ffe6c8a0 #00000000 none",
        "MENU COLOR tabmsg 0 #ff70747e #00000000 none",
    ]
    with open(os.path.join(ARGS.bios_out, "menu.cfg"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def write_cfg(made):
    """theme.cfg, which grub.cfg reads.

    First the terminal is taken in the mode that the firmware is in (gfxmode=auto keeps it: efi_gop.c),
    and if that mode has a theme, that is the menu: nothing switches. Only where it has none are the
    three modes of FALLBACK asked for; and where the terminal did not get one of them either, it goes
    back to the console: the text menu, in the mode of the firmware, as before. The mode is the one that
    the terminal says it got (gfxterm_mode, grub-9000; a GRUB that does not say it gets the text menu).
    Every entry of grub.cfg starts with "terminal_output console", which gives the screen back to the
    mode that the firmware had where GRUB switched it (grub_video_gop_fini), so that the kernel starts in
    the mode it started in before, in every case."""
    lines = [
        "# LibreELEC Hades: the look of the boot menu (packages/tools/grub/theme, written by",
        "# make-boot-theme.py). A theme is used only in the mode it is drawn for, and the mode of the",
        "# firmware is kept where it has one.",
    ]
    fonts = []
    for _, _, _, fname in made:
        f = "jost-%s.pf2" % fname.split()[-1]
        if f not in fonts:
            fonts.append(f)

    def pick(indent, sets):
        out = []
        for i, (name, W, H, _) in enumerate(sets):
            out.append('%s%s [ "${gfxterm_mode}" = "%dx%d" ]; then' % (indent, "if" if i == 0 else "elif", W, H))
            out.append('%s  set theme="${prefix}/hades/%s/theme.txt"' % (indent, name))
        return out

    lines.append('if loadfont "${prefix}/hades/fonts/%s"; then' % fonts[0])
    lines += ['  loadfont "${prefix}/hades/fonts/%s"' % f for f in fonts[1:]]
    # (the terminal writes with the font that gfxterm_font names, or else with the last one loaded,
    # here the largest: with it the terminal does not start, "glyphs too large to fit on screen"
    # (gfxterm.c), measured in QEMU. The smallest one, which fits every mode of the list.)
    smallest = min((int(f.split()[-1]), f) for _, _, _, f in made)[1]
    lines += ['  set gfxterm_font="%s"' % smallest, "  set gfxmode=auto", "  if terminal_output gfxterm; then"]
    lines += pick("    ", made)
    lines += ["    fi", "  fi", '  if [ -z "${theme}" ]; then', "    terminal_output console",
              "    set gfxmode=%s" % ",".join("%dx%d" % m for m in FALLBACK),
              "    if terminal_output gfxterm; then"]
    lines += pick("      ", [m for m in made if (m[1], m[2]) in FALLBACK])
    lines += ["      else", "        terminal_output console", "      fi", "    fi", "  fi", "fi"]
    with open(os.path.join(ARGS.out, "theme.cfg"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    global ARGS, B
    p = argparse.ArgumentParser()
    p.add_argument("--jost", required=True)
    p.add_argument("--brand", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--bios-out", required=True, help="the folder of the BIOS menu (syslinux)")
    p.add_argument("--zopflipng", help="zopflipng, to make every picture smaller without changing a pixel")
    ARGS = p.parse_args()
    B = load_brand(ARGS.brand, ARGS.jost)
    d = LOOK
    made = []
    for W, H in SETS:
        name = "%dx%d" % (W, H)
        made.append((name, W, H, theme_set(name, W, H, d)))
    write_cfg(made)
    write_bios(d)
    shutil.copyfile(os.path.join(os.path.dirname(os.path.abspath(ARGS.jost)), "OFL.txt"),
                    os.path.join(ARGS.out, "fonts", "OFL.txt"))
    for root, _, files in sorted(os.walk(ARGS.out)):
        for f in sorted(files):
            path = os.path.join(root, f)
            print("%-40s %8d bytes" % (os.path.relpath(path, ARGS.out), os.path.getsize(path)))


if __name__ == "__main__":
    main()
