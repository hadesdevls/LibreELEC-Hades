# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""The clip of the card "3D: HDR" of "Check this setup" (hades-check-3d-hdr10-sbs.mkv): a short 3D video in HDR10
at 1080p and 24000/1001 pictures a second, which Kodi plays as 3D, in the display's 3D layout, and which LibreELEC
Hades sends as HDR where "How 3D video goes out" asks for HDR: the card shows whether the display shows HDR in 3D,
which nothing that a display sends tells (its EDID says what it takes in 2D).

The picture: the HDR10 card of the check (card.py: PQ, BT.2020, 10 bits, the static metadata of its own light) at
1920x1080, once for each eye with its own label ("3D HDR LEFT", "3D HDR RIGHT"), each squeezed to half its width
(every other sample of luma and of chroma, so that every region stays flat and on whole chroma samples) and put side
by side, the left eye first, as the clip of the frame packing card (fpclip.py) is. The file says so as Matroska does
(StereoMode 1, "side by side (left eye is first)", RFC 9559), which FFmpeg gives Kodi as the stream's "stereo_mode"
("left_right"). Coded as the HDR10 card (QP 8). Everything here is the standard library and the modules of this
directory; deterministic.
"""
import sys
from array import array

import card

FILE = "hades-check-3d-hdr10-sbs.mkv"
FORMAT = "hdr10-3d-sbs"
LABELS = ("3D HDR LEFT", "3D HDR RIGHT")
STEREO_MODE = 1  # Matroska StereoMode: side by side, the left eye first
STEREO_NAME = "left_right"  # what FFmpeg calls it (libavformat/matroska.c, ff_matroska_video_stereo_mode)


def base():
    """the HDR10 card at the size of a 3D Blu-ray's eye, 1920x1080"""
    return dict(card.fmt("hdr10"), width=1920, height=1080)


def fmt():
    """the format of the clip: that of the HDR10 card at 1920x1080, with this clip's file and name"""
    f = base()
    f.update(id=FORMAT, file=FILE, label=" / ".join(LABELS))
    return f


def eye(label):
    """the planes of the card of one eye, at the full size of the frame"""
    return card.planes(dict(base(), label=label))


def _samples(data):
    """the 16-bit little-endian samples of a plane (as card.planes() gives them)"""
    a = array("H")
    a.frombytes(data)
    if sys.byteorder != "little":
        a.byteswap()
    return a


def _bytes(samples):
    if sys.byteorder != "little":
        samples = array("H", samples)
        samples.byteswap()
    return samples.tobytes()


def planes():
    """the frame: each eye squeezed to half its width (the even samples), the left eye on the left"""
    f = base()
    w, h = f["width"], f["height"]
    eyes = [eye(label) for label in LABELS]
    out = []
    for c in range(3):
        cw, ch = (w, h) if c == 0 else (w // 2, h // 2)
        left, right = _samples(eyes[0][c]), _samples(eyes[1][c])
        plane = array("H")
        for y in range(ch):
            row = slice(y * cw, (y + 1) * cw, 2)
            plane += left[row]
            plane += right[row]
        if len(plane) != cw * ch:
            raise ValueError("a plane of the HDR 3D clip of another size")
        out.append(_bytes(plane))
    return out
