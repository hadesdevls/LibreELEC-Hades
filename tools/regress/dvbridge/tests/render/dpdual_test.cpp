// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: native Dolby Vision on a dual-mode DisplayPort connector of
// nvidia-drm (issue #4, 1.0 RC4: it never entered), and every change of mode there with the head
// turned off and on.
//
// A model of what CDRMAtomic does with a modeset (DRMAtomic.cpp: SetVideoMode() and SetActive() ask
// for one, FlipPage() clears the request before its commit, FlipPageTransactional() after an
// accepted one, an accepted commit with ALLOW_MODESET carries MODE_ID of the selected mode and
// ACTIVE, CCommittedMode::Accepted() after it; a refused ordinary test falls back to the request
// before it), with the real CCommittedMode (DRMCommittedMode.h) and the real decisions of the tree
// (NvidiaDualMode.h, ModeChangeHold.h), driven through the sequence of a user's kodi.log of
// 2026-10-28 (start-8): the GUI at 60 Hz, the change of mode to 23.976 Hz for a profile 7 video
// alone (not held), its commit, frames of the GUI, the renderer of the video configured
// (SetColorimetry(): the Colorspace property and SetActive(true)), native output captured, and its
// frames for two seconds.
// Build: g++ -std=c++20 -Wall -Wextra -I<tree>/xbmc/windowing/gbm -I<tree>/xbmc/windowing/gbm/drm
//        dpdual_test.cpp   (-DOLD_HEADERS: the decisions of 1.0 RC4, before CCommittedMode)
#include "DRMCommittedMode.h"
#include "ModeChangeHold.h"
#include "NvidiaDualMode.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

using KODI::WINDOWING::GBM::CCommittedMode;
using KODI::WINDOWING::GBM::CModeChangeHold;

static int g_checks, g_failed;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
    ++g_failed;
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

struct Mode // the fields of drmModeModeInfo that a timing has
{
  uint32_t clock = 0;
  uint16_t hdisplay = 0, hsync_start = 0, hsync_end = 0, htotal = 0, hskew = 0;
  uint16_t vdisplay = 0, vsync_start = 0, vsync_end = 0, vtotal = 0, vscan = 0;
  uint32_t vrefresh = 0, flags = 0;
};
// 3840x2160 at 60 Hz (594000 kHz) and at 23.976 Hz (296700 kHz), as the user's TV offers them
static const Mode GUI60{594000, 3840, 4016, 4104, 4400, 0, 2160, 2168, 2178, 2250, 0, 60, 5};
static const Mode VIDEO24{296700, 3840, 5116, 5204, 5500, 0, 2160, 2168, 2178, 2250, 0, 24, 5};

// ------------------------------------------------------------------------------------------------
// The decisions, as the tree has them
static bool EntersRule(std::optional<bool> onCrtc, bool committedWithPending, bool committed)
{
#ifdef OLD_HEADERS // 1.0 RC4: CDRMAtomic::IsSelectedModeCommitted()
  (void)onCrtc;
  (void)committed;
  return committedWithPending;
#else
  (void)committedWithPending;
  return NVIDIA_DUAL_MODE::EntersInMode(onCrtc, committed);
#endif
}

static bool Restarts(std::string_view type, std::optional<uint64_t> tmds, bool setting, bool active,
                     bool otherTiming)
{
#if defined(OLD_HEADERS) || defined(ENTRY_ONLY) // 1.0 RC4 (and the first commit alone): in place
  (void)type, (void)tmds, (void)setting, (void)active, (void)otherTiming;
  return false;
#else
  return NVIDIA_DUAL_MODE::ModeChangeRestarts(type, tmds, setting, active, otherTiming);
#endif
}

// -DENTRY_ONLY: the tree at the first commit (the entry), without the changes of mode from scratch:
// their checks are left out
#ifdef ENTRY_ONLY
static constexpr bool kFromScratch = false;
#else
static constexpr bool kFromScratch = true;
#endif

template<typename T>
static void SetFromScratch(T& c, bool value)
{
  if constexpr (requires { c.modeFromScratch; })
    c.modeFromScratch = value;
}

