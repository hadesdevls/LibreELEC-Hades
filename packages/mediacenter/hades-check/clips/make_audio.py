#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Makes the sounds of "Check this setup" at build time, on the build host.

- A short chime for each passthrough format that can be checked by ear with an encoder of the host
  FFmpeg: AC-3, E-AC-3 and TrueHD (hades-check-<id>.mkv). The receiver gets 1.5 s of coded silence
  to lock onto the format, then the chime twice.
- A speaker walk in PCM (hades-check-speakers-<layout>.mkv: FLAC, 48 kHz, 16 bits) for 2.0, 5.1 and
  7.1: pink noise in one channel at a time, clockwise as a receiver's own test tones go, the LFE
  with noise below about 120 Hz.
- sounds.json, which the settings add-on reads (what each file is, where its sound is, and which
  formats cannot be checked here and why).

Every sound is Matroska with its one audio track and the name .mkv: Kodi plays a .mkv with its
video player, the way it plays a film (VideoPlayerAudio, the passthrough codec, the player's
information), while .mka and .flac are music to it (AdvancedSettings.cpp, m_musicExtensions) and go
to the music player. With no video track the display does not change mode. The channel layout of
a walk is the one FLAC defines for its number of channels (2.0, 7.1), or, for 5.1 with back
surrounds, the channel mask that FFmpeg's Matroska muxer writes into the FLAC header it carries.

Everything is made from numbers here: no recording and no sound of a third party. Only the Python
standard library, and of the C maths library only the square root, which IEEE 754 requires exact to
the last bit (sine, power and exponential may differ in their last bit between hosts): the sine is
a series of our own, filters and gains are plain arithmetic, so two runs on any host give the same
samples, and FFmpeg runs with its bit-exact flags, so the same files.

