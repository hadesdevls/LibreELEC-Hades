#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Tests of make_audio.py, the sounds of "Check this setup".

Makes the sounds twice (the two runs must be the same bytes), then checks every file against
sounds.json, against a reference made here independently (numpy), and against Kodi's own
passthrough code compiled on this host (stream parser, Hades' dialogue normalisation rewriter, MAT
and IEC 61937 packers). Then each mutant breaks one thing in a copy and the check meant for it must
fail. Prints every check and a summary; exits 1 if a check fails or a mutant goes unseen.

usage: check_audio_tests.py --make-audio <make_audio.py> --ffmpeg <ffmpeg> --ffprobe <ffprobe>
                            --kodi <Kodi source tree> --ffmpeg-prefix <prefix with include/, lib/>
                            --work <empty folder> [--cxx g++]
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

import numpy as np

RATE = 48000

# --- what the sounds are meant to be (written here again, not read from make_audio.py) ----------

CHIME_TOTAL = 6.4
CHIME_STARTS = (1.5, 3.5)
CHIME_NOTES = ((523, 0.0), (659, 0.08), (784, 0.16))
NOTE = 1.4
ATTACK = 600
PARTIALS = ((1, 1.0, 3), (2, 0.25, 5), (3, 0.08, 8))
SOUND_FROM, SOUND_TO = 1.5, 3.5 + 0.16 + 1.4
LEVEL_TARGET, LEVEL_WINDOW, TRUE_PEAK_MAX = -31.0, 1.0, -12.0
PASSTHROUGH = {
    "ac3": ("pt-ac3", "audiooutput.ac3passthrough", "AC3"),
    "eac3": ("pt-eac3", "audiooutput.eac3passthrough", "EAC3"),
    "truehd": ("pt-truehd", "audiooutput.truehdpassthrough", "TRUEHD"),
}
NOT_CHECKABLE = {
    "dts": ("audiooutput.dtspassthrough", "encoder-not-conformant"),
    "dtshd": ("audiooutput.dtshdpassthrough", "no-encoder"),
}
SEED = 0x4841444553434B31
NOISE_DBFS, LFE_DBFS = -33.0, -43.0
WALK_LEAD, SLOT, GAP, FADE = 1.5, 1.5, 0.5, 2400
LAYOUTS = {
    "2.0": ("stereo", ["FL", "FR"], ["FL", "FR"], 0x3),
    "5.1": ("5.1", ["FL", "FR", "FC", "LFE", "BL", "BR"], ["FL", "FC", "FR", "BR", "BL", "LFE"], 0x3F),
    "7.1": ("7.1", ["FL", "FR", "FC", "LFE", "BL", "BR", "SL", "SR"],
            ["FL", "FC", "FR", "SR", "BR", "BL", "SL", "LFE"], 0x63F),
}
# the channel order FLAC itself defines for 2, 6 and 8 channels, as FFmpeg reads it (flac.c)
FLAC_NATIVE = {2: 0x3, 6: 0x60F, 8: 0x63F}
NOISE_STEP = 1 << 16  # 16 bits of precision in 24-bit samples held in 32-bit words
SIZE_LIMIT = 2_300_000  # bytes, all the sounds together (made: 2.06 MB; the check card's clips are far smaller)

# --- Kodi on the host -----------------------------------------------------------------------------