// ------------------------------------------------------------------------------------------------
// The model of the output
struct Output
{
  // the connector
  std::string type = "DP";
  std::optional<uint64_t> tmds = 1;
  bool setting = true; // video/dualmoderestart
  // Kodi's side (CDRMAtomic)
  const Mode* selected = &GUI60;
  bool needModeset = true;
  bool active = true;
  CCommittedMode<Mode> committed;
  // the kernel's side
  Mode kernelMode{};
  bool kernelActive = false;
  bool kernelReadable = true;
  bool refuseModesets = false; // the driver refuses a modeset (a test that fails)
  int bringUps = 0;            // the head turned on from off (NVKMS brings the head and link up)
  int inPlaceChanges = 0;      // the mode changed on a running head
  // native output
  bool linkFromScratch() const { return NVIDIA_DUAL_MODE::LinkFromScratch(type, tmds); }
  bool captured = false;
  bool transportLive = false;
  bool entryRestartTaken = false;
  int waited = 0;

  // DrmAtomicCommit(): accepted, or, refused (the ordinary output), the request before it
  bool Commit(bool modeset, bool strict)
  {
    if (modeset && refuseModesets)
    {
      if (strict)
        return false;
      committed.Accepted(false, selected); // (fell back: no mode carried)
      return true;
    }
    if (modeset)
    {
      const bool wasOn = kernelActive;
      const bool sameTiming = CCommittedMode<Mode>::SameTiming(kernelMode, *selected);
      if (!wasOn && active)
        ++bringUps;
      else if (wasOn && active && !sameTiming)
        ++inPlaceChanges;
      kernelMode = *selected;
      kernelActive = active;
    }
    committed.Accepted(modeset, selected);
    return true;
  }
  void SetVideoMode() { needModeset = true; }
  void SetActive(bool a)
  {
    needModeset = true;
    active = a;
  }
  // CDRMAtomic::FlipPage(): the request of a modeset cleared before its commit
  void FlipPage()
  {
    const bool m = needModeset;
    needModeset = false;
    Commit(m, false);
  }
  bool FlipPageTransactional(bool modeset)
  {
    const bool ok = Commit(modeset || needModeset, true);
    if (ok)
      needModeset = false;
    return ok;
  }
  std::optional<bool> SelectedModeOnCrtc() const
  {
    if (!kernelReadable)
      return std::nullopt;
    return kernelActive && CCommittedMode<Mode>::SameTiming(kernelMode, *selected);
  }

  // CWinSystemGbm::SetFullScreen() for a change of mode that is not held
  void ChangeMode(const Mode* to)
  {
    const bool other = !CCommittedMode<Mode>::SameTiming(*selected, *to);
    const bool fromScratch = Restarts(type, tmds, setting, active, other);
    selected = to;
    SetVideoMode();
    if (fromScratch)
    {
      SetActive(false);
      FlipPage();
      SetActive(true);
      FlipPage();
    }
    else
      FlipPage();
  }
  // CWinSystemGbm::SetColorimetry() of the renderer of the video (WinSystemGbm.cpp)
  void SetColorimetry() { SetActive(true); }
  // a frame of native output, CWinSystemGbm::FlipDVBridgePage() (not a restoration)
  enum class Frame
  {
    IN_PLACE,
    WAITED,
    ENTERED_FROM_SCRATCH,
  };
  Frame DVFrame()
  {
    if (!transportLive && linkFromScratch() && active)
    {
      if (!EntersRule(SelectedModeOnCrtc(), committed.Is(selected, needModeset),
                      committed.Is(selected, false)))
      {
        ++waited;
        return Frame::WAITED;
      }
      if (!entryRestartTaken)
      {
        entryRestartTaken = true;
        SetActive(false);
        const bool off = FlipPageTransactional(true);
        SetActive(true);
        if (off && FlipPageTransactional(true))
        {
          transportLive = true;
          return Frame::ENTERED_FROM_SCRATCH;
        }
      }
    }
    if (FlipPageTransactional(!transportLive))
      transportLive = true;
    return Frame::IN_PLACE;
  }
};

