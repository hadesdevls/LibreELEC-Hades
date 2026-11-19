#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026-present Hades
#
# Draws the pictures of the public documents of LibreELEC Hades (README.md and docs/): the header with the mark
# of the build, the version badge, and the diagram of the path of a video, each in a version for a light page
# and one for a dark page (GitHub shows the one that fits the reader's theme, with <picture>).
#
#   make-docs-images.py --tree <root of the LibreELEC Hades tree> --version 1.1 [--out docs/images]
#
# The header is drawn by the very code that draws the splash screens of the build
# (packages/mediacenter/kodi/brand/src/make-brand-images.py: the eclipse at the instant of its first light,
# and the name in Jost), so the mark here is the mark of the build, not a copy of it:
#   - header-dark.png: the mark in its light, on nothing (its transparency is its light), for a dark page;
#   - header-light.png: the same picture in a frame of black, for a light page: the mark is light next to black,
#     and a light page has no black of its own.
# The badge and the diagram are SVG written here, with the colours of the mark (its silver light and the warm
# point of first light) and the system's sans-serif type, so that GitHub's own rendering draws them sharp at
# any size. Needs Python 3 with Pillow (FreeType) and NumPy for the header; the SVGs need nothing.
import argparse
import importlib.util
import os
import types

import numpy as np
from PIL import Image

ARGS = None


def brand_module():
    path = os.path.join(ARGS.tree, 'packages', 'mediacenter', 'kodi', 'brand', 'src', 'make-brand-images.py')
    spec = importlib.util.spec_from_file_location('hades_brand', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.ARGS = types.SimpleNamespace(
        jost=os.path.join(ARGS.tree, 'packages', 'mediacenter', 'kodi', 'brand', 'src', 'Jost-VariableFont_wght.ttf'))
    return mod


def save_png(name, rgba):
    path = os.path.join(ARGS.out, name)
    Image.fromarray(rgba, 'RGBA').save(path, optimize=True)
    print('%-28s %4dx%-4d %8d bytes' % (name, rgba.shape[1], rgba.shape[0], os.path.getsize(path)))


def headers(b):
    w, h, s = 1600, 560, 0.82
    light = b.tall(w, h, s, h / 2.0 - 4 * s)  # (h, w, 3), 0 where there is no light
    # for a dark page: the light is the picture, and how much light, its opacity
    alpha = light.max(axis=2)
    # the far glow reaches the top edge of the picture: it fades out over the top lines instead of being cut
    fade = b.smoothstep(0.0, 150.0, np.arange(h, dtype=np.float64) + 0.5)[:, None]
    alpha = alpha * fade
    light = light * fade[..., None]
    colour = np.where(alpha[..., None] > 1e-6, light / np.maximum(alpha[..., None], 1e-6), 0.0)
    out = np.empty((h, w, 4), np.uint8)
    out[..., :3] = b.to8(colour)
    out[..., 3] = b.to8(alpha)
    save_png('header-dark.png', out)
    # for a light page: the same picture in a frame of black with round corners
    tile = b.TILE[None, None, :] + light * (1.0 - b.TILE[None, None, :])
    out = np.empty((h, w, 4), np.uint8)
    out[..., :3] = b.to8(tile, b.TILE)
    out[..., 3] = np.clip(np.rint(b.rounded(w, h, 0.045 * h) * 255.0), 0, 255).astype(np.uint8)
    save_png('header-light.png', out)


FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"

THEMES = {
    'dark': dict(card='#161b22', edge='#30363d', title='#e6edf3', text='#9198a1', line='#6e7681',
                 accent='#ffdeb3', accent_text='#ffdeb3', hades_card='#1d1a16', hades_edge='#6b5a43',
                 note='#9198a1', note_edge='#3d444d'),
    'light': dict(card='#f6f8fa', edge='#d1d9e0', title='#1f2328', text='#59636e', line='#8c959f',
                  accent='#b3742f', accent_text='#8a5419', hades_card='#fbf6ef', hades_edge='#d8b98f',
                  note='#59636e', note_edge='#d1d9e0'),
}


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def signal_path(theme):
    c = THEMES[theme]
    W, H = 1000, 330
    cards = [
        ('THE FILM', ['Dolby Vision 5, 7 and 8.1,', 'HDR10+, HDR10, HLG, SDR', 'and 3D, from a file,', 'a disc or a stream']),
        ('HADES, ON YOUR PC', ['decodes it on the graphics,', 'rebuilds the full picture', 'from both layers of a disc,', 'with metadata for each frame']),
        ('HDMI', ['the Dolby Vision signal,', 'its metadata inside the', 'picture, which has to', 'arrive bit for bit']),
        ('YOUR TV', ['in its Dolby Vision mode,', 'maps each frame to its', 'own screen, as with a', 'Dolby Vision player']),
    ]
    x0, y0, cw, ch, gap = 16, 24, 220, 168, 32
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" role="img" '
             'aria-labelledby="t d">' % (W, H, W, H),
             '<title id="t">How a video reaches your TV</title>',
             '<desc id="d">The film is decoded by the graphics, Hades rebuilds the full picture with its '
             'enhancement layer and the metadata of every frame, HDMI carries it as the Dolby Vision signal, and '
             'the TV maps it in its own Dolby Vision mode. If anything does not qualify, the video plays as HDR10 '
             'or SDR, and the player information says why.</desc>',
             '<g font-family="%s">' % FONT]
    for i, (title, lines) in enumerate(cards):
        x = x0 + i * (cw + gap)
        hades = i == 1
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" rx="12" fill="%s" stroke="%s" stroke-width="%s"/>'
                     % (x, y0, cw, ch, c['hades_card'] if hades else c['card'],
                        c['hades_edge'] if hades else c['edge'], '1.5' if hades else '1'))
        parts.append('<text x="%d" y="%d" font-size="12" font-weight="600" letter-spacing="1.2" fill="%s">%s</text>'
                     % (x + 18, y0 + 32, c['accent_text'] if hades else c['text'], esc(title)))
        for j, line in enumerate(lines):
            parts.append('<text x="%d" y="%d" font-size="14" fill="%s">%s</text>'
                         % (x + 18, y0 + 66 + j * 23, c['title'], esc(line)))
        if i < len(cards) - 1:
            ax, ay = x + cw + 4, y0 + ch / 2.0
            bx = x + cw + gap - 4
            parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="1.5"/>'
                         % (ax, ay, bx - 7, ay, c['line']))
            parts.append('<path d="M %.1f %.1f L %.1f %.1f L %.1f %.1f Z" fill="%s"/>'
                         % (bx, ay, bx - 8, ay - 4.5, bx - 8, ay + 4.5, c['line']))
    # the eclipse, small, at the corner of the card of Hades: the point of first light
    hx = x0 + (cw + gap)
    cx, cy, r = hx + cw - 26, y0 + 27, 9
    parts.append('<circle cx="%d" cy="%d" r="%d" fill="none" stroke="%s" stroke-width="1.6"/>' % (cx, cy, r, c['accent']))
    parts.append('<circle cx="%.1f" cy="%.1f" r="2.6" fill="%s"/>' % (cx + r * 0.75, cy - r * 0.66, c['accent']))
    # the way back: from Hades, down to a note
    bx, by = hx + cw / 2.0, y0 + ch
    ny = by + 46
    parts.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="1.5" stroke-dasharray="4 4"/>'
                 % (bx, by + 4, bx, ny - 8, c['line']))
    parts.append('<path d="M %.1f %.1f L %.1f %.1f L %.1f %.1f Z" fill="%s"/>'
                 % (bx, ny - 1, bx - 4.5, ny - 9, bx + 4.5, ny - 9, c['line']))
    nw, nh = 700, 58
    nx = bx - 110
    parts.append('<rect x="%.1f" y="%d" width="%d" height="%d" rx="10" fill="none" stroke="%s" stroke-width="1" '
                 'stroke-dasharray="5 4"/>' % (nx, ny, nw, nh, c['note_edge']))
    parts.append('<text x="%.1f" y="%d" font-size="14.5" fill="%s"><tspan font-weight="600" fill="%s">'
                 'If the video, the TV, the link or the driver does not qualify,</tspan></text>'
                 % (nx + 18, ny + 24, c['title'], c['title']))
    parts.append('<text x="%.1f" y="%d" font-size="14.5" fill="%s">the video plays as HDR10 or SDR, with no black '
                 'screen, and the player information says why.</text>' % (nx + 18, ny + 45, c['note']))
    parts.append('</g></svg>')
    path = os.path.join(ARGS.out, 'signal-path-%s.svg' % theme)
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(parts) + '\n')
    print('%-28s %8d bytes' % (os.path.basename(path), os.path.getsize(path)))


