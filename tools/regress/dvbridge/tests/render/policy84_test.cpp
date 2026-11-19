// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (profile 8.4): the pure rules of DVPlaybackPolicy.h for a base layer
// in HLG, on the RPUs read with FFmpeg's parser (rpuinfo): Dolby's profile 8.4 sample and dovi_tool's static
// profile 8.4 (iPhone 13) declare PQ at 12 bits; dovi_tool's profile 4 declares a gamma signal at 14 bits.
// Build: g++ -std=c++20 -I<kodi tree>/xbmc/cores/VideoPlayer/DVDCodecs/Video -I<kodi tree>/xbmc policy84_test.cpp
#include "DVPlaybackPolicy.h"

#include <cstdio>

using namespace DVBRIDGE;

static int g_fail;
static void check(bool ok, const char* what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  g_fail += !ok;
}

int main()
{
  // the transfer of the base layer from the record
  check(BaseLayerTransferOf(8, 1) == BASE_LAYER_PQ, "8.1: PQ");
  check(BaseLayerTransferOf(7, 6) == BASE_LAYER_PQ, "7.6: PQ");
  check(BaseLayerTransferOf(5, 0) == BASE_LAYER_PQ, "5: PQ (no picture of its own, decided elsewhere)");
  check(BaseLayerTransferOf(8, 2) == BASE_LAYER_SDR, "8.2: SDR (as BaseLayerIsSdr() said)");
  check(BaseLayerTransferOf(9, 2) == BASE_LAYER_SDR, "9.2: SDR");
  check(BaseLayerTransferOf(10, 2) == BASE_LAYER_SDR, "10.2: SDR");
  check(BaseLayerTransferOf(8, 4) == BASE_LAYER_HLG, "8.4: HLG");
  check(BaseLayerTransferOf(10, 4) == BASE_LAYER_HLG, "10.4: HLG");
  check(BaseLayerTransferOf(0, 4) == BASE_LAYER_PQ, "profile not known: as before");
  check(BaseLayerTransferOf(0, 2) == BASE_LAYER_PQ, "profile not known (2): as before");
  // what the RPU declares (fields as FFmpeg's parser gave them)
  check(RpuDeclaresPq(65535, 0, 0, 0, 12, 12), "Dolby's 8.4 sample: PQ at 12 bits declared");
  check(RpuDeclaresPq(65535, 0, 0, 0, 12, 12), "dovi_tool's static 8.4 (iPhone 13): PQ at 12 bits declared");
  check(!RpuDeclaresPq(39322, 15867, 228, 1383604, 14, 14), "dovi_tool's profile 4: a gamma signal, not PQ");
  check(!RpuDeclaresPq(65535, 1, 0, 0, 12, 12), "PQ with a parameter: not as CCM 001 sends one");
  check(!RpuDeclaresPq(65535, 0, 0, 1, 12, 12), "PQ with the 32-bit parameter: not as CCM 001 sends one");
  check(!RpuDeclaresPq(65535, 0, 0, 0, 10, 12), "PQ at 10 bits over a composer of 12: not (6.2.2)");
  check(!RpuDeclaresPq(0, 0, 0, 0, 12, 12), "signal_eotf 0 (an RPU that leaves it at zero): not");
  // composable
  check(BaseLayerComposable(BASE_LAYER_HLG, true), "HLG whose RPU declares PQ: composed");
  check(!BaseLayerComposable(BASE_LAYER_HLG, false), "HLG whose RPU declares something else: refused");
  check(BaseLayerComposable(BASE_LAYER_PQ, false), "PQ: as before, whatever the RPU says");
  check(BaseLayerComposable(BASE_LAYER_SDR, false), "SDR: as before (clause 5.5 decides in libplacebo)");
  static_assert(BaseLayerTransferOf(8, 4) == BASE_LAYER_HLG && RpuDeclaresPq(65535, 0, 0, 0, 12, 12));
  std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail != 0;
}