// The sequence of the log of 2026-10-28 (start-8): Kodi starts in 60 Hz, a video changes the mode
// to 23.976 Hz (alone: on a dual-mode connector a native Dolby Vision video is not held), frames of
// the GUI, the renderer configured (its colorimetry), native output captured, 48 frames (2 s)
struct Run
{
  int framesToEnter = -1; // -1: it never entered within the 2 s
  bool modesetLeftPending = false;
  int bringUps = 0, inPlaceChanges = 0, waited = 0;
  bool kernelHasVideoMode = false;
};

static Run Sequence(Output o, bool colorimetry = true, bool refuseVideoMode = false,
                    bool kernelReadable = true)
{
  o.kernelReadable = kernelReadable;
  o.FlipPage(); // Kodi's first commit: the mode of the GUI
  for (int i = 0; i < 10; ++i)
    o.FlipPage();
  o.refuseModesets = refuseVideoMode;
  o.ChangeMode(&VIDEO24);
  o.refuseModesets = false;
  for (int i = 0; i < 12; ++i) // frames of the GUI until the renderer is configured
    o.FlipPage();
  if (colorimetry)
    o.SetColorimetry();
  o.captured = true;
  Run r;
  for (int i = 0; i < 48; ++i)
  {
    const auto f = o.DVFrame();
    if (o.transportLive && r.framesToEnter < 0)
      r.framesToEnter = i + 1;
    (void)f;
  }
  r.modesetLeftPending = o.needModeset;
  r.bringUps = o.bringUps;
  r.inPlaceChanges = o.inPlaceChanges;
  r.waited = o.waited;
  r.kernelHasVideoMode = o.kernelActive && CCommittedMode<Mode>::SameTiming(o.kernelMode, VIDEO24);
  return r;
}

