// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// What Kodi decides of an HEVC track of two views (MV-HEVC), xbmc/cores/VideoPlayer/MultiviewChoice.h, against
// the ITU-T MV-HEVC conformance streams (H.265.1) and records made of them:
//   - MvHevcSecondLayer() on each stream as it is (units behind start codes) and on an 'hvcC' record made of its
//     parameter sets (with those of the second layer appended, as FFmpeg appends 'lhvC'): the layer of the second
//     view for the streams of two views (A to F), nothing for those of three views and more (G, H, I), nothing
//     where the record lacks the second layer's SPS or PPS, nothing for a set of one layer, of another scalability,
//     or cut short;
//   - the eye of the base view by the container and by the stream, Dolby Vision profile 20 kept to one view,
//     TwoViews() as a whole, and Paced(): the processor that does not keep up; Worked(): the same, found sooner by
//     the work of the decoder (two windows in a row under nine tenths of the rate of the video, after a skip).
//   - the private data of files that FFmpeg wrote and read back (an MP4 with 'lhvC', a Matroska file made of it):
//     the layer of the second view, or nothing;
//   - the parameter sets of the second view looked for in packets (LookInPacket(), HvccWithSets()), the choice of
//     NVIDIA's decoder (NvidiaDriverSeries(), NvdecForTwoViews()) and the disparity adjustment (LeftEyeShift()).
// usage: mvhevc_choice_test <folder of MVHEVCS_*.bit> [<private data file> <layer|none>]...
// Prints FAIL lines and "N checks, M failed".
#include "MultiviewChoice.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace KODI::MULTIVIEW;

static int checks = 0, failed = 0;
#define CHECK(cond, ...) \
  do \
  { \
    ++checks; \
    if (!(cond)) \
    { \
      ++failed; \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__); \
      std::printf("\n"); \
    } \
  } while (0)

using Bytes = std::vector<uint8_t>;