KODI_SOURCES = [
    "xbmc/cores/AudioEngine/Utils/AEStreamInfo.cpp",
    "xbmc/cores/AudioEngine/Utils/AEBitstreamPacker.cpp",
    "xbmc/cores/AudioEngine/Utils/AEPackIEC61937.cpp",
    "xbmc/cores/AudioEngine/Utils/PackerMAT.cpp",
    "xbmc/cores/AudioEngine/Utils/AEChannelInfo.cpp",
    "xbmc/utils/BitstreamReader.cpp",
]
HARNESS_CPP = r'''// Kodi's passthrough path on the build host: the stream parser, Hades' dialogue normalisation
// rewriter, the MAT packer and the IEC 61937 packer, compiled from Kodi's sources, fed a stream the
// way CDVDAudioCodecPassthrough feeds them (AddData of each demuxer packet, then GetData until it
// gives nothing). Prints what came out, as JSON, for check_audio_tests.py.
// usage: harness <elementary stream> <packet sizes, one per line>
#include "cores/AudioEngine/Utils/AEBitstreamPacker.h"
#include "cores/AudioEngine/Utils/AEStreamInfo.h"
#include "cores/AudioEngine/Utils/DialnormOverride.h"
#include "cores/AudioEngine/Utils/PackerMAT.h"
#include "utils/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
const char* TypeName(CAEStreamInfo::DataType t)
{
  switch (t)
  {
    case CAEStreamInfo::STREAM_TYPE_AC3: return "AC3";
    case CAEStreamInfo::STREAM_TYPE_EAC3: return "EAC3";
    case CAEStreamInfo::STREAM_TYPE_TRUEHD: return "TRUEHD";
    case CAEStreamInfo::STREAM_TYPE_DTS_512: return "DTS_512";
    case CAEStreamInfo::STREAM_TYPE_DTS_1024: return "DTS_1024";
    case CAEStreamInfo::STREAM_TYPE_DTS_2048: return "DTS_2048";
    case CAEStreamInfo::STREAM_TYPE_DTSHD: return "DTSHD";
    case CAEStreamInfo::STREAM_TYPE_DTSHD_MA: return "DTSHD_MA";
    case CAEStreamInfo::STREAM_TYPE_DTSHD_CORE: return "DTSHD_CORE";
    default: return "NULL";
  }
}

uint16_t Word(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::string Esc(const std::string& s)
{
  std::string o;
  for (char c : s)
  {
    if (c == '"' || c == '\\')
      o += '\\';
    o += c;
  }
  return o;
}

// the passthrough codec's AddData()/GetData() as DVDAudioCodecPassthrough.cpp has them, without
// the player around it
struct Codec
{
  CAEStreamParser parser;
  uint8_t* buffer = nullptr;
  unsigned int bufferSize = 0;
  unsigned int dataSize = 0;
  std::vector<uint8_t> backlog;

  void AddData(const uint8_t* data, int size)
  {
    if (!backlog.empty())
    {
      dataSize = bufferSize;
      unsigned int consumed = parser.AddData(backlog.data(), backlog.size(), &buffer, &dataSize);
      bufferSize = std::max(bufferSize, dataSize);
      backlog.erase(backlog.begin(), backlog.begin() + consumed);
    }
    if (data && backlog.empty())
    {
      if (size <= 0)
        return;
      dataSize = bufferSize;
      int used = parser.AddData(const_cast<uint8_t*>(data), size, &buffer, &dataSize);
      bufferSize = std::max(bufferSize, dataSize);
      if (used != size)
        backlog.assign(data + used, data + size);
    }
    else if (data)
      backlog.insert(backlog.end(), data, data + size);
  }

  // one frame as the codec hands it on, or nothing
  bool GetFrame(std::vector<uint8_t>& out)
  {
    if (!dataSize)
      AddData(nullptr, 0);
    if (!dataSize)
      return false;
    out.assign(buffer, buffer + dataSize);
    dataSize = 0;
    return true;
  }
};
} // namespace

int main(int argc, char** argv)
{
  if (argc != 3)
  {
    std::fprintf(stderr, "usage: %s <stream> <packet sizes>\n", argv[0]);
    return 2;
  }
  std::ifstream fs(argv[1], std::ios::binary);
  std::vector<uint8_t> es((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
  std::ifstream ps(argv[2]);
  std::vector<int> sizes;
  for (int n; ps >> n;)
    sizes.push_back(n);

  Codec codec;
  CAEBitstreamPacker packer;
  CPackerMAT mat;
  KODI::AE::DIALNORM::CStream dialnorm;
  std::vector<std::vector<uint8_t>> frames;
  std::map<std::string, int> types;
  unsigned int rate = 0, channels = 0;
  bool consistent = true;
  std::string firstType;

  // bursts
  int bursts = 0, burstBad = 0;
  std::map<unsigned int, int> burstSizes;
  std::map<unsigned int, int> pcs;
  // MAT
  std::vector<std::vector<uint8_t>> matFrames;
  std::vector<int> unitsPerMat;
  // dialogue normalisation, with Hades' setting on
  int dialnormChanged = 0;
  std::vector<std::string> dialnormLines;

  auto checkBurst = [&](const uint8_t* src, unsigned int srcSize, uint16_t pc, uint16_t pd,
                        unsigned int expect) {
    const uint8_t* b = packer.GetBuffer();
    const unsigned int n = packer.GetSize();
    ++bursts;
    burstSizes[n]++;
    pcs[Word(b + 4)]++;
    bool ok = n == expect && Word(b) == 0xF872 && Word(b + 2) == 0x4E1F && Word(b + 4) == pc &&
              Word(b + 6) == pd;
    // the payload: the source in 16-bit words swapped to little-endian, zeros after it
    for (unsigned int i = 0; ok && i < srcSize; ++i)
      ok = b[8 + (i ^ 1)] == src[i];
    for (unsigned int i = 8 + ((srcSize + 1) & ~1u); ok && i < n; ++i)
      ok = b[i] == 0;
    if (!ok)
      ++burstBad;
  };

  auto process = [&](std::vector<uint8_t>& f) {
    CAEStreamInfo& info = codec.parser.GetStreamInfo();
    const std::string type = TypeName(info.m_type);
    types[type]++;
    if (frames.empty())
    {
      firstType = type;
      rate = codec.parser.GetSampleRate();
      channels = codec.parser.GetChannels();
    }
    else if (type != firstType || rate != codec.parser.GetSampleRate() ||
             channels != codec.parser.GetChannels())
      consistent = false;
    frames.push_back(f);

    if (info.m_type != CAEStreamInfo::STREAM_TYPE_TRUEHD)
    {
      std::vector<uint8_t> copy = f;
      if (const auto line = dialnorm.Apply(copy.data(), copy.size()))
        dialnormLines.push_back(*line);
      if (copy != f)
        ++dialnormChanged;
    }

    switch (info.m_type)
    {
      case CAEStreamInfo::STREAM_TYPE_AC3:
        packer.Pack(info, f.data(), f.size());
        checkBurst(f.data(), f.size(), 0x0001 | ((f[5] & 7) << 8), f.size() * 8, 6144);
        break;
      case CAEStreamInfo::STREAM_TYPE_EAC3:
        packer.Pack(info, f.data(), f.size());
        if (info.m_repeat == 1)
          checkBurst(f.data(), f.size(), 0x0015, f.size(), 24576);
        break;
      case CAEStreamInfo::STREAM_TYPE_DTS_512:
        packer.Pack(info, f.data(), f.size());
        checkBurst(f.data(), f.size(), 0x000B, f.size() * 8, 2048);
        break;
      case CAEStreamInfo::STREAM_TYPE_TRUEHD:
        if (mat.PackTrueHD(f.data(), f.size()))
        {
          std::vector<uint8_t> m = mat.GetOutputFrame();
          matFrames.push_back(m);
          packer.Pack(info, m.data(), m.size());
          checkBurst(m.data() + 8, m.size() - 8, 0x0016, 61424, 61440);
        }
        break;
      default:
        break;
    }
  };

  size_t at = 0;
  std::vector<uint8_t> f;
  for (int s : sizes)
  {
    if (s <= 0 || at + s > es.size())
    {
      std::fprintf(stderr, "packet sizes do not match the stream\n");
      return 2;
    }
    codec.AddData(es.data() + at, s);
    at += s;
    while (codec.GetFrame(f))
      process(f);
  }
  if (at != es.size())
  {
    std::fprintf(stderr, "packet sizes do not cover the stream\n");
    return 2;
  }

  // frames back to back are the stream, byte for byte, from its start; what the parser still holds
  // at the end (it hands an AC-3 frame on only once 8 bytes of the next are there) is said apart
  std::vector<uint8_t> joined;
  for (const auto& fr : frames)
    joined.insert(joined.end(), fr.begin(), fr.end());
  const bool framesArePrefix = !joined.empty() && joined.size() <= es.size() &&
                               std::equal(joined.begin(), joined.end(), es.begin());
  const size_t heldBack = es.size() - std::min(es.size(), joined.size());

  // MAT frames: their codes in place, and the TrueHD units in them, in order, with zeros between
  bool matCodes = true, matUnits = true;
  int matUnitsFound = 0;
  if (!matFrames.empty())
  {
    static const uint8_t start[20] = {0x07, 0x9E, 0x00, 0x03, 0x84, 0x01, 0x01, 0x01, 0x80, 0x00,
                                      0x56, 0xA5, 0x3B, 0xF4, 0x81, 0x83, 0x49, 0x80, 0x77, 0xE0};
    static const uint8_t middle[12] = {0xC3, 0xC1, 0x42, 0x49, 0x3B, 0xFA,
                                       0x82, 0x83, 0x49, 0x80, 0x77, 0xE0};
    static const uint8_t end[24] = {0xC3, 0xC2, 0xC0, 0xC4, 0, 0, 0, 0, 0, 0, 0, 0,
                                    0,    0,    0x97, 0x11, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint8_t> payload;
    for (const auto& m : matFrames)
    {
      if (m.size() != 61440 || std::memcmp(m.data() + 8, start, 20) ||
          std::memcmp(m.data() + 30716, middle, 12) || std::memcmp(m.data() + 61416, end, 24))
        matCodes = false;
      payload.insert(payload.end(), m.begin() + 28, m.begin() + 30716);
      payload.insert(payload.end(), m.begin() + 30728, m.begin() + 61416);
    }
    size_t pos = 0;
    for (const auto& u : frames)
    {
      size_t z = pos;
      while (z < payload.size() && payload[z] == 0)
        ++z;
      if (z >= payload.size())
        break; // units still in the packer when the stream ended
      bool found = false;
      for (size_t p = (z >= pos + 4 ? z - 4 : pos); p <= z && !found; ++p)
        if (p + u.size() <= payload.size() && std::equal(u.begin(), u.end(), payload.begin() + p))
        {
          found = true;
          pos = p + u.size();
        }
      if (!found)
      {
        matUnits = false;
        break;
      }
      ++matUnitsFound;
      // the MAT frame in which the unit starts (61376 bytes of each frame are not codes)
      const size_t frame = (pos - u.size()) / 61376;
      if (unitsPerMat.size() <= frame)
        unitsPerMat.resize(frame + 1, 0);
      unitsPerMat[frame]++;
    }
  }

  std::printf("{\n");
  std::printf("  \"type\": \"%s\",\n", firstType.c_str());
  std::printf("  \"types\": {");
  bool first = true;
  for (const auto& [k, v] : types)
  {
    std::printf("%s\"%s\": %d", first ? "" : ", ", k.c_str(), v);
    first = false;
  }
  std::printf("},\n");
  std::printf("  \"sample_rate\": %u,\n  \"channels\": %u,\n  \"consistent\": %s,\n", rate, channels,
              consistent ? "true" : "false");
  std::printf("  \"frames\": %zu,\n  \"stream_bytes\": %zu,\n  \"held_back_bytes\": %zu,\n",
              frames.size(), es.size(), heldBack);
  std::printf("  \"frames_are_prefix\": %s,\n", framesArePrefix ? "true" : "false");
  std::printf("  \"bursts\": %d,\n  \"bursts_bad\": %d,\n", bursts, burstBad);
  std::printf("  \"burst_sizes\": {");
  first = true;
  for (const auto& [k, v] : burstSizes)
  {
    std::printf("%s\"%u\": %d", first ? "" : ", ", k, v);
    first = false;
  }
  std::printf("},\n  \"pc\": {");
  first = true;
  for (const auto& [k, v] : pcs)
  {
    std::printf("%s\"0x%04x\": %d", first ? "" : ", ", k, v);
    first = false;
  }
  std::printf("},\n");
  std::printf("  \"mat_frames\": %zu,\n  \"mat_codes_ok\": %s,\n  \"mat_units_ok\": %s,\n",
              matFrames.size(), matCodes ? "true" : "false", matUnits ? "true" : "false");
  std::printf("  \"mat_units_found\": %d,\n  \"units_per_mat\": [", matUnitsFound);
  for (size_t i = 0; i < unitsPerMat.size(); ++i)
    std::printf("%s%d", i ? ", " : "", unitsPerMat[i]);
  std::printf("],\n");
  std::printf("  \"dialnorm_frames_changed\": %d,\n  \"dialnorm_lines\": [", dialnormChanged);
  for (size_t i = 0; i < dialnormLines.size(); ++i)
    std::printf("%s\"%s\"", i ? ", " : "", Esc(dialnormLines[i]).c_str());
  std::printf("],\n  \"log\": [");
  const auto& log = HarnessLog();
  for (size_t i = 0; i < log.size(); ++i)
    std::printf("%s\n    \"%s\"", i ? "," : "", Esc(log[i]).c_str());
  std::printf("\n  ]\n}\n");
  return 0;
}
'''
STUB_LOG_H = r'''// Stand-in for Kodi's utils/log.h on the build host: CLog::Log() formats as Kodi does ("{}"
// placeholders) and keeps the line, so that the harness can print what kodi.log would say.
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <vector>

#define LOGDEBUG 0
#define LOGINFO 1
#define LOGWARNING 2
#define LOGERROR 3
#define LOGFATAL 4

inline std::vector<std::string>& HarnessLog()
{
  static std::vector<std::string> lines;
  return lines;
}

class CLog
{
public:
  template<typename... Args>
  static void Log(int level, std::string_view format, Args&&... args)
  {
    static const char* names[] = {"debug", "info", "warning", "error", "fatal"};
    HarnessLog().push_back(std::string(names[level < 0 || level > 4 ? 0 : level]) + " <general>: " +
                           std::vformat(format, std::make_format_args(args...)));
  }
};
'''


class Fail(Exception):
    pass


def need(cond, what):
    if not cond:
        raise Fail(what)


