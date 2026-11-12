# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""A minimal Matroska writer for one HEVC video track (RFC 9559; the Dolby Vision block addition
mapping as FFmpeg's muxer writes it, libavformat/matroskaenc.c): deterministic, nothing of the time
or the machine in it. FFmpeg's muxer is not used because it takes the Dolby Vision configuration only
from the codec parameters of a stream, which a raw HEVC stream given to the ffmpeg program does not
carry."""
import struct

# element IDs (RFC 9559, and the Matroska registry for BlockAdditionMapping)
EBML, EBML_VERSION, EBML_READ_VERSION, EBML_MAX_ID, EBML_MAX_SIZE = 0x1A45DFA3, 0x4286, 0x42F7, 0x42F2, 0x42F3
DOCTYPE, DOCTYPE_VERSION, DOCTYPE_READ_VERSION = 0x4282, 0x4287, 0x4285
SEGMENT, SEEKHEAD, SEEK, SEEK_ID, SEEK_POSITION = 0x18538067, 0x114D9B74, 0x4DBB, 0x53AB, 0x53AC
INFO, TIMESTAMP_SCALE, DURATION, MUXING_APP, WRITING_APP = 0x1549A966, 0x2AD7B1, 0x4489, 0x4D80, 0x5741
TRACKS, TRACK_ENTRY, TRACK_NUMBER, TRACK_UID, TRACK_TYPE = 0x1654AE6B, 0xAE, 0xD7, 0x73C5, 0x83
FLAG_LACING, LANGUAGE, CODEC_ID, CODEC_PRIVATE, DEFAULT_DURATION = 0x9C, 0x22B59C, 0x86, 0x63A2, 0x23E383
BLOCK_ADDITION_MAPPING, BLOCK_ADD_ID_NAME, BLOCK_ADD_ID_TYPE, BLOCK_ADD_ID_EXTRA = 0x41E4, 0x41A4, 0x41E7, 0x41ED
VIDEO, PIXEL_WIDTH, PIXEL_HEIGHT, FLAG_INTERLACED = 0xE0, 0xB0, 0xBA, 0x9A
COLOUR, MATRIX, RANGE, TRANSFER, PRIMARIES, MAX_CLL, MAX_FALL = 0x55B0, 0x55B1, 0x55B9, 0x55BA, 0x55BB, 0x55BC, 0x55BD
MASTERING = 0x55D0
MASTERING_IDS = (0x55D1, 0x55D2, 0x55D3, 0x55D4, 0x55D5, 0x55D6, 0x55D7, 0x55D8)  # R x y, G, B, white
LUMINANCE_MAX, LUMINANCE_MIN = 0x55D9, 0x55DA
CLUSTER, CLUSTER_TIMESTAMP, SIMPLE_BLOCK = 0x1F43B675, 0xE7, 0xA3
CUES, CUE_POINT, CUE_TIME, CUE_TRACK_POSITIONS, CUE_TRACK, CUE_CLUSTER_POSITION = 0x1C53BB6B, 0xBB, 0xB3, 0xB7, 0xF7, 0xF1

APP = "LibreELEC Hades check clips"


def _id(i):
    return i.to_bytes((i.bit_length() + 7) // 8, "big")


def _size(n, length=None):
    if length is None:
        length = 1
        while n >= (1 << (7 * length)) - 1:
            length += 1
    if length > 8 or n >= (1 << (7 * length)) - 1:
        raise ValueError("an element too large")
    return ((1 << (7 * length)) | n).to_bytes(length, "big")


def el(i, payload, size_length=None):
    return _id(i) + _size(len(payload), size_length) + payload


def uint(i, v, length=None):
    if length is None:
        length = max(1, (v.bit_length() + 7) // 8)
    return el(i, v.to_bytes(length, "big"))


def flt(i, v):
    return el(i, struct.pack(">d", v))


def text(i, s):
    return el(i, s.encode("utf-8"))


def master(i, *children):
    return el(i, b"".join(children))


def ms(frame, fps):
    """the time of a frame in milliseconds (the time scale of the file), rounded half up"""
    num, den = fps
    return (2 * frame * 1000 * den + num) // (2 * num)


def write(path, f, hvcc, aus, colour, mastering=None, max_cll=None, max_fall=None, dovi=None):
    """f: the format (width, height, fps); aus: the access units in decode order, which is presentation
    order (the stream has no reordering); colour: (matrix, range, transfer, primaries) as ITU-T H.273
    codes and Matroska's range (1 limited, 2 full), each written only where it says something (not 2,
    "unspecified"), as FFmpeg's muxer does; dovi: (box type, record) of the Dolby Vision configuration"""
    fps = f["fps"]
    num, den = fps
    frames = len(aus)
    info = master(INFO, uint(TIMESTAMP_SCALE, 1000000), text(MUXING_APP, APP), text(WRITING_APP, APP),
                  flt(DURATION, float(frames * 1000 * den) / num))

    matrix, rng, transfer, primaries = colour
    col = b""
    if matrix != 2:
        col += uint(MATRIX, matrix)
    col += uint(RANGE, rng)
    if transfer != 2:
        col += uint(TRANSFER, transfer)
    if primaries != 2:
        col += uint(PRIMARIES, primaries)
    if max_cll is not None:
        col += uint(MAX_CLL, max_cll) + uint(MAX_FALL, max_fall)
    if mastering is not None:
        prim, white, lmax, lmin = mastering
        xy = [c for p in prim for c in p] + list(white)
        col += master(MASTERING, *[flt(i, v) for i, v in zip(MASTERING_IDS, xy)],
                      flt(LUMINANCE_MAX, lmax), flt(LUMINANCE_MIN, lmin))
    video = master(VIDEO, uint(PIXEL_WIDTH, f["width"]), uint(PIXEL_HEIGHT, f["height"]),
                   uint(FLAG_INTERLACED, 2), master(COLOUR, col))
    entry = [uint(TRACK_NUMBER, 1), uint(TRACK_UID, 1), uint(TRACK_TYPE, 1), uint(FLAG_LACING, 0),
             text(LANGUAGE, "und"), text(CODEC_ID, "V_MPEGH/ISO/HEVC"), el(CODEC_PRIVATE, hvcc),
             uint(DEFAULT_DURATION, (2 * 1000000000 * den + num) // (2 * num))]
    if dovi is not None:
        box, record = dovi
        entry.append(master(BLOCK_ADDITION_MAPPING, text(BLOCK_ADD_ID_NAME, "Dolby Vision configuration"),
                            uint(BLOCK_ADD_ID_TYPE, int.from_bytes(box, "big")), el(BLOCK_ADD_ID_EXTRA, record)))
    entry.append(video)
    tracks = master(TRACKS, master(TRACK_ENTRY, *entry))

    # clusters of at most 5 s (as FFmpeg's muxer makes them), each block with its time from the
    # cluster's
    clusters, starts = [], []
    k = 0
    while k < frames:
        t0 = ms(k, fps)
        blocks = []
        j = k
        while j < frames and ms(j, fps) - t0 <= 5000:
            rel = ms(j, fps) - t0
            flags = 0x80 if j == 0 else 0x00  # only the first picture is a key frame (an IDR)
            blocks.append(el(SIMPLE_BLOCK, b"\x81" + struct.pack(">hB", rel, flags) + aus[j]))
            j += 1
        starts.append(t0)
        clusters.append(master(CLUSTER, uint(CLUSTER_TIMESTAMP, t0), *blocks))
        k = j

    def seekhead(pos_info, pos_tracks, pos_cues):
        def seek(i, pos):
            return master(SEEK, el(SEEK_ID, _id(i)), uint(SEEK_POSITION, pos, 8))
        return master(SEEKHEAD, seek(INFO, pos_info), seek(TRACKS, pos_tracks), seek(CUES, pos_cues))

    sh_len = len(seekhead(0, 0, 0))  # fixed: positions are written in 8 bytes
    pos_info = sh_len
    pos_tracks = pos_info + len(info)
    pos_clusters = pos_tracks + len(tracks)
    pos_cues = pos_clusters + sum(len(c) for c in clusters)
    cues = master(CUES, master(CUE_POINT, uint(CUE_TIME, 0),
                               master(CUE_TRACK_POSITIONS, uint(CUE_TRACK, 1),
                                      uint(CUE_CLUSTER_POSITION, pos_clusters))))
    body = seekhead(pos_info, pos_tracks, pos_cues) + info + tracks + b"".join(clusters) + cues
    header = master(EBML, uint(EBML_VERSION, 1), uint(EBML_READ_VERSION, 1), uint(EBML_MAX_ID, 4),
                    uint(EBML_MAX_SIZE, 8), text(DOCTYPE, "matroska"), uint(DOCTYPE_VERSION, 4),
                    uint(DOCTYPE_READ_VERSION, 2))
    with open(path, "wb") as out:
        out.write(header + el(SEGMENT, body, 8))


# ---------------------------------------------------------------------------------------------------
# reading, for the tests


def read_vint(data, pos, keep_marker=False):
    first = data[pos]
    length = 1
    while length <= 8 and not first & (0x80 >> (length - 1)):
        length += 1
    if length > 8:
        raise ValueError("bad vint at %d" % pos)
    v = int.from_bytes(data[pos:pos + length], "big")
    if not keep_marker:
        v &= (1 << (7 * length)) - 1
    return v, pos + length


def elements(data, start=0, end=None):
    """[(id, payload start, payload end)] of the elements between start and end"""
    end = len(data) if end is None else end
    out, pos = [], start
    while pos < end:
        i, p = read_vint(data, pos, True)
        size, p = read_vint(data, p)
        out.append((i, p, p + size))
        pos = p + size
    if pos != end:
        raise ValueError("elements run past their parent")
    return out
