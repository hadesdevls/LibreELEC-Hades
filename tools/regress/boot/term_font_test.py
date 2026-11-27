# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# term_font_test.py <theme folder> (packages/tools/grub/theme/hades): the frame of GRUB's editor of an entry, drawn as
# gfxterm draws it, with the terminal's font of every theme of the boot menu (its terminal-font, Hades Mono, made by
# make-boot-theme.py), on every screen of the theme:
#  - the cells as gfxterm makes them (grub-core/term/gfxterm.c: as wide as the widest letter of ASCII,
#    calculate_normal_character_width(); as high as the highest glyph of the font, grub_font_get_max_char_height(); a
#    glyph drawn from its baseline at the font's ascent, grub_font_draw_glyph())
#  - the frame as normal/menu_text.c, draw_border(), writes it: U+250F, U+2501 x (width + 1), U+2513 on its top row,
#    U+2503 down both sides, U+2517, U+2501, U+251B on its bottom row; the width and rows of the editor of that screen
#  - every line glyph fills its cell where its lines run (U+2501 every column, U+2503 every row, each arm of a corner to
#    the edges of its cell), nothing of it outside its cell
#  - the top and the bottom line of the frame: the same rows lit in every column from the middle of one corner to the
#    middle of the other (no step, no gap); the sides: the same columns lit in every row from corner to corner; each
#    corner joins the line on either side (the band of the line runs into it)
#  - and a font with a corner one row off (U+250F drawn a row higher), and one whose U+2501 is a column short: both
#    must fail
# Prints a FAIL line for each failed check and "term-font: <n> checks passed, <m> failed" last.
import os
import struct
import sys

THEMES = sys.argv[1]
checks = fails = 0
quiet = False


def check(ok, text):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        if not quiet:
            print('FAIL ' + text)


