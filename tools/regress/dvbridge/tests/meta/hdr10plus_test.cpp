// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of the Kodi side of HDR10+: the InfoFrame payload of a picture (HDR10PlusVsif.h) and
// what the window system queues for each frame (HDR10PlusOutput.h).
//
// The metadata is the one of FFmpeg's FATE test hevc-hdr10-plus-metadata, written as ITU-T T.35
// with av_dynamic_hdr_plus_to_t35() and read back with av_dynamic_hdr_plus_from_t35(), as the
// HEVC and AV1 decoders read it. The expected bytes are worked out by hand in the comments.
//
// The display of the output declares an HDR10+ block of application version 1: CHDR10PlusOutput sends a payload
// only to a display whose block takes its version (VersionFits(); receiverVersion -1, the default, is a display
// without one). The runner proves the checks with mutants of HDR10PlusVsif.h and HDR10PlusOutput.h.

#include "HDR10PlusOutput.h"
#include "utils/HDR10PlusVsif.h"

extern "C"
{
#include <libavutil/mem.h>
}

#include <cstdio>
#include <cstring>
#include <string>

using KODI::UTILS::HDR10PLUS::Payload;
using KODI::WINDOWING::GBM::CHDR10PlusOutput;
using Step = CHDR10PlusOutput::Step;

static int g_failures = 0;
static int g_checks = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

static std::optional<Payload> Make(const AVDynamicHDRPlus& m)
{
  return KODI::UTILS::HDR10PLUS::MakeVsif(m);
}

static Step Next(const CHDR10PlusOutput& o,
                 const CHDR10PlusOutput::Output& out,
                 const std::optional<Payload>& p)
{
  return o.Next(out, p);
}

static AVDynamicHDRPlus Fate()
{
  AVDynamicHDRPlus m{};
  m.itu_t_t35_country_code = 0xB5;
  m.application_version = 1;
  m.num_windows = 1;
  m.targeted_system_display_maximum_luminance = {400, 1};
  AVHDRPlusColorTransformParams& w = m.params[0];
  w.maxscl[0] = {3340, 100000};
  w.maxscl[1] = {2870, 100000};
  w.maxscl[2] = {2720, 100000};
  w.average_maxrgb = {510, 100000};
  const int percentages[9] = {1, 5, 10, 25, 50, 75, 90, 95, 99};
  const int percentiles[9] = {30, 2940, 255, 70, 1340, 1600, 1850, 1950, 2940};
  w.num_distribution_maxrgb_percentiles = 9;
  for (int i = 0; i < 9; ++i)
  {
    w.distribution_maxrgb[i].percentage = percentages[i];
    w.distribution_maxrgb[i].percentile = {percentiles[i], 100000};
  }
  w.fraction_bright_pixels = {1, 1000};
  w.tone_mapping_flag = 1;
  w.knee_point_x = {0, 4095};
  w.knee_point_y = {0, 4095};
  w.num_bezier_curve_anchors = 9;
  const int anchors[9] = {102, 205, 307, 410, 512, 614, 717, 819, 922};
  for (int i = 0; i < 9; ++i)
    w.bezier_curve_anchors[i] = {anchors[i], 1023};
  return m;
}

// As a decoder gives it: through the T.35 message
static AVDynamicHDRPlus RoundTrip(const AVDynamicHDRPlus& m)
{
  uint8_t* data = nullptr;
  size_t size = 0;
  AVDynamicHDRPlus out{};
  if (av_dynamic_hdr_plus_to_t35(&m, &data, &size) < 0)
  {
    Check(false, "the metadata is written as T.35");
    return out;
  }
  // (from_t35 takes the message after the country code and the provider codes)
  if (av_dynamic_hdr_plus_from_t35(&out, data, size) < 0)
    Check(false, "the T.35 message is read back");
  av_free(data);
  return out;
}

static std::string Hex(const std::optional<Payload>& p)
{
  if (!p)
    return "(none)";
  std::string s;
  char b[4];
  for (uint8_t v : *p)
  {
    std::snprintf(b, sizeof(b), "%02X ", v);
    s += b;
  }
  return s;
}