def run(cmd, ok=True):
    env = dict(os.environ, LC_ALL="C", TZ="UTC")
    r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       env=env)
    if ok and r.returncode != 0:
        raise Fail("failed (%d): %s\n%s" % (r.returncode, " ".join(cmd),
                                            r.stderr.decode("utf-8", "replace")[-2000:]))
    return r


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class Tools:
    def __init__(self, a):
        self.ffmpeg, self.ffprobe, self.work = a.ffmpeg, a.ffprobe, a.work
        self.harness = None
        self.kodi, self.prefix, self.cxx = a.kodi, a.ffmpeg_prefix, a.cxx

    def build_harness(self):
        d = os.path.join(self.work, "harness")
        os.makedirs(os.path.join(d, "stub", "utils"), exist_ok=True)
        with open(os.path.join(d, "harness.cpp"), "w") as f:
            f.write(HARNESS_CPP)
        with open(os.path.join(d, "stub", "utils", "log.h"), "w") as f:
            f.write(STUB_LOG_H)
        out = os.path.join(d, "harness")
        run([self.cxx, "-std=c++20", "-O1", "-g0", "-Wall", "-Wno-unused-variable",
             "-I", os.path.join(d, "stub"), "-I", os.path.join(self.kodi, "xbmc"),
             "-I", os.path.join(self.prefix, "include"), os.path.join(d, "harness.cpp")]
            + [os.path.join(self.kodi, s) for s in KODI_SOURCES]
            + ["-L", os.path.join(self.prefix, "lib"), "-lavutil", "-lm", "-pthread", "-o", out])
        self.harness = out

    def decode(self, path):
        """(frames x channels) int32 array of what ffmpeg decodes"""
        r = run([self.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-i", path, "-map",
                 "0:a", "-c:a", "pcm_s32le", "-f", "wav", "-"])
        w = r.stdout
        need(w[:4] == b"RIFF" and w[8:12] == b"WAVE", "not a WAV")
        at = 12
        while w[at:at + 4] != b"data":
            at += 8 + int.from_bytes(w[at + 4:at + 8], "little")
            need(at + 8 <= len(w), "WAV without data")
        ch = int(self.probe(path)["channels"])
        return np.frombuffer(w[at + 8:], dtype="<i4").reshape(-1, ch)

    def probe(self, path):
        r = run([self.ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", path])
        d = json.loads(r.stdout)
        s = d["streams"][0]
        s["format"] = d["format"]
        return s

    def packet_sizes(self, path):
        r = run([self.ffprobe, "-v", "error", "-select_streams", "a:0", "-show_entries", "packet=size",
                 "-of", "json", path])
        return [int(p["size"]) for p in json.loads(r.stdout)["packets"]]

    def elementary(self, path, fmt):
        return run([self.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-i", path, "-map",
                    "0:a", "-c:a", "copy", "-f", fmt, "-"]).stdout

    def ebur128(self, path):
        r = run([self.ffmpeg, "-nostdin", "-hide_banner", "-nostats", "-i", path, "-map", "0:a",
                 "-af", "ebur128=peak=true", "-f", "null", "-"])
        t = r.stderr.decode()
        s = t[t.rfind("Summary:"):]
        i = re.search(r"I:\s+(-?[\d.]+|-inf) LUFS", s)
        p = re.search(r"True peak:\s+Peak:\s+(-?[\d.]+|-inf) dBFS", s)
        return float(i.group(1)), float(p.group(1))

    def kodi_path(self, es_bytes, sizes, tag):
        d = os.path.join(self.work, "harness", "runs")
        os.makedirs(d, exist_ok=True)
        es, sz = os.path.join(d, tag + ".es"), os.path.join(d, tag + ".sizes")
        with open(es, "wb") as f:
            f.write(es_bytes)
        with open(sz, "w") as f:
            f.write("\n".join(str(s) for s in sizes) + "\n")
        r = run([self.harness, es, sz])
        return json.loads(r.stdout)


# --- independent references -----------------------------------------------------------------------

def ref_chime():
    """the chime at the gain that make_audio.py uses is not known here: this is the shape at gain 1"""
    n = int(round(CHIME_TOTAL * RATE))
    out = np.zeros(n)
    k = np.arange(int(round(NOTE * RATE)))
    d = 1.0 - k / len(k)
    att = np.ones(len(k))
    att[:ATTACK] = np.sin(np.pi * k[:ATTACK] / (2 * ATTACK)) ** 2
    for start in CHIME_STARTS:
        for hz, onset in CHIME_NOTES:
            at = int(round((start + onset) * RATE))
            for mult, amp, p in PARTIALS:
                out[at:at + len(k)] += amp * d ** p * att * np.sin(2 * np.pi * ((k * hz * mult) % RATE) / RATE)
    return out


class SplitMix64:
    MASK = (1 << 64) - 1

    def __init__(self, seed):
        self.s = seed

    def uniform(self, n):
        out = np.empty(n)
        s, m = self.s, self.MASK
        for i in range(n):
            s = (s + 0x9E3779B97F4A7C15) & m
            z = ((s ^ (s >> 30)) * 0xBF58476D1CE4E5B9) & m
            z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & m
            out[i] = ((z ^ (z >> 31)) >> 11) * (2.0 / (1 << 53)) - 1.0
        self.s = s
        return out


def iir(b, a, x):
    """direct form I, b and a with a[0] = 1"""
    y = np.zeros(len(x))
    x1 = x2 = y1 = y2 = 0.0
    b0, b1, b2 = b
    _, a1, a2 = a
    xs = x.tolist()
    ys = [0.0] * len(xs)
    for i, v in enumerate(xs):
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        ys[i] = o
    y[:] = ys
    return y


def rbj(kind, f0, q):
    w0 = 2 * np.pi * f0 / RATE
    alpha = np.sin(w0) / (2 * q)
    c = np.cos(w0)
    a0 = 1 + alpha
    if kind == "lp":
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]
    else:
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    return [v / a0 for v in b], [1.0, -2 * c / a0, (1 - alpha) / a0]


def ref_pink(white):
    b = [0.0] * 7
    out = np.empty(len(white))
    for i, w in enumerate(white.tolist()):
        b[0] = 0.99886 * b[0] + w * 0.0555179
        b[1] = 0.99332 * b[1] + w * 0.0750759
        b[2] = 0.96900 * b[2] + w * 0.1538520
        b[3] = 0.86650 * b[3] + w * 0.3104856
        b[4] = 0.55000 * b[4] + w * 0.5329522
        b[5] = -0.7616 * b[5] - w * 0.0168980
        out[i] = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362
        b[6] = w * 0.115926
    return out


def ref_slot(rng, lfe):
    settle, n = 9600, int(SLOT * RATE)
    x = ref_pink(rng.uniform(n + settle))
    if lfe:
        # 8th-order Butterworth low pass at 100 Hz: four sections, Q = 1 / (2 cos((2k - 1) pi / 16))
        q = [1 / (2 * np.cos((2 * k - 1) * np.pi / 16)) for k in range(1, 5)]
        sections = [rbj("hp", 25, 1 / np.sqrt(2))] + [rbj("lp", 100, v) for v in q]
    else:
        sections = [rbj("hp", 150, 1 / np.sqrt(2))]
    for b, a in sections:
        x = iir(b, a, x)
    x = x[settle:]
    rms = np.sqrt(np.mean(x[FADE:n - FADE] ** 2))
    g = np.full(n, 10 ** ((LFE_DBFS if lfe else NOISE_DBFS) / 20) / rms)
    ramp = np.sin(np.pi * np.arange(FADE) / (2 * FADE)) ** 2
    g[:FADE] *= ramp
    g[n - FADE:] *= ramp[::-1]
    return x * g


def ref_walk(layout):
    index = list(LAYOUTS).index(layout)
    _, order, walk, _ = LAYOUTS[layout]
    rng = SplitMix64(SEED + index)
    total = int(round((WALK_LEAD + len(walk) * (SLOT + GAP)) * RATE))
    out = np.zeros((total, len(order)))
    for k, ch in enumerate(walk):
        at = int(round((WALK_LEAD + k * (SLOT + GAP)) * RATE))
        x = ref_slot(rng, ch == "LFE")
        out[at:at + len(x), order.index(ch)] = x
    return out


def k_weighted_loudness(x):
    """ITU-R BS.1770-4 integrated loudness of channels x (frames x channels, no LFE, all weight 1)"""
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]
    z = []
    for c in range(x.shape[1]):
        y = iir(b2, a2, iir(b1, a1, x[:, c]))
        z.append(y * y)
    z = np.array(z)
    block, hop = int(0.4 * RATE), int(0.1 * RATE)
    starts = range(0, x.shape[0] - block + 1, hop)
    ms = np.array([z[:, s:s + block].mean(axis=1).sum() for s in starts])
    lk = -0.691 + 10 * np.log10(np.maximum(ms, 1e-30))
    gated = ms[lk > -70]
    rel = -0.691 + 10 * np.log10(gated.mean()) - 10
    gated = ms[(lk > -70) & (lk > rel)]
    return -0.691 + 10 * np.log10(gated.mean())


# --- the bitstreams, read here -----------------------------------------------------------------

class Bits:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def read(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v


def ac3_frames(es):
    """(offset, size, dialnorm codes) of each AC-3 or E-AC-3 syncframe"""
    out, at = [], 0
    rates = [32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640]
    while at < len(es):
        need(es[at] == 0x0B and es[at + 1] == 0x77, "no syncword at %d" % at)
        bsid = es[at + 5] >> 3
        if bsid <= 10:
            fscod, frmsizecod = es[at + 4] >> 6, es[at + 4] & 0x3F
            need(fscod == 0, "AC-3 not at 48 kHz")
            size = rates[frmsizecod >> 1] * 4
            b = Bits(es, (at + 6) * 8)
            acmod = b.read(3)
            if (acmod & 1) and acmod != 1:
                b.read(2)
            if acmod & 4:
                b.read(2)
            if acmod == 2:
                b.read(2)
            b.read(1)
            dn = [b.read(5)]
        else:
            size = ((((es[at + 2] & 7) << 8) | es[at + 3]) + 1) * 2
            dn = [Bits(es, at * 8 + 45).read(5)]
        out.append((at, size, dn))
        at += size
    return out


def crc16_poly(data, poly):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc8_63(data):
    """FFmpeg's ff_mlp_checksum8() before its last XOR: CRC-8, polynomial 0x63, MSB first, start 0x3C"""
    crc = 0x3C
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x63) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def thd_check_bytes(major):
    """bytes 26 and 27 of a major sync as FFmpeg writes them: its CRC-16 (0x002D, MSB first) of
    bytes 0..23, which av_crc() keeps byte-swapped, XOR bytes 24 and 25, little-endian"""
    crc = crc16_poly(major[:24], 0x002D)
    return (crc >> 8) ^ major[24], (crc & 0xFF) ^ major[25]