def badge():
    # One badge for both themes: it has its own background, black with the light of the mark.
    W, H, split = 196, 28, 150
    s = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" '
         'aria-label="LibreELEC Hades %s">' % (W, H, W, H, ARGS.version),
         '<title>LibreELEC Hades %s</title>' % ARGS.version,
         '<clipPath id="c"><rect width="%d" height="%d" rx="7"/></clipPath>' % (W, H),
         '<g clip-path="url(#c)">',
         '<rect width="%d" height="%d" fill="#0b0d12"/>' % (split, H),
         '<rect x="%d" width="%d" height="%d" fill="#ffdeb3"/>' % (split, W - split, H),
         '</g>',
         '<rect x="0.5" y="0.5" width="%d" height="%d" rx="6.5" fill="none" stroke="#3a3f4a" stroke-opacity="0.6"/>'
         % (W - 1, H - 1),
         '<circle cx="15" cy="14" r="6.2" fill="none" stroke="#dbe6ff" stroke-width="1.5"/>',
         '<circle cx="19.6" cy="9.9" r="2" fill="#ffdeb3"/>',
         '<g font-family="%s" font-size="12.5">' % FONT,
         '<text x="30" y="18.6" fill="#a7b0c0" textLength="56" lengthAdjust="spacingAndGlyphs">LibreELEC</text>',
         '<text x="91" y="18.6" fill="#ffffff" font-weight="600" letter-spacing="1.4" textLength="50" '
         'lengthAdjust="spacingAndGlyphs">HADES</text>',
         '<text x="%.1f" y="18.6" fill="#1a140c" font-weight="700" text-anchor="middle">%s</text>'
         % ((split + W) / 2.0, ARGS.version),
         '</g></svg>']
    path = os.path.join(ARGS.out, 'version.svg')
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(s) + '\n')
    print('%-28s %8d bytes' % ('version.svg', os.path.getsize(path)))


def main():
    global ARGS
    p = argparse.ArgumentParser()
    p.add_argument('--tree', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--out', default=None)
    p.add_argument('--no-header', action='store_true', help='only the SVGs (no Pillow and NumPy needed)')
    ARGS = p.parse_args()
    ARGS.out = ARGS.out or os.path.join(ARGS.tree, 'docs', 'images')
    os.makedirs(ARGS.out, exist_ok=True)
    if not ARGS.no_header:
        headers(brand_module())
    badge()
    for theme in ('dark', 'light'):
        signal_path(theme)


if __name__ == '__main__':
    main()
