// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: native Dolby Vision on a connector whose link is brought up from scratch
// (nvidia-drm on a dual-mode DisplayPort connector, issue #4): NVIDIA_DUAL_MODE::LinkFromScratch(),
// CModeChangeHold::StartHolds() and CDVModeMove::Moves() with it, and CCommittedMode (whether the
// selected mode is the one on the output).
// Build: g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I <tree>/xbmc/windowing/gbm
//        -I <tree>/xbmc/windowing/gbm/drm dualmode_test.cpp
// With the headers before (no LinkFromScratch(), no Change::linkFromScratch, no DRMCommittedMode.h)
// it builds as well: what they lack is not set or answers no, and the checks of it fail.
// "table:" lines: every combination of the decisions without the new field, to compare between the
// two builds (an HDMI connector must not change at all).
#include "DVModeMove.h"
#include "ModeChangeHold.h"
#include "NvidiaDualMode.h"
#if __has_include("DRMCommittedMode.h")
#include "DRMCommittedMode.h"
#define HAVE_COMMITTED 1
#endif

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

using KODI::WINDOWING::GBM::CDVModeMove;
using KODI::WINDOWING::GBM::CModeChangeHold;

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

static bool FromScratch(std::string_view type, std::optional<uint64_t> tmds)
{
#ifdef OLD_HEADERS // (built with -DOLD_HEADERS against the headers before: they have no such call)
  (void)type;
  (void)tmds;
  return false;
#else
  return NVIDIA_DUAL_MODE::LinkFromScratch(type, tmds);
#endif
}

template<typename T>
static void SetLink(T& t, bool value)
{
  if constexpr (requires { t.linkFromScratch; })
    t.linkFromScratch = value;
}

struct Mode // the fields of drmModeModeInfo that a timing has
{
  uint32_t clock = 0;
  uint16_t hdisplay = 0, hsync_start = 0, hsync_end = 0, htotal = 0, hskew = 0;
  uint16_t vdisplay = 0, vsync_start = 0, vsync_end = 0, vtotal = 0, vscan = 0;
  uint32_t vrefresh = 0, flags = 0;
};