int main()
{
  // 1. the sequence of the report, on the dual-mode connector DP-2 of the ZBook 17 G6
  {
    Output dp;
    const Run r = Sequence(dp);
    std::printf("     DP-2 (dual-mode, TMDS): entered at frame %d, waited %d, head brought up %d times, "
                "mode changed in place %d times\n",
                r.framesToEnter, r.waited, r.bringUps, r.inPlaceChanges);
    Check(r.kernelHasVideoMode, "the kernel has the mode of the video (as its log says at 46.47 s)");
    Check(r.framesToEnter == 1, "native output enters with the first frame of the video (1.0 RC4: never, in 2 s)");
    Check(r.waited == 0, "no frame of native output waits");
    Check(!r.modesetLeftPending, "the modeset of the colorimetry went out with the entry");
    if (kFromScratch)
    {
      Check(r.inPlaceChanges == 0, "no change of mode on a running head (the change to 23.976 Hz went out from scratch)");
      Check(r.bringUps >= 3, "the head is brought up from scratch: at start, with the change of mode, with the entry");
    }
  }
  // 2. ... without the colorimetry's modeset: entered in 1.0 RC4 as well
  {
    Output dp;
    const Run r = Sequence(dp, false);
    Check(r.framesToEnter == 1, "without a modeset pending, native output enters at once (as in 1.0 RC4)");
  }
  // 3. a change of mode that the driver refused: it waits, as before (fail closed)
  {
    Output dp;
    const Run r = Sequence(dp, true, true);
    Check(!r.kernelHasVideoMode, "the driver refused the change of mode: the kernel has the mode before it");
    Check(r.framesToEnter < 0 && r.waited == 48,
          "native output does not enter in a mode that is not on the output: it waits, and ends after 2 s");
    const Run u = Sequence(dp, true, true, false);
    Check(u.framesToEnter < 0 && u.waited == 48,
          "... also where the kernel cannot be asked: Kodi's commits know that the change fell back");
  }
  // 4. the kernel cannot be asked: Kodi's commits say it, whatever modeset is pending
  {
    Output dp;
    const Run r = Sequence(dp, true, false, false);
    Check(r.framesToEnter == 1, "the kernel cannot be asked: what Kodi's commits put there, with a modeset pending");
  }
  // 5. the kernel and Kodi's commits disagree: the kernel's word goes first
  {
    Output dp;
    dp.FlipPage();
    dp.ChangeMode(&VIDEO24);
    dp.kernelActive = false; // (the driver turned the head off: a suspend)
    dp.captured = true;
    Check(dp.DVFrame() == Output::Frame::WAITED, "the kernel has the head off, Kodi's commits say the mode: it waits");
    Output dp2;
    dp2.FlipPage();
    dp2.selected = &VIDEO24;
    dp2.kernelMode = VIDEO24; // (the mode set by a commit that Kodi did not count)
    dp2.kernelActive = true;
    dp2.captured = true;
    Check(dp2.DVFrame() == Output::Frame::ENTERED_FROM_SCRATCH,
          "the kernel has the mode, Kodi's commits did not see it put there: it enters");
  }
  // 6. an HDMI connector (the RTX 4080 SUPER and the LG C5): nothing changes
  {
    Output hdmi;
    hdmi.type = "HDMI-A";
    hdmi.tmds = std::nullopt;
    const Run r = Sequence(hdmi);
    Check(r.framesToEnter == 1 && r.waited == 0, "HDMI-A: the first frame of native output enters in place");
    Check(r.bringUps == 1 && r.inPlaceChanges == 1, "HDMI-A: the change of mode on the running head, as before");
    for (int s = 0; s < 2; ++s)
      for (int a = 0; a < 2; ++a)
        for (int t = 0; t < 2; ++t)
          Check(!Restarts("HDMI-A", std::nullopt, s, a, t) && !Restarts("HDMI-A", 1, s, a, t),
                "HDMI-A never turns the head off for a change of mode (setting " + std::to_string(s) +
                    ", active " + std::to_string(a) + ", another mode " + std::to_string(t) + ")");
  }
  // 7. the change of mode from scratch: only a dual-mode connector in TMDS, on, another mode, the setting on
  if (kFromScratch)
  {
    Check(Restarts("DP", 1, true, true, true), "DP in TMDS, on, another mode: from scratch");
    Check(!Restarts("DP", 1, false, true, true), "... not with video/dualmoderestart off");
    Check(!Restarts("DP", 1, true, false, true), "... not with the output meant to be off");
    Check(!Restarts("DP", 1, true, true, false), "... not without another mode");
    Check(!Restarts("DP", 0, true, true, true), "... not a DisplayPort display (NV_DUAL_MODE_TMDS 0)");
    Check(!Restarts("DP", std::nullopt, true, true, true), "... not without the property (another driver)");
    Check(!Restarts("eDP", 1, true, true, true), "... not the panel of the laptop");
    Output off;
    off.setting = false;
    const Run r = Sequence(off);
    Check(r.inPlaceChanges == 1, "video/dualmoderestart off: the change of mode in place, as in 1.0 RC4");
    Check(r.framesToEnter == 1, "... and native output still enters (the fix of the entry does not depend on it)");
  }
  // 8. the hold of a change of mode for an HDR video: not where the change goes out from scratch
  {
    CModeChangeHold::Change c;
    c.atomic = c.active = c.sameWindow = c.timingChanges = true;
    using V = CModeChangeHold::Video;
    Check(CModeChangeHold::StartHolds(V::HDR, c), "HDMI: an HDR video's change of mode is held for its output");
    SetFromScratch(c, true);
    if (kFromScratch)
      Check(!CModeChangeHold::StartHolds(V::HDR, c),
            "dual-mode: an HDR video's change of mode goes out alone first, from scratch, and its HDR after it");
    c.linkFromScratch = true;
    Check(!CModeChangeHold::StartHolds(V::NATIVE_DV, c), "dual-mode: native Dolby Vision is not held (as in 1.0 RC4)");
    Check(!CModeChangeHold::StartHolds(V::ORDINARY, c), "an SDR video is never held");
    CModeChangeHold::Change h;
    h.atomic = h.active = h.sameWindow = h.timingChanges = true;
    Check(CModeChangeHold::StartHolds(V::NATIVE_DV, h) && CModeChangeHold::StartHolds(V::HDR, h),
          "HDMI: native Dolby Vision and HDR held as before");
  }
  std::printf("%s: %d checks, %d failed\n", g_failed ? "DPDUAL-FAILED" : "DPDUAL-OK", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