Levels: the chime and each speaker of the walk play at about the level at which a receiver
reproduces the dialogue of a film (-31 LUFS, the reference of Dolby's dialogue normalisation).
AC-3 and E-AC-3 carry a dialogue normalisation of -31 dB, which asks for no attenuation: the
receiver plays them as they are, and Hades' "Dialogue normalisation at 0 dB in passthrough"
(audiooutput.hadesdialnorm) finds nothing to change, so the level is the same with it on or off.
The LFE is 10 dB below the other channels: a receiver plays the LFE channel 10 dB louder, so the
subwoofer comes out at the level of the others, as with a receiver's own test tones.

usage: make_audio.py --ffmpeg <ffmpeg> --ffprobe <ffprobe> --out <dir>
"""

import argparse
import array
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import tempfile

RATE = 48000
MS = RATE // 1000  # samples in a millisecond: every time below is a whole number of them

# --- the sine, without the C maths library ---------------------------------------------------------

PI = 3.141592653589793  # the double nearest to pi (the value of math.pi)


def _taylor_sin(x):
    """sin(x) for 0 <= x <= pi/4, Taylor series to x^23 (error far below one unit in the last
    place), evaluated in a fixed order with + - * / only"""
    x2 = x * x
    term = x
    total = x
    for k in range(1, 12):
        term = -term * x2 / ((2 * k) * (2 * k + 1))
        total += term
    return total


def _taylor_cos(x):
    """cos(x) for 0 <= x <= pi/4, Taylor series to x^22"""
    x2 = x * x
    term = 1.0
    total = 1.0
    for k in range(1, 12):
        term = -term * x2 / ((2 * k - 1) * (2 * k))
        total += term
    return total


def sine_table():
    """sin(2 pi k / RATE) for k in 0..RATE-1: every sinusoid of a whole number of hertz is read from
    it exactly, its phase being a whole number of steps"""
    quarter = RATE // 4
    eighth = RATE // 8
    q = [0.0] * (quarter + 1)
    for k in range(quarter + 1):
        if k <= eighth:
            q[k] = _taylor_sin(PI * k / (RATE // 2))
        else:
            q[k] = _taylor_cos(PI * (quarter - k) / (RATE // 2))
    table = [0.0] * RATE
    half = RATE // 2
    for k in range(RATE):
        if k <= quarter:
            table[k] = q[k]
        elif k < half:
            table[k] = q[half - k]
        else:
            kk = k - half
            table[k] = -(q[kk] if kk <= quarter else q[half - kk])
    return table


def sin_of(x):
    """sin(x) for 0 <= x <= pi/2 (for filter design)"""
    return _taylor_sin(x) if x <= PI / 4 else _taylor_cos(PI / 2 - x)


def cos_of(x):
    """cos(x) for 0 <= x <= pi/2"""
    return _taylor_cos(x) if x <= PI / 4 else _taylor_sin(PI / 2 - x)


# --- the chime ----------------------------------------------------------------------------------

CHIME_TOTAL_MS = 6400  # a whole number of AC-3 frames (1536), TrueHD access units (40) and 160 ms
CHIME_STARTS_MS = (1500, 3500)  # coded silence first, for the receiver to lock onto the format
# a C major chord struck as a soft arpeggio: (hertz, onset within the chime in ms)
CHIME_NOTES = ((523, 0), (659, 80), (784, 160))
NOTE_MS = 1400
ATTACK = 600  # samples (12.5 ms), a raised-cosine rise
# partials of each note: (multiple of the note, amplitude, power of its decay (1 - t/T)^p), the
# higher ones dying away sooner, as on a struck bar
PARTIALS = ((1, 1.0, 3), (2, 0.25, 5), (3, 0.08, 8))
# the gain that puts the chime at -31 LUFS (integrated, as ffmpeg's ebur128 measures it; at 0.1 it
# measured -22.7 LUFS, so 0.1 * 10^(-8.3/20)), checked at every build (LEVEL_WINDOW)
CHIME_GAIN = 0.03845917820453536
CHIME_LEVEL_LUFS = -31.0
LEVEL_WINDOW = 1.0  # +/- LU around the target, measured on the decoded stream
TRUE_PEAK_MAX = -12.0  # dBTP: a moderate sound, nowhere near full scale
SOUND_FROM_MS = CHIME_STARTS_MS[0]
SOUND_TO_MS = CHIME_STARTS_MS[-1] + CHIME_NOTES[-1][1] + NOTE_MS


def chime_samples(sin):
    """the chime as floats (one channel; both channels of the stereo stream carry it)"""
    total = CHIME_TOTAL_MS * MS
    out = [0.0] * total
    note_len = NOTE_MS * MS
    for start_ms in CHIME_STARTS_MS:
        for hz, onset_ms in CHIME_NOTES:
            at = (start_ms + onset_ms) * MS
            for mult, amp, power in PARTIALS:
                f = hz * mult
                for n in range(note_len):
                    d = 1.0 - n / note_len
                    env = d
                    for _ in range(power - 1):
                        env *= d
                    if n < ATTACK:
                        a = sin[n * (RATE // (4 * ATTACK))]
                        env *= a * a
                    out[at + n] += CHIME_GAIN * amp * env * sin[(n * f) % RATE]
    return out


# --- the speaker walk ---------------------------------------------------------------------------

SEED = 0x4841444553434B31  # "HADESCK1"
# -33 dBFS RMS: a front channel's slot measures -31.2 LUFS alone (the K-weighting of BS.1770 counts
# this noise 1.8 dB above its RMS), the level of the chime; a surround one 1.5 LU more, by the
# weight BS.1770 gives surrounds, at the same electrical level, as a receiver's own test tones
NOISE_RMS = 0.0223872113856834
LFE_RMS = 0.00707945784384138  # -43 dBFS: 10 dB below, as the receiver adds 10 dB to the LFE
WALK_LEAD_MS = 1500
SLOT_MS = 1500
GAP_MS = 500
FADE = 2400  # samples (50 ms) of raised-cosine fade in and out of each slot
# the walks, clockwise from the front left as a receiver's own test tones go, and the LFE last;
# channel order in the file is FFmpeg's for the layout
LAYOUTS = (
    ("2.0", "stereo", ("FL", "FR"), ("FL", "FR"), 0x3),
    ("5.1", "5.1", ("FL", "FR", "FC", "LFE", "BL", "BR"), ("FL", "FC", "FR", "BR", "BL", "LFE"), 0x3F),
    ("7.1", "7.1", ("FL", "FR", "FC", "LFE", "BL", "BR", "SL", "SR"),
     ("FL", "FC", "FR", "SR", "BR", "BL", "SL", "LFE"), 0x63F),
)
# The noise is quantised to 16 bits: its own quantisation noise is 98 dB down, far below audibility.
# The files are 16-bit FLAC: they hold those samples bit for bit (checked at every build), and Kodi
# plays PCM through its float mixer and opens the output in the format it chooses whatever the
# source's depth (ActiveAE.cpp, ApplySettingsToFormat()). In 24 bits FFmpeg's FLAC encoder stores the
# 8 unused bits of every channel after the first silent one (flacenc.c, remove_wasted_bits() returns
# at a silent channel instead of going on to the next): the same samples of the 7.1 walk took
# 1 239 459 bytes in 24 bits against 744 688 in 16.
PRECISION_BITS = 16


class SplitMix64:
    """Sebastiano Vigna's SplitMix64 (public domain): a small generator with a fixed seed"""

    MASK = (1 << 64) - 1

    def __init__(self, seed):
        self.state = seed & self.MASK

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & self.MASK
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & self.MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & self.MASK
        return z ^ (z >> 31)

    def uniform(self):
        """in [-1, 1), 53 bits"""
        return (self.next() >> 11) * (2.0 / (1 << 53)) - 1.0


def biquad(kind, f0, q):
    """Robert Bristow-Johnson's cookbook filter (W3C Audio EQ Cookbook): (b0, b1, b2, a1, a2)
    normalised by a0, for a low or high pass at f0 with quality q"""
    w0 = 2.0 * PI * f0 / RATE
    s, c = sin_of(w0), cos_of(w0)
    alpha = s / (2.0 * q)
    a0 = 1.0 + alpha
    if kind == "lp":
        b0, b1 = (1.0 - c) / 2.0, 1.0 - c
    else:
        b0, b1 = (1.0 + c) / 2.0, -(1.0 + c)
    return (b0 / a0, b1 / a0, b0 / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0)


SQRT_HALF = 0.7071067811865476  # 1/sqrt(2), the Q of a 2nd-order Butterworth
# the four sections of an 8th-order Butterworth: Q = 1 / (2 cos((2k - 1) pi / 16)), k = 1..4
Q8 = tuple(1.0 / (2.0 * cos_of((2 * k - 1) * PI / 16)) for k in range(1, 5))
MAIN_FILTERS = (biquad("hp", 150.0, SQRT_HALF),)  # little left for a receiver's bass management
# the LFE: 25 Hz to 100 Hz, 8th order above (-13 dB at 120 Hz; the noise above 150 Hz is 42 dB
# below the whole, measured; a 4th order left it at 25 dB)
LFE_FILTERS = (biquad("hp", 25.0, SQRT_HALF),) + tuple(biquad("lp", 100.0, q) for q in Q8)


def pink(rng, n):
    """pink noise: white noise through Paul Kellet's refined pink filter (within 0.05 dB of
    -3 dB per octave from 9.2 Hz up), without a gain of its own"""
    b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0
    out = [0.0] * n
    for i in range(n):
        w = rng.uniform()
        b0 = 0.99886 * b0 + w * 0.0555179
        b1 = 0.99332 * b1 + w * 0.0750759
        b2 = 0.96900 * b2 + w * 0.1538520
        b3 = 0.86650 * b3 + w * 0.3104856
        b4 = 0.55000 * b4 + w * 0.5329522
        b5 = -0.7616 * b5 - w * 0.0168980
        out[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362
        b6 = w * 0.115926
    return out


def filtered(x, sections):
    for b0, b1, b2, a1, a2 in sections:
        y = [0.0] * len(x)
        x1 = x2 = y1 = y2 = 0.0
        for i, v in enumerate(x):
            o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1, y2, y1 = x1, v, y1, o
            y[i] = o
        x = y
    return x


def slot_noise(rng, lfe, sin):
    """one slot: noise at its level (RMS measured between the fades), faded in and out"""
    settle = 9600  # 200 ms run in, dropped, so that the filters start settled
    n = SLOT_MS * MS
    x = filtered(pink(rng, n + settle), LFE_FILTERS if lfe else MAIN_FILTERS)[settle:]
    acc = 0.0
    for v in x[FADE:n - FADE]:  # a plain sum in order (sum() of floats differs between Pythons)
        acc += v * v
    rms = math.sqrt(acc / (n - 2 * FADE))  # IEEE 754 makes the square root exact to the last bit
    gain = (LFE_RMS if lfe else NOISE_RMS) / rms
    step = RATE // (4 * FADE)
    for i in range(n):
        g = gain
        if i < FADE:
            a = sin[i * step]
            g *= a * a
        elif i >= n - FADE:
            a = sin[(n - 1 - i) * step]
            g *= a * a
        x[i] *= g
    return x


def walk(layout_index, sin):
    """(interleaved samples as floats, slots) of one walk"""
    name, _, order, walk_order, _ = LAYOUTS[layout_index]
    channels = len(order)
    rng = SplitMix64(SEED + layout_index)
    total = (WALK_LEAD_MS + len(walk_order) * (SLOT_MS + GAP_MS)) * MS
    buf = [0.0] * (total * channels)
    slots = []
    for k, ch in enumerate(walk_order):
        start_ms = WALK_LEAD_MS + k * (SLOT_MS + GAP_MS)
        x = slot_noise(rng, ch == "LFE", sin)
        c = order.index(ch)
        at = start_ms * MS
        for i, v in enumerate(x):
            buf[(at + i) * channels + c] = v
        slots.append({"channel": ch, "from": start_ms / 1000, "to": (start_ms + SLOT_MS) / 1000})
    return buf, slots, total


# --- PCM and FFmpeg -----------------------------------------------------------------------------

def to_s32(samples, bits):
    """floats to signed 32-bit words holding 24-bit samples (bits of precision, the rest zero)"""
    full = 1 << 23
    step = 1 << (24 - bits)
    words = array.array("i")
    if words.itemsize != 4:
        raise SystemExit("make_audio.py: this Python has no 32-bit array type")
    hi = full - step
    for v in samples:
        s = round(v * full / step) * step
        if s > hi:
            s = hi
        elif s < -full:
            s = -full
        words.append(s * 256)
    if sys.byteorder != "little":
        words.byteswap()
    return words


def run(cmd):
    env = dict(os.environ, LC_ALL="C", TZ="UTC")
    r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       env=env)
    if r.returncode != 0:
        sys.stderr.write(r.stderr.decode("utf-8", "replace"))
        raise SystemExit("make_audio.py: failed: " + " ".join(cmd))
    return r


BITEXACT = ["-flags", "+bitexact", "-fflags", "+bitexact", "-map_metadata", "-1", "-map_chapters", "-1"]


def encode(ffmpeg, raw, layout, codec_args, out_fmt, out):
    run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", str(RATE),
         "-ch_layout", layout, "-i", raw, "-map", "0:a", *codec_args, *BITEXACT, "-f", out_fmt, out])


