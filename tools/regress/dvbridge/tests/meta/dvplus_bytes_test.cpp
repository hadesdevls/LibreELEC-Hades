// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the HDR10+ InfoFrame of Dolby Vision sent as HDR10+, byte by byte, from the
// module of the tree (tools/dvbridge/dvbridge_dvplus.c) through Kodi's own coding (MakeVsif() of
// xbmc/utils/HDR10PlusVsif.h). Every expected byte is written out here.
//  P  the places of 5 and 10 % are measured percentiles of the same histogram as the others: a picture with
//     dark pixels gives them in their bins, the distribution never decreases; 1.0 RC4 put 0 and 0.00255 there;
//  B  the payload of given statistics, application version 1 and 0 (items 4 and 6): PB4 0x40 / 0x00 (no targeted
//     display luminance: 0 without a curve), PB7 the 5 % in units of 16 cd/m2, PB8 the 10 % as coded in version 1
//     (0.1 cd/m2, 255 at most) and in units of 16 cd/m2 in version 0, no curve, no delay;
//  L  provisional statistics from a level 1 block: in order, within min and max, nothing without one.
// (The module of 1.0 RC4, which had no metadata of version 0 and no provisional statistics, failed P and B1; the
// runner proves the checks with mutants of dvbridge_dvplus.c.)
extern "C"
{
#include "dvbridge_dvplus.h"
}
#include "utils/HDR10PlusVsif.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace KODI::UTILS::HDR10PLUS;

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

static void Print(const char* what, const Payload& p)
{
  std::printf("     %s:", what);
  for (unsigned char c : p)
    std::printf(" %02x", c);
  std::printf("\n");
}

