// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the native HDR10+ path of Kodi with four shapes of metadata reported from real streams
// (a CoreELEC box sends all four as HDR10+):
//   Birds of Prey UHD  version 1, targeted display 500 cd/m2, a curve of 9 anchors
//   The Outpost UHD    version 1, 300 cd/m2, 9 anchors; its first frame as reported: one window, MaxSCL 10000/100000,
//                      average maxRGB 581/100000, 9 percentiles, knee point 281/614
//   Widows UHD         version 1 (the report gives none: taken as 1), targeted display 0, no curve
//   My Spy WEB-DL      version 0, 350 cd/m2, 3 anchors (as 9 and as 10 percentiles: the report gives no count)
// Only those parameters are reported; the values not given (percentiles, anchors) are made up here, of the forms that
// real metadata have (hdr10plus_tool's samples: a value near the peak at 5 %, a small one at 10 %; or percentiles).
// Each goes the way of a picture: an AVDynamicHDRPlus serialised as the ITU-T T.35 message (av_dynamic_hdr_plus_to_t35,
// with the application version of the shape in its first byte: FFmpeg's writer always writes 1) and parsed again by FFmpeg (av_dynamic_hdr_plus_from_t35, what the HEVC decoder gives Kodi), then MakeVsif() of the
// tree, then CHDR10PlusOutput (VersionFits(), Next()) for a display of version 1 and one of version 0. The expected
// payload is coded here on its own from the layout that MakeVsif() documents (Amlogic's hdmitx_set_hdr10plus_pkt()).
// usage: native_shapes_test
extern "C"
{
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/mem.h>
}
#include "utils/HDR10PlusVsif.h"
#include "windowing/gbm/HDR10PlusOutput.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace KODI::UTILS::HDR10PLUS;
using KODI::WINDOWING::GBM::CHDR10PlusOutput;

static int g_failures, g_checks;
#define CHECK(c, ...) \
  do \
  { \
    ++g_checks; \
    if (!(c)) \
    { \
      ++g_failures; \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__); \
      std::printf("\n"); \
    } \
  } while (0)

struct Shape
{
  const char* name;
  int version;
  int target; // cd/m2
  std::vector<int> anchors; // 10 bits each; none: no curve
  int kneeX, kneeY; // 12 bits
  int maxscl, average; // 1/100000
  std::vector<int> pct; // percentages
  std::vector<int> val; // percentiles, 1/100000
};

static AVDynamicHDRPlus Make(const Shape& s)
{
  AVDynamicHDRPlus m{};
  m.itu_t_t35_country_code = 0xB5;
  m.application_version = static_cast<uint8_t>(s.version);
  m.num_windows = 1;
  m.targeted_system_display_maximum_luminance = AVRational{s.target, 1};
  AVHDRPlusColorTransformParams& w = m.params[0];
  for (int i = 0; i < 3; ++i)
    w.maxscl[i] = AVRational{s.maxscl, 100000};
  w.average_maxrgb = AVRational{s.average, 100000};
  w.num_distribution_maxrgb_percentiles = static_cast<uint8_t>(s.pct.size());
  for (std::size_t i = 0; i < s.pct.size(); ++i)
  {
    w.distribution_maxrgb[i].percentage = static_cast<uint8_t>(s.pct[i]);
    w.distribution_maxrgb[i].percentile = AVRational{s.val[i], 100000};
  }
  w.fraction_bright_pixels = AVRational{0, 1000};
  w.tone_mapping_flag = !s.anchors.empty();
  if (w.tone_mapping_flag)
  {
    w.knee_point_x = AVRational{s.kneeX, 4095};
    w.knee_point_y = AVRational{s.kneeY, 4095};
    w.num_bezier_curve_anchors = static_cast<uint8_t>(s.anchors.size());
    for (std::size_t i = 0; i < s.anchors.size(); ++i)
      w.bezier_curve_anchors[i] = AVRational{s.anchors[i], 1023};
  }
  return m;
}

// the layout, coded here on its own
static Payload Expected(const Shape& s)
{
  auto code16 = [](int v) { return std::min(255, (v / 10 + 8) >> 4); };
  Payload p{};
  p[0] = 0x8B;
  p[1] = 0x84;
  p[2] = 0x90;
  const int t = s.target < 1024 ? std::min(31, (s.target + 16) >> 5) : 31;
  p[3] = static_cast<uint8_t>((s.version << 6) | (t << 1));
  p[4] = static_cast<uint8_t>(code16(s.average));
  // the nine places: 1, 5, 10, 25, 50, 75, 90, 95, 99 (with ten entries, the one at 98 left out)
  std::vector<int> nine;
  for (std::size_t i = 0; i < s.pct.size(); ++i)
    if (s.pct[i] != 98)
      nine.push_back(s.val[i]);
  for (int i = 0; i < 9; ++i)
    p[5 + i] = static_cast<uint8_t>(i == 2 && s.version == 1 ? std::min(255, nine[i]) : code16(nine[i]));
  if (!s.anchors.empty())
  {
    const int kx = std::min(1023, (s.kneeX + 2) >> 2), ky = std::min(1023, (s.kneeY + 2) >> 2);
    p[14] = static_cast<uint8_t>((static_cast<int>(s.anchors.size()) << 4) | ((kx >> 6) & 0xF));
    p[15] = static_cast<uint8_t>(((kx & 0x3F) << 2) | ((ky >> 8) & 0x3));
    p[16] = static_cast<uint8_t>(ky & 0xFF);
    for (std::size_t i = 0; i < s.anchors.size(); ++i)
      p[17 + i] = static_cast<uint8_t>(std::min(255, (s.anchors[i] + 2) >> 2));
  }
  p[26] = 0x40;
  return p;
}

