// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of the places of the distribution of maxRGB in the HDR10+ InfoFrame (HDR10PlusVsif.h:
// VSIF_PERCENTAGES, DistributionFrom(), MakeVsif()) and of the line of the log for metadata that it
// cannot carry (CGap::Picture() with a reason); and, for the player information, what the display is
// sent: the payload that the kernel keeps (CHDR10PlusOutput::Sent()) and what it says (Describe()).
//
// The InfoFrame has nine places, each of one percentage (1, 5, 10, 25, 50, 75, 90, 95 and 99); the
// metadata give each entry its percentage. The runner proves the checks with mutants of HDR10PlusVsif.h and
// HDR10PlusOutput.h: entries taken by their places, and no reading of what is sent; the checks of each fail there.
//
// Usage: hdr10plus_vsif_test (exit 0 when every check passed)

#include "HDR10PlusOutput.h"
#include "utils/HDR10PlusVsif.h"

#include <cstdio>
#include <cstring>
#include <string>

using KODI::UTILS::HDR10PLUS::CGap;
using KODI::UTILS::HDR10PLUS::Payload;

static int g_failures = 0;
static int g_checks = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

static std::optional<Payload> Make(const AVDynamicHDRPlus& m, const char** why = nullptr)
{
  return KODI::UTILS::HDR10PLUS::MakeVsif(m, why);
}

static std::optional<Payload> Picture(CGap& g, const std::optional<Payload>& own, const char* why)
{
  return g.Picture(own, why);
}

// The metadata of FFmpeg's FATE test hevc-hdr10-plus-metadata (as the first host test has them)
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

// A distribution of the given percentages, each value made so that its code in the InfoFrame tells it
// apart: entry k gets (16 * (10 + k) + 8) * 10 in 1/100000 (code 10 + k), so a place that takes another
// entry shows another code
static AVDynamicHDRPlus Distribution(int version, const std::initializer_list<int>& percentages)
{
  AVDynamicHDRPlus m = Fate();
  m.application_version = version;
  AVHDRPlusColorTransformParams& w = m.params[0];
  w.num_distribution_maxrgb_percentiles = static_cast<uint8_t>(percentages.size());
  int k = 0;
  for (int p : percentages)
  {
    w.distribution_maxrgb[k].percentage = static_cast<uint8_t>(p);
    w.distribution_maxrgb[k].percentile = {(16 * (10 + k)) * 10, 100000};
    ++k;
  }
  return m;
}

static std::string Hex(const std::optional<Payload>& p)
{
  if (!p)
    return "(none)";
  std::string s;
  char b[4];
  for (int i = 5; i < 14; ++i)
  {
    std::snprintf(b, sizeof(b), "%02X ", (*p)[i]);
    s += b;
  }
  return s;
}