int main()
{
  // ---------------------------------------------------------------- the InfoFrame of a picture
  {
    // PB4: version 1 << 6 | ((400 + 16) >> 5 = 13) << 1 = 0x40 | 0x1A = 0x5A
    // PB5: 510 / 10 = 51, (51 + 8) >> 4 = 3
    // PB6..14: 30 -> 0, 2940 -> (294 + 8) >> 4 = 18, 255 as it is (third, version 1),
    //          70 -> 0, 1340 -> 8, 1600 -> 10, 1850 -> 12, 1950 -> 12, 2940 -> 18
    // PB15..17: 9 anchors << 4, knee points 0
    // PB18..26: (102 + 2) >> 2 = 26, 207 >> 2 = 51, 309 >> 2 = 77, 103, 128, 154, 179, 205, 231
    // PB27: no delay
    const Payload expected = {0x8B, 0x84, 0x90, 0x5A, 0x03, 0x00, 0x12, 0xFF, 0x00,
                              0x08, 0x0A, 0x0C, 0x0C, 0x12, 0x90, 0x00, 0x00, 0x1A,
                              0x33, 0x4D, 0x67, 0x80, 0x9A, 0xB3, 0xCD, 0xE7, 0x40};
    const AVDynamicHDRPlus decoded = RoundTrip(Fate());
    const auto p = Make(decoded);
    Check(p == expected, "the FATE metadata: " + Hex(p));
    Check(Make(Fate()) == expected, "the same without the T.35 round trip");
  }
  {
    // (not through T.35, which would drop the anchors: the flag alone must take them away)
    AVDynamicHDRPlus m = Fate();
    m.params[0].tone_mapping_flag = 0;
    m.params[0].knee_point_x = {400, 4095};
    const auto p = Make(m);
    Check(p && (*p)[14] == 0 && (*p)[15] == 0 && (*p)[16] == 0 && (*p)[17] == 0 && (*p)[25] == 0,
          "no tone mapping: no anchors, no knee point: " + Hex(p));
  }
  {
    // knee x 6 -> (6 + 2) >> 2 = 2; knee y 1001 -> 1003 >> 2 = 250 (PB16 0xFA, PB15 low bits 0)
    AVDynamicHDRPlus m = Fate();
    m.params[0].knee_point_x = {6, 4095};
    m.params[0].knee_point_y = {1001, 4095};
    const auto p = Make(m);
    Check(p && (*p)[14] == 0x90 && (*p)[15] == (2 << 2) && (*p)[16] == 0xFA,
          "the knee point rounds to 10 bits: " + Hex(p));
  }
  {
    AVDynamicHDRPlus m = Fate();
    m.application_version = 0;
    m.params[0].num_distribution_maxrgb_percentiles = 10;
    // (the form of ten of the metadata, 98 before 99; this had 99 before 100, a form that
    // MakeVsif() no longer sends, since it puts each entry in the place of its percentage)
    m.params[0].distribution_maxrgb[8].percentage = 98;
    m.params[0].distribution_maxrgb[9].percentage = 99;
    m.params[0].distribution_maxrgb[9].percentile = {5000, 100000};
    // (not through T.35: av_dynamic_hdr_plus_to_t35() always writes application version 1)
    const auto p = Make(m);
    // third: (25 + 8) >> 4 = 2; ninth from the tenth: (500 + 8) >> 4 = 31
    Check(p && ((*p)[3] >> 6) == 0 && (*p)[7] == 2 && (*p)[13] == 31,
          "version 0: the third percentile coded, the tenth for the ninth: " + Hex(p));
  }
  {
    AVDynamicHDRPlus m = Fate();
    m.targeted_system_display_maximum_luminance = {4000, 1};
    m.params[0].average_maxrgb = {99999, 100000};
    m.params[0].knee_point_x = {4095, 4095};
    m.params[0].knee_point_y = {4094, 4095};
    m.params[0].bezier_curve_anchors[0] = {1023, 1023};
    m.params[0].num_bezier_curve_anchors = 15;
    const auto p = Make(m);
    // knee x (4095 + 2) >> 2 = 1024 -> 1023 = 0x3FF; knee y (4094 + 2) >> 2 = 1024 -> 1023
    Check(p && ((*p)[3] & 0x3E) == (31 << 1) && (*p)[4] == 255, "the largest luminances: " + Hex(p));
    Check(p && (*p)[14] == ((9 << 4) | 0xF) && (*p)[15] == ((0x3F << 2) | 0x3) && (*p)[16] == 0xFF,
          "at most 9 anchors, knee points at most 1023: " + Hex(p));
    Check(p && (*p)[17] == 255, "an anchor at most 255: " + Hex(p));
  }
  {
    AVDynamicHDRPlus m = Fate();
    m.targeted_system_display_maximum_luminance = {975, 1};
    const auto p30 = Make(m);
    m.targeted_system_display_maximum_luminance = {976, 1};
    const auto p31 = Make(m);
    Check(p30 && p31 && (((*p30)[3] >> 1) & 0x1F) == 30 && (((*p31)[3] >> 1) & 0x1F) == 31,
          "the targeted luminance rounds to units of 32");
  }
  {
    AVDynamicHDRPlus m = Fate();
    m.params[0].average_maxrgb = {51, 10000};
    Check(Make(m) && (*Make(m))[4] == 0x03, "a rational of another denominator: the same code");
  }
  {
    AVDynamicHDRPlus m = Fate();
    m.num_windows = 0;
    Check(!Make(m), "no window: no InfoFrame");
    m = Fate();
    m.application_version = 2;
    Check(!Make(m), "an application version above 1: no InfoFrame");
  }

  // ---------------------------------------------------------------- what a frame queues
  const Payload a = *KODI::UTILS::HDR10PLUS::MakeVsif(Fate());
  Payload b = a;
  b[4] = 0x09;
  CHDR10PlusOutput::Output on;
  on.pq = on.display = on.driver = true;
  on.receiverVersion = 1; // the display's HDR10+ block, of application version 1
  {
    CHDR10PlusOutput o;
    Check(Next(o, on, a) == Step::SEND, "HDR10 with HDR10+ metadata: sent");
    o.Queued(100, a);
    Check(Next(o, on, a) == Step::NOTHING, "the same metadata again: nothing more");
    Check(Next(o, on, b) == Step::SEND, "other metadata: sent");
    o.Queued(101, b);
    Check(Next(o, on, std::nullopt) == Step::CLEAR, "a frame without metadata: taken away");
    o.Queued(0, std::nullopt);
    Check(Next(o, on, std::nullopt) == Step::NOTHING, "... and nothing more after");
  }
  for (int i = 0; i < 3; ++i)
  {
    CHDR10PlusOutput o;
    CHDR10PlusOutput::Output off = on;
    (i == 0 ? off.pq : i == 1 ? off.display : off.driver) = false;
    const char* why[3] = {"no PQ signalling", "a display without HDR10+", "no HDR10PLUS_VSIF"};
    Check(Next(o, off, a) == Step::NOTHING, std::string(why[i]) + ": nothing queued");
    o.Queued(100, a);
    Check(Next(o, off, a) == Step::CLEAR, std::string(why[i]) + " after HDR10+: taken away");
  }
  {
    // the driver takes it: what the kernel keeps is a value queued
    CHDR10PlusOutput o;
    o.Queued(100, a);
    Check(!o.Committed(100), "taken: not refused");
    o.Queued(101, b);
    for (unsigned int i = 0; i < 20; ++i)
      Check(!o.Committed(i % 2 ? 101 : 100), "a commit later than the queue: not refused");
    Check(!o.Refused(), "the driver takes it");
  }
  {
    // the driver refuses it: the kernel keeps 0 (or nothing) commit after commit
    CHDR10PlusOutput o;
    o.Queued(100, a);
    bool refusedAt = false;
    unsigned int at = 0;
    for (unsigned int i = 1; i <= CHDR10PlusOutput::REFUSED_AFTER + 2; ++i)
    {
      if (o.Committed(0))
      {
        refusedAt = true;
        at = i;
      }
    }
    Check(refusedAt && at == CHDR10PlusOutput::REFUSED_AFTER, "refused after REFUSED_AFTER commits");
    Check(o.Refused() && Next(o, on, a) == Step::CLEAR, "refused: taken away, HDR10");
    o.Queued(0, std::nullopt);
    Check(Next(o, on, a) == Step::NOTHING, "refused: nothing more for this video");
    o.NewVideo();
    Check(!o.Refused() && Next(o, on, a) == Step::SEND, "a new video: tried again");
  }
  {
    // a clear that the kernel keeps counts; a stale 0 does not count for a payload
    CHDR10PlusOutput o;
    o.Queued(0, std::nullopt);
    Check(!o.Committed(0), "a clear kept: not refused");
    o.Queued(102, a);
    unsigned int refused = 0;
    for (unsigned int i = 0; i < CHDR10PlusOutput::REFUSED_AFTER; ++i)
      refused += o.Committed(0) ? 1 : 0;
    Check(refused == 1, "0 in the kernel does not take a payload that was queued");
  }
  {
    CHDR10PlusOutput o;
    o.Queued(100, a);
    o.NewVideo();
    Check(!o.Active() && Next(o, on, std::nullopt) == Step::NOTHING,
          "a new video: nothing queued from before");
  }

  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