def ac3_crc_ok(frame):
    """the CRC of the whole frame after the syncword is 0 (A/52: crc2), and for AC-3 also of 5/8"""
    if crc16_poly(frame[2:], 0x8005) != 0:
        return False
    if frame[5] >> 3 <= 10:
        five8 = ((len(frame) >> 2) + (len(frame) >> 4)) << 1
        return crc16_poly(frame[2:five8], 0x8005) == 0
    return True


def thd_units(es):
    """(offset, length, input timing, major sync bytes or None) of each TrueHD access unit"""
    out, at = [], 0
    while at < len(es):
        need(at + 4 <= len(es), "TrueHD: truncated unit header")
        length = (((es[at] & 0x0F) << 8) | es[at + 1]) * 2
        need(length >= 4 and at + length <= len(es), "TrueHD: unit length %d at %d" % (length, at))
        timing = (es[at + 2] << 8) | es[at + 3]
        major = es[at + 4:at + 32] if es[at + 4:at + 8] == b"\xf8\x72\x6f\xba" else None
        out.append((at, length, timing, major))
        at += length
    return out


def flac_header_in_mkv(path):
    """the FLAC header that the Matroska file carries as its track's CodecPrivate ("fLaC", STREAMINFO
    and, for a layout that is not FLAC's own, a VORBIS_COMMENT with the channel mask):
    (sample rate, channels, bits per sample, comments)"""
    with open(path, "rb") as f:
        d = f.read()
    need(d.count(b"fLaC") == 1, "no single FLAC header in the Matroska file")
    at = d.index(b"fLaC") + 4
    need(d[at] & 0x7F == 0, "the FLAC header does not start with STREAMINFO")
    si = Bits(d, (at + 4) * 8 + 80)
    rate, channels, bits = si.read(20), si.read(3) + 1, si.read(5) + 1
    comments = {}
    while True:
        last, kind, size = d[at] >> 7, d[at] & 0x7F, int.from_bytes(d[at + 1:at + 4], "big")
        if kind == 4:
            b = d[at + 4:at + 4 + size]
            vlen = struct.unpack("<I", b[:4])[0]
            n = struct.unpack("<I", b[4 + vlen:8 + vlen])[0]
            p = 8 + vlen
            for _ in range(n):
                ln = struct.unpack("<I", b[p:p + 4])[0]
                k, _, v = b[p + 4:p + 4 + ln].decode("utf-8").partition("=")
                comments[k.upper()] = v
                p += 4 + ln
        at += 4 + size
        if last:
            return rate, channels, bits, comments


# --- the checks -------------------------------------------------------------------------------------

class Ctx:
    """one folder of sounds, with what is read from it kept"""

    def __init__(self, tools, folder, ma):
        self.t, self.dir, self.ma = tools, folder, ma
        with open(os.path.join(folder, "sounds.json")) as f:
            self.sounds = json.load(f)
        self.cache = {}

    def path(self, name):
        return os.path.join(self.dir, name)

    def get(self, key, fn):
        if key not in self.cache:
            self.cache[key] = fn()
        return self.cache[key]

    def pt(self, ident):
        return next(e for e in self.sounds["passthrough"] if e["id"] == ident)

    def decoded(self, name):
        return self.get(("dec", name), lambda: self.t.decode(self.path(name)))

    def es(self, ident):
        fmt = {"ac3": "ac3", "eac3": "eac3", "truehd": "truehd"}[ident]
        return self.get(("es", ident), lambda: self.t.elementary(self.path(self.pt(ident)["file"]), fmt))

    def sizes(self, ident):
        return self.get(("sizes", ident), lambda: self.t.packet_sizes(self.path(self.pt(ident)["file"])))

    def kodi(self, ident):
        return self.get(("kodi", ident), lambda: self.t.kodi_path(
            self.es(ident), self.sizes(ident), "%s-%x" % (ident, id(self))))


def c_schema(ctx):
    s = ctx.sounds
    need(s.get("version") == 1, "version")
    need(set(s) == {"version", "passthrough", "not_checkable", "speakers"}, "keys %s" % sorted(s))
    ids = [e["id"] for e in s["passthrough"]]
    need(sorted(ids) == sorted(PASSTHROUGH), "passthrough ids %s" % ids)
    for e in s["passthrough"]:
        dec, setting, _ = PASSTHROUGH[e["id"]]
        need(e["file"] == "hades-check-%s.mkv" % e["id"], "file name %s" % e["file"])
        need(e["kodi_decoder"] == dec and e["setting"] == setting, "decoder/setting of %s" % e["id"])
        for k in ("duration", "sound_from", "sound_to", "channels", "sample_rate", "sha256"):
            need(k in e, "%s lacks %s" % (e["id"], k))
        need(0 < e["sound_from"] < e["sound_to"] <= e["duration"] <= 8.0, "times of %s" % e["id"])
        need(e["sha256"] == sha256(ctx.path(e["file"])), "sha256 of %s" % e["file"])
    nc = {e["id"]: (e["setting"], e["why"]) for e in s["not_checkable"]}
    need(nc == NOT_CHECKABLE, "not_checkable %s" % nc)
    need(not set(nc) & set(ids), "a format both checkable and not")
    layouts = [e["layout"] for e in s["speakers"]]
    need(layouts == list(LAYOUTS), "speaker layouts %s" % layouts)
    for e in s["speakers"]:
        _, order, walk, mask = LAYOUTS[e["layout"]]
        need(e["file"] == "hades-check-speakers-%s.mkv" % e["layout"], "file %s" % e["file"])
        need(e["channels"] == len(order) and e["passes"] == 1, "channels/passes of %s" % e["layout"])
        need([x["channel"] for x in e["slots"]] == walk, "slot order of %s" % e["layout"])
        need(e["sha256"] == sha256(ctx.path(e["file"])), "sha256 of %s" % e["file"])
        prev = 0.0
        for x in e["slots"]:
            need(prev <= x["from"] < x["to"] <= e["duration"], "slot times of %s" % e["layout"])
            prev = x["to"]


def c_probe_passthrough(ctx):
    for e in ctx.sounds["passthrough"]:
        p = ctx.t.probe(ctx.path(e["file"]))
        need(p["codec_name"] == e["id"], "%s codec %s" % (e["file"], p["codec_name"]))
        need(int(p["channels"]) == e["channels"] == 2, "%s channels %s" % (e["file"], p["channels"]))
        need(int(p["sample_rate"]) == e["sample_rate"] == RATE, "%s rate" % e["file"])
        need_one_audio_track(p, e["file"])
        dur = float(p["format"]["duration"])
        need(abs(dur - e["duration"]) <= 0.04, "%s lasts %.3f s, said %.3f" % (e["file"], dur, e["duration"]))


def need_one_audio_track(p, name):
    """a .mkv (Kodi's video player, as a film) holding one track, of sound (the display stays as it is)"""
    need(name.endswith(".mkv") and p["format"]["format_name"].startswith("matroska"), "%s not .mkv" % name)
    need(int(p["format"]["nb_streams"]) == 1 and p["codec_type"] == "audio",
         "%s: %s streams, the first %s" % (name, p["format"]["nb_streams"], p["codec_type"]))


def c_truehd_lossless(ctx):
    dec = ctx.decoded(ctx.pt("truehd")["file"])
    src = ctx.ma.source_chime()
    need(dec.shape == src.shape, "TrueHD decodes to %s, source %s" % (dec.shape, src.shape))
    need(np.array_equal(dec, src), "TrueHD does not decode bit for bit to its source")
    # and FFmpeg's decoder had nothing to say of it (it reports failed checks and goes on)
    r = run([ctx.t.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-i",
             ctx.path(ctx.pt("truehd")["file"]), "-f", "null", "-"])
    need(not r.stderr.strip(), "FFmpeg's TrueHD decoder: %s" % r.stderr.decode()[:200])


def chime_gain_and_error(dec_mono):
    ref = ref_chime()
    n = min(len(ref), len(dec_mono))
    g = float(np.dot(dec_mono[:n], ref[:n]) / np.dot(ref[:n], ref[:n]))
    return g, np.abs(dec_mono[:n] - g * ref[:n]).max(), ref


def c_chime_reference(ctx):
    """the TrueHD chime (lossless) is the designed chime: notes, partials, envelopes, timing"""
    dec = ctx.decoded(ctx.pt("truehd")["file"]).astype(np.float64) / 2 ** 31
    need(np.array_equal(dec[:, 0], dec[:, 1]), "the two channels differ")
    g, err, _ = chime_gain_and_error(dec[:, 0])
    # one 24-bit step of rounding, and the reference's own sine (numpy) against make_audio's
    need(err <= 1.5 / 2 ** 23, "differs from the designed chime by %.3g (gain %.5f)" % (err, g))


