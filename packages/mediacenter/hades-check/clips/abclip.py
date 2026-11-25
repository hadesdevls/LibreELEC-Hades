# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""The clip of the A/B comparison of native Dolby Vision in "Check this setup" (hades-check-ab-dv81.mkv): the
picture where what Hades sends by default and what licensed players send can look different on a TV, and
where the TV's own player (the clip played from a USB drive) is the reference.

What differs, as fandangos measured it on an LG C1 and a Hisense with a colorimeter (CroqueMr/libreelec-CB1
#212) and as two licensed players were seen to send (a Homatics R 4K Plus, read on 2026-11-24; an OPPO UDP-203,
fandangos/oppo-dv-mkv fix 24): a positive lift of the trims of level 2 (trim_offset above 2048) is applied by
the display management of the TV only where the metadata carry an active area (level 5), and inside it. With
neutral trims, an active area or none gives the same picture: the clip must have a lift, a dark picture where
it shows, and black bars inside the coded picture. Licensed players send no level 5 (and IPT, a neutral level
3 for CM v2.9, level 254 0, BT.2020 in the AVI InfoFrame); Hades sends the level 5 of the video, placed; the
TV's own player uses the video's.

The picture (a grid of 1920x1080, every coordinate doubled at 3840x2160; edges on multiples of 8 so that every
region has a flat 4:2:0 chroma), Dolby Vision profile 8.1 (the base layer an HDR10 picture, BT.2020 PQ):
  - bars of a 2.37:1 film inside the 16:9 picture: rows 0-136 and 944-1080 black (PQ 0);
  - inside, the black of the picture (PQ 0), where a lift shows first;
  - the label, top left inside the picture (grey at 50 cd/m2);
  - a reference white (203 cd/m2, ITU-R BT.2408) and a highlight of 1000 cd/m2, so that the content range
    (level 1) reaches the mastering peak and a TV maps the picture;
  - ten near-black squares, 0.005 to 2 cd/m2, where a lift shows as squares that become visible;
  - a ramp of 32 steps from 0 to 5 cd/m2 in equal steps of PQ, where it shows as a brighter start.