def load(path):
    """The sections and glyphs of a pf2: {code: (w, h, xo, yo, adv, rows of bits)}"""
    d = open(path, 'rb').read()
    i, sec, chix = 0, {}, []
    while i < len(d):
        tag, ln = d[i:i + 4], struct.unpack('>I', d[i + 4:i + 8])[0]
        if tag == b'DATA':
            break
        body = d[i + 8:i + 8 + ln]
        if tag == b'CHIX':
            chix = [struct.unpack('>IBI', body[k:k + 9]) for k in range(0, ln, 9)]
        else:
            sec[tag] = body
        i += 8 + ln
    glyphs = {}
    for code, _flags, off in chix:
        w, h, xo, yo, adv = struct.unpack('>HHhhh', d[off:off + 10])
        bits = d[off + 10:off + 10 + (w * h + 7) // 8]
        rows = [[(bits[(y * w + x) // 8] >> (7 - (y * w + x) % 8)) & 1 for x in range(w)] for y in range(h)]
        glyphs[code] = (w, h, xo, yo, adv, rows)
    font = {k.decode(): struct.unpack('>H', v)[0] for k, v in sec.items() if k in (b'ASCE', b'DESC', b'MAXH', b'MAXW')}
    return font, glyphs


def frame(font, glyphs, columns, rows):
    """The frame of the editor on a screen of columns x rows cells, as gfxterm draws it: the lit pixels, and where each
    cell is"""
    cw = max(glyphs[c][4] for c in range(32, 127) if c in glyphs)
    ch = font['MAXH']
    ascent = font['ASCE']
    lit = set()

    def put(code, cx, cy):
        w, h, xo, yo, adv, bits = glyphs[code]
        left, top = cx * cw + xo, cy * ch + ascent - (h + yo)
        for y in range(h):
            for x in range(w):
                if bits[y][x]:
                    lit.add((left + x, top + y))
    # (draw_border(): the frame from column 1 to columns - 2, its top on row 3, its bottom on row rows - 6, as the
    # editor of GRUB 2.16 places it: menu_text.c, grub_menu_text_geometry; the exact place does not matter here)
    x0, x1, y0, y1 = 1, columns - 2, 3, rows - 6
    put(0x250F, x0, y0)
    for cx in range(x0 + 1, x1):
        put(0x2501, cx, y0)
    put(0x2513, x1, y0)
    for cy in range(y0 + 1, y1):
        put(0x2503, x0, cy)
        put(0x2503, x1, cy)
    put(0x2517, x0, y1)
    for cx in range(x0 + 1, x1):
        put(0x2501, cx, y1)
    put(0x251B, x1, y1)
    return lit, cw, ch, (x0, x1, y0, y1)


def judge(name, font, glyphs, columns, rows):
    ok = True
    cw = max(glyphs[c][4] for c in range(32, 127) if c in glyphs)
    ch = font['MAXH']
    # every line glyph in its cell, filling it where its lines run
    for code in (0x2501, 0x2503, 0x250F, 0x2513, 0x2517, 0x251B):
        w, h, xo, yo, adv, bits = glyphs[code]
        top = font['ASCE'] - (h + yo)
        if xo < 0 or top < 0 or xo + w > cw or top + h > ch:
            check(False, '%s: U+%04X is outside its cell of %dx%d (%dx%d at %d,%d)' % (name, code, cw, ch, w, h, xo, top))
            ok = False
    lit, cw, ch, (x0, x1, y0, y1) = frame(font, glyphs, columns, rows)
    # the band of a vertical line (its columns in a cell) and of a horizontal one (its rows in a cell)
    w, h, xo, yo, adv, bits = glyphs[0x2503]
    vband = sorted({xo + x for y in range(h) for x in range(w) if bits[y][x]})
    w, h, xo, yo, adv, bits = glyphs[0x2501]
    top = font['ASCE'] - (h + yo)
    hband = sorted({top + y for y in range(h) for x in range(w) if bits[y][x]})
    if not vband or not hband:
        check(False, '%s: U+2503 or U+2501 draws nothing' % name)
        return False
    # the top and the bottom line: the rows of the band of U+2501 lit, and no other, in every column from the side of
    # one corner's arm to that of the other (no step, no gap), and lit across the arm going down or up (the corner
    # closes)
    for row, what in ((y0, 'top'), (y1, 'bottom')):
        want = tuple(row * ch + r for r in hband)
        for x in range(x0 * cw + vband[0], x1 * cw + vband[-1] + 1):
            ys = tuple(sorted(y for y in range(row * ch, row * ch + ch) if (x, y) in lit))
            inside = x0 * cw + vband[0] <= x <= x0 * cw + vband[-1] or x1 * cw + vband[0] <= x <= x1 * cw + vband[-1]
            if inside:
                good = all(y in ys for y in want)
            else:
                good = ys == want
            if not good:
                check(False, '%s: the %s line %s at x %d (rows %s, the line %s)' %
                      (name, what, 'does not close its corner' if inside else ('has a gap' if not ys else 'steps'),
                       x, ys, want))
                return False
    # the sides: the columns of the band of U+2503, and no other, in every row from one corner's arm to the other's
    for col, what in ((x0, 'left'), (x1, 'right')):
        want = tuple(col * cw + c for c in vband)
        for y in range(y0 * ch + hband[0], y1 * ch + hband[-1] + 1):
            xs = tuple(sorted(x for x in range(col * cw, col * cw + cw) if (x, y) in lit))
            inside = y0 * ch + hband[0] <= y <= y0 * ch + hband[-1] or y1 * ch + hband[0] <= y <= y1 * ch + hband[-1]
            good = all(x in xs for x in want) if inside else xs == want
            if not good:
                check(False, '%s: the %s side %s at y %d (columns %s, the line %s)' %
                      (name, what, 'does not close its corner' if inside else ('has a gap' if not xs else 'steps'),
                       y, xs, want))
                return False
    return ok


def screens():
    for entry in sorted(os.listdir(THEMES)):
        theme = os.path.join(THEMES, entry, 'theme.txt')
        if 'x' not in entry or not os.path.isfile(theme):
            continue
        w, h = (int(v) for v in entry.split('x'))
        size = None
        for line in open(theme, encoding='utf-8'):
            if line.startswith('terminal-font: "Hades Mono Regular '):
                size = int(line.split()[-1].strip('"'))
        yield entry, w, h, size


seen = 0
for entry, w, h, size in screens():
    seen += 1
    path = os.path.join(THEMES, 'fonts', 'mono-%d.pf2' % size) if size else ''
    check(size is not None and os.path.isfile(path), '%s: its terminal-font is a Hades Mono of the theme' % entry)
    if not size or not os.path.isfile(path):
        continue
    font, glyphs = load(path)
    cw = max(glyphs[c][4] for c in range(32, 127) if c in glyphs)
    columns, rows = w // cw, h // font['MAXH']
    check(judge('%s (mono-%d, cells %dx%d, %dx%d of them)' % (entry, size, cw, font['MAXH'], columns, rows), font,
                glyphs, columns, rows), '%s: the frame of the editor straight and joined (mono-%d)' % (entry, size))
check(seen >= 16, 'every screen of the theme (%d)' % seen)

# The test fails where it must: a corner a row off, and a horizontal line a column short
font, glyphs = load(os.path.join(THEMES, 'fonts', 'mono-15.pf2'))
saved = checks, fails
quiet = True
g = dict(glyphs)
w, h, xo, yo, adv, bits = g[0x250F]
g[0x250F] = (w, h, xo, yo + 1, adv, bits)
off = not judge('mutant', font, g, 113, 42)
g = dict(glyphs)
w, h, xo, yo, adv, bits = g[0x2501]
g[0x2501] = (w, h, xo, yo, adv, [r[:-1] + [0] for r in bits])
short = not judge('mutant', font, g, 113, 42)
checks, fails = saved
quiet = False
check(off, 'a corner drawn a row higher is caught')
check(short, 'a horizontal line a column short is caught')
print('term-font: %d checks passed, %d failed' % (checks - fails, fails))
sys.exit(1 if fails else 0)
