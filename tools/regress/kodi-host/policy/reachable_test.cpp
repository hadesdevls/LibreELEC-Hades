// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: DVBRIDGE::RefreshRateKeepsFromTransport(), the rule by which
// CWinSystemGbm::NativeDVOutputReachable() tells add-ons whether native Dolby Vision can be had with
// the settings as they are, against a model of an output: its list of modes and the mode that is set.
//
// The model: native output needs a transport mode (3840x2160 progressive at a rate of the transport).
// With the refresh rate adjusted to the video, the video gets a mode at its own rate: reachable where
// the output has any transport mode. With the rate kept where it is set, the video gets that rate:
// reachable where the mode set is a transport mode, or the output has one at that rate; an interlaced
// or 3D mode set keeps nothing (as RefreshRateKeepsOrdinary()). Before a video, the window system asks
// this only where the output has a transport mode at all (SupportsNativeDVOutput(true)).
//
// Build: g++ -std=c++20 -Wall -Wextra -I<Video dir> reachable_test.cpp [-DOLD_RULE]
// OLD_RULE: the rule of before the modes of the output were taken per refresh rate (the refresh rate
// kept is none of the transport's), which this test must find wrong.
#include "DVPlaybackPolicy.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace DVBRIDGE;

static bool Rule(bool fixed, const ModeTraits& set, const std::vector<ModeTraits>& modes)
{
  const bool transport = IsTransportMode(set.width, set.height, set.refresh, set.interlacedOr3D);
#ifdef OLD_RULE
  (void)transport;
  return RefreshRateKeepsOrdinary(fixed, set.refresh, set.interlacedOr3D) &&
         TransportModesOf(modes, set.refresh) != TransportModes::NONE;
#else
  return RefreshRateKeepsFromTransport(fixed, set.refresh, transport, set.interlacedOr3D,
                                       TransportModesOf(modes, set.refresh));
#endif
}

// the model, written apart from DVPlaybackPolicy.h
static const double TRANSPORT[] = {24000.0 / 1001, 24.0, 25.0, 30000.0 / 1001, 30.0, 50.0, 60000.0 / 1001, 60.0};
static bool Carries(const ModeTraits& m)
{
  if (m.width != 3840 || m.height != 2160 || m.interlacedOr3D)
    return false;
  for (const double r : TRANSPORT)
    if (std::abs(m.refresh - r) < 0.012)
      return true;
  return false;
}
static bool Model(bool fixed, const ModeTraits& set, const std::vector<ModeTraits>& modes)
{
  if (!fixed || set.interlacedOr3D || Carries(set))
    return false;
  for (const auto& m : modes)
    if (Carries(m) && std::abs(m.refresh - set.refresh) < 0.01)
      return false; // the size alone changes
  return true;
}

int main()
{
  const double rates[] = {23.976, 24.0, 25.0, 29.97, 30.0, 47.952, 48.0, 50.0, 59.94, 60.0, 100.0, 119.88, 120.0, 144.0};
  // the 3840x2160 rates an output may offer: 24 to 30 Hz only (an HDMI port of 300 MHz), all of the
  // transport's, 50 and 60 Hz only, 120 Hz only (none of the transport's), and each rate alone
  std::vector<std::vector<double>> lists = {{23.976, 24.0, 25.0, 29.97, 30.0},
                                            {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0},
                                            {50.0, 59.94, 60.0},
                                            {120.0, 119.88},
                                            {}};
  for (const double r : rates)
    lists.push_back({r});
  int cases = 0, wrong = 0, blocked = 0, otherRates = 0;
  for (const auto& list : lists)
  {
    std::vector<ModeTraits> modes;
    for (const double r : list)
      modes.push_back({3840, 2160, r, false});
    for (const double r : rates)
    {
      modes.push_back({1920, 1080, r, false});
      modes.push_back({1920, 1080, r, true});
      modes.push_back({4096, 2160, r, false});
      modes.push_back({3840, 2160, r, true});
    }
    bool hasTransport = false;
    for (const auto& m : modes)
      hasTransport |= Carries(m);
    if (!hasTransport)
      continue; // (not asked: SupportsNativeDVOutput(true) says no before)
    for (const bool fixed : {false, true})
      for (const auto& set : modes)
      {
        ++cases;
        const bool want = Model(fixed, set, modes), got = Rule(fixed, set, modes);
        blocked += want;
        // (kept at a rate of the transport: the output has its transport modes at other rates)
        if (want && Carries(ModeTraits{3840, 2160, set.refresh, false}))
          ++otherRates;
        if (want != got && ++wrong <= 6)
          std::printf("WRONG: fixed=%d set %dx%d@%.3f%s, %zu 2160p rates: model %d, rule %d\n", fixed,
                      set.width, set.height, set.refresh, set.interlacedOr3D ? " (i/3D)" : "",
                      list.size(), want, got);
      }
  }
  std::printf("REACHABLE-TEST: %d cases (%d kept from native output, %d of them at a rate of the "
              "transport that the output has no transport mode at), %d wrong\n", cases, blocked,
              otherRates, wrong);
  return wrong ? 1 : 0;
}