Its metadata: CM v2.9, level 1 of the picture, level 2 for targets of 100, 600 and 1000 cd/m2 with a lift
(trim_offset 2048 + 600, as fandangos' test of the lift), the rest of the trims neutral, level 5 of the bars
(272 rows at the top and at the bottom of the 3840x2160 picture), level 6 of the mastering display.
Everything here is the standard library and the modules of this directory; deterministic.
"""
import card
import dovi

FILE = "hades-check-ab-dv81.mkv"
FORMAT = "dv81-ab"
LABEL = "DOLBY VISION LIFT"
BAR = 136  # rows of the grid at the top and at the bottom
LIFT = 600  # trim_offset above 2048
TARGETS = (100.0, 600.0, 1000.0)  # the targets of level 2, cd/m2
DARK = (0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0)  # cd/m2 (with the black of the picture: ten)
RAMP_STEPS, RAMP_TOP = 32, 5.0
LABEL_GREY, WHITE, HIGHLIGHT = 50.0, card.REF_WHITE, 1000.0
LABEL_XY = (120, 176)


def fmt():
    """the clip, as the formats of card.py are described (its own layout)"""
    return {"id": FORMAT, "label": LABEL, "file": FILE, "width": 3840, "height": 2160, "fps": (24000, 1001),
            "frames": 360, "bits": 10, "signal": "pq", "range": "limited", "primaries": 9, "transfer": 16,
            "matrix": 9, "dovi_profile": 8, "layout": layout}


def regions():
    """[(name, rect of the grid)] painted in order over the background (the bars and the black of the
    picture are the background: PQ 0)"""
    out = [("white", (120, 264, 360, 424)), ("highlight", (408, 312, 456, 376))]
    for k in range(len(DARK) + 1):
        x = 120 + 168 * k
        out.append(("dark%d" % k, (x, 520, x + 144, 664)))
    step = (1680 - 120) // RAMP_STEPS // 8 * 8  # 48: whole chroma samples
    for k in range(RAMP_STEPS):
        x = 120 + step * k
        out.append(("ramp%d" % k, (x, 736, x + step, 864)))
    return out


def label_dots():
    """the dots of the label (card.FONT, a dot 8x8 of the grid, 6 dots a character) at LABEL_XY"""
    out = []
    for n, ch in enumerate(LABEL):
        glyph = card.FONT[ch]
        for row in range(7):
            for col in range(5):
                if glyph[row][col] == "1":
                    x = LABEL_XY[0] + (n * card.ADVANCE + col) * card.DOT
                    y = LABEL_XY[1] + row * card.DOT
                    out.append((x, y, x + card.DOT, y + card.DOT))
    return out


def layout(f):
    s = f["width"] // card.GRID_W
    items = [(tuple(c * s for c in r), name) for name, r in regions()]
    items += [(tuple(c * s for c in r), "label") for r in label_dots()]
    return items


def ramp_nits(k):
    """step k of the ramp: equal steps of PQ from black to RAMP_TOP"""
    return card.pq_eotf(card.pq_oetf(RAMP_TOP) * k / (RAMP_STEPS - 1))


def _grey(nits, what):
    ep = card.pq_oetf(nits)
    return card.Value(card.limited_codes((ep, 0.0, 0.0), 10, what), (nits,) * 3, (ep, ep, ep))


def region_values():
    out = {"background": _grey(0.0, "background"), "label": _grey(LABEL_GREY, "label"),
           "white": _grey(WHITE, "white"), "highlight": _grey(HIGHLIGHT, "highlight")}
    out["dark0"] = _grey(0.0, "dark0")
    for k, nits in enumerate(DARK, 1):
        out["dark%d" % k] = _grey(nits, "dark%d" % k)
    for k in range(RAMP_STEPS):
        out["ramp%d" % k] = _grey(ramp_nits(k), "ramp%d" % k)
    return out


def light_stats(f):
    """MaxCLL and MaxFALL from the light of the picture (as card.light_stats())"""
    vals, ar = region_values(), card.areas(f)
    total = sum(ar.values())
    return max(max(vals[n].light) for n in ar), sum(max(vals[n].light) * a for n, a in ar.items()) / total


def level1(f):
    """level 1 of the picture from its codes, and clamped as dovi_tool clamps it for CM v2.9 (card.level1())"""
    vals, ar = region_values(), card.areas(f)
    total = sum(ar.values())

    def e(n, pick):
        rgbp = card.rgb_from_ycbcr(card.limited_values(vals[n].codes, 10), "bt2020")
        return min(max(pick(rgbp), 0.0), 1.0)

    mn = card.rnd(min(e(n, min) for n in ar) * 4095.0, "L1 min")
    mx = card.rnd(max(e(n, max) for n in ar) * 4095.0, "L1 max")
    avg = card.rnd(sum(e(n, max) * a for n, a in ar.items()) / total * 4095.0, "L1 avg")
    measured = (mn, mx, avg)
    cmx = max(mx, 2081)
    return measured, (min(mn, 12), cmx, min(max(avg, 819), cmx - 1))


def level2():
    """the trims: one block a target, the lift on each, the rest neutral (2048; ms_weight 2048 as dovi_tool
    writes it)"""
    return [{"level": 2, "target_max_pq": dovi.pq_code12(t), "trim_slope": 2048, "trim_offset": 2048 + LIFT,
             "trim_power": 2048, "trim_chroma_weight": 2048, "trim_saturation_gain": 2048, "ms_weight": 2048}
            for t in TARGETS]


def level5(f):
    s = f["height"] // card.GRID_H
    return {"left": 0, "right": 0, "top": BAR * s, "bottom": BAR * s}