def delay_of(dec, ref):
    a = dec[: int(2.2 * RATE)]
    best, lag = -1, 0
    for d in range(0, 2048):
        v = float(np.dot(a[d:d + int(1.8 * RATE)], ref[:int(1.8 * RATE)]))
        if v > best:
            best, lag = v, d
    return lag


def decode_delay(ctx, ident):
    """samples by which the decoded chime lags its source: 0 where FFmpeg's decoder drops the
    encoder's priming (the Matroska muxer records it), 256 (AC-3's) where not"""
    def find():
        dec = ctx.decoded(ctx.pt(ident)["file"]).astype(np.float64) / 2 ** 31
        ref = ctx.ma.source_chime()[:, 0].astype(np.float64) / 2 ** 31
        return delay_of(dec[:, 0], ref)
    return ctx.get(("delay", ident), find)


def c_lossy_roundtrip(ctx):
    for ident in ("ac3", "eac3"):
        dec = ctx.decoded(ctx.pt(ident)["file"]).astype(np.float64) / 2 ** 31
        ref = ctx.ma.source_chime()[:, 0].astype(np.float64) / 2 ** 31
        lag = decode_delay(ctx, ident)
        need(lag in (0, 256), "%s: delay %d samples, the encoder's priming is 256" % (ident, lag))
        for c in range(2):
            d = dec[lag:lag + len(ref), c]
            r = ref[:len(d)]
            snr = 10 * np.log10(np.sum(r ** 2) / max(np.sum((d - r) ** 2), 1e-30))
            need(snr >= 30.0, "%s channel %d: %.1f dB of signal to coding noise" % (ident, c, snr))