def wav_data(wav):
    """the samples of a WAV that FFmpeg wrote to a pipe (its sizes unknown there): the data chunk to
    the end"""
    if wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise SystemExit("make_audio.py: not a WAV from ffmpeg")
    at = 12
    while at + 8 <= len(wav):
        cid, size = wav[at:at + 4], int.from_bytes(wav[at + 4:at + 8], "little")
        if cid == b"data":
            return wav[at + 8:]
        at += 8 + size + (size & 1)
    raise SystemExit("make_audio.py: a WAV from ffmpeg without data")


def decode_s32(ffmpeg, path):
    """the samples as signed 32-bit little-endian words (the host FFmpeg has no raw PCM muxer: WAV)"""
    return wav_data(run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-i", path, "-map",
                         "0:a", "-c:a", "pcm_s32le", "-f", "wav", "-"]).stdout)


def probe(ffprobe, path):
    r = run([ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", path])
    data = json.loads(r.stdout)
    return data["streams"][0], data["format"]


def one_audio_track(stream, fmt):
    """Matroska with one track, of sound: nothing for the display to switch to"""
    return fmt["format_name"].startswith("matroska") and int(fmt["nb_streams"]) == 1 and \
        stream["codec_type"] == "audio"


def loudness(ffmpeg, path):
    """(integrated LUFS, true peak dBTP) of the decoded stream, by ffmpeg's ebur128"""
    r = run([ffmpeg, "-nostdin", "-hide_banner", "-nostats", "-i", path, "-map", "0:a",
             "-af", "ebur128=peak=true", "-f", "null", "-"])
    text = r.stderr.decode("utf-8", "replace")
    summary = text[text.rfind("Summary:"):]
    i = re.search(r"I:\s+(-?[\d.]+|-inf) LUFS", summary)
    p = re.search(r"True peak:\s+Peak:\s+(-?[\d.]+|-inf) dBFS", summary)
    if not i or not p:
        raise SystemExit("make_audio.py: no ebur128 summary for " + path)
    return float(i.group(1)), float(p.group(1))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# --- TrueHD: the two-channel presentation said as plain stereo -----------------------------------

def thd_major_sync_check(ms):
    """the two check bytes (26 and 27) of a TrueHD major sync of 28 bytes: the CRC of its first 24
    bytes (polynomial 0x002D, MSB first, start 0; av_crc() gives it with its bytes swapped), XOR
    bytes 24 and 25, stored little-endian, as FFmpeg's ff_mlp_checksum16() writes it and Kodi's
    CAEStreamParser::SyncTrueHD() checks it"""
    crc = 0
    for byte in ms[:24]:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x002D) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return (crc >> 8) ^ ms[24], (crc & 0xFF) ^ ms[25]


def thd_major_syncs(es):
    """offsets of the access units of a TrueHD elementary stream that carry a major sync"""
    at, found = 0, []
    while at + 4 <= len(es):
        length = (((es[at] & 0x0F) << 8) | es[at + 1]) * 2
        if length < 4 or at + length > len(es):
            raise SystemExit("make_audio.py: TrueHD access unit of a bad length at %d" % at)
        if es[at + 4:at + 8] == b"\xf8\x72\x6f\xba":
            found.append(at)
        at += length
    return found


def thd_set_plain_stereo(es):
    """FFmpeg's encoder marks a stereo stream's two-channel presentation as Lt/Rt (Dolby Surround
    encoded, 2ch_presentation_channel_modifier 1), which it is not. Each major sync gets 0 (stereo)
    and its CRC again; the CRC as it came is checked first, so that a sync this does not read as
    FFmpeg wrote it is never touched."""
    es = bytearray(es)
    syncs = thd_major_syncs(es)
    if not syncs:
        raise SystemExit("make_audio.py: no TrueHD major sync")
    for at in syncs:
        ms = at + 4
        if es[ms + 25] & 1:
            raise SystemExit("make_audio.py: TrueHD major sync with extensions: not expected")
        if thd_major_sync_check(es[ms:ms + 28]) != (es[ms + 26], es[ms + 27]):
            raise SystemExit("make_audio.py: TrueHD major sync CRC does not check as it came")
        es[ms + 5] &= 0x3F  # 2ch_presentation_channel_modifier: the two top bits of byte 5
        es[ms + 26], es[ms + 27] = thd_major_sync_check(es[ms:ms + 28])
    return bytes(es), len(syncs)


# --- the files ----------------------------------------------------------------------------------

PASSTHROUGH = (
    # id, codec arguments, Kodi's decoder name in passthrough, setting
    ("ac3", ["-c:a", "ac3", "-b:a", "192k", "-dialnorm", "-31"], "pt-ac3", "audiooutput.ac3passthrough"),
    ("eac3", ["-c:a", "eac3", "-b:a", "192k", "-dialnorm", "-31"], "pt-eac3",
     "audiooutput.eac3passthrough"),
    ("truehd", ["-c:a", "truehd", "-strict", "experimental", "-sample_fmt", "s32p"], "pt-truehd",
     "audiooutput.truehdpassthrough"),
)

NOT_CHECKABLE = (
    # DTS: FFmpeg's DTS encoder is experimental and its streams have been heard as a loud buzz on
    # receivers' hardware decoders; an unreliable sound would decide wrongly, or hurt
    {"id": "dts", "setting": "audiooutput.dtspassthrough", "why": "encoder-not-conformant"},
    # DTS-HD: no encoder of it exists in FFmpeg (its DTS encoder writes the core only)
    {"id": "dtshd", "setting": "audiooutput.dtshdpassthrough", "why": "no-encoder"},
)


def make_passthrough(ffmpeg, ffprobe, out, tmp, sin):
    chime = chime_samples(sin)
    stereo = []
    for v in chime:
        stereo.append(v)
        stereo.append(v)
    raw = os.path.join(tmp, "chime.s32")
    pcm = to_s32(stereo, 24)
    with open(raw, "wb") as f:
        pcm.tofile(f)
    source = pcm.tobytes()
    entries = []
    for ident, codec, decoder, setting in PASSTHROUGH:
        name = "hades-check-%s.mkv" % ident
        path = os.path.join(out, name)
        if ident == "truehd":
            thd = os.path.join(tmp, "chime.thd")
            encode(ffmpeg, raw, "stereo", codec, "truehd", thd)
            with open(thd, "rb") as f:
                es, _ = thd_set_plain_stereo(f.read())
            with open(thd, "wb") as f:
                f.write(es)
            run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-f", "truehd", "-i", thd,
                 "-map", "0:a", "-c:a", "copy", *BITEXACT, "-f", "matroska", path])
            if decode_s32(ffmpeg, path) != source:
                raise SystemExit("make_audio.py: the TrueHD chime does not decode to its samples")
        else:
            encode(ffmpeg, raw, "stereo", codec, "matroska", path)
        stream, fmt = probe(ffprobe, path)
        if not one_audio_track(stream, fmt) or stream["codec_name"] != ident or \
                int(stream["channels"]) != 2 or int(stream["sample_rate"]) != RATE:
            raise SystemExit("make_audio.py: %s is not what it should be: %r" % (name, stream))
        integrated, peak = loudness(ffmpeg, path)
        if abs(integrated - CHIME_LEVEL_LUFS) > LEVEL_WINDOW or peak > TRUE_PEAK_MAX:
            raise SystemExit("make_audio.py: %s at %.1f LUFS, true peak %.1f dBTP: out of its window"
                             % (name, integrated, peak))
        entries.append({
            "id": ident, "file": name, "kodi_decoder": decoder, "setting": setting,
            "duration": CHIME_TOTAL_MS / 1000, "sound_from": SOUND_FROM_MS / 1000,
            "sound_to": SOUND_TO_MS / 1000, "channels": 2, "sample_rate": RATE,
            "loudness_lufs": integrated, "true_peak_dbtp": peak, "sha256": sha256(path)})
    return entries