int main()
{
  const std::vector<int> p9{1, 5, 10, 25, 50, 75, 90, 95, 99};
  const std::vector<int> p10{1, 5, 10, 25, 50, 75, 90, 95, 98, 99};
  const std::vector<Shape> shapes = {
      {"Birds of Prey (v1, 500 cd/m2, 9 anchors)", 1, 500, {143, 298, 447, 592, 731, 864, 891, 917, 938}, 164, 240,
       17830, 1037, p9, {3, 14024, 43, 56, 219, 1036, 2714, 4668, 14445}},
      {"The Outpost (v1, 300 cd/m2, 9 anchors, its first frame)", 1, 300,
       {120, 260, 400, 530, 660, 780, 860, 920, 970}, 281, 614, 10000, 581, p9,
       {0, 9000, 86, 12, 120, 640, 2100, 3400, 9800}},
      {"Widows (v1, 0 cd/m2, no curve)", 1, 0, {}, 0, 0, 45000, 900, p9, {5, 300, 780, 1500, 2600, 6000, 14000, 20000, 41000}},
      {"My Spy (v0, 350 cd/m2, 3 anchors, 9 percentiles)", 0, 350, {300, 600, 850}, 400, 500, 30000, 750, p9,
       {2, 30, 80, 400, 1300, 3500, 9000, 14000, 28000}},
      {"My Spy (v0, 350 cd/m2, 3 anchors, 10 percentiles)", 0, 350, {300, 600, 850}, 400, 500, 30000, 750, p10,
       {2, 30, 80, 400, 1300, 3500, 9000, 14000, 26000, 28000}},
  };
  for (const Shape& s : shapes)
  {
    const AVDynamicHDRPlus made = Make(s);
    uint8_t* t35 = nullptr;
    size_t size = 0;
    const int w = av_dynamic_hdr_plus_to_t35(&made, &t35, &size);
    // (FFmpeg's writer always writes application version 1, its first byte: a stream of version 0 has 0 there)
    if (w >= 0 && size > 0)
      t35[0] = static_cast<uint8_t>(s.version);
    AVDynamicHDRPlus parsed{};
    const int r = w >= 0 ? av_dynamic_hdr_plus_from_t35(&parsed, t35, size) : w;
    av_free(t35);
    CHECK(w >= 0 && r >= 0, "%s: FFmpeg writes and parses its T.35 message (%d, %d)", s.name, w, r);
    if (r < 0)
      continue;
    const char* why = "unset";
    const auto p = MakeVsif(parsed, &why);
    const Payload want = Expected(s);
    CHECK(p && why == nullptr, "%s: MakeVsif() takes it (%s)", s.name, why ? why : "-");
    CHECK(p && *p == want, "%s: the payload, byte by byte", s.name);
    if (p)
    {
      std::printf("     %s:", s.name);
      for (unsigned char c : *p)
        std::printf(" %02x", c);
      std::printf("\n");
      for (int receiver = 0; receiver <= 1; ++receiver)
      {
        const bool fits = CHDR10PlusOutput::VersionFits(*p, receiver);
        CHECK(fits == (s.version <= receiver), "%s: to a display of version %d: %s", s.name, receiver,
              s.version <= receiver ? "goes" : "HDR10");
        CHDR10PlusOutput out;
        CHDR10PlusOutput::Output o;
        o.pq = o.display = o.driver = true;
        o.receiverVersion = receiver;
        CHECK((out.Next(o, *p) == CHDR10PlusOutput::Step::SEND) == fits,
              "%s: CHDR10PlusOutput to a display of version %d", s.name, receiver);
      }
    }
  }
  // the first bytes of The Outpost's first frame, written out: version 1 and 300 cd/m2 (code 9), average 581 (code 4),
  // knee point 281/614 (10 bits: 70 and 154), 9 anchors
  {
    const Payload want = Expected(shapes[1]);
    CHECK(want[3] == 0x52 && want[4] == 4 && want[14] == 0x91 && want[15] == 0x18 && want[16] == 0x9A,
          "The Outpost: PB4 0x52, PB5 4, PB15..17 0x91 0x18 0x9a (%02x %02x %02x %02x %02x)", want[3], want[4],
          want[14], want[15], want[16]);
  }
  std::printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures != 0;
}