def c_timing(ctx):
    for e in ctx.sounds["passthrough"]:
        dec = ctx.decoded(e["file"]).astype(np.float64) / 2 ** 31
        delay = decode_delay(ctx, e["id"]) if e["id"] != "truehd" else 0
        a = int(e["sound_from"] * RATE) + delay
        b = int(e["sound_to"] * RATE) + delay
        # silence: TrueHD gives zeros; an AC-3 decoder fills the mantissas the stream gives no bits
        # with its own dither (A/52, dithflag), measured here at about -101 dBFS peak: below -90
        quiet = 2 ** -20 if e["id"] == "truehd" else 10 ** (-90 / 20)
        # (a lossy codec spreads an onset back over its transform, 512 samples at most)
        need(np.abs(dec[:a - 600]).max() < quiet, "%s sounds before sound_from" % e["id"])
        first = np.sqrt(np.mean(dec[a:a + 4800] ** 2))
        need(first > 10 ** (-60 / 20), "%s silent just after sound_from" % e["id"])
        need(np.abs(dec[b + 1440:]).max() < quiet, "%s sounds after sound_to" % e["id"])
        last = np.sqrt(np.mean(dec[b - 24000:b] ** 2))
        need(last > 10 ** (-100 / 20), "%s already silent well before sound_to" % e["id"])
        # twice: an onset at each start
        env = np.sqrt(np.mean(dec[: len(dec) // 2400 * 2400, 0].reshape(-1, 2400) ** 2, axis=1))
        on = [i for i in range(1, len(env)) if env[i] > 10 ** (-50 / 20) and env[i - 1] <= 10 ** (-50 / 20)]
        need(len(on) == 2, "%s: %d onsets" % (e["id"], len(on)))


def c_level(ctx):
    for e in ctx.sounds["passthrough"]:
        i, tp = ctx.t.ebur128(ctx.path(e["file"]))
        need(abs(i - LEVEL_TARGET) <= LEVEL_WINDOW, "%s at %.1f LUFS" % (e["id"], i))
        need(tp <= TRUE_PEAK_MAX, "%s true peak %.1f dBTP" % (e["id"], tp))
        need(abs(e.get("loudness_lufs", i) - i) < 0.05, "%s: sounds.json says another level" % e["id"])
        if e["id"] == "truehd":
            dec = ctx.decoded(e["file"]).astype(np.float64) / 2 ** 31
            own = k_weighted_loudness(dec)
            need(abs(own - i) <= 0.3, "BS.1770 here %.2f against ebur128 %.2f" % (own, i))


def c_dialnorm(ctx):
    for ident in ("ac3", "eac3"):
        frames = ac3_frames(ctx.es(ident))
        need(frames, "%s: no frames" % ident)
        bad = [at for at, size, dn in frames if any(v != 31 for v in dn)]
        need(not bad, "%s: %d frames with a dialogue normalisation other than -31 dB" % (ident, len(bad)))
        k = ctx.kodi(ident)
        need(k["dialnorm_frames_changed"] == 0, "%s: Hades' rewriter changed %d frames"
             % (ident, k["dialnorm_frames_changed"]))
        need(any("already asks for no attenuation" in x for x in k["dialnorm_lines"]),
             "%s: rewriter said %s" % (ident, k["dialnorm_lines"]))


def c_ac3_crc(ctx):
    for ident in ("ac3", "eac3"):
        es = ctx.es(ident)
        for at, size, _ in ac3_frames(es):
            need(ac3_crc_ok(es[at:at + size]), "%s: frame at %d fails its CRC" % (ident, at))


def c_kodi_parser(ctx):
    for ident, (_, _, kind) in PASSTHROUGH.items():
        k = ctx.kodi(ident)
        need(k["type"] == kind and k["consistent"], "%s parsed as %s %s" % (ident, k["type"], k["types"]))
        need(k["sample_rate"] == RATE and k["channels"] == 2, "%s: %d Hz, %d channels"
             % (ident, k["sample_rate"], k["channels"]))
        need(k["frames_are_prefix"], "%s: the frames are not the stream as it came" % ident)
        # the parser hands an AC-3 or E-AC-3 frame on only when 8 bytes of the next one are there
        # (AEStreamInfo.cpp, TrySyncAC3): the last one stays in it at the end; TrueHD gives all
        held = ac3_frames(ctx.es(ident))[-1][1] if kind != "TRUEHD" else 0
        need(k["held_back_bytes"] == held, "%s: %d bytes not handed on, %d expected"
             % (ident, k["held_back_bytes"], held))
        line = {"AC3": "AC3 stream detected (2 channels, 48000Hz)",
                "EAC3": "E-AC3 stream detected (2 channels, 48000Hz)",
                "TRUEHD": "TrueHD stream detected (2 channels, 48000Hz)"}[kind]
        need(any(line in x for x in k["log"]), "%s: no '%s' in %s" % (ident, line, k["log"][:4]))
        need(not any("sync lost" in x.lower() or "Sync Lost" in x for x in k["log"]), "%s lost sync" % ident)


def c_kodi_packer(ctx):
    for ident in ("ac3", "eac3"):
        k = ctx.kodi(ident)
        size = {"ac3": 6144, "eac3": 24576}[ident]
        need(k["bursts"] == k["frames"] and k["bursts_bad"] == 0, "%s: %d bursts, %d bad, %d frames"
             % (ident, k["bursts"], k["bursts_bad"], k["frames"]))
        need(k["burst_sizes"] == {str(size): k["frames"]}, "%s: burst sizes %s" % (ident, k["burst_sizes"]))
    k = ctx.kodi("truehd")
    need(k["mat_frames"] > 0 and k["mat_codes_ok"], "TrueHD: MAT codes")
    need(k["mat_units_ok"], "TrueHD: the units are not in the MAT frames as they came")
    # the units of a MAT frame not yet full stay in the packer at the end: fewer than 24 more
    need(k["frames"] - 24 <= k["mat_units_found"] <= k["frames"], "TrueHD: %d of %d units in MAT"
         % (k["mat_units_found"], k["frames"]))
    upm = k["units_per_mat"]
    need(all(n == 24 for n in upm[:k["mat_frames"]]), "TrueHD: units per MAT frame %s" % upm)
    need(k["bursts"] == k["mat_frames"] and k["bursts_bad"] == 0 and
         k["burst_sizes"] == {"61440": k["mat_frames"]} and k["pc"] == {"0x0016": k["mat_frames"]},
         "TrueHD bursts %s %s" % (k["burst_sizes"], k["pc"]))


def c_truehd_stream(ctx):
    es = ctx.es("truehd")
    units = thd_units(es)
    majors = [i for i, u in enumerate(units) if u[3] is not None]
    need(majors and majors[0] == 0, "TrueHD does not start on a major sync")
    need(all(b - a == 16 for a, b in zip(majors, majors[1:])), "major syncs not every 16 units")
    for i, (at, length, timing, major) in enumerate(units):
        if i:
            need(timing == (units[i - 1][2] + 40) & 0xFFFF, "input timing at unit %d" % i)
        # the check nibble: XOR of the unit header and substream directory nibbles is 0xF
        hdr = 4 + (28 if major is not None else 0)
        words = [es[at], es[at + 1], es[at + 2], es[at + 3], es[at + hdr], es[at + hdr + 1]]
        x = 0
        for w in words:
            x ^= w
        need(((x >> 4) ^ x) & 0xF == 0xF, "check nibble of unit %d" % i)
        # the one substream: where it ends, and its parity and checksum bytes (as FFmpeg's decoder,
        # mlpdec.c, and a Dolby decoder check them)
        word = (es[at + hdr] << 8) | es[at + hdr + 1]
        need(not word & 0x8000 and word & 0x2000, "substream directory of unit %d" % i)
        start = at + hdr + 2
        need(start + (word & 0x0FFF) * 2 == at + length, "substream end of unit %d" % i)
        data = es[start:at + length]
        par = 0
        for byte in data[:-2]:
            par ^= byte
        need(par ^ 0xA9 == data[-2], "substream parity of unit %d" % i)
        need(crc8_63(data[:-3]) ^ data[-3] == data[-1], "substream checksum of unit %d" % i)
        if major is not None:
            need(thd_check_bytes(major) == (major[26], major[27]), "major sync CRC of unit %d" % i)
            need(major[4] >> 4 == 0, "rate bits")
            need(major[5] >> 6 == 0, "2ch presentation modifier %d (1 = Lt/Rt)" % (major[5] >> 6))
            need(((major[6] & 0x1F) << 8 | major[7]) == 1, "8ch channel assignment")
            need((major[8] << 8 | major[9]) == 0xB752, "signature")
            need(major[16] >> 4 == 1, "%d substreams" % (major[16] >> 4))


def c_speakers_probe(ctx):
    for e in ctx.sounds["speakers"]:
        name, order, _, mask = LAYOUTS[e["layout"]]
        p = ctx.t.probe(ctx.path(e["file"]))
        need_one_audio_track(p, e["file"])
        need(p["codec_name"] == "flac" and int(p["channels"]) == len(order), "%s codec" % e["file"])
        need(int(p["sample_rate"]) == RATE and p.get("bits_per_raw_sample") == "16" and
             p.get("sample_fmt") == "s16" and e.get("bits_per_sample") == 16, "%s format" % e["file"])
        # the layout as FFmpeg's Matroska demuxer (Kodi's) reads it back
        need(p.get("channel_layout") == name, "%s layout %s" % (e["file"], p.get("channel_layout")))
        need(int(e["channel_mask"], 16) == mask, "%s: sounds.json mask %s" % (e["file"], e["channel_mask"]))
        # in the file: FLAC's own order for its number of channels, or the mask in its header
        rate, channels, bits, c = flac_header_in_mkv(ctx.path(e["file"]))
        need((rate, channels, bits) == (RATE, len(order), 16), "%s FLAC header %s" % (e["file"], (rate, channels, bits)))
        written = c.get("WAVEFORMATEXTENSIBLE_CHANNEL_MASK")
        if FLAC_NATIVE[len(order)] == mask:
            need(written is None, "%s: a mask %s where FLAC's own order is meant" % (e["file"], written))
        else:
            need(written is not None and int(written, 16) == mask, "%s mask %s" % (e["file"], written))
        need(abs(float(p["format"]["duration"]) - e["duration"]) < 0.01, "%s duration" % e["file"])


def c_speakers_slots(ctx):
    for e in ctx.sounds["speakers"]:
        _, order, _, _ = LAYOUTS[e["layout"]]
        x = ctx.decoded(e["file"]).astype(np.float64) / 2 ** 31
        need(len(x) == int(round(e["duration"] * RATE)), "%s length" % e["file"])
        covered = np.zeros(len(x), bool)
        for s in e["slots"]:
            a, b = int(round(s["from"] * RATE)), int(round(s["to"] * RATE))
            c = order.index(s["channel"])
            others = np.delete(x[a:b], c, axis=1)
            need(not others.any(), "%s: other channels sound during %s" % (e["file"], s["channel"]))
            body = x[a + FADE:b - FADE, c]
            rms = 20 * np.log10(np.sqrt(np.mean(body ** 2)))
            want = LFE_DBFS if s["channel"] == "LFE" else NOISE_DBFS
            need(abs(rms - want) <= 0.1, "%s %s at %.2f dBFS" % (e["file"], s["channel"], rms))
            need(np.abs(x[a:a + 48, c]).max() < np.abs(body).max() / 20, "%s %s: no fade in" % (e["file"], s["channel"]))
            covered[a:b] = True
            if s["channel"] in ("FL", "FR", "FC"):
                lufs = k_weighted_loudness(x[a:b, c:c + 1])
                need(abs(lufs - LEVEL_TARGET) <= LEVEL_WINDOW, "%s %s at %.1f LUFS" % (e["file"], s["channel"], lufs))
        need(not x[~covered].any(), "%s: sound outside the slots" % e["file"])


def c_speakers_spectrum(ctx):
    for e in ctx.sounds["speakers"]:
        _, order, _, _ = LAYOUTS[e["layout"]]
        x = ctx.decoded(e["file"]).astype(np.float64) / 2 ** 31
        for s in e["slots"]:
            a, b = int(round(s["from"] * RATE)) + FADE, int(round(s["to"] * RATE)) - FADE
            v = x[a:b, order.index(s["channel"])]
            seg = 8192
            spec = np.mean([np.abs(np.fft.rfft(v[i:i + seg] * np.hanning(seg))) ** 2
                            for i in range(0, len(v) - seg, seg // 2)], axis=0)
            f = np.fft.rfftfreq(seg, 1 / RATE)
            total = spec.sum()
            if s["channel"] == "LFE":
                hi = spec[f > 150].sum() / total
                need(hi < 10 ** (-30 / 10), "%s LFE: %.1f dB above 150 Hz" % (e["file"], 10 * np.log10(hi)))
            else:
                lo = spec[f < 80].sum() / total
                need(lo < 10 ** (-20 / 10), "%s %s: %.1f dB below 80 Hz" % (e["file"], s["channel"], 10 * np.log10(lo)))
                band = (f > 400) & (f < 8000)
                slope = np.polyfit(np.log10(f[band]), 10 * np.log10(spec[band]), 1)[0]
                need(-11.5 < slope < -8.5, "%s %s: %.1f dB/decade, pink is -10" % (e["file"], s["channel"], slope))


def c_speakers_reference(ctx):
    for e in ctx.sounds["speakers"]:
        x = ctx.decoded(e["file"]).astype(np.int64)
        ref = ref_walk(e["layout"])
        need(ref.shape == x.shape, "%s: %s against %s" % (e["file"], x.shape, ref.shape))
        q = np.clip(np.round(ref * 2 ** 31 / NOISE_STEP) * NOISE_STEP, -2 ** 31, 2 ** 31 - NOISE_STEP)
        diff = np.abs(x - q.astype(np.int64))
        need(diff.max() <= NOISE_STEP, "%s differs from the reference by %d steps" % (e["file"], diff.max() // NOISE_STEP))
        need((diff == 0).mean() > 0.9999, "%s: %.5f of samples as the reference" % (e["file"], (diff == 0).mean()))


def dts_evidence(ctx):
    """what says that DTS and DTS-HD cannot be checked here: the encoders of this FFmpeg, and two
    seconds of the chime through its DTS encoder"""
    def make():
        t = ctx.t
        enc = run([t.ffmpeg, "-hide_banner", "-encoders"]).stdout.decode()
        d = os.path.join(t.work, "dts")
        os.makedirs(d, exist_ok=True)
        src, out = os.path.join(d, "src.s32"), os.path.join(d, "chime.dts")
        ctx.ma.source_chime().astype("<i4").tofile(src)
        run([t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
             "-ch_layout", "stereo", "-i", src, "-ss", "1.4", "-t", "2", "-c:a", "dca", "-strict",
             "experimental", "-f", "dts", out])
        with open(out, "rb") as f:
            data = f.read()
        return {"encoders": enc, "dts": data, "sizes": t.packet_sizes(out)}
    return ctx.get("dts", make)


def c_not_checkable_evidence(ctx):
    ev = dts_evidence(ctx)
    lines = [x for x in ev["encoders"].splitlines()
             if re.match(r"\s*A\S{5}\s", x) and (" dca " in x or "(codec dts)" in x)]
    need(len(lines) == 1 and lines[0].split()[1] == "dca", "DTS encoders: %s" % lines)
    need(lines[0].split()[0][3] == "X", "the DTS encoder is not experimental any more: re-evaluate "
         "(%s)" % lines[0].strip())
    at = 0
    for size in ev["sizes"]:
        f = ev["dts"][at:at + size]
        need(f[:4] == b"\x7f\xfe\x80\x01", "DTS frame without the core sync")
        fsize = ((((f[5] & 3) << 12) | (f[6] << 4) | (f[7] >> 4)) + 1)
        need(fsize == size, "DTS frame of %d bytes holds %d: something after the core" % (fsize, size))
        at += size
    k = ctx.t.kodi_path(ev["dts"], ev["sizes"], "dts-%x" % id(ctx))
    need(k["types"] == {"DTS_512": k["frames"]} and k["sample_rate"] == RATE and k["channels"] == 2,
         "Kodi reads the DTS sample as %s" % k["types"])
    need(k["bursts_bad"] == 0 and k["burst_sizes"] == {"2048": k["frames"]}, "DTS bursts %s" % k["burst_sizes"])


def c_sizes(ctx):
    names = [e["file"] for e in ctx.sounds["passthrough"] + ctx.sounds["speakers"]] + ["sounds.json"]
    total = sum(os.path.getsize(ctx.path(n)) for n in names)
    need(total <= SIZE_LIMIT, "the sounds take %d bytes" % total)
    need(sorted(os.listdir(ctx.dir)) == sorted(names), "unexpected files %s" % sorted(set(os.listdir(ctx.dir)) - set(names)))


CHECKS = [
    ("schema", c_schema), ("probe_passthrough", c_probe_passthrough),
    ("truehd_lossless", c_truehd_lossless), ("chime_reference", c_chime_reference),
    ("lossy_roundtrip", c_lossy_roundtrip), ("timing", c_timing), ("level", c_level),
    ("dialnorm", c_dialnorm), ("ac3_crc", c_ac3_crc), ("kodi_parser", c_kodi_parser),
    ("kodi_packer", c_kodi_packer), ("truehd_stream", c_truehd_stream),
    ("speakers_probe", c_speakers_probe), ("speakers_slots", c_speakers_slots),
    ("speakers_spectrum", c_speakers_spectrum), ("speakers_reference", c_speakers_reference),
    ("not_checkable_evidence", c_not_checkable_evidence), ("sizes", c_sizes),
]


# --- make_audio.py, run and read ---------------------------------------------------------------

class MakeAudio:
    def __init__(self, path, tools):
        spec = importlib.util.spec_from_file_location("make_audio_under_test", path)
        self.m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.m)
        self.path, self.t = path, tools
        self._src = None

    def run(self, out):
        os.makedirs(out)
        run([sys.executable, "-I", self.path, "--ffmpeg", self.t.ffmpeg, "--ffprobe", self.t.ffprobe,
             "--out", out])

    def source_chime(self):
        if self._src is None:
            sin = self.m.sine_table()
            mono = self.m.chime_samples(sin)
            st = []
            for v in mono:
                st += [v, v]
            self._src = np.frombuffer(self.m.to_s32(st, 24).tobytes(), dtype="<i4").reshape(-1, 2)
        return self._src


# --- mutants -----------------------------------------------------------------------------------

def copy_ctx(ctx, name):
    d = os.path.join(ctx.t.work, "mutants", name)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(ctx.dir, d)
    return d


def reencode_walk(tools, path, x, layout):
    raw = path + ".raw"
    x.astype("<i4").tofile(raw)
    run([tools.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
         "-ch_layout", layout, "-i", raw, "-c:a", "flac", "-sample_fmt", "s16", "-bits_per_raw_sample", "16",
         "-f", "matroska", path])
    os.remove(raw)


def m_channels_swapped(ctx):
    d = copy_ctx(ctx, "channels_swapped")
    x = ctx.decoded("hades-check-speakers-5.1.mkv").copy()
    x[:, [0, 1]] = x[:, [1, 0]]
    reencode_walk(ctx.t, os.path.join(d, "hades-check-speakers-5.1.mkv"), x, "5.1")
    return d


def m_level_high(ctx):
    d = copy_ctx(ctx, "level_high")
    src = os.path.join(d, "src.s32")
    ctx.ma.source_chime().astype("<i4").tofile(src)
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
         "-ch_layout", "stereo", "-i", src, "-af", "volume=10dB", "-c:a", "ac3", "-b:a", "192k",
         "-dialnorm", "-31", "-f", "matroska", os.path.join(d, "hades-check-ac3.mkv")])
    os.remove(src)
    return d


def m_seed(ctx):
    d = copy_ctx(ctx, "seed")
    m = ctx.ma.m
    old = m.SEED
    m.SEED = old ^ 1
    try:
        sin = m.sine_table()
        buf, _, _ = m.walk(0, sin)
        x = np.frombuffer(m.to_s32(buf, m.PRECISION_BITS).tobytes(), dtype="<i4").reshape(-1, 2)
    finally:
        m.SEED = old
    reencode_walk(ctx.t, os.path.join(d, "hades-check-speakers-2.0.mkv"), x, "stereo")
    return d


def remux_truehd(ctx, d, es):
    thd = os.path.join(d, "x.thd")
    with open(thd, "wb") as f:
        f.write(es)
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "truehd", "-i", thd,
         "-c:a", "copy", "-f", "matroska", os.path.join(d, "hades-check-truehd.mkv")])
    os.remove(thd)


def m_truehd_crc(ctx):
    """one major sync with a CRC that does not check. FFmpeg's demuxer drops such a group when the
    file is remuxed, so the stream reaches Kodi's parser here as it is, packet by packet"""
    d = copy_ctx(ctx, "truehd_crc")
    es = bytearray(ctx.es("truehd"))
    units = thd_units(es)
    at = [u[0] for u in units if u[3] is not None][3]
    es[at + 4 + 26] ^= 0x01
    remux_truehd(ctx, d, bytes(es))
    return d, {("es", "truehd"): bytes(es), ("sizes", "truehd"): ctx.sizes("truehd")}


def m_truehd_substream(ctx):
    d = copy_ctx(ctx, "truehd_substream")
    es = bytearray(ctx.es("truehd"))
    at, length, _, _ = thd_units(es)[40]
    es[at + length - 1] ^= 0x5A  # the substream's checksum byte, at its end
    remux_truehd(ctx, d, bytes(es))
    return d


def m_ltrt(ctx):
    d = copy_ctx(ctx, "ltrt")
    es = bytearray(ctx.es("truehd"))
    for at, _, _, major in thd_units(es):
        if major is not None:
            ms = at + 4
            es[ms + 5] |= 0x40
            es[ms + 26], es[ms + 27] = thd_check_bytes(es[ms:ms + 28])
    remux_truehd(ctx, d, bytes(es))
    return d


def edit_json(d, fn):
    p = os.path.join(d, "sounds.json")
    with open(p) as f:
        s = json.load(f)
    fn(s)
    with open(p, "w") as f:
        json.dump(s, f, indent=2)


def m_from_late(ctx):
    d = copy_ctx(ctx, "from_late")
    edit_json(d, lambda s: s["passthrough"][0].__setitem__("sound_from", s["passthrough"][0]["sound_from"] + 0.3))
    return d


def m_to_early(ctx):
    d = copy_ctx(ctx, "to_early")
    edit_json(d, lambda s: s["passthrough"][1].__setitem__("sound_to", s["passthrough"][1]["sound_to"] - 0.5))
    return d


def m_side_layout(ctx):
    d = copy_ctx(ctx, "side_layout")
    x = ctx.decoded("hades-check-speakers-5.1.mkv")
    reencode_walk(ctx.t, os.path.join(d, "hades-check-speakers-5.1.mkv"), x, "5.1(side)")
    return d


def m_dialnorm27(ctx):
    d = copy_ctx(ctx, "dialnorm27")
    src = os.path.join(d, "src.s32")
    ctx.ma.source_chime().astype("<i4").tofile(src)
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
         "-ch_layout", "stereo", "-i", src, "-c:a", "ac3", "-b:a", "192k", "-dialnorm", "-27",
         "-f", "matroska", os.path.join(d, "hades-check-ac3.mkv")])
    os.remove(src)
    return d


def m_eac3_51(ctx):
    d = copy_ctx(ctx, "eac3_51")
    src = os.path.join(d, "src.s32")
    st = ctx.ma.source_chime()
    six = np.zeros((len(st), 6), dtype="<i4")
    six[:, :2] = st
    six.tofile(src)
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
         "-ch_layout", "5.1", "-i", src, "-c:a", "eac3", "-b:a", "384k", "-dialnorm", "-31",
         "-f", "matroska", os.path.join(d, "hades-check-eac3.mkv")])
    os.remove(src)
    return d