int main()
{
  setvbuf(stdout, nullptr, _IOLBF, 0);
  std::printf("HDR10+ InfoFrame test, %s\n",
              "the new HDR10PlusVsif.h"
  );

  // ---------------------------------------------------------------- what was right stays
  {
    const Payload expected = {0x8B, 0x84, 0x90, 0x5A, 0x03, 0x00, 0x12, 0xFF, 0x00,
                              0x08, 0x0A, 0x0C, 0x0C, 0x12, 0x90, 0x00, 0x00, 0x1A,
                              0x33, 0x4D, 0x67, 0x80, 0x9A, 0xB3, 0xCD, 0xE7, 0x40};
    const char* why = "x";
    const auto p = Make(Fate(), &why);
    Check(p == expected && !why, "the FATE metadata (nine at 1 ... 99): the same 27 bytes, and no reason");
    // the standard nine, in their places: entry k in place k
    const auto q = Make(Distribution(1, {1, 5, 10, 25, 50, 75, 90, 95, 99}));
    bool places = q.has_value();
    for (int i = 0; q && i < 9; ++i)
      places = places && (i == 2 ? (*q)[5 + i] == 255 : (*q)[5 + i] == 10 + i);
    Check(places, "version 1, the nine: each entry in its place (the third as coded, 255): " + Hex(q));
    // version 0 with ten (98 before 99): the tenth in the ninth place, as before
    const auto v0 = Make(Distribution(0, {1, 5, 10, 25, 50, 75, 90, 95, 98, 99}));
    Check(v0 && (*v0)[13] == 10 + 9 && (*v0)[12] == 10 + 7 && (*v0)[7] == 10 + 2,
          "version 0, ten with 98 before 99: the 99th in the ninth place, the third coded: " + Hex(v0));
  }

  // ---------------------------------------------------------------- the place of each value
  {
    // version 1 with ten (hdr10plus_tool's second form): the 99th goes in the ninth place, not the 98th
    const auto p = Make(Distribution(1, {1, 5, 10, 25, 50, 75, 90, 95, 98, 99}));
    Check(p && (*p)[13] == 10 + 9,
          "version 1, ten with 98 before 99: the ninth place takes the 99th (it took the 98th): " + Hex(p));
  }
  {
    // a percentage that is not the one of its place, at each place: no InfoFrame
    const int standard[9] = {1, 5, 10, 25, 50, 75, 90, 95, 99};
    for (int at = 0; at < 9; ++at)
    {
      AVDynamicHDRPlus m = Distribution(1, {1, 5, 10, 25, 50, 75, 90, 95, 99});
      m.params[0].distribution_maxrgb[at].percentage = static_cast<uint8_t>(standard[at] + 1);
      const char* why = nullptr;
      const auto p = Make(m, &why);
      Check(!p, "the entry of place " + std::to_string(at + 1) + " at " + std::to_string(standard[at] + 1) +
                    " %: no InfoFrame (its value would go in the place of the one at " +
                    std::to_string(standard[at]) + " %): " + Hex(p));
      if (at == 0)
        Check(why && std::strstr(why, "distribution of maxRGB"), std::string("... and why: ") + (why ? why : ""));
    }
  }
  {
    struct
    {
      int version;
      std::initializer_list<int> percentages;
      const char* what;
    } cases[] = {
        {1, {1, 5, 10, 25, 50, 75, 90, 95}, "eight entries (the ninth place went out 0)"},
        {1, {1, 5, 10, 25, 50, 75, 90, 95, 99, 100}, "ten entries that end at 100"},
        {0, {1, 5, 10, 25, 50, 75, 90, 95, 97, 99}, "version 0, ten with 97 before 99"},
        {0, {1, 5, 10, 25, 50, 75, 90, 95, 99, 98}, "version 0, ten with 99 before 98"},
        {1, {1, 5, 10, 50, 25, 75, 90, 95, 99}, "the nine, two of them in each other's place"},
        {1, {1, 2, 5, 10, 25, 50, 75, 90, 95, 99, 100}, "eleven entries"},
        {1, {50}, "one entry"},
        {1, {}, "no entry"},
        {1, {10, 20, 30, 40, 50, 60, 70, 80, 90}, "nine entries of other percentages"},
    };
    for (const auto& c : cases)
    {
      const auto p = Make(Distribution(c.version, c.percentages));
      Check(!p, std::string(c.what) + ": no InfoFrame: " + Hex(p));
    }
  }
  {
    // the rest is as it was: no window, or a version above 1
    AVDynamicHDRPlus m = Fate();
    m.num_windows = 0;
    const char* why = nullptr;
    Check(!Make(m, &why), "no window: no InfoFrame");
    m = Fate();
    m.application_version = 2;
    Check(!Make(m, &why), "an application version above 1: no InfoFrame");
  }

  // ---------------------------------------------------------------- the log, once a stream
  {
    const Payload a = *Make(Fate());
    CGap g;
    const char* why = "a distribution of maxRGB that is not the one its InfoFrame carries";
    int told = 0;
    std::string first;
    // the first pictures with metadata it can carry, then pictures whose metadata it cannot
    for (int i = 0; i < 5; ++i)
      Picture(g, a, nullptr);
    for (int i = 0; i < 30; ++i)
    {
      const auto sent = Picture(g, std::nullopt, why);
      if (g.Message().find("cannot go in its InfoFrame") != std::string::npos)
      {
        if (!told++)
          first = g.Message();
      }
      // (fewer than 12 in a row keep the last InfoFrame: a picture whose metadata cannot be carried is a
      // picture without)
      if (i == 3)
        Check(sent == a, "a picture whose metadata cannot be carried keeps the last InfoFrame, as one without");
    }
    Check(told == 1 && first.find(why) != std::string::npos,
          "the log says once a stream that the metadata cannot be carried, and why: \"" + first + "\"");
    g.Flush();
    told = 0;
    for (int i = 0; i < 3; ++i)
    {
      Picture(g, std::nullopt, why);
      told += g.Message().find("cannot go in its InfoFrame") != std::string::npos;
    }
    Check(told == 0, "... not again after a seek");
    g.Reset();
    Picture(g, std::nullopt, why);
    Check(g.Message().find(why) != std::string::npos, "... and again in a new stream");
    CGap h;
    int said = 0;
    for (int i = 0; i < 20; ++i)
    {
      Picture(h, std::nullopt, nullptr);
      said += h.Message().find("cannot go in its InfoFrame") != std::string::npos;
    }
    Check(said == 0, "pictures that have no metadata say nothing of it");
  }

  // ---------------------------------------------------------------- what the display is sent
  {
    using KODI::UTILS::HDR10PLUS::Describe;
    const Payload a = *Make(Fate());
    // PB4 0x5A: version 1, target 13 x 32 = 416; PB5 3 x 16 = 48; PB14 0x12 x 16 = 288; 9 anchors
    const auto s = Describe(a);
    Check(s.version == 1 && s.target == 416 && s.average == 48 && s.p99 == 288 && s.anchors == 9 && !s.overlay,
          "what the FATE payload says: version " + std::to_string(s.version) + ", target " +
              std::to_string(s.target) + ", average " + std::to_string(s.average) + ", 99th " +
              std::to_string(s.p99) + ", " + std::to_string(s.anchors) + " anchors");
    Check(Describe(KODI::UTILS::HDR10PLUS::WithGraphicsOverlay(a, true)).overlay, "... and its overlay flag");
    AVDynamicHDRPlus m = Fate();
    m.targeted_system_display_maximum_luminance = {0, 1};
    m.params[0].tone_mapping_flag = 0;
    const auto profileA = Describe(*Make(m));
    Check(profileA.target == 0 && profileA.anchors == 0, "no target display and no curve (profile A): 0 and 0");
    m.targeted_system_display_maximum_luminance = {4000, 1};
    Check(Describe(*Make(m)).target == 992, "a target of 4000: the most the InfoFrame codes, 992 (976 or more)");

    using KODI::WINDOWING::GBM::CHDR10PlusOutput;
    Payload b = a;
    b[4] = 0x09;
    CHDR10PlusOutput o;
    Check(!o.Sent(std::nullopt) && !o.Sent(0) && !o.Sent(100), "nothing queued: nothing is sent");
    o.Queued(100, a);
    Check(o.Sent(100) == a, "the kernel keeps the blob queued: its payload is the one sent");
    Check(!o.Sent(0) && !o.Sent(std::nullopt), "the kernel keeps 0, or nothing: none is sent");
    Check(!o.Sent(77), "a value that was not queued here: none is known to be sent");
    o.Queued(101, b);
    Check(o.Sent(101) == b && o.Sent(100) == a,
          "two queued: the one that the kernel keeps (a commit may not have taken the last)");
    o.Queued(0, std::nullopt);
    Check(!o.Sent(0), "a clear queued and kept: none is sent");
    o.Queued(100, b); // (a blob id that comes back once its blob is gone)
    Check(o.Sent(100) == b, "a blob id queued again with another payload: the newest");
    for (uint64_t v = 200; v < 204; ++v)
      o.Queued(v, a);
    Check(!o.Sent(101), "a value older than the last four queued: not known any more");
    o.NewVideo();
    Check(!o.Sent(203), "a new video: nothing known from the one before");
  }

  std::printf("%d checks, %d failed\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "ALL PASSED");
  return g_failures ? 1 : 0;
}
