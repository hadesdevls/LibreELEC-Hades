// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Test tool of Hades: what Kodi decides of the private data that FFmpeg gives for an H.264 track, with the header
// that decides it in Kodi (xbmc/cores/VideoPlayer/MultiviewChoice.h): whether it declares a second view
// (HasMvcConfiguration()), whether the track is decoded as two views where the setting and the decoder allow it
// (TwoViews(), with the container naming no layout), and what the decoder of both views is opened with
// (WithSecondViewSets(): its size, 0 where the private data is used as it is, and an FNV-1a hash of the
// AVCDecoderConfigurationRecord at its head, the part that the decoder reads).
// Usage: mvc_declared <file of private data>...
//   prints "<file> declared=<0|1> twoviews=<0|1> opened=<bytes> record=<hash of the record it is opened with>"
#include "MultiviewChoice.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
// The length of the AVCDecoderConfigurationRecord at the head of the data (its parameter sets), 0 where it cannot
// be read
size_t RecordLength(const std::vector<uint8_t>& d)
{
  if (d.size() < 7 || d[0] != 1)
    return 0;
  size_t pos = 6;
  for (unsigned n = d[5] & 0x1f; n; --n)
  {
    if (d.size() - pos < 2)
      return 0;
    const size_t len = (static_cast<size_t>(d[pos]) << 8) | d[pos + 1];
    pos += 2;
    if (d.size() - pos < len)
      return 0;
    pos += len;
  }
  if (d.size() - pos < 1)
    return 0;
  for (unsigned n = d[pos++]; n; --n)
  {
    if (d.size() - pos < 2)
      return 0;
    const size_t len = (static_cast<size_t>(d[pos]) << 8) | d[pos + 1];
    pos += 2;
    if (d.size() - pos < len)
      return 0;
    pos += len;
  }
  return pos;
}

uint64_t Fnv(const std::vector<uint8_t>& d, size_t n)
{
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i)
    h = (h ^ d[i]) * 1099511628211ull;
  return h;
}
} // namespace

int main(int argc, char** argv)
{
  for (int i = 1; i < argc; ++i)
  {
    std::ifstream file(argv[i], std::ios::binary);
    if (!file)
    {
      std::printf("%s cannot be read\n", argv[i]);
      return 1;
    }
    const std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), {}};
    KODI::MULTIVIEW::Track track;
    track.h264 = true;
    track.allowed = true;
    track.decoder = true;
    track.extradata = data.data();
    track.extradataSize = data.size();
    const bool declared = KODI::MULTIVIEW::HasMvcConfiguration(data.data(), data.size());
    const bool twoViews = KODI::MULTIVIEW::TwoViews(track).has_value();
    const std::vector<uint8_t> opened = KODI::MULTIVIEW::WithSecondViewSets(data.data(), data.size());
    const std::vector<uint8_t>& decoder = opened.empty() ? data : opened;
    std::printf("%s declared=%d twoviews=%d opened=%zu record=%016llx\n", argv[i], declared ? 1 : 0,
                twoViews ? 1 : 0, opened.size(),
                static_cast<unsigned long long>(Fnv(decoder, RecordLength(decoder))));
  }
  return 0;
}