def m_lfe_fullband(ctx):
    d = copy_ctx(ctx, "lfe_fullband")
    m = ctx.ma.m
    old = m.LFE_FILTERS
    m.LFE_FILTERS = m.MAIN_FILTERS
    try:
        sin = m.sine_table()
        buf, _, _ = m.walk(1, sin)
        x = np.frombuffer(m.to_s32(buf, m.PRECISION_BITS).tobytes(), dtype="<i4").reshape(-1, 6)
    finally:
        m.LFE_FILTERS = old
    reencode_walk(ctx.t, os.path.join(d, "hades-check-speakers-5.1.mkv"), x, "5.1")
    return d


def m_wrong_decoder(ctx):
    d = copy_ctx(ctx, "wrong_decoder")
    edit_json(d, lambda s: s["passthrough"][0].__setitem__("kodi_decoder", "pt-dts"))
    return d


def m_eac3_truncated(ctx):
    d = copy_ctx(ctx, "eac3_truncated")
    es = bytearray(ctx.es("eac3"))
    frames = ac3_frames(es)
    at, size, _ = frames[20]
    del es[at + size // 2:at + size]
    eac3 = os.path.join(d, "x.eac3")
    with open(eac3, "wb") as f:
        f.write(es)
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "eac3", "-i", eac3,
         "-c:a", "copy", "-f", "matroska", os.path.join(d, "hades-check-eac3.mkv")])
    os.remove(eac3)
    return d


def m_chime_detuned(ctx):
    """the TrueHD chime with its E at 660 Hz instead of 659"""
    m = ctx.ma.m
    old = m.CHIME_NOTES
    m.CHIME_NOTES = tuple((660 if hz == 659 else hz, t) for hz, t in old)
    try:
        mono = m.chime_samples(m.sine_table())
    finally:
        m.CHIME_NOTES = old
    st = []
    for v in mono:
        st += [v, v]
    x = np.frombuffer(m.to_s32(st, 24).tobytes(), dtype="<i4").reshape(-1, 2)
    return ctx.dir, {("dec", ctx.pt("truehd")["file"]): x}


def m_ac3_low_rate(ctx):
    d = copy_ctx(ctx, "ac3_low_rate")
    src = os.path.join(d, "src.s32")
    ctx.ma.source_chime().astype("<i4").tofile(src)
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
         "-ch_layout", "stereo", "-i", src, "-c:a", "ac3", "-b:a", "32k", "-dialnorm", "-31",
         "-f", "matroska", os.path.join(d, "hades-check-ac3.mkv")])
    os.remove(src)
    return d


