// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the arithmetic of the GPU time of each pass, without a GPU:
// tools/dvbridge/dvbridge_passtime.h (which times are new in libplacebo's list, the window, merging and order) and
// xbmc/windowing/gbm/DVPassTimes.h (what the log and the Dolby Vision page say). Every expected value is written
// out here.
// Usage: passtimes_test   (built with -I<tree>/tools/dvbridge -I<tree>/xbmc/windowing/gbm)
#include "DVPassTimes.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using KODI::WINDOWING::GBM::CDVPassTimes;

static int fails, checks;
#define CHECK(c, ...) \
  do \
  { \
    checks++; \
    if (!(c)) \
    { \
      fails++; \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__); \
      std::printf("\n"); \
    } \
  } while (0)

static bool Near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

int main()
{
  static dvbridge_passtime t;
  std::memset(&t, 0, sizeof(t));
  uint64_t ring[256];
  dvbridge_pass_time out[8];

  // a new pass: its first call has its first few times, all new
  ring[0] = 1000000; // 1 ms
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 1, 1);
  CHECK(t.n == 1 && t.e[0].t.count == 1 && Near(t.e[0].t.sum_ms, 1.0), "the first time of a new pass");
  // no new time: the list as it was
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 1, 1);
  CHECK(t.e[0].t.count == 1, "a call with no new time adds nothing (%u)", t.e[0].t.count);
  // three new at once (oldest first): the last three of the list
  ring[1] = 2000000;
  ring[2] = 3000000;
  ring[3] = 4000000;
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 4, 4);
  CHECK(t.e[0].t.count == 4 && Near(t.e[0].t.sum_ms, 10.0) && Near(t.e[0].t.max_ms, 4.0),
        "three new times at once (%u, %.3f, %.3f)", t.e[0].t.count, t.e[0].t.sum_ms, t.e[0].t.max_ms);
  // a full list keeps its length: the count tells the new ones (2 here, the last two of it)
  for (int i = 0; i < 256; i++)
    ring[i] = 500000; // 0.5 ms
  ring[254] = 7000000;
  ring[255] = 8000000;
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 256, 300);
  // (the count went from 4 to 300: 296 new, of which the list holds 256)
  CHECK(t.e[0].t.count == 4 + 256, "more new times than the list holds: as many as it holds (%u)", t.e[0].t.count);
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 256, 302);
  CHECK(t.e[0].t.count == 4 + 256 + 2 && Near(t.e[0].t.max_ms, 8.0), "a full list: the two new ones (%u)",
        t.e[0].t.count);
  // a count that goes back (another pass in the place of a pass): nothing taken
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 256, 10);
  CHECK(t.e[0].t.count == 262, "a count that goes back adds nothing");
  // a pass first seen with many times (its place had been given away): only its count is taken
  dvbridge_passtime_ring(&t, 0xB, "decoding (YCbCr), sampling", ring, 256, 5000);
  CHECK(t.n == 2 && t.e[1].t.count == 0, "a pass first seen with a long list takes none of it (%u)", t.e[1].t.count);
  dvbridge_passtime_ring(&t, 0xB, "decoding (YCbCr), sampling", ring, 256, 5001);
  CHECK(t.e[1].t.count == 1 && Near(t.e[1].t.sum_ms, 8.0), "... and its next times");
  // the bridge's own: one time each
  dvbridge_passtime_add(&t, 0x44564250ULL, "bridge: packing (source transport)", 2500000);
  dvbridge_passtime_add(&t, 0x44564250ULL, "bridge: packing (source transport)", 3500000);
  // another pass of the same name (another signature of one kind of pass): merged in the window
  ring[0] = 9000000;
  dvbridge_passtime_ring(&t, 0xC, "decoding (YCbCr), sampling", ring, 1, 1);

  // the window: the most time first, merged by name
  unsigned n = dvbridge_passtime_take(&t, out, 8);
  CHECK(n == 3, "three names (%u)", n);
  // composer luma: 262 times, 1+2+3+4 + 254*0.5 + 7+8 + 7+8 = 167 ms; decoding: 8 + 9 = 17 ms; packing 6 ms
  CHECK(n >= 3 && !std::strcmp(out[0].name, "DV composer: luma (CCM 001 5.4)") && out[0].count == 262 &&
            Near(out[0].sum_ms, 167.0),
        "the most time first: %s %u %.3f", out[0].name, out[0].count, out[0].sum_ms);
  CHECK(n >= 3 && !std::strcmp(out[1].name, "decoding (YCbCr), sampling") && out[1].count == 2 &&
            Near(out[1].sum_ms, 17.0) && Near(out[1].max_ms, 9.0),
        "two passes of one name merged: %s %u %.3f %.3f", out[1].name, out[1].count, out[1].sum_ms, out[1].max_ms);
  CHECK(n >= 3 && !std::strcmp(out[2].name, "bridge: packing (source transport)") && out[2].count == 2 &&
            Near(out[2].max_ms, 3.5),
        "the bridge's own: %s %u %.3f", out[2].name, out[2].count, out[2].max_ms);
  dvbridge_pass_time one[1];
  CHECK(dvbridge_passtime_take(&t, one, 1) == 0, "the window started again: nothing in it");
  // the counts are kept across windows: the list as it was gives nothing, one more time one
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 256, 10);
  CHECK(dvbridge_passtime_take(&t, one, 1) == 0, "the count kept from the last window: nothing new");
  ring[255] = 1500000;
  dvbridge_passtime_ring(&t, 0xA, "DV composer: luma (CCM 001 5.4)", ring, 256, 11);
  n = dvbridge_passtime_take(&t, one, 1);
  CHECK(n == 1 && one[0].count == 1 && Near(one[0].sum_ms, 1.5), "one new time after the window (%u)", n);
  // times that are not to be trusted (GL_GPU_DISJOINT_EXT): dropped, the counts kept
  dvbridge_passtime_add(&t, 0x44564250ULL, "bridge: packing (source transport)", 2500000);
  dvbridge_passtime_drop(&t);
  CHECK(dvbridge_passtime_take(&t, one, 1) == 0, "the window dropped");
  // a long name is cut, never past its buffer
  std::string longName(200, 'x');
  dvbridge_passtime_add(&t, 0xD, longName.c_str(), 1000000);
  n = dvbridge_passtime_take(&t, one, 1);
  CHECK(n == 1 && std::strlen(one[0].name) == DVBRIDGE_PASS_NAME - 1, "a long name cut (%zu)", std::strlen(one[0].name));
  // places: a pass idle for long is forgotten only when places run out
  static dvbridge_passtime full;
  std::memset(&full, 0, sizeof(full));
  for (unsigned k = 0; k < DVBRIDGE_PASSTIME_ENTRIES; k++)
    dvbridge_passtime_add(&full, 100 + k, "a pass", 1000000);
  dvbridge_passtime_add(&full, 1000, "one too many", 1000000);
  CHECK(full.n == DVBRIDGE_PASSTIME_ENTRIES && full.unplaced == 1, "no place left: the time is counted as unplaced");
  for (int w = 0; w < DVBRIDGE_PASSTIME_IDLE + 1; w++)
  {
    dvbridge_passtime_add(&full, 100, "a pass", 1000000); // (only the first one keeps working)
    dvbridge_passtime_take(&full, nullptr, 0);
  }
  CHECK(full.n == 1 && full.e[0].key == 100, "the idle ones forgotten once places ran out (%u left)", full.n);
  static dvbridge_passtime few;
  std::memset(&few, 0, sizeof(few));
  dvbridge_passtime_add(&few, 1, "a", 1000000);
  dvbridge_passtime_add(&few, 2, "b", 1000000);
  for (int w = 0; w < DVBRIDGE_PASSTIME_IDLE + 2; w++)
    dvbridge_passtime_take(&few, nullptr, 0);
  CHECK(few.n == 2, "with places to spare an idle pass keeps its count (%u)", few.n);

  // --- what the log and the page say
  CHECK(CDVPassTimes::Short("decoding (YCbCr), sampling, color conversion") == "decoding", "short: the first step");
  // (the names that libplacebo and the bridge gave the passes of the render tests on zink, 2026-10-27)
  CHECK(CDVPassTimes::Short("DV composer: luma (CCM 001 5.4)") == "luma", "short: the composer's plane (%s)",
        CDVPassTimes::Short("DV composer: luma (CCM 001 5.4)").c_str());
  CHECK(CDVPassTimes::Short("DV composer: EL luma, vertical (CCM 001 annex B.3)") == "EL luma",
        "short: the composer's plane of the layer (%s)",
        CDVPassTimes::Short("DV composer: EL luma, vertical (CCM 001 annex B.3)").c_str());
  CHECK(CDVPassTimes::Short("nearest, ortho (horiz) upscaling (lanczos), color decoding, DV active picture blanking "
                            "before target overlays") == "output",
        "short: the pass that draws into the target");
  CHECK(CDVPassTimes::Short("nearest, ortho (horiz) upscaling (lanczos), Skip reconstruction outside the DV active "
                            "picture") == "output",
        "short: the pass that draws into the target, with the bars written first");
  CHECK(CDVPassTimes::Short("ortho (vert) upscaling (lanczos)") == "vertical scaling" &&
            CDVPassTimes::Short("ortho (horiz) upscaling (lanczos)") == "horizontal scaling",
        "short: a scaling pass of one direction");
  CHECK(CDVPassTimes::Short("bridge: picture drawn over (source transport)") == "picture drawn over",
        "short: 18 characters fit");
  CHECK(CDVPassTimes::Short("DV active picture blanking before target overlays") == "DV active picture",
        "short: cut at 18, its end trimmed (%s)",
        CDVPassTimes::Short("DV active picture blanking before target overlays").c_str());
  CHECK(CDVPassTimes::Short("bridge: 4:2:2 rows (source transport)") == "4:2:2 rows", "short: the bridge's own");
  CHECK(CDVPassTimes::Short("an extremely long name of a pass") == "an extremely long", "short: cut at 18, its end trimmed (%s)",
        CDVPassTimes::Short("an extremely long name of a pass").c_str());
  CHECK(CDVPassTimes::Short("") == "?" && CDVPassTimes::Short(", x") == "?", "short: nothing left");
  CHECK(CDVPassTimes::Short("abcdefghijklmnopq\xc3\xa9xyz") == "abcdefghijklmnopq", "short: not inside a UTF-8 sequence");
  std::vector<dvbridge_pass_time> v(3);
  std::snprintf(v[0].name, sizeof(v[0].name), "DV composer: chroma (CCM 001 5.4)");
  v[0].count = 240;
  v[0].sum_ms = 1008.0;
  v[0].max_ms = 5.25;
  std::snprintf(v[1].name, sizeof(v[1].name), "DV composer: luma (CCM 001 5.4)");
  v[1].count = 240;
  v[1].sum_ms = 744.0;
  v[1].max_ms = 3.5;
  std::snprintf(v[2].name, sizeof(v[2].name), "bridge: 4:2:2 rows (source transport)");
  v[2].count = 240;
  v[2].sum_ms = 240.0;
  v[2].max_ms = 1.25;
  const std::string line = CDVPassTimes::Line(v);
  CHECK(line == "; GPU time by pass, the most first (average / longest ms, times): \"DV composer: chroma (CCM 001 5.4)\" "
                "4.20 / 5.25 x240; \"DV composer: luma (CCM 001 5.4)\" 3.10 / 3.50 x240; \"bridge: 4:2:2 rows (source "
                "transport)\" 1.00 / 1.25 x240",
        "the line of the log: %s", line.c_str());
  CHECK(CDVPassTimes::Line({}).empty() && CDVPassTimes::Compact({}).empty(), "nothing without passes");
  const std::string page = CDVPassTimes::Compact(v);
  // "chroma 4.2, luma 3.1 ms" (23 characters): the third would make it 39, over 36
  CHECK(page == "chroma 4.2, luma 3.1 ms", "the page: %s", page.c_str());
  CHECK(CDVPassTimes::Chars(page) <= CDVPassTimes::SHOWN_CHARS && page.find("\xc2\xb7") == std::string::npos,
        "the page: within 36 characters, no middle dot (%zu)", CDVPassTimes::Chars(page));
  CHECK(CDVPassTimes::Compact(v, 200) == "chroma 4.2, luma 3.1, 4:2:2 rows 1.0 ms",
        "the page with room: %s", CDVPassTimes::Compact(v, 200).c_str());
  CHECK(CDVPassTimes::Compact(v, 5) == "chroma 4.2 ms", "the page: the first always (%s)",
        CDVPassTimes::Compact(v, 5).c_str());
  // the longest the page can be: the first pass alone, its name at 18 and a time of five digits, or several
  // within 36
  std::vector<dvbridge_pass_time> w(1);
  std::snprintf(w[0].name, sizeof(w[0].name), "bridge: picture drawn over (source transport)");
  w[0].count = 1;
  w[0].sum_ms = 1234.5;
  CHECK(CDVPassTimes::Compact(w) == "picture drawn over 1234.5 ms" && CDVPassTimes::Chars(CDVPassTimes::Compact(w)) <= 36,
        "the page, one slow pass: %s", CDVPassTimes::Compact(w).c_str());

  std::printf("PASSTIMES %s: %d checks, %d failed\n", fails ? "FAILED" : "PASSED", checks, fails);
  return fails != 0;
}
