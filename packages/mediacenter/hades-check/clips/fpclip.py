# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""The clip of the frame packing card of "Check this setup" (hades-check-3d-sbs.mkv): a short 3D video at 1080p
and 24000/1001 pictures a second, the format of a 3D Blu-ray, that Kodi plays as 3D and that LibreELEC Hades sends
in HDMI frame packing where the display says it takes 3D (the HDMI vendor block of its EDID): every HDMI 1.4 display
with 3D takes frame packing at 1080p24, so the card shows whether the driver took it, or what it went out as.

The picture: the SDR card of the check (card.py, 1920x1080, BT.709, 8 bits), once for each eye with its own label
("3D LEFT", "3D RIGHT"), each squeezed to half its width (every other column of luma and of chroma, so that every
region stays flat and on whole chroma samples) and put side by side, the left eye first. The file says so as
Matroska does (StereoMode 1, "side by side (left eye is first)", RFC 9559), which FFmpeg gives Kodi as the stream's
"stereo_mode" ("left_right"); its name says it too (Kodi's names of 3D files: "3d" and "sbs"). Coded lossless, as
the SDR card. Everything here is the standard library and the modules of this directory; deterministic.
"""
import card

FILE = "hades-check-3d-sbs.mkv"
FORMAT = "sdr-3d-sbs"
LABELS = ("3D LEFT", "3D RIGHT")
STEREO_MODE = 1  # Matroska StereoMode: side by side, the left eye first
STEREO_NAME = "left_right"  # what FFmpeg calls it (libavformat/matroska.c, ff_matroska_video_stereo_mode)


def fmt():
    """the format of the clip: that of the SDR card, with this clip's file and name"""
    f = dict(card.fmt("sdr"))
    f.update(id=FORMAT, file=FILE, label=" / ".join(LABELS))
    return f


def eye(label):
    """the planes of the card of one eye, at the full size of the frame"""
    return card.planes(dict(card.fmt("sdr"), label=label))


def planes():
    """the frame: each eye squeezed to half its width (the even columns), the left eye on the left"""
    f = card.fmt("sdr")
    w, h = f["width"], f["height"]
    eyes = [eye(label) for label in LABELS]
    out = []
    for c in range(3):
        cw, ch = (w, h) if c == 0 else (w // 2, h // 2)
        left, right = eyes[0][c], eyes[1][c]
        plane = bytearray()
        for y in range(ch):
            row = slice(y * cw, (y + 1) * cw, 2)
            plane += left[row]
            plane += right[row]
        if len(plane) != cw * ch:
            raise ValueError("a plane of the 3D clip of another size")
        out.append(bytes(plane))
    return out