def m_ac3_payload(ctx):
    """one byte of one AC-3 frame's audio changed: the frame no longer checks"""
    es = bytearray(ctx.es("ac3"))
    at, size, _ = ac3_frames(es)[50]
    es[at + size // 2] ^= 0x10
    return ctx.dir, {("es", "ac3"): bytes(es)}


def m_truehd_timing_jump(ctx):
    """the input timing jumps by one unit at unit 100 (each unit's check nibble kept right): Kodi's
    MAT packer leaves a unit's room empty, and that MAT frame holds 23 units"""
    es = bytearray(ctx.es("truehd"))
    for at, _, timing, _ in thd_units(bytes(es))[100:]:
        new = (timing + 40) & 0xFFFF
        old_x = (timing >> 12) ^ (timing >> 8) ^ (timing >> 4) ^ timing
        new_x = (new >> 12) ^ (new >> 8) ^ (new >> 4) ^ new
        es[at] ^= ((old_x ^ new_x) & 0xF) << 4
        es[at + 2], es[at + 3] = new >> 8, new & 0xFF
    return ctx.dir, {("es", "truehd"): bytes(es), ("sizes", "truehd"): ctx.sizes("truehd")}


def m_two_tracks(ctx):
    """the AC-3 file with a second track (the E-AC-3 one) beside its own"""
    d = copy_ctx(ctx, "two_tracks")
    run([ctx.t.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-i", ctx.path("hades-check-ac3.mkv"),
         "-i", ctx.path("hades-check-eac3.mkv"), "-map", "0", "-map", "1", "-c", "copy", "-f", "matroska",
         os.path.join(d, "hades-check-ac3.mkv")])
    return d


def m_mka_name(ctx):
    """the TrueHD chime named .mka, which Kodi would hand to its music player"""
    d = copy_ctx(ctx, "mka_name")
    os.rename(os.path.join(d, "hades-check-truehd.mkv"), os.path.join(d, "hades-check-truehd.mka"))
    edit_json(d, lambda s: s["passthrough"][2].__setitem__("file", "hades-check-truehd.mka"))
    return d


def m_too_big(ctx):
    d = copy_ctx(ctx, "too_big")
    with open(os.path.join(d, "hades-check-speakers-7.1.mkv"), "ab") as f:
        f.write(bytes(300_000))
    return d


def m_dts_stable(ctx):
    ev = dict(dts_evidence(ctx))
    ev["encoders"] = ev["encoders"].replace("A..X.D dca", "A....D dca")
    return ctx.dir, {"dts": ev}


def m_dts_extension(ctx):
    ev = dict(dts_evidence(ctx))
    first = ev["sizes"][0]
    ext = b"\x64\x58\x20\x25" + bytes(60)
    ev["dts"] = ev["dts"][:first] + ext + ev["dts"][first:]
    ev["sizes"] = [first + len(ext)] + ev["sizes"][1:]
    return ctx.dir, {"dts": ev}


MUTANTS = [
    ("chime with a detuned note", m_chime_detuned, ["chime_reference"]),
    ("AC-3 at 32 kb/s", m_ac3_low_rate, ["lossy_roundtrip"]),
    ("AC-3 frame with a changed byte", m_ac3_payload, ["ac3_crc"]),
    ("TrueHD input timing jumps", m_truehd_timing_jump, ["kodi_packer", "truehd_stream"]),
    ("a sound file 300 kB bigger", m_too_big, ["sizes"]),
    ("AC-3 file with a second track", m_two_tracks, ["probe_passthrough"]),
    ("TrueHD chime named .mka", m_mka_name, ["schema", "probe_passthrough"]),
    ("DTS encoder no longer experimental", m_dts_stable, ["not_checkable_evidence"]),
    ("a DTS-HD substream after the core", m_dts_extension, ["not_checkable_evidence"]),
    ("channels swapped (5.1 FL<->FR)", m_channels_swapped, ["speakers_slots", "speakers_reference"]),
    ("chime 10 dB louder (AC-3)", m_level_high, ["level"]),
    ("another seed (2.0 walk)", m_seed, ["speakers_reference"]),
    ("TrueHD major sync CRC broken", m_truehd_crc, ["truehd_stream", "kodi_parser", "truehd_lossless"]),
    ("TrueHD substream checksum broken", m_truehd_substream, ["truehd_stream", "truehd_lossless"]),
    ("TrueHD left as Lt/Rt", m_ltrt, ["truehd_stream"]),
    ("sound_from 0.3 s late", m_from_late, ["timing"]),
    ("sound_to 0.5 s early", m_to_early, ["timing"]),
    ("5.1 with side surrounds", m_side_layout, ["speakers_probe"]),
    ("AC-3 dialnorm -27", m_dialnorm27, ["dialnorm"]),
    ("E-AC-3 in 5.1", m_eac3_51, ["probe_passthrough", "kodi_parser"]),
    ("LFE not band-limited", m_lfe_fullband, ["speakers_spectrum", "speakers_reference"]),
    ("wrong decoder name in sounds.json", m_wrong_decoder, ["schema"]),
    ("E-AC-3 frame cut short", m_eac3_truncated, ["kodi_parser", "ac3_crc"]),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for k in ("make-audio", "ffmpeg", "ffprobe", "kodi", "ffmpeg-prefix", "work"):
        ap.add_argument("--" + k, required=True)
    ap.add_argument("--cxx", default="g++")
    a = ap.parse_args()
    if os.path.exists(a.work) and os.listdir(a.work):
        raise SystemExit("check_audio_tests.py: %s is not empty" % a.work)
    os.makedirs(a.work, exist_ok=True)
    tools = Tools(a)
    ma = MakeAudio(a.make_audio, tools)
    failures, done = [], 0

    def report(name, ok, detail=""):
        nonlocal done
        done += 1
        print("%-5s %s%s" % ("ok" if ok else "FAIL", name, (": " + detail) if detail else ""), flush=True)
        if not ok:
            failures.append(name)

    t0 = time.time()
    tools.build_harness()
    report("harness compiled from Kodi's sources", True, "%.0f s" % (time.time() - t0))
    out1, out2 = os.path.join(a.work, "run1"), os.path.join(a.work, "run2")
    t0 = time.time()
    ma.run(out1)
    report("make_audio.py run 1", True, "%.0f s" % (time.time() - t0))
    ma.run(out2)
    same = sorted(os.listdir(out1)) == sorted(os.listdir(out2)) and all(
        sha256(os.path.join(out1, n)) == sha256(os.path.join(out2, n)) for n in os.listdir(out1))
    report("determinism: two runs, the same bytes", same)
    # the bit-exact flags matter: without them, two encodes differ
    r = []
    src = os.path.join(a.work, "nobitexact.s32")
    ma.source_chime().astype("<i4").tofile(src)
    for i in range(2):
        o = os.path.join(a.work, "nobitexact%d.mkv" % i)
        run([a.ffmpeg, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "s32le", "-ar", "48000",
             "-ch_layout", "stereo", "-i", src, "-c:a", "ac3", "-f", "matroska", o])
        r.append(sha256(o))
        time.sleep(1.1)
    report("mutant: without the bit-exact flags two runs differ", r[0] != r[1])

    ctx = Ctx(tools, out1, ma)
    results = {}
    for name, fn in CHECKS:
        try:
            fn(ctx)
            results[name] = True
            report("check " + name, True)
        except Fail as e:
            results[name] = False
            report("check " + name, False, str(e))

    for label, make, targets in MUTANTS:
        try:
            made = make(ctx)
            mctx = Ctx(tools, made[0] if isinstance(made, tuple) else made, ma)
            if isinstance(made, tuple):
                mctx.cache.update(made[1])
        except Fail as e:
            report("mutant " + label, False, "could not be made: %s" % e)
            continue
        caught = []
        for t in targets:
            fn = dict(CHECKS)[t]
            try:
                fn(mctx)
            except Fail as e:
                caught.append("%s (%s)" % (t, str(e).splitlines()[0][:90]))
        report("mutant " + label, len(caught) == len(targets),
               "caught by " + "; ".join(caught) if caught else "NOT CAUGHT")

    sizes = {n: os.path.getsize(os.path.join(out1, n)) for n in sorted(os.listdir(out1))}
    print("sizes: " + ", ".join("%s %d" % kv for kv in sizes.items()) + "; total %d" % sum(sizes.values()))
    for ident in PASSTHROUGH:
        k = ctx.kodi(ident)
        print("kodi %s: %s" % (ident, json.dumps({x: k[x] for x in (
            "type", "sample_rate", "channels", "frames", "bursts", "burst_sizes", "pc", "mat_frames",
            "units_per_mat", "dialnorm_frames_changed", "dialnorm_lines")})))
        print("kodi %s log: %s" % (ident, json.dumps(k["log"][:3])))
    for e in ctx.sounds["passthrough"]:
        print("level %s: %s LUFS, true peak %s dBTP" % (e["id"], e["loudness_lufs"], e["true_peak_dbtp"]))
    n_mut = len(MUTANTS) + 1
    print("SUMMARY: %d results, %d failed (checks %d, mutants %d)" % (done, len(failures), len(CHECKS), n_mut))
    for f in failures:
        print("  failed: " + f)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