static Bytes Read(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

//! the NAL units of an Annex B stream, without their start codes
static std::vector<Bytes> Units(const Bytes& s)
{
  std::vector<Bytes> out;
  DETAIL::ForEachHevcNal(s.data(), s.size(),
                         [&](const uint8_t* nal, size_t size) { out.emplace_back(nal, nal + size); });
  return out;
}

//! an 'hvcC' record of the parameter sets (VPS, SPS, PPS in three arrays, every layer), as FFmpeg's mov demuxer
//! gives it once it appended 'lhvC'; keep(type, layer) says which units go in
template<typename Keep>
static Bytes Record(const std::vector<Bytes>& units, Keep keep)
{
  Bytes r(23, 0);
  r[0] = 1;
  r[21] = 0x0f; // lengthSizeMinusOne 3
  uint8_t arrays = 0;
  for (int type : {32, 33, 34})
  {
    std::vector<const Bytes*> sel;
    for (const Bytes& u : units)
    {
      const auto h = DETAIL::HevcNalHeader(u.data(), u.size());
      if (h.type == type && keep(h.type, h.layer))
        sel.push_back(&u);
    }
    if (sel.empty())
      continue;
    ++arrays;
    r.push_back(static_cast<uint8_t>(0x80 | type));
    r.push_back(static_cast<uint8_t>(sel.size() >> 8));
    r.push_back(static_cast<uint8_t>(sel.size()));
    for (const Bytes* u : sel)
    {
      r.push_back(static_cast<uint8_t>(u->size() >> 8));
      r.push_back(static_cast<uint8_t>(u->size()));
      r.insert(r.end(), u->begin(), u->end());
    }
  }
  r[22] = arrays;
  return r;
}

static const Bytes* FirstOf(const std::vector<Bytes>& units, int type, int layer)
{
  for (const Bytes& u : units)
  {
    const auto h = DETAIL::HevcNalHeader(u.data(), u.size());
    if (h.type == type && h.layer == layer)
      return &u;
  }
  return nullptr;
}

//! a VPS changed at a bit of its RBSP (the header of two bytes left as it is), with the emulation prevention of
//! the result written again
static Bytes WithBit(const Bytes& vps, size_t bit, int value)
{
  // the RBSP bytes
  Bytes rbsp;
  {
    unsigned int zeros = 0;
    for (size_t i = 2; i < vps.size(); ++i)
    {
      if (zeros >= 2 && vps[i] == 3)
      {
        zeros = 0;
        continue;
      }
      rbsp.push_back(vps[i]);
      zeros = vps[i] == 0 ? zeros + 1 : 0;
    }
  }
  if (value)
    rbsp[bit / 8] |= static_cast<uint8_t>(0x80 >> (bit % 8));
  else
    rbsp[bit / 8] &= static_cast<uint8_t>(~(0x80 >> (bit % 8)));
  Bytes out(vps.begin(), vps.begin() + 2);
  unsigned int zeros = 0;
  for (uint8_t b : rbsp)
  {
    if (zeros >= 2 && b <= 3)
    {
      out.push_back(3);
      zeros = 0;
    }
    out.push_back(b);
    zeros = b == 0 ? zeros + 1 : 0;
  }
  return out;
}

static void Streams(const std::string& dir)
{
  struct Case
  {
    const char* name;
    std::optional<int> layer;
  };
  const Case cases[] = {{"A", 1}, {"B", 1}, {"C", 1}, {"D", 1}, {"E", 1}, {"F", 1},
                        {"G", std::nullopt}, {"H", std::nullopt}, {"I", std::nullopt}};
  for (const Case& c : cases)
  {
    const Bytes s = Read(dir + "/MVHEVCS_" + c.name + ".bit");
    CHECK(!s.empty(), "MVHEVCS_%s.bit not read", c.name);
    if (s.empty())
      continue;
    const auto annexB = MvHevcSecondLayer(s.data(), s.size());
    CHECK(annexB == c.layer, "%s as a stream: layer %d, expected %d", c.name, annexB ? *annexB : -1,
          c.layer ? *c.layer : -1);
    const auto units = Units(s);
    const Bytes all = Record(units, [](int, int) { return true; });
    const auto rec = MvHevcSecondLayer(all.data(), all.size());
    CHECK(rec == c.layer, "%s as a record: layer %d, expected %d", c.name, rec ? *rec : -1,
          c.layer ? *c.layer : -1);
    CHECK(MvHevcDeclared(all.data(), all.size()) == c.layer.has_value(), "%s: declared", c.name);
    if (!c.layer)
      continue;
    // the base layer's sets alone (a Matroska file of a muxer that keeps those only): declared, not decodable
    const Bytes base = Record(units, [](int type, int layer) { return type == 32 || layer == 0; });
    CHECK(!MvHevcSecondLayer(base.data(), base.size()), "%s: base sets only taken", c.name);
    CHECK(MvHevcDeclared(base.data(), base.size()), "%s: base sets only, not declared", c.name);
    const Bytes noPps = Record(units, [](int type, int layer) { return type != 34 || layer == 0; });
    CHECK(!MvHevcSecondLayer(noPps.data(), noPps.size()), "%s: without the second PPS taken", c.name);
    const Bytes noSps = Record(units, [](int type, int layer) { return type != 33 || layer == 0; });
    CHECK(!MvHevcSecondLayer(noSps.data(), noSps.size()), "%s: without the second SPS taken", c.name);
    // cut short anywhere: never more than the whole says, never a read beyond
    for (size_t n = 0; n < all.size(); n += 7)
    {
      const auto cut = MvHevcSecondLayer(all.data(), n);
      CHECK(!cut, "%s: a record cut at %zu taken", c.name, n);
    }
    // the VPS: one layer; another scalability; no extension
    const Bytes* vps = FirstOf(units, 32, 0);
    CHECK(vps != nullptr, "%s: no VPS", c.name);
    if (!vps)
      continue;
    CHECK(DETAIL::SecondViewLayerOfVps(vps->data(), vps->size()) == 1, "%s: VPS", c.name);
    // vps_max_layers_minus1 is bits 6..11 of the RBSP: 1 -> 0
    const Bytes oneLayer = WithBit(*vps, 11, 0);
    CHECK(!DETAIL::SecondViewLayerOfVps(oneLayer.data(), oneLayer.size()), "%s: one layer taken", c.name);
    // ... 1 -> 2: three layers (FFmpeg decodes two at most)
    const Bytes threeLayers = WithBit(WithBit(*vps, 11, 0), 10, 1);
    CHECK(!DETAIL::SecondViewLayerOfVps(threeLayers.data(), threeLayers.size()), "%s: three layers taken",
          c.name);
    // vps_base_layer_internal_flag (bit 4) off
    const Bytes external = WithBit(*vps, 4, 0);
    CHECK(!DETAIL::SecondViewLayerOfVps(external.data(), external.size()), "%s: external base taken",
          c.name);
    // the whole VPS cut short
    for (size_t n = 2; n < vps->size(); ++n)
      CHECK(!DETAIL::SecondViewLayerOfVps(vps->data(), n) ||
                DETAIL::SecondViewLayerOfVps(vps->data(), n) == 1,
            "%s: a VPS cut at %zu read as another layer", c.name, n);
  }
}

//! the bit of scalability_mask_flag[1] in the VPS of A, found by reading it as SecondViewLayerOfVps() does
static void Scalability(const std::string& dir)
{
  const Bytes s = Read(dir + "/MVHEVCS_A.bit");
  const auto units = Units(s);
  const Bytes* vps = FirstOf(units, 32, 0);
  if (!vps)
    return;
  // every single bit of the RBSP flipped in turn: whatever the set then says, the answer is the layer 1 or
  // nothing (never another layer from a field read wrong), and a flip of a mask bit other than multiview
  // gives nothing
  int answers1 = 0, nothing = 0;
  DETAIL::Rbsp all(vps->data(), vps->size(), 2);
  size_t bits = 0;
  while (true)
  {
    all.Bits(1);
    if (!all.Ok())
      break;
    ++bits;
  }
  for (size_t b = 0; b < bits; ++b)
  {
    DETAIL::Rbsp at(vps->data(), vps->size(), 2);
    at.Skip(b);
    const int was = static_cast<int>(at.Bits(1));
    const Bytes v = WithBit(*vps, b, !was);
    const auto r = DETAIL::SecondViewLayerOfVps(v.data(), v.size());
    if (!r)
      ++nothing;
    else if (*r == 1)
      ++answers1;
  }
  CHECK(nothing > 0, "no single flipped bit of the VPS refuses it");
  CHECK(answers1 + nothing <= static_cast<int>(bits), "count");
  std::printf("vps of A: %zu bits; a flip of one gives layer 1 %d times, nothing %d times, another layer %d "
              "times\n",
              bits, answers1, nothing, static_cast<int>(bits) - answers1 - nothing);
}

static void Eyes()
{
  using CL = ContainerLayout;
  CHECK(BaseEyeOfHevcContainer(CL::NONE, false, "") == BaseEye::LEFT, "nothing: left");
  CHECK(BaseEyeOfHevcContainer(CL::NONE, false, "block_rl") == BaseEye::RIGHT, "block_rl: right");
  CHECK(!BaseEyeOfHevcContainer(CL::NONE, false, "left_right"), "a layout in the picture taken");
  CHECK(BaseEyeOfHevcContainer(CL::TWO_VIEWS, false, "") == BaseEye::LEFT, "stri both: left");
  CHECK(BaseEyeOfHevcContainer(CL::TWO_VIEWS, true, "") == BaseEye::RIGHT, "stri reversed: right");
  CHECK(!BaseEyeOfHevcContainer(CL::ONE_EYE, false, ""), "one eye taken");
  CHECK(!BaseEyeOfHevcContainer(CL::OTHER, false, ""), "side by side taken");
  CHECK(EyeOfBaseView(BaseEye::LEFT, PictureEye::RIGHT) == BaseEye::RIGHT, "the stream not believed");
  CHECK(EyeOfBaseView(BaseEye::RIGHT, PictureEye::LEFT) == BaseEye::LEFT, "the stream not believed");
  CHECK(EyeOfBaseView(BaseEye::RIGHT, PictureEye::UNSAID) == BaseEye::RIGHT, "the container not kept");
  CHECK(EyeOfBaseView(BaseEye::LEFT, PictureEye::UNSAID) == BaseEye::LEFT, "the container not kept");
  CHECK(KeptToOneView(20, 0), "profile 20.0 not kept to one view");
  CHECK(!KeptToOneView(20, 0, true), "profile 20.0 kept to one view where it is rebuilt in 3D");
  CHECK(!KeptToOneView(20, 4, true) && !KeptToOneView(5, 0, true), "rebuilt in 3D: another kept");
  CHECK(!KeptToOneView(20, 4), "profile 20.4 kept to one view");
  CHECK(!KeptToOneView(5, 0), "profile 5 kept");
  CHECK(!KeptToOneView(0, 0), "no Dolby Vision kept");
}

static void Tracks(const std::string& dir)
{
  const Bytes s = Read(dir + "/MVHEVCS_A.bit");
  const Bytes rec = Record(Units(s), [](int, int) { return true; });
  Track t;
  t.hevc = true;
  t.allowed = true;
  t.decoder = true;
  t.extradata = rec.data();
  t.extradataSize = rec.size();
  CHECK(TwoViews(t) == BaseEye::LEFT, "A: not two views");
  t.layout = ContainerLayout::TWO_VIEWS;
  t.inverted = true;
  CHECK(TwoViews(t) == BaseEye::RIGHT, "A reversed");
  t.inverted = false;
  Track u = t;
  u.allowed = false;
  CHECK(!TwoViews(u), "not allowed, taken");
  u = t;
  u.decoder = false;
  CHECK(!TwoViews(u), "no decoder, taken");
  u = t;
  u.hevc = false;
  CHECK(!TwoViews(u), "not HEVC, taken");
  u = t;
  u.dvProfile = 20;
  u.dvCompatibility = 0;
  CHECK(!TwoViews(u), "profile 20.0 taken as two views");
  u.rebuiltIn3D = true;
  CHECK(TwoViews(u) == BaseEye::LEFT, "profile 20.0 not two views where it is rebuilt in 3D");
  u.rebuiltIn3D = false;
  u.dvCompatibility = 4;
  CHECK(TwoViews(u) == BaseEye::LEFT, "profile 20.4 not two views");
  u = t;
  u.layout = ContainerLayout::OTHER;
  CHECK(!TwoViews(u), "a layout in the picture taken");
  // an H.264 track with this data is not MVC
  u = t;
  u.hevc = false;
  u.h264 = true;
  CHECK(!TwoViews(u), "HEVC data taken as MVC");
}

static void Pacing()
{
  // a processor that keeps up: never
  {
    Pace p;
    bool gave = false;
    for (int i = 0; i < 100000; ++i)
      gave = gave || Paced(p, false, true);
    CHECK(!gave, "given up while keeping up");
  }
  // always late: after the grace and two windows, not before
  {
    Pace p;
    int at = -1;
    for (int i = 0; i < 1000 && at < 0; ++i)
      if (Paced(p, true, true))
        at = i;
    CHECK(at == static_cast<int>(PACE_GRACE + 2 * PACE_WINDOW - 1), "always late: given up at %d", at);
  }
  // one picture in eight late: given up; one fewer in each window: never
  {
    Pace p;
    int at = -1;
    for (int i = 0; i < 2000 && at < 0; ++i)
      if (Paced(p, i >= static_cast<int>(PACE_GRACE) && (i - PACE_GRACE) % 8 == 0, true))
        at = i;
    CHECK(at > 0, "one in eight late: not given up");
    Pace q;
    bool gave = false;
    for (int i = 0; i < 20000; ++i)
    {
      const int inWindow = (i - static_cast<int>(PACE_GRACE)) % static_cast<int>(PACE_WINDOW);
      gave = gave || Paced(q, i >= static_cast<int>(PACE_GRACE) &&
                                  inWindow < static_cast<int>(PACE_LATE_IN_WINDOW) - 1,
                           true);
    }
    CHECK(!gave, "eleven late in a window: given up");
  }
  // slow windows that are not in a row: never
  {
    Pace p;
    bool gave = false;
    for (int w = 0; w < 200; ++w)
      for (unsigned int i = 0; i < PACE_WINDOW + (w == 0 ? PACE_GRACE : 0); ++i)
        gave = gave || Paced(p, w % 2 == 0, true);
    CHECK(!gave, "slow windows apart: given up");
  }
  // late during the grace only (a start, a seek): never
  {
    Pace p;
    bool gave = false;
    for (unsigned int i = 0; i < 100000; ++i)
      gave = gave || Paced(p, i < PACE_GRACE, true);
    CHECK(!gave, "late in the grace: given up");
  }
  // not judged (another speed, a seek): never, however late
  {
    Pace p;
    bool gave = false;
    for (int i = 0; i < 100000; ++i)
      gave = gave || Paced(p, true, false);
    CHECK(!gave, "not judged: given up");
  }
}

// Worked(): the processor that cannot keep up found by the work of the decoder, sooner than Paced()
static void Working()
{
  const double fps = 24000.0 / 1001.0;
  const double frameMs = 1000.0 / fps;
  // a decoder that needs 25 % more than a picture's time: given up after its skip and two windows, about four
  // seconds of film, long before Paced() could
  {
    Work w;
    int at = -1;
    for (int i = 0; i < 2000 && at < 0; ++i)
      if (Worked(w, 1.25 * frameMs, fps, true))
        at = i;
    CHECK(at == static_cast<int>(WORK_SKIP + 2 * WORK_WINDOW - 1), "too slow: given up at %d", at);
    CHECK(at < static_cast<int>(PACE_GRACE + 2 * PACE_WINDOW - 1), "too slow: not sooner than Paced()");
    CHECK(w.rate > 0.79 * fps && w.rate < 0.81 * fps, "the rate measured: %.3f", w.rate);
  }
  // nearly in time (5 % more than a picture's time), in time, or waiting for nothing: never by the work
  for (const double share : {1.05, 1.0, 0.5, 0.0})
  {
    Work w;
    bool gave = false;
    for (int i = 0; i < 20000; ++i)
      gave = gave || Worked(w, share * frameMs, fps, true);
    CHECK(!gave, "given up at %.2f of a picture's time", share);
  }
  // a slow window between quick ones (a start, a moment of load): never; two in a row: given up
  {
    Work w;
    bool gave = false;
    for (unsigned int win = 0; win < 200; ++win)
      for (unsigned int i = 0; i < WORK_WINDOW + (win == 0 ? WORK_SKIP : 0); ++i)
        gave = gave || Worked(w, (win % 2 == 0 ? 2.0 : 0.5) * frameMs, fps, true);
    CHECK(!gave, "slow windows apart: given up");
  }
  // the first pictures after a start or a seek cost more (the threads fill): not counted
  {
    Work w;
    bool gave = false;
    for (unsigned int i = 0; i < 20000; ++i)
      gave = gave || Worked(w, (i < WORK_SKIP ? 100.0 : 0.8) * frameMs, fps, true);
    CHECK(!gave, "the skipped pictures counted");
  }
  // not judged (another speed, before the target of a seek), or the rate of the video not known: never
  {
    Work w, u;
    bool gave = false;
    for (int i = 0; i < 20000; ++i)
      gave = gave || Worked(w, 3.0 * frameMs, fps, false) || Worked(u, 3.0 * frameMs, 0.0, true);
    CHECK(!gave, "not judged or no rate: given up");
  }
  // the stream given up for slowness, opened again while the same file plays: its base view at once
  const SlowStream slow{"/storage/videos/a.mp4", 3840, 2160};
  CHECK(StillSlow(slow, "/storage/videos/a.mp4", 3840, 2160), "the same stream of the same file: not kept");
  CHECK(!StillSlow(slow, "/storage/videos/b.mp4", 3840, 2160), "another file: kept");
  CHECK(!StillSlow(slow, "/storage/videos/a.mp4", 1920, 1080), "another stream of the file: kept");
  CHECK(!StillSlow(SlowStream{}, "", 0, 0), "a stream of no file: kept as slow");
}

//! units into one packet of a Matroska or MP4 sample: each behind its length in lengthSize bytes
static Bytes Packet(const std::vector<const Bytes*>& units, unsigned int lengthSize = 4)
{
  Bytes p;
  for (const Bytes* u : units)
  {
    for (unsigned int i = lengthSize; i > 0; --i)
      p.push_back(static_cast<uint8_t>(u->size() >> (8 * (i - 1))));
    p.insert(p.end(), u->begin(), u->end());
  }
  return p;
}

// The parameter sets of the second view in the packets (LookInPacket(), HvccWithSets()), on the units of A
static void InBandSets(const std::string& dir)
{
  const std::vector<Bytes> units = Units(Read(dir + "/MVHEVCS_A.bit"));
  const Bytes base = Record(units, [](int, int layer) { return layer == 0; });
  CHECK(!MvHevcSecondLayer(base.data(), base.size()), "A without the second layer's sets: two views");
  CHECK(MvHevcDeclaredLayer(base.data(), base.size()) == 1, "A: no second layer declared");
  CHECK(HvccLengthSize(base.data(), base.size()) == 4u, "lengthSizeMinusOne 3: not 4");
  {
    Bytes two = base;
    two[21] = 0xfd;
    CHECK(HvccLengthSize(two.data(), two.size()) == 2u, "lengthSizeMinusOne 1: not 2");
    two[21] = 0xfe;
    CHECK(!HvccLengthSize(two.data(), two.size()), "a length of 3 bytes taken");
    CHECK(!HvccLengthSize(nullptr, 0) && !HvccLengthSize(base.data(), 22), "no record taken");
  }
  // the units up to the first slice of the second layer, in the order of the stream
  std::vector<const Bytes*> first;
  for (const Bytes& u : units)
  {
    first.push_back(&u);
    const auto h = DETAIL::HevcNalHeader(u.data(), u.size());
    if (h.layer == 1 && h.type < 32)
      break;
  }
  const auto search = [](const Bytes& packet, unsigned int lengthSize = 4)
  {
    InBandSearch s;
    s.layer = 1;
    s.lengthSize = lengthSize;
    const InBand found = LookInPacket(s, packet.data(), packet.size());
    return std::pair{found, s};
  };
  for (unsigned int lengthSize : {2u, 4u})
  {
    const auto [found, s] = search(Packet(first, lengthSize), lengthSize);
    CHECK(found == InBand::FOUND && s.sps.size() == 1 && s.pps.size() == 1,
          "lengths of %u bytes: the sets before the first picture of the layer not found (%d, %zu, %zu)",
          lengthSize, static_cast<int>(found), s.sps.size(), s.pps.size());
    const Bytes record = HvccWithSets(base.data(), base.size(), s);
    CHECK(MvHevcSecondLayer(record.data(), record.size()) == 1,
          "the record with the sets found: not two views");
    CHECK(record.size() > base.size() && record[22] == base[22] + 2 &&
              std::equal(base.begin() + 23, base.end(), record.begin() + 23),
          "the record is not the old one with two arrays appended");
  }
  // without the second layer's SPS, or with its sets after its first picture: not there
  {
    std::vector<const Bytes*> noSps;
    for (const Bytes* u : first)
    {
      const auto h = DETAIL::HevcNalHeader(u->data(), u->size());
      if (!(h.layer == 1 && h.type == 33))
        noSps.push_back(u);
    }
    CHECK(search(Packet(noSps)).first == InBand::NOT_THERE, "no SPS of the layer: found");
    std::vector<const Bytes*> late;
    std::vector<const Bytes*> sets;
    for (const Bytes* u : first)
    {
      const auto h = DETAIL::HevcNalHeader(u->data(), u->size());
      (h.layer == 1 && (h.type == 33 || h.type == 34) ? sets : late).push_back(u);
    }
    late.insert(late.end(), sets.begin(), sets.end());
    CHECK(search(Packet(late)).first == InBand::NOT_THERE, "sets after the first picture: found");
  }
  // a packet that does not hold together ends it
  {
    Bytes cut = Packet(first);
    cut.resize(cut.size() - 1);
    CHECK(search(cut).first == InBand::NOT_THERE, "a cut packet: not ended");
  }
  // packets without the second layer: looked through IN_BAND_PACKETS of them, then not there
  {
    std::vector<const Bytes*> baseOnly;
    for (const Bytes* u : first)
      if (DETAIL::HevcNalHeader(u->data(), u->size()).layer == 0)
        baseOnly.push_back(u);
    const Bytes p = Packet(baseOnly);
    InBandSearch s;
    unsigned int at = 0;
    for (unsigned int i = 1; i <= 2 * IN_BAND_PACKETS && !at; ++i)
      if (LookInPacket(s, p.data(), p.size()) != InBand::LOOKING)
        at = i;
    CHECK(at == IN_BAND_PACKETS, "given up after %u packets, not %u", at, IN_BAND_PACKETS);
    // the same sets again are kept once
    InBandSearch twice;
    const Bytes* sps1 = FirstOf(units, 33, 1);
    const Bytes again = Packet({sps1, sps1});
    LookInPacket(twice, again.data(), again.size());
    CHECK(twice.sps.size() == 1, "an SPS kept %zu times", twice.sps.size());
    // a record of 254 arrays takes no more
    InBandSearch full = search(Packet(first)).second;
    Bytes many = base;
    many[22] = 254;
    CHECK(HvccWithSets(many.data(), many.size(), full).empty(), "255 arrays and more made");
    CHECK(HvccWithSets(base.data(), base.size(), InBandSearch{}).empty(), "a record without sets made");
  }
  std::printf("in band: the sets of the second view found before its first picture, and nowhere else\n");
}

static void NvdecChoice()
{
  CHECK(NvidiaDriverSeries("620.73.04\n") == 620, "620.73.04");
  CHECK(NvidiaDriverSeries("580.196.11") == 580, "580.196.11");
  CHECK(NvidiaDriverSeries("615") == 615, "615");
  CHECK(!NvidiaDriverSeries("") && !NvidiaDriverSeries("abc") && !NvidiaDriverSeries("62a.1") &&
            !NvidiaDriverSeries("1234567.1"),
        "a version that is none taken");
  using N = KODI::MULTIVIEW::Nvdec;
  CHECK(NvdecForTwoViews(true, false, true, false, 620) == N::ASKED, "620: not asked");
  CHECK(NvdecForTwoViews(true, false, true, false, NVDEC_TWO_VIEWS_DRIVER) == N::ASKED, "610: not asked");
  CHECK(NvdecForTwoViews(true, false, true, false, 609) == N::OLD_DRIVER, "609: asked");
  CHECK(NvdecForTwoViews(true, false, true, false, 580) == N::OLD_DRIVER, "580: asked");
  CHECK(NvdecForTwoViews(true, false, true, false, std::nullopt) == N::NO_DRIVER, "no driver: asked");
  CHECK(NvdecForTwoViews(false, false, true, false, 620) == N::NOT_HEVC, "MVC: asked");
  CHECK(NvdecForTwoViews(true, true, true, false, 620) == N::SOFTWARE, "software asked: asked");
  CHECK(NvdecForTwoViews(true, false, false, false, 620) == N::NOT_ALLOWED, "hardware off: asked");
  CHECK(NvdecForTwoViews(true, false, true, true, 620) == N::FAILED_BEFORE, "failed before: asked");
  for (N n : {N::ASKED, N::NOT_HEVC, N::SOFTWARE, N::NOT_ALLOWED, N::FAILED_BEFORE, N::NO_DRIVER,
              N::OLD_DRIVER})
    CHECK(NvdecWhyNot(n)[0] != '\0', "a reason without words");
  std::printf("NVDEC: asked with a 610 driver or later only, never against the user or for MVC\n");
}

static void Disparity()
{
  // 0.02 of 1920 (a value of iPhone's spatial video): 38.4 pixels between the eyes, 19.2 each, in steps of
  // 2: 20, the left eye to the left for a positive value
  CHECK(LeftEyeShift(200, 1920, 2) == -20, "+0.02 of 1920: %d", LeftEyeShift(200, 1920, 2));
  CHECK(LeftEyeShift(-200, 1920, 2) == 20, "-0.02 of 1920: %d", LeftEyeShift(-200, 1920, 2));
  CHECK(LeftEyeShift(200, 3840, 2) == -38, "+0.02 of 3840: %d", LeftEyeShift(200, 3840, 2));
  CHECK(LeftEyeShift(10000, 1920, 2) == -960, "+1 of 1920: %d", LeftEyeShift(10000, 1920, 2));
  CHECK(LeftEyeShift(10, 2000, 2) == -2 && LeftEyeShift(-10, 2000, 2) == 2, "a half step not away from 0");
  CHECK(LeftEyeShift(9, 2000, 2) == 0, "less than half a step moved");
  CHECK(LeftEyeShift(0, 1920, 2) == 0 && LeftEyeShift(10001, 1920, 2) == 0 &&
            LeftEyeShift(-10001, 1920, 2) == 0 && LeftEyeShift(200, 0, 2) == 0 &&
            LeftEyeShift(200, 1920, 0) == 0,
        "nothing, or out of its range, moved");
  CHECK(LeftEyeShift(200, 1920, 1) == -19, "steps of 1: %d", LeftEyeShift(200, 1920, 1));
  CHECK(LeftEyeShift(200, 1920, 2) == -LeftEyeShift(-200, 1920, 2), "the sign is not the only difference");
  std::printf("disparity: positive moves the left eye left and the scene back, in whole chroma steps\n");
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::printf("usage: %s <folder of MVHEVCS_*.bit>\n", argv[0]);
    return 2;
  }
  Streams(argv[1]);
  Scalability(argv[1]);
  Eyes();
  Tracks(argv[1]);
  Pacing();
  Working();
  InBandSets(argv[1]);
  NvdecChoice();
  Disparity();
  for (int i = 2; i + 1 < argc; i += 2)
  {
    const Bytes data = Read(argv[i]);
    const auto layer = MvHevcSecondLayer(data.data(), data.size());
    const std::string want = argv[i + 1];
    CHECK(!data.empty() && (want == "none" ? !layer : layer && *layer == std::stoi(want)),
          "%s: layer %d, expected %s", argv[i], layer ? *layer : -1, want.c_str());
    if (layer)
      std::printf("%s (%zu bytes): two views, the second in layer %d\n", argv[i], data.size(), *layer);
    else
      std::printf("%s (%zu bytes): not two views\n", argv[i], data.size());
  }
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
