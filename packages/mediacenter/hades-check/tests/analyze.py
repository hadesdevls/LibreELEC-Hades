# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Measurement helpers for the host tests of the check clips (numpy is allowed here, not in the generator).
The card module is given by the caller (analyze.card = <the generator's card.py>)."""
import subprocess

import numpy as np

card = None


def frame_bytes(f):
    return f["width"] * f["height"] * 3 // 2 * (1 if f["bits"] == 8 else 2)


def to_planes(buf, f):
    """one decoded picture (raw 4:2:0 bytes) -> numpy planes (Y, Cb, Cr) of int32 codes"""
    w, h = f["width"], f["height"]
    dt = np.dtype(np.uint8) if f["bits"] == 8 else np.dtype("<u2")
    a = np.frombuffer(buf, dtype=dt)
    y = a[: w * h].reshape(h, w)
    u = a[w * h: w * h + w * h // 4].reshape(h // 2, w // 2)
    v = a[w * h + w * h // 4:].reshape(h // 2, w // 2)
    return y.astype(np.int32), u.astype(np.int32), v.astype(np.int32)


def decoded_pictures(ffmpeg, path, f, count=None):
    """every decoded picture of a clip in turn, as numpy planes of the stream's codes, read one at a time
    from FFmpeg (a 4K clip of 750 pictures is 18 GB of samples). The pixel format asked for is the
    decoder's own, so nothing is converted."""
    pix = "yuv420p" if f["bits"] == 8 else "yuv420p10le"
    args = [ffmpeg, "-v", "error", "-nostdin", "-i", path]
    if count:
        args += ["-frames:v", str(count)]
    args += ["-f", "rawvideo", "-pix_fmt", pix, "-"]
    per = frame_bytes(f)
    p = subprocess.Popen(args, stdout=subprocess.PIPE)
    try:
        while True:
            buf = bytearray()
            while len(buf) < per:
                chunk = p.stdout.read(per - len(buf))
                if not chunk:
                    break
                buf += chunk
            if not buf:
                break
            if len(buf) != per:
                raise RuntimeError("a decoded picture of %d bytes, %d expected" % (len(buf), per))
            yield to_planes(bytes(buf), f)
    finally:
        p.stdout.close()
        rc = p.wait()
    if rc:
        raise RuntimeError("ffmpeg could not decode %s (%d)" % (path, rc))


def expected_planes(f, values=None):
    w, h = f["width"], f["height"]
    raw = card.planes(f, values)
    dt = np.uint8 if f["bits"] == 8 else np.dtype("<u2")
    y = np.frombuffer(raw[0], dtype=dt).reshape(h, w).astype(np.int32)
    u = np.frombuffer(raw[1], dtype=dt).reshape(h // 2, w // 2).astype(np.int32)
    v = np.frombuffer(raw[2], dtype=dt).reshape(h // 2, w // 2).astype(np.int32)
    return y, u, v


def region_ids(f):
    """an id per pixel (luma resolution) and the names"""
    names = sorted({n for _, _, runs in card.spans(f) for _, _, n in runs})
    index = {n: i for i, n in enumerate(names)}
    ids = np.zeros((f["height"], f["width"]), np.int16)
    for y0, y1, runs in card.spans(f):
        for x0, x1, n in runs:
            ids[y0:y1, x0:x1] = index[n]
    return ids, names


def edge_distance(ids, maxd):
    """the Chebyshev distance of every pixel to the nearest pixel of another region (capped at maxd;
    1 = a neighbour differs). Beyond the picture counts as the same region."""
    h, w = ids.shape
    p = np.pad(ids, 1, mode="edge")
    change = np.zeros((h, w), bool)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dy or dx:
                change |= p[1 + dy:1 + dy + h, 1 + dx:1 + dx + w] != ids
    dist = np.full((h, w), maxd, np.int16)
    cur = change
    for k in range(1, maxd):
        dist[cur & (dist == maxd)] = k
        q = np.pad(cur, 1)
        nxt = cur.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nxt |= q[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]
        cur = nxt
    return dist


def error_profile(expected, decoded, ids, names, maxd=32):
    """per plane: {region: [max |error| at distance d] for d = 1..maxd (maxd: maxd and beyond)}, and the
    smallest margin m such that every pixel at distance >= m of every region is exact (1: all exact)"""
    out = []
    for c in range(3):
        e, d = expected[c], decoded[c]
        cid = ids if c == 0 else ids[::2, ::2]
        dist = edge_distance(cid, maxd)
        err = np.abs(d - e)
        prof = {}
        for i, n in enumerate(names):
            m = cid == i
            row = []
            for k in range(1, maxd + 1):
                sel = m & (dist == k)
                row.append(int(err[sel].max()) if sel.any() else -1)
            prof[n] = row
        worst = [max(prof[n][k] for n in names) for k in range(maxd)]
        margin = maxd + 1
        for k in range(maxd, 0, -1):
            if worst[k - 1] > 0:
                break
            margin = k
        out.append({"profile": prof, "worst": worst, "exact_from": margin, "max": int(err.max())})
    return out