def make_speakers(ffmpeg, ffprobe, out, tmp, sin):
    entries = []
    for index, (name, ff_layout, order, _, mask) in enumerate(LAYOUTS):
        samples, slots, frames = walk(index, sin)
        raw = os.path.join(tmp, "walk-%s.s32" % name)
        pcm = to_s32(samples, PRECISION_BITS)
        with open(raw, "wb") as f:
            pcm.tofile(f)
        file = "hades-check-speakers-%s.mkv" % name
        path = os.path.join(out, file)
        encode(ffmpeg, raw, ff_layout,
               ["-c:a", "flac", "-sample_fmt", "s16", "-bits_per_raw_sample", "16",
                "-compression_level", "8"], "matroska", path)
        if decode_s32(ffmpeg, path) != pcm.tobytes():
            raise SystemExit("make_audio.py: %s does not decode to its samples" % file)
        stream, fmt = probe(ffprobe, path)
        # the layout as FFmpeg's Matroska demuxer, Kodi's, reads it back
        if not one_audio_track(stream, fmt) or stream["codec_name"] != "flac" or \
                int(stream["channels"]) != len(order) or int(stream["sample_rate"]) != RATE or \
                stream.get("bits_per_raw_sample") != "16" or stream.get("channel_layout") != ff_layout:
            raise SystemExit("make_audio.py: %s is not what it should be: %r" % (file, stream))
        entries.append({
            "layout": name, "file": file, "channels": len(order), "sample_rate": RATE,
            "bits_per_sample": 16, "channel_mask": "0x%x" % mask, "duration": frames / RATE,
            "passes": 1, "slots": slots, "sha256": sha256(path)})
    return entries


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--ffprobe", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    if not os.path.isdir(args.out):
        raise SystemExit("make_audio.py: %s is not a folder" % args.out)
    sin = sine_table()
    with tempfile.TemporaryDirectory(prefix="hades-check-audio-") as tmp:
        sounds = {
            "version": 1,
            "passthrough": make_passthrough(args.ffmpeg, args.ffprobe, args.out, tmp, sin),
            "not_checkable": list(NOT_CHECKABLE),
            "speakers": make_speakers(args.ffmpeg, args.ffprobe, args.out, tmp, sin),
        }
    with open(os.path.join(args.out, "sounds.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(sounds, indent=2) + "\n")


if __name__ == "__main__":
    main()