int main()
{
  using Video = CModeChangeHold::Video;
  // ---- the connectors
  Check("a dual-mode DisplayPort connector driven as HDMI (NV_DUAL_MODE_TMDS 1) brings its link up from scratch",
        FromScratch("DP", 1));
  Check("... not one driven as DisplayPort (0), nor one without the property",
        !FromScratch("DP", 0) && !FromScratch("DP", std::nullopt) && !FromScratch("DP", 2));
  Check("an HDMI connector never does (the RTX 4080 SUPER with the LG C5)",
        !FromScratch("HDMI-A", 1) && !FromScratch("HDMI-A", 0) && !FromScratch("HDMI-A", std::nullopt));
  Check("... nor any other type", !FromScratch("eDP", 1) && !FromScratch("DVI-D", 1) && !FromScratch("", 1));
  Check("what drives the backend is as before", NVIDIA_DUAL_MODE::DrivenAsHdmi("HDMI-A", std::nullopt) &&
                                                     NVIDIA_DUAL_MODE::DrivenAsHdmi("DP", 1) &&
                                                     !NVIDIA_DUAL_MODE::DrivenAsHdmi("DP", 0));

  // ---- the change of mode for a video that opens
  CModeChangeHold::Change c;
  c.atomic = true;
  c.active = true;
  c.sameWindow = true;
  c.timingChanges = true;
  Check("on an HDMI connector the mode of a native Dolby Vision video is held for its first frame",
        CModeChangeHold::StartHolds(Video::NATIVE_DV, c));
  SetLink(c, true);
  Check("where the link is brought up from scratch it is not: it goes out alone first",
        !CModeChangeHold::StartHolds(Video::NATIVE_DV, c));
  Check("... the mode of an HDR video still is (another rule decides it)", CModeChangeHold::StartHolds(Video::HDR, c));
  Check("... an SDR one never is", !CModeChangeHold::StartHolds(Video::ORDINARY, c));
  {
    CModeChangeHold::Change r = c;
    r.captured = true;
    Check("... and the end of native output still goes out with the next mode (K57)", CModeChangeHold::DVRestoreJoins(r));
  }

  // ---- native output that goes on into the mode of the next video
  CDVModeMove::State s;
  s.captured = true;
  s.transportLive = true;
  s.joins = true;
  s.transportMode = true;
  s.rendererNative = true;
  Check("on an HDMI connector native output moves into the next mode in one commit", CDVModeMove::Moves(s));
  SetLink(s, true);
  Check("where the link is brought up from scratch it does not: it ends, the mode goes out, it enters again",
        !CDVModeMove::Moves(s));
  s.rendererNative = false;
  s.endWaits = true;
  s.nativeOpens = true;
  Check("... nor for a video that opens after the end of the one before", !CDVModeMove::Moves(s));

  // ---- the decisions without the new field: every combination, printed for the other build
  unsigned int held = 0, joined = 0, moved = 0;
  for (unsigned int bits = 0; bits < (1u << 8); ++bits)
  {
    CModeChangeHold::Change t;
    t.atomic = bits & 1;
    t.active = bits & 2;
    t.sameWindow = bits & 4;
    t.mode3D = bits & 8;
    t.stereoBusy = bits & 16;
    t.captured = bits & 32;
    t.timingChanges = bits & 64;
    for (int v = 0; v < 4; ++v)
    {
      const bool h = CModeChangeHold::StartHolds(static_cast<Video>(v), t);
      held += h;
      std::printf("table: hold %u %d %d\n", bits & 127, v, h);
    }
    const bool j = CModeChangeHold::DVRestoreJoins(t);
    const bool st = CModeChangeHold::StopHolds(bits & 128, true, t);
    joined += j;
    std::printf("table: joins %u %d stop %d\n", bits, j, st);
  }
  for (unsigned int bits = 0; bits < (1u << 8); ++bits)
  {
    CDVModeMove::State t;
    t.captured = bits & 1;
    t.transportLive = bits & 2;
    t.disabled = bits & 4;
    t.joins = bits & 8;
    t.transportMode = bits & 16;
    t.rendererNative = bits & 32;
    t.endWaits = bits & 64;
    t.nativeOpens = bits & 128;
    const bool m = CDVModeMove::Moves(t);
    moved += m;
    std::printf("table: moves %u %d\n", bits, m);
  }
  std::printf("table: %u holds, %u joins, %u moves\n", held, joined, moved);

  // ---- the mode on the output
#ifdef HAVE_COMMITTED
  using Committed = KODI::WINDOWING::GBM::CCommittedMode<Mode>;
  Mode m24{297000, 3840, 5116, 5204, 5500, 0, 2160, 2168, 2178, 2250, 0, 24, 5};
  Mode m60 = m24;
  m60.clock = 594000;
  m60.hsync_start = 4016;
  m60.hsync_end = 4104;
  m60.htotal = 4400;
  m60.vrefresh = 60;
  Mode m24nv = m24;
  m24nv.flags = 10; // another sync polarity: another timing
  Committed k;
  Check("nothing committed yet: no mode is the one on the output", !k.Is(&m24, false) && !k.Is(nullptr, false));
  k.Accepted(true, &m60);
  Check("a modeset that the driver took puts its mode there", k.Is(&m60, false));
  Check("... a modeset pending of it is not there yet", !k.Is(&m60, true));
  Check("... and another selected mode is not the one there", !k.Is(&m24, false));
  k.Accepted(false, &m24); // the test of the change to 24 Hz refused: the request before it went out
  Check("a change of mode that fell back to the request before it leaves the mode as it was",
        k.Is(&m60, false) && !k.Is(&m24, false));
  k.Accepted(true, &m24);
  Check("the change accepted puts the new mode there", k.Is(&m24, false) && !k.Is(&m60, false));
  Check("a timing that differs in its flags only is another one", !k.Is(&m24nv, false));
  Check("the refresh rate number alone does not tell timings apart (as OtherTiming())",
        [&] { Mode x = m24; x.vrefresh = 23; return k.Is(&x, false); }());
  k.Accepted(true, nullptr);
  Check("an accepted commit without a selected mode changes nothing", k.Is(&m24, false));
#else
  Check("the mode on the output is known (DRMCommittedMode.h)", false);
#endif
#ifndef OLD_HEADERS
  // whether native output enters now on such a connector: the kernel's word first (the CRTC has the selected
  // mode), what Kodi's commits put there only where the kernel could not be read (NVIDIA_DUAL_MODE::EntersInMode())
  Check("enters: the kernel has the mode, whatever the commits say", NVIDIA_DUAL_MODE::EntersInMode(true, false));
  Check("enters: the kernel has another mode (a change refused), whatever the commits say",
        !NVIDIA_DUAL_MODE::EntersInMode(false, true));
  Check("enters: the kernel not read, the commits put the mode there", NVIDIA_DUAL_MODE::EntersInMode(std::nullopt, true));
  Check("enters: the kernel not read, a change pending", !NVIDIA_DUAL_MODE::EntersInMode(std::nullopt, false));
#endif
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
