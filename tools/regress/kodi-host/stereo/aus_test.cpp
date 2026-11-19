// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test: the plane offsets of Blu-ray 3D read out of the access units of a real H.264 MVC stream (the JM
// encoder's, base and second view together) into which mkaus.py put an offset metadata SEI at each GOP start, by
// both parsers of BlurayOffsetMetadata.cpp (Annex B, as the dependent view clip of a disc gives it, and with 4-byte
// lengths, as a Matroska remux carries it), and kept in the store as the demuxers keep them (Add() at the carrier,
// NoteFrame() at the others). A stream without the SEI gives nothing at any frame.
// Usage: aus_test <dir of mkaus.py>
#include "BlurayOffsetMetadata.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace KODI::VIDEO::BLURAY;

static int failures = 0;
#define CHECK(c, ...) \
  do \
  { \
    if (!(c)) \
    { \
      std::printf("FAIL: " __VA_ARGS__); \
      std::printf("\n"); \
      ++failures; \
    } \
  } while (0)

static std::vector<std::vector<uint8_t>> Records(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  std::vector<std::vector<uint8_t>> out;
  uint8_t h[4];
  while (f.read(reinterpret_cast<char*>(h), 4))
  {
    const uint32_t n = uint32_t(h[0]) << 24 | uint32_t(h[1]) << 16 | uint32_t(h[2]) << 8 | h[3];
    std::vector<uint8_t> r(n);
    f.read(reinterpret_cast<char*>(r.data()), n);
    out.push_back(std::move(r));
  }
  return out;
}

int main(int argc, char** argv)
{
  if (argc < 2)
    return 2;
  const std::string dir = argv[1];
  std::ifstream e(dir + "/expected.txt");
  std::string word;
  unsigned frames, reps, sequences;
  e >> word >> frames >> word >> reps >> word >> sequences;
  std::vector<std::vector<int>> expected(reps, std::vector<int>(sequences * frames));
  for (auto& g : expected)
    for (int& v : g)
      e >> v;

  constexpr double FRAME{41708.0};
  // (othermarker: a SEI with the UUID of offset metadata and another marker after it, which is none)
  for (const char* variant : {"sei", "plain", "othermarker"})
  {
    const bool sei = std::string(variant) == "sei";
    const auto annexb = Records(dir + "/combined-" + variant + ".annexb");
    const auto avcc = Records(dir + "/combined-" + variant + ".avcc");
    CHECK(annexb.size() == frames * reps && avcc.size() == annexb.size(), "%s: %zu records", variant,
          annexb.size());
    COffsetMetadataStore storeB, storeL;
    unsigned found = 0, answered = 0;
    for (size_t i = 0; i < annexb.size(); ++i)
    {
      const unsigned g = i / frames, k = i % frames;
      OffsetMetadata b, l;
      const bool fb = ParseOffsetMetadata(annexb[i].data(), annexb[i].size(), b);
      const bool fl = ParseOffsetMetadataAvcc(avcc[i].data(), avcc[i].size(), 4, l);
      CHECK(fb == fl, "%s: access unit %zu: Annex B %d, lengths %d", variant, i, fb, fl);
      CHECK(fb == (sei && k == 0), "%s: access unit %zu: found %d", variant, i, fb);
      const double pts = i * FRAME;
      if (fb)
      {
        ++found;
        CHECK(b.sequences == sequences && b.frames == frames, "%s: au %zu: %u sequences, %u frames",
              variant, i, b.sequences, b.frames);
        CHECK(b.offsets == l.offsets, "%s: au %zu: the two parsers read other offsets", variant, i);
        for (unsigned s = 0; s < sequences; ++s)
          for (unsigned f = 0; f < frames; ++f)
            CHECK(b.Offset(s, f) == expected[g][s * frames + f], "%s: au %zu: sequence %u frame %u: %d, want %d",
                  variant, i, s, f, b.Offset(s, f), expected[g][s * frames + f]);
        storeB.Add(pts, FRAME, std::move(b));
        storeL.Add(pts, FRAME, std::move(l));
      }
      else
      {
        storeB.NoteFrame(pts);
        storeL.NoteFrame(pts);
      }
      // what the render thread would get for this frame (the stream is taken in decoding order)
      for (unsigned s = 0; s < sequences; ++s)
      {
        const std::optional<int> got = storeB.GetOffset(pts, s);
        CHECK(got == storeL.GetOffset(pts, s), "%s: au %zu: the stores differ", variant, i);
        if (sei)
          CHECK(got && *got == expected[g][s * frames + k], "%s: au %zu sequence %u: %s %d, want %d", variant, i,
                s, got ? "answer" : "no answer", got ? *got : 0, expected[g][s * frames + k]);
        else
          CHECK(!got, "%s: au %zu: an answer without any SEI", variant, i);
        answered += got.has_value();
      }
      CHECK(!storeB.GetOffset(pts, sequences), "%s: au %zu: an answer for a sequence not carried", variant, i);
    }
    // a frame after the last block: nothing describes it
    CHECK(!storeB.GetOffset(annexb.size() * FRAME, 0), "%s: an answer after the last block", variant);
    std::printf("%s: %zu access units, %u carry offsets, %u answers of the store\n", variant, annexb.size(), found,
                answered);
  }

  // the dependent view alone (a disc's dependent view clip)
  const auto dep = Records(dir + "/dependent-sei.annexb");
  unsigned depFound = 0;
  for (size_t i = 0; i < dep.size(); ++i)
  {
    OffsetMetadata m;
    const bool f = ParseOffsetMetadata(dep[i].data(), dep[i].size(), m);
    CHECK(f == (i % frames == 0), "dependent view: access unit %zu: found %d", i, f);
    if (f)
    {
      ++depFound;
      CHECK(m.Offset(2, frames - 1) == expected[i / frames][2 * frames + frames - 1], "dependent view: au %zu",
            i);
    }
  }
  std::printf("dependent view alone: %zu access units, %u carry offsets\n", dep.size(), depFound);
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
