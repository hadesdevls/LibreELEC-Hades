/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of HDMI VRR left off with native Dolby Vision
 * (windowing/gbm/HdmiVrrOff.h, Stage(nativeDV)). Both directions: the commits of HDR10+ stage the
 * property as before, and while native Dolby Vision owns the output nothing is staged over its
 * transport value.
 */
#include "windowing/gbm/HdmiVrrOff.h"

#include <cstdio>
#include <string>

using namespace KODI::WINDOWING::GBM;
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

int main()
{
  // as before (HDR10+, no native Dolby Vision)
  Check(CHdmiVrrOff::Stage(true, true, 0u) == 1u, "HDR10+ mode set, kernel 0: staged 1");
  Check(!CHdmiVrrOff::Stage(true, true, 1u), "kernel has 1 already: nothing");
  Check(CHdmiVrrOff::Stage(true, false, 1u) == 0u, "any other mode set puts 0 back");
  Check(!CHdmiVrrOff::Stage(false, true, 0u), "no mode set pending: nothing");
  Check(!CHdmiVrrOff::Stage(true, true, std::nullopt), "no property: nothing");
  // native Dolby Vision owns the property: the GUI mode set that Kodi stages must not put 0 over
  // the 1 of the transport
  Check(!CHdmiVrrOff::Stage(true, false, 1u, true), "native DV: the 0 of another mode set is not staged");
  Check(!CHdmiVrrOff::Stage(true, true, 0u, true), "native DV: HDR10+ wish is not staged either");
  Check(CHdmiVrrOff::Stage(true, false, 1u, false) == 0u, "after native DV the 0 goes again");
  // Wanted(): the predicate of nvGetAllowedDpyVrrType() (nvkms-vrr.c: "vVisible < 720 || RRx1k < 50000" is
  // none) and the four conditions of Kodi. Each operand of the && alone false, the others true, and the limits
  // of both >= on the value, one below it and one above.
  Check(CHdmiVrrOff::Wanted(true, true, true, true, 2160, 60000), "Wanted: all true");
  Check(!CHdmiVrrOff::Wanted(false, true, true, true, 2160, 60000), "Wanted: no property");
  Check(!CHdmiVrrOff::Wanted(true, false, true, true, 2160, 60000), "Wanted: not the commit of the mode");
  Check(!CHdmiVrrOff::Wanted(true, true, false, true, 2160, 60000), "Wanted: no HDR10+");
  Check(!CHdmiVrrOff::Wanted(true, true, true, false, 2160, 60000), "Wanted: the display declares no VRR");
  Check(!CHdmiVrrOff::Wanted(true, true, true, true, 719, 60000), "Wanted: 719 lines");
  Check(CHdmiVrrOff::Wanted(true, true, true, true, 720, 60000), "Wanted: 720 lines");
  Check(CHdmiVrrOff::Wanted(true, true, true, true, 721, 60000), "Wanted: 721 lines");
  Check(!CHdmiVrrOff::Wanted(true, true, true, true, 0, 60000), "Wanted: 0 lines");
  Check(!CHdmiVrrOff::Wanted(true, true, true, true, 2160, 49999), "Wanted: 49999 mHz");
  Check(CHdmiVrrOff::Wanted(true, true, true, true, 2160, 50000), "Wanted: 50000 mHz");
  Check(CHdmiVrrOff::Wanted(true, true, true, true, 2160, 50001), "Wanted: 50001 mHz");
  Check(!CHdmiVrrOff::Wanted(true, true, true, true, 2160, 0), "Wanted: no refresh");
  Check(!CHdmiVrrOff::Wanted(true, true, true, true, -1, 60000), "Wanted: negative lines");
  Check(!CHdmiVrrOff::Wanted(true, true, true, true, 2160, -50000), "Wanted: negative refresh");
  // every one of the 64 combinations of the four booleans and the two limits: wanted only when all hold
  for (int m = 0; m < 64; ++m)
  {
    const bool a = m & 1, b = m & 2, c = m & 4, d = m & 8, e = m & 16, f = m & 32;
    const bool want = a && b && c && d && e && f;
    Check(CHdmiVrrOff::Wanted(a, b, c, d, e ? 720 : 719, f ? 50000 : 49999) == want, "Wanted: truth table " + std::to_string(m));
  }
  // RefreshMilliHz(): drm_mode_vrefresh() times 1000, the refresh rounded to the nearest Hz
  Check(CHdmiVrrOff::RefreshMilliHz(0, 4400, 2250) == 0, "refresh: no clock");
  Check(CHdmiVrrOff::RefreshMilliHz(594000, 0, 2250) == 0, "refresh: no htotal");
  Check(CHdmiVrrOff::RefreshMilliHz(594000, 4400, 0) == 0, "refresh: no vtotal");
  Check(CHdmiVrrOff::RefreshMilliHz(594000, 4400, 2250) == 60000, "refresh: 4K60 (594000 kHz)");
  Check(CHdmiVrrOff::RefreshMilliHz(593407, 4400, 2250) == 60000, "refresh: 59.94 Hz is 60000");
  Check(CHdmiVrrOff::RefreshMilliHz(297000, 5500, 2250) == 24000, "refresh: 24 Hz");
  Check(CHdmiVrrOff::RefreshMilliHz(296703, 5500, 2250) == 24000, "refresh: 23.976 Hz is 24000");
  // 49.95 Hz (clock 1000 * 49.95 * 1000 / 1000...: htotal 1000, vtotal 1000, 49950 kHz) rounds up to 50000
  Check(CHdmiVrrOff::RefreshMilliHz(49950, 1000, 1000) == 50000, "refresh: 49.95 Hz is 50000");
  // just under the half: 49.4999 Hz stays at 49000; exactly the half rounds up
  Check(CHdmiVrrOff::RefreshMilliHz(49499, 1000, 1000) == 49000, "refresh: below the half rounds down");
  Check(CHdmiVrrOff::RefreshMilliHz(49500, 1000, 1000) == 50000, "refresh: the half rounds up");
  Check(CHdmiVrrOff::RefreshMilliHz(4000000000u, 100, 100) == 400000000000LL, "refresh: no 32-bit overflow (4e9 kHz, 100x100: 4e8 Hz)");
  // Without(): a failed test of a request that asked for 1 is made again with 0; 0 or nothing: nothing
  Check(CHdmiVrrOff::Without(1u) == 0u, "Without: 1 becomes 0");
  Check(CHdmiVrrOff::Without(7u) == 0u, "Without: any non-zero becomes 0");
  Check(!CHdmiVrrOff::Without(0u), "Without: 0 stays as it is");
  Check(!CHdmiVrrOff::Without(std::nullopt), "Without: nothing stays nothing");
  // Stage(): the remaining operand of its first condition, and the kernel value that is neither 0 nor 1
  Check(!CHdmiVrrOff::Stage(false, false, 1u), "Stage: no mode set, not wanted: nothing");
  Check(!CHdmiVrrOff::Stage(true, false, 0u), "Stage: not wanted, kernel 0: nothing");
  Check(CHdmiVrrOff::Stage(true, false, 2u) == 0u, "Stage: not wanted, kernel 2: 0");
  Check(CHdmiVrrOff::Stage(true, true, 2u) == 1u, "Stage: wanted, kernel 2: 1");
  Check(!CHdmiVrrOff::Stage(true, false, std::nullopt), "Stage: not wanted, no property: nothing");
  Check(!CHdmiVrrOff::Stage(false, true, 1u, true), "Stage: native DV and no mode set");
  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