int main()
{
  // ---- P: a picture: 3 % of its samples in PQ bin 1100, 4 % in bin 1600, the rest in bin 2600 and 0.01 % in bin 3200
  // (bins of 1/4096 of PQ)
  {
    static uint64_t h[DVBRIDGE_DVPLUS_BINS];
    std::memset(h, 0, sizeof(h));
    h[1100] = 300;
    h[1600] = 400;
    h[2600] = 9299;
    h[3200] = 1;
    dvbridge_dvplus_stats s{};
    dvbridge_dvplus_stats_of(h, DVBRIDGE_DVPLUS_BINS, 10000, 0, &s);
    auto in = [](unsigned v, unsigned bin)
    {
      const double lo = dvbridge_dvplus_nits(bin / 4096.0) * 10, hi = dvbridge_dvplus_nits((bin + 1) / 4096.0) * 10;
      return v + 1 >= lo && v <= hi + 1;
    };
    CHECK(in(s.percentiles[1], 1600), "P1 the 5 %% is in bin 1600 (%u in 1/100000)", s.percentiles[1]);
    CHECK(in(s.percentiles[2], 2600), "P1 the 10 %% is in bin 2600 (%u)", s.percentiles[2]);
    CHECK(in(s.percentiles[0], 1100), "P1 the 1 %% is in bin 1100 (%u)", s.percentiles[0]);
    bool order = true;
    for (int i = 0; i + 1 < 9; ++i)
      order &= s.percentiles[i] <= s.percentiles[i + 1];
    CHECK(order, "P2 the distribution never decreases");
    CHECK(!(s.percentiles[1] == 0 && s.percentiles[2] == 255), "P3 not the 0 and 0.00255 of 1.0 RC4");
    AVDynamicHDRPlus m{};
    dvbridge_dvplus_metadata(&s, &m);
    const auto p = MakeVsif(m);
    // (in version 1 PB7 and PB8 carry y99 and y100nit, 0 for statistics of a histogram alone)
    CHECK(p && (*p)[6] == dvbridge_dvplus_vsif_code(s.y99) && (*p)[7] == s.y100nit && (*p)[7] != 255,
          "P4 version 1: PB7 DistributionY99, PB8 DistributionY100nit as it is (never the 255 of 1.0 RC5)");
    if (p)
      Print("P picture, version 1", *p);
  }

  // ---- B: given statistics (1/100000 of 10000 cd/m2: 10 is 1 cd/m2)
  dvbridge_dvplus_stats s{};
  s.average = 523; // 52.3 cd/m2
  const unsigned v[9] = {3, 9, 78, 560, 2190, 10360, 27140, 46680, 99450};
  for (int i = 0; i < 9; ++i)
    s.percentiles[i] = v[i];
  s.maxrgb = 99900;
  s.y99 = 98000; // 9800 cd/m2 -> 613, at most 255
  s.y100nit = 87;
  {
    AVDynamicHDRPlus m{};
    dvbridge_dvplus_metadata(&s, &m);
    const char* why = "unset";
    const auto p = MakeVsif(m, &why);
    // 52.3 cd/m2 -> (52 + 8) >> 4 = 3; 0.3 -> 0; PB7 DistributionY99 9800 -> 255, PB8 DistributionY100nit
    // 87 as it is; 56 -> 4; 219 -> 14; 1036 -> 65; 2714 -> 170; 4668 -> 255 (292 at most 255); 9945 -> 255
    static const Payload want = {0x8B, 0x84, 0x90, 0x40, 3, 0, 255, 87, 4, 14, 65, 170, 255, 255,
                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40};
    CHECK(p && *p == want && why == nullptr, "B1 version 1, every place measured: the bytes");
    if (p)
      Print("B1 version 1", *p);
    CHECK(m.targeted_system_display_maximum_luminance.num == 0 && !m.params[0].tone_mapping_flag,
          "B1 no curve and a targeted display luminance of 0 (what licensed films carry without a curve)");
  }
  {
    AVDynamicHDRPlus m{};
    dvbridge_dvplus_metadata_version(&s, 0, &m);
    const auto p = MakeVsif(m);
    // version 0: PB4 0x00; PB8 in units of 16 cd/m2: 7.8 -> 0
    static const Payload want = {0x8B, 0x84, 0x90, 0x00, 3, 0, 0, 0, 4, 14, 65, 170, 255, 255,
                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x40};
    CHECK(p && *p == want, "B2 version 0: the bytes");
    if (p)
      Print("B2 version 0", *p);
    CHECK(m.application_version == 0, "B2 the metadata are of version 0");
    dvbridge_dvplus_metadata_version(&s, 7, &m);
    CHECK(m.application_version == 1, "B3 any other version is taken as 1");
  }
  // ---- L
  {
    dvbridge_dvplus_rpu rpu{};
    rpu.level1 = true;
    rpu.min_pq = 62;
    rpu.avg_pq = 1806;
    rpu.max_pq = 3079;
    dvbridge_dvplus_stats t{};
    CHECK(dvbridge_dvplus_provisional(&rpu, &t), "L1 made from a level 1 block");
    bool order = true;
    for (int i = 0; i + 1 < 9; ++i)
      order &= t.percentiles[i] <= t.percentiles[i + 1];
    const double lo = dvbridge_dvplus_nits(62 / 4095.0) * 10, hi = dvbridge_dvplus_nits(3079 / 4095.0) * 10;
    CHECK(order && t.percentiles[0] + 1 >= lo && t.percentiles[8] <= hi + 1 && t.maxrgb <= hi + 1,
          "L1 in order, within min and max");
    CHECK(std::fabs(t.percentiles[4] - dvbridge_dvplus_nits(1806 / 4095.0) * 10) <= 1, "L1 the median at avg_pq");
    AVDynamicHDRPlus m{};
    dvbridge_dvplus_metadata(&t, &m);
    CHECK(MakeVsif(m).has_value(), "L1 a payload of it");
    rpu.level1 = false;
    CHECK(!dvbridge_dvplus_provisional(&rpu, &t), "L2 nothing without a level 1 block");
    CHECK(!dvbridge_dvplus_provisional(nullptr, &t), "L2 nothing without an RPU");
    rpu.level1 = true;
    rpu.min_pq = 2000;
    rpu.avg_pq = 1000;
    rpu.max_pq = 500;
    CHECK(dvbridge_dvplus_provisional(&rpu, &t), "L3 a block out of order");
    order = true;
    for (int i = 0; i + 1 < 9; ++i)
      order &= t.percentiles[i] <= t.percentiles[i + 1];
    CHECK(order, "L3 ... still never decreases");
  }
  std::printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures != 0;
}
