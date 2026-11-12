/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (tools/regress/mvc, finding 11): host test of how Kodi reads the count of sequence
 * parameter sets of an 'mvcC' record (xbmc/cores/VideoPlayer/MultiviewChoice.h), included as it is.
 *
 * Finding 11 of the automatic review of FFmpeg pull request 24303 is about FFmpeg's mov demuxer, which the
 * series teaches to read 'mvcC' with a count of five bits where ISO/IEC 14496-15 has seven. Hades does not
 * ship that part of the series (twin.sh leaves libavformat out); Kodi reads the box itself and puts its sets
 * into the 'avcC' record that h264_mvc is opened with. This pins that Kodi reads seven bits (a count of 32 is
 * 32, not 0, and what follows the sets is read at its place), keeps the five bits of 'avcC' when it merges,
 * and refuses a merge that five bits cannot count.
 *
 * Build: g++ -std=c++20 -Wall -Wextra -Werror -I <kodi tree>/xbmc/cores/VideoPlayer mvcc_count_test.cpp
 */
#include "MultiviewChoice.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::MULTIVIEW;
using Bytes = std::vector<uint8_t>;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

static void Put16(Bytes& out, size_t v)
{
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}
static void Put32(Bytes& out, size_t v)
{
  for (int s = 24; s >= 0; s -= 8)
    out.push_back(static_cast<uint8_t>(v >> s));
}

// a NAL unit: its header, a profile byte for a sequence parameter set, and bytes that tell it apart
static Bytes Nal(int type, int profile, int tag)
{
  return {static_cast<uint8_t>(0x60 | type), static_cast<uint8_t>(profile), 0x00, 0x29,
          static_cast<uint8_t>(0xa0 + (tag & 0x1f)), static_cast<uint8_t>(tag >> 5)};
}
// a picture parameter set of number 0 or 1 (ue(id), ue(0) for its SPS, the stop bit)
static Bytes Pps(int id)
{
  return id == 0 ? Bytes{0x68, 0xe0} : Bytes{0x68, 0x58};
}

static Bytes operator+(Bytes a, const Bytes& b)
{
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

static Bytes Record(int profile, uint8_t countBits, const std::vector<Bytes>& first, const std::vector<Bytes>& second)
{
  Bytes out{1, static_cast<uint8_t>(profile), 0x00, 0x29, 0xff};
  out.push_back(static_cast<uint8_t>(countBits | first.size()));
  for (const Bytes& nal : first)
  {
    Put16(out, nal.size());
    out.insert(out.end(), nal.begin(), nal.end());
  }
  out.push_back(static_cast<uint8_t>(second.size()));
  for (const Bytes& nal : second)
  {
    Put16(out, nal.size());
    out.insert(out.end(), nal.begin(), nal.end());
  }
  return out;
}

static Bytes Box(const char* type, const Bytes& content)
{
  Bytes out;
  Put32(out, content.size() + 8);
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), content.begin(), content.end());
  return out;
}

int main()
{
  // an 'mvcC' record of the base view's profile with 32 sets, the 32nd a subset sequence parameter set of
  // Stereo High, and one picture parameter set behind them: seven bits count 32 (0x20); five would count 0
  std::vector<Bytes> sets;
  for (int i = 0; i < 31; ++i)
    sets.push_back(Nal(7, 100, i));
  sets.push_back(Nal(15, 128, 31));
  const Bytes mvcc = Record(100, 0x00, sets, {Pps(1)});
  Check(mvcc[5] == 0x20, "the record counts 32 sets in seven bits");
  Check(DETAIL::DeclaresSecondView(mvcc.data(), mvcc.size()),
        "32 sets, the last a subset SPS of Stereo High: a second view is declared");
  std::vector<DETAIL::Set> sps, pps;
  Check(DETAIL::SecondViewSets(mvcc.data(), mvcc.size(), sps, pps), "the sets of 32 are read to the end");
  Check(sps.size() == 1 && sps[0].size() == 2 + 6 && sps[0][2] == (0x60 | 15),
        "of the 32 sets, the subset SPS is taken (" + std::to_string(sps.size()) + ")");
  Check(pps.size() == 1 && pps[0].size() == 2 + Pps(1).size() && DETAIL::PpsId(pps[0]) == 1,
        "the picture parameter set behind the 32 sets is read at its place (" + std::to_string(pps.size()) + ")");
  // the count with the three bits of 'avcC' set: five bits, as a writer that kept them meant
  const Bytes avcLike = Record(100, 0xe0, {Nal(15, 128, 0)}, {Pps(1)});
  Check(DETAIL::DeclaresSecondView(avcLike.data(), avcLike.size()), "a count written as 'avcC' has it (0xe1)");
  // seven bits reach 127; 96 sets (0x60, the two bits below the top one set) are 96, not 0
  std::vector<Bytes> many;
  for (int i = 0; i < 95; ++i)
    many.push_back(Nal(7, 100, i));
  many.push_back(Nal(15, 118, 95));
  const Bytes big = Record(100, 0x00, many, {});
  Check(big[5] == 0x60 && DETAIL::DeclaresSecondView(big.data(), big.size()), "96 sets, the last a subset SPS");

  // the merge into 'avcC' keeps its five bits: 1 + 30 subset SPSs are counted (31), 1 + 31 cannot be
  const Bytes base = Record(100, 0xe0, {Nal(7, 100, 0)}, {Pps(0)});
  std::vector<Bytes> subs30, subs31;
  for (int i = 0; i < 31; ++i)
  {
    Bytes s = Nal(15, 128, i + 1);
    if (i < 30)
      subs30.push_back(s);
    subs31.push_back(s);
  }
  const Bytes with30 = base + Box("mvcC", Record(128, 0x00, subs30, {Pps(1)}));
  const Bytes with31 = base + Box("mvcC", Record(128, 0x00, subs31, {Pps(1)}));
  const std::vector<uint8_t> merged30 = WithSecondViewSets(with30.data(), with30.size());
  Check(!merged30.empty() && merged30[5] == (0xe0 | 31), "1 + 30 sets merged: 31, in the five bits of 'avcC'");
  Check(WithSecondViewSets(with31.data(), with31.size()).empty(),
        "1 + 31 sets: not merged (five bits cannot count 32); the data is used as it is");

  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
