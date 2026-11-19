/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of "Video at its own rate with VRR (no change of mode)"
 * (windowing/gbm/VrrVideo.h): the choice (Decide()), the target of Media VRR, the times of the flips
 * (CSchedule), the measure of how exact they came out (CIntervals), which flips are paced (Paced(): Kodi's GLES
 * renderer included, which 1.1 left at a fixed refresh) and whether VRR came on at all (CStartCheck: from the
 * vblanks of the first second, or from what nvidia-drm says, NV_VRR_ACTIVE; where it did not, the mode changes to
 * the video's rate), and the video at its own rate on its own where the display has no mode at its rate
 * (Automatic(), AutoForDisplay(), DisplayHasModeFor(): never on a display with a mode at the video's rate at
 * any resolution), and the signal measured by the out-fences of the flips where the driver says VRR is
 * active (CStartCheck::FenceInterval()); the three values of the setting (Mode: off by default, where the display
 * has no mode at the video's rate, always; VrrInModeSets(), TakesBeforeKodi()) and the boolean of the 1.1 test
 * builds taken over when the settings load (settings/SettingTakeOver.h: on is always); (fixV) Dolby Vision that does
 * not go out natively taken as HDR10+ is, native Dolby Vision never (also with "Always": its change of mode to its
 * rate, AskedAfterKodi(), IsTransportMode() of DVPlaybackPolicy.h), a video kept in its mode where "Always" was refused
 * only for the mode set of the menus (RefusedForModeSetOnly()), the rate of the flips that the player information
 * says (CFlipRate, ShownRefresh(), VrrModeText() of utils/PlaybackMode.h) and its VRR row (RowValue()). Built like vrr_test.cpp
 * beside it:
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -o t vrr_video_test.cpp && ./t
 * With -DOLD_RULE the rule of 1.1 decides which flips are paced: that run must fail (run-kodi-host.sh).
 */
#include "cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h"
#include "settings/SettingTakeOver.h"
#include "utils/PlaybackMode.h"
#include "windowing/gbm/VrrVideo.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::WINDOWING;
using namespace KODI::WINDOWING::GBM::VRR_VIDEO;
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

// The LG C5 of the user (HDMI Forum VRR 40 to 120 Hz) on an RTX 4080 SUPER with the patches of Hades,
// the menus at 3840x2160 and 120 Hz
static Query C5On4080(float fps)
{
  Query q;
  q.setting = true;
  q.fps = fps;
  q.progressive = true;
  q.video = WholeMultipleVideo::SDR;
  q.vrrCapable = true;
  q.crtcVrrEnabled = true;
  q.nvidia = true;
  q.flipTime = true;
  q.vrrMin = 40;
  q.vrrMax = 120;
  q.guiRefresh = 120.0f;
  q.guiLines = 2160;
  return q;
}

static bool Near(double a, double b, double tolerance = 1e-6)
{
  return std::fabs(a - b) <= tolerance;
}

// Whether a flip is paced. -DOLD_RULE is the rule of the first builds of 1.1 (WinSystemGbm.cpp): the video layer
// instead of a video on its way to the screen; the run built with it must fail the checks of Kodi's GLES renderer.
static bool PacedAs(bool wanted, bool videoLayer, bool crtc, bool videoRendering, bool fullScreenVideo, bool nativeDV)
{
#ifdef OLD_RULE
  (void)videoRendering;
  return wanted && videoLayer && crtc && fullScreenVideo && !nativeDV;
#else
  (void)videoLayer;
  return Paced(wanted, crtc, fullScreenVideo, videoRendering, nativeDV);
#endif
}

int main()
{
  const float f23976 = 24000.0f / 1001.0f;
  const float f2997 = 30000.0f / 1001.0f;
  const float f5994 = 60000.0f / 1001.0f;

  // the rates of the C5 at 120 Hz: the smallest whole multiple at or above 42 Hz (40 Hz and 5 %)
  struct
  {
    float fps;
    int k;
  } rates[] = {{f23976, 2}, {24.0f, 2}, {25.0f, 2}, {f2997, 2}, {30.0f, 2}, {50.0f, 1},
               {f5994, 1}, {60.0f, 1}, {100.0f, 1}, {120.0f, 1}};
  for (const auto& r : rates)
  {
    const Verdict v = Decide(C5On4080(r.fps));
    Check(v.use && v.multiple == r.k && Near(v.rate, r.k * static_cast<double>(r.fps), 1e-4) &&
              v.pacing == Pacing::FLIP_TIME && !v.why,
          "C5 at 120 Hz, " + std::to_string(r.fps) + " fps: k " + std::to_string(v.multiple) + ", " +
              std::to_string(v.rate) + " Hz");
  }
  // 47.952 Hz exactly for 23.976 fps
  Check(Near(Decide(C5On4080(f23976)).rate, 48000.0 / 1001.0, 1e-3), "23.976 fps: 47.952 Hz");
  // the 5 % margin: 40 fps on a display whose VRR starts at 40 Hz goes twice, at 80 Hz
  Check(Decide(C5On4080(40.0f)).multiple == 2, "40 fps at a VRR minimum of 40 Hz: twice, at 80 Hz");
  Check(Decide(C5On4080(42.0f)).multiple == 1, "42 fps: once (42 Hz is the minimum with its margin)");
  // a display whose VRR starts at 48 Hz (#4): 23.976 fps three times, at 71.928 Hz
  {
    Query q = C5On4080(f23976);
    q.vrrMin = 48;
    const Verdict v = Decide(q);
    Check(v.use && v.multiple == 3 && Near(v.rate, 72000.0 / 1001.0, 1e-3), "VRR from 48 Hz: 71.928 Hz");
    q.guiRefresh = 60.0f;
    q.guiLines = 2160;
    Check(!Decide(q).use && Decide(q).why, "VRR from 48 Hz and menus at 60 Hz: 71.928 is over 60, no");
  }
  // menus at 60 Hz with the C5: 23.976 at 47.952, 59.94 at 59.94, 100 fps no
  {
    Query q = C5On4080(f23976);
    q.guiRefresh = 60.0f;
    Check(Decide(q).use && Decide(q).multiple == 2, "menus at 60 Hz: 23.976 at 47.952 Hz");
    q.fps = 100.0f;
    Check(!Decide(q).use, "menus at 60 Hz: 100 fps is over the menus' rate, no");
    q.fps = 60.0f;
    Check(Decide(q).use, "menus at 60 Hz: 60 fps once");
    q.fps = 60.5f;
    Check(!Decide(q).use, "menus at 60 Hz: 60.5 fps is over the menus' rate, no");
  }
  // NVKMS: a mode of 50 Hz has VRR only on a display whose VRR starts at 45 Hz or lower (110 %)
  {
    Query q = C5On4080(50.0f);
    q.vrrMin = 46;
    q.guiRefresh = 50.0f;
    Check(!Decide(q).use, "NVIDIA, menus at 50 Hz, VRR from 46 Hz: 50.6 Hz needed, no VRR in the mode");
    q.vrrMin = 45;
    Check(Decide(q).use, "NVIDIA, menus at 50 Hz, VRR from 45 Hz: 49.5 Hz needed, 50 fps once");
  }
  // each refusal says why, in its own words
  {
    Query q = C5On4080(f23976);
    q.video = WholeMultipleVideo::HLG;
    Check(std::string(Decide(q).why ? Decide(q).why : "").find("HLG") != std::string::npos,
          "HLG: the reason names it");
    q.video = WholeMultipleVideo::HDR10PLUS;
    Check(std::string(Decide(q).why ? Decide(q).why : "").find("HDR10+") != std::string::npos,
          "HDR10+: the reason names it");
  }
  // NVKMS gives the menus' mode VRR from 50 Hz, 720 lines and 110 % of the minimum
  {
    Query q = C5On4080(f23976);
    q.vrrMin = 48;
    q.guiRefresh = 50.0f;
    Check(!Decide(q).use, "NVIDIA, menus at 50 Hz, VRR from 48 Hz: under 110 %, no VRR in the mode");
    q.guiRefresh = 120.0f;
    q.guiLines = 719;
    Check(!Decide(q).use, "NVIDIA, 719 lines: no VRR in the mode");
    q.guiLines = 720;
    Check(Decide(q).use, "NVIDIA, 720 lines: VRR in the mode");
    q.guiRefresh = 49.9f;
    q.vrrMin = 40;
    Check(!Decide(q).use, "NVIDIA, menus at 49.9 Hz: no VRR in the mode");
  }
  // "HDMI variable refresh rate (VRR)" off: NVIDIA has no VRR in the mode; amdgpu has no such rule
  {
    Query q = C5On4080(f23976);
    q.hdmiVrrAllowed = false;
    Check(!Decide(q).use && Decide(q).why, "NVIDIA with HDMI VRR off: no");
    q.nvidia = false;
    q.flipTime = false;
    const Verdict v = Decide(q);
    Check(v.use && v.pacing == Pacing::TIMER, "amdgpu: the HDMI VRR setting does not apply; Kodi times it");
  }
  // the mode of the menus set with HDMI VRR off (NV_HDMI_VRR_OFF 1: it was set before "Video at its own rate
  // with VRR" was turned on, with "HDMI variable refresh rate (VRR)" off, the default): its refresh stays
  // fixed, so it is not used until the next change of mode; amdgpu has no such property
  {
    Query q = C5On4080(f23976);
    Check(Decide(q).use, "NVIDIA, the mode of the menus with HDMI VRR allowed: used");
    q.hdmiVrrOffInMode = true;
    const Verdict v = Decide(q);
    Check(!v.use && v.why && std::string(v.why).find("next change of mode") != std::string::npos,
          "NVIDIA, the mode of the menus set with HDMI VRR off: not used, until the next change of mode");
    q.nvidia = false;
    q.flipTime = false;
    Check(Decide(q).use, "amdgpu: no NV_HDMI_VRR_OFF, nothing to wait for");
  }
  // amdgpu, the iGPU at 3840x2160 and 60 Hz, FreeSync of the C5 at 40 to 120 Hz
  {
    Query q = C5On4080(f23976);
    q.nvidia = false;
    q.flipTime = false;
    q.guiRefresh = 60.0f;
    const Verdict v = Decide(q);
    Check(v.use && v.multiple == 2 && v.pacing == Pacing::TIMER, "amdgpu at 60 Hz: 47.952 Hz, timed by Kodi");
  }
  // Media VRR (Blackwell): the display engine keeps the rate where it is a whole number of Hz, or one
  // divided by 1.001
  {
    Query q = C5On4080(f23976);
    q.media = true;
    Verdict v = Decide(q);
    Check(v.use && v.pacing == Pacing::MEDIA && v.mediaTarget == (48u << 1 | 1u), "Media VRR: 47.952 is 48/1.001");
    q.fps = 25.0f;
    v = Decide(q);
    Check(v.pacing == Pacing::MEDIA && v.mediaTarget == 100u, "Media VRR: 50 Hz");
    q.fps = f5994;
    v = Decide(q);
    Check(v.pacing == Pacing::MEDIA && v.mediaTarget == (60u << 1 | 1u), "Media VRR: 59.94 Hz");
    q.fps = 23.7f;
    v = Decide(q);
    Check(v.use && v.pacing == Pacing::FLIP_TIME && v.mediaTarget == 0,
          "Media VRR: 47.4 Hz is no target: the flips carry their times");
  }
  Check(MediaTarget(48000.0 / 1001.0) == (48u << 1 | 1u), "MediaTarget: 47.952");
  Check(MediaTarget(48.0) == 96u, "MediaTarget: 48");
  Check(MediaTarget(24000.0 / 1001.0) == (24u << 1 | 1u), "MediaTarget: 23.976");
  Check(MediaTarget(1000.0) == 2000u, "MediaTarget: 1000 Hz");
  Check(MediaTarget(1024.0) == 0u, "MediaTarget: over 1023 Hz: none");
  Check(MediaTarget(47.5) == 0u, "MediaTarget: 47.5 Hz: none");
  Check(MediaTarget(0.0) == 0u && MediaTarget(std::nan("")) == 0u, "MediaTarget: 0 or NaN: none");

  // what never goes at its own rate, each with a reason
  {
    Query q = C5On4080(f23976);
    q.setting = false;
    Check(!Decide(q).use && !Decide(q).why, "setting off: no, and nothing to say");
  }
  struct
  {
    const char* what;
    void (*set)(Query&);
  } refusals[] = {
      {"native Dolby Vision", [](Query& q) { q.nativeDV = true; }},
      {"Dolby Vision", [](Query& q) { q.video = WholeMultipleVideo::DOLBY_VISION; }},
      {"HDR10+", [](Query& q) { q.video = WholeMultipleVideo::HDR10PLUS; }},
      {"HLG", [](Query& q) { q.video = WholeMultipleVideo::HLG; }},
      {"not told", [](Query& q) { q.video = WholeMultipleVideo::UNKNOWN; }},
      {"3D", [](Query& q) { q.is3D = true; }},
      {"not progressive", [](Query& q) { q.progressive = false; }},
      {"no frame rate", [](Query& q) { q.fps = 0.0f; }},
      {"vrr_capable 0", [](Query& q) { q.vrrCapable = false; }},
      {"no VRR_ENABLED", [](Query& q) { q.crtcVrrEnabled = false; }},
      {"no VRR range", [](Query& q) { q.vrrMin = 0; }},
      {"VRR range upside down", [](Query& q) { q.vrrMax = 30; }},
      {"menus interlaced", [](Query& q) { q.guiInterlaced = true; }},
      {"menus 3D", [](Query& q) { q.gui3D = true; }},
      {"menus without a rate", [](Query& q) { q.guiRefresh = 0.0f; }},
  };
  for (const auto& r : refusals)
  {
    Query q = C5On4080(f23976);
    r.set(q);
    const Verdict v = Decide(q);
    Check(!v.use && v.why && v.multiple == 0 && v.pacing == Pacing::NONE, std::string("refused: ") + r.what);
  }
  {
    Query q = C5On4080(f23976);
    q.video = WholeMultipleVideo::HDR10;
    Check(Decide(q).use, "HDR10 goes at its own rate");
  }

  // the times of the flips: no error that grows with n
  {
    CSchedule s;
    const double period = 1e9 * 1001.0 / 48000.0;
    // (the first slot 100 ms after the clock's 0, the "now" of the first two flips)
    s.Start(100000000, period);
    bool exact = true;
    for (uint64_t n : {0ULL, 1ULL, 2ULL, 47952ULL, 1000000ULL, 345600ULL})
    {
      const long double want = 1.0e8L + static_cast<long double>(n) * 1001.0e9L / 48000.0L;
      exact = exact && std::fabs(static_cast<long double>(s.Time(n)) - want) <= 1.0L;
    }
    Check(exact, "schedule: slot n within 1 ns of first + n * 1001/48000 s, to a million slots");
    uint64_t skipped = 0;
    Check(s.Take(0, 500000, skipped) == 0 && skipped == 0, "schedule: first slot");
    Check(s.Take(0, 500000, skipped) == 1 && skipped == 0, "schedule: second slot");
    // late: now is past slot 5 by 1 ms; slot 6 is 19.85 ms ahead: slots 2 to 5 are missed
    const int64_t now = s.Time(5) + 1000000;
    // (a slot closer to now than the margin is missed too: from a clock at 0, slot 0 at 100 ms is not)
    const uint64_t n = s.Take(now, 500000, skipped);
    Check(n == 6 && skipped == 4, "schedule: late by four slots: slot 6, four missed (" + std::to_string(n) +
                                      ", " + std::to_string(skipped) + ")");
    // just inside the margin: the slot is taken only if it is half a millisecond ahead
    const int64_t close = s.Time(7) - 400000;
    Check(s.Take(close, 500000, skipped) == 8 && skipped == 1, "schedule: slot 7 too close: slot 8");
    Check(s.Take(close, 500000, skipped) == 9 && skipped == 0, "schedule: then the next one");
  }

  // the measure of the intervals
  {
    CIntervals iv;
    for (int i = 0; i < 98; ++i)
      iv.Add(i % 2 ? 50000 : -50000, 0); // +-50 us
    iv.Add(1500000, 1); // one 1.5 ms late, with a refresh of the display's own
    iv.Add(-2500000, 0); // one 2.5 ms early
    Check(iv.Count() == 100, "intervals: count");
    Check(iv.Over1ms() == 2, "intervals: two over 1 ms");
    Check(iv.ExtraRefreshes() == 1, "intervals: one refresh of the display's own");
    Check(Near(iv.QuantileUs(0.5), -45.0, 0.1), "intervals: median in the bin of -50 us (" +
                                                    std::to_string(iv.QuantileUs(0.5)) + ")");
    Check(Near(iv.QuantileUs(0.01), -2495.0, 0.1), "intervals: p1 is the early one");
    Check(Near(iv.QuantileUs(0.99), 55.0, 0.1), "intervals: p99 in the bin of +50 us");
    Check(Near(iv.MaxUs(), 1500.0) && Near(iv.MinUs(), -2500.0), "intervals: min and max");
    CIntervals far;
    far.Add(-30000000, 0);
    far.Add(40000000, 0);
    Check(Near(far.QuantileUs(0.01), -19995.0, 0.1) && Near(far.QuantileUs(1.0), 29995.0, 0.1),
          "intervals: what is out of range is counted at the edges");
  }

  // ---- which flips are paced (Paced()): the chain from the renderer to the window system. 1.1 asked
  // for the video layer (CApplicationPlayer::IsRenderingVideoLayer() = the renderer's
  // VideoBypassesFramebuffer()), which Kodi's GLES renderer, the one of VAAPI on NVIDIA and AMD, says
  // false for whatever IsGuiLayer() says (LinuxRendererGLES.h): VRR was never turned on (test of
  // 2026-11-18). What the window system now asks for is a video on its way to the screen.
  {
    struct Renderer
    {
      const char* name;
      bool guiLayer; // IsGuiLayer()
      bool bypasses; // VideoBypassesFramebuffer()
    } renderers[] = {
        {"GLES, SDR (IsGuiLayer true)", true, false},
        {"GLES, HDR composited (IsGuiLayer false)", false, false},
        {"DRM PRIME plane (IsGuiLayer false)", false, true},
    };
    for (const auto& r : renderers)
    {
      // (what 1.1 gave PaceVrrVideo(): the video layer, which is VideoBypassesFramebuffer())
      const bool videoLayer = r.bypasses;
      (void)r.guiLayer;
      Check(PacedAs(true, videoLayer, true, true, true, false), std::string("paced with ") + r.name);
    }
    Check(!PacedAs(true, true, true, false, true, false), "no video on its way to the screen: not paced");
    Check(!PacedAs(true, true, true, true, false, false), "not in the full-screen video: not paced");
    Check(!PacedAs(false, true, true, true, true, false), "not wanted: not paced");
    Check(!PacedAs(true, true, false, true, true, false), "no CRTC: not paced");
    Check(!PacedAs(true, true, true, true, true, true), "native Dolby Vision: not paced");
  }

  // ---- whether VRR came on (CStartCheck): the vblanks of the first second
  {
    const double vrr = 48000.0 / 1001.0; // 23.976 fps twice
    const int64_t s = 1000000000;
    // the flips of a second at rate, on an output whose refreshes follow them (VRR on): one vblank a
    // flip, at the time asked within +-50 us
    const auto run = [](CStartCheck& c, double rate, double modeRefresh, bool follows, double seconds,
                        double jitterUs = 50.0)
    {
      const double asked = 1e9 / rate;
      const double mode = 1e9 / modeRefresh;
      double t = 0.0;
      uint64_t lastSeq = 0;
      const int flips = static_cast<int>(seconds * rate);
      double lastShown = 0.0;
      for (int i = 0; i < flips; ++i)
      {
        t = i * asked;
        uint64_t seq;
        double shown;
        if (follows)
        {
          seq = static_cast<uint64_t>(i);
          shown = t + ((i % 2) ? jitterUs : -jitterUs) * 1000.0;
        }
        else
        {
          // a fixed refresh: the first vblank of the mode at or after the time of the flip
          seq = static_cast<uint64_t>(std::ceil(t / mode - 1e-9));
          shown = seq * mode;
        }
        if (i > 0)
          c.Interval(seq - lastSeq, static_cast<int64_t>(std::llround((shown - lastShown) - asked)));
        lastShown = shown;
        lastSeq = seq;
      }
      return static_cast<int64_t>(flips * asked);
    };
    {
      CStartCheck c;
      c.Start(0, vrr, 120.0);
      c.KernelSays(1);
      const int64_t end = run(c, vrr, 120.0, true, 1.1);
      Check(c.Check(end) == Applied::FOLLOWS,
            "47.952 Hz in a mode of 120 Hz, one vblank a flip: VRR is on (" +
                std::to_string(c.ExtraPerInterval()) + " own refreshes a flip)");
      Check(c.Check(s / 2) == Applied::CHECKING, "... not judged before its first second");
    }
    {
      // the LG C5 with its VRR off in the menu does not change what the source does; a mode set
      // without VRR in its timings does: the refresh stays at 120 Hz
      CStartCheck c;
      c.Start(0, vrr, 120.0);
      c.KernelSays(1);
      const int64_t end = run(c, vrr, 120.0, false, 1.1);
      Check(c.Check(end) == Applied::FIXED,
            "47.952 Hz asked, the refresh stays at 120 Hz (VRR_ENABLED read back 1): not on (" +
                std::to_string(c.ExtraPerInterval()) + " own refreshes a flip, " +
                std::to_string(100.0 * c.OffShare()) + " % off)");
      Check(std::fabs(c.FixedExtra() - 1.5) < 0.01, "a fixed 120 Hz makes 1.5 own refreshes a flip at 47.952");
    }
    {
      // 60 fps in a mode of 120 Hz: the intervals are exact either way; the vblanks tell
      CStartCheck c;
      c.Start(0, 60.0, 120.0);
      c.KernelSays(1);
      const int64_t end = run(c, 60.0, 120.0, false, 1.1);
      Check(c.Check(end) == Applied::FIXED && c.OffShare() < 0.01,
            "60 Hz asked in a mode of 120 Hz, a fixed refresh: exact intervals, one own refresh a "
            "flip: not on");
      CStartCheck d;
      d.Start(0, 60.0, 120.0);
      d.KernelSays(1);
      Check(d.Check(run(d, 60.0, 120.0, true, 1.1)) == Applied::FOLLOWS, "... and on where they follow");
    }
    {
      // 50 fps in a mode of 60 Hz: 0.2 own refreshes a flip with a fixed refresh
      CStartCheck c;
      c.Start(0, 50.0, 60.0);
      c.KernelSays(1);
      Check(c.Check(run(c, 50.0, 60.0, false, 1.1)) == Applied::FIXED, "50 Hz in a mode of 60 Hz, fixed: not on");
    }
    {
      // refreshes that follow the flips but not at the times asked for (more than a quarter off by
      // over 1 ms): not taken as VRR either
      CStartCheck c;
      c.Start(0, vrr, 120.0);
      c.KernelSays(1);
      Check(c.Check(run(c, vrr, 120.0, true, 1.1, 1500.0)) == Applied::FIXED,
            "one vblank a flip but each 1.5 ms off: not taken as on");
    }
    {
      CStartCheck c;
      c.Start(0, vrr, 120.0);
      c.KernelSays(0);
      Check(c.Check(0) == Applied::NOT_KEPT, "VRR_ENABLED read back 0: not on, at once");
      CStartCheck d;
      d.Start(0, vrr, 120.0);
      d.KernelSays(std::nullopt);
      Check(d.Check(run(d, vrr, 120.0, true, 1.1)) == Applied::FOLLOWS,
            "VRR_ENABLED not read: the vblanks decide");
    }
    {
      CStartCheck c;
      c.Start(0, 60.0, 60.0);
      Check(c.Check(0) == Applied::SAME_RATE, "the rate of the mode: nothing to tell apart");
      CStartCheck d;
      d.Start(0, 60000.0 / 1001.0, 60.0);
      Check(d.Check(0) == Applied::CHECKING, "59.94 in a mode of 60 Hz is measured");
    }
    {
      CStartCheck c;
      c.Start(0, vrr, 120.0);
      Check(c.Check(4 * s) == Applied::CHECKING, "no vblank read: still checking at 4 s");
      Check(c.Check(5 * s) == Applied::UNMEASURED, "... not measured at 5 s, which is not on");
    }
    {
      CStartCheck c;
      Check(c.Check(10 * s) == Applied::NONE, "not started: nothing");
    }
    // nvidia-drm without vblanks (its defaults): the driver says it, NV_VRR_ACTIVE (9079). The test of
    // 2026-11-18: VRR_ENABLED 1, no vblank read, "did not come on ... after 0 flips" at 5 s.
    {
      // three flips after the first with VRR active: on, without a vblank
      CStartCheck c;
      c.Start(0, 3 * 24000.0 / 1001.0, 359.999);
      c.KernelSays(1);
      for (int i = 0; i < 4; ++i)
        c.DriverSays(0x101);
      Check(c.Check(s / 10) == Applied::FOLLOWS,
            "NV_VRR_ACTIVE 1 on the flips after the first: on, before any vblank and before 1 s");
      Check(c.DriverKnown() && c.DriverOn() == 3, "... three flips said it after the one left out");
      // before enough flips: still checking, also past 5 s while the driver has spoken
      CStartCheck d;
      d.Start(0, 3 * 24000.0 / 1001.0, 359.999);
      d.DriverSays(0x101);
      d.DriverSays(0x101);
      Check(d.Check(s / 10) == Applied::CHECKING, "one flip with VRR active after the first: checking");
      Check(d.Check(5 * s) == Applied::UNMEASURED, "... and not on at 5 s if no more come");
    }
    {
      // the driver says that VRR is not active (the timings of the mode have none, or a flip
      // without it): not on, at once, which the vblanks did not wait for
      CStartCheck c;
      c.Start(0, vrr, 120.0);
      c.KernelSays(1);
      c.DriverSays(0x000);
      Check(c.Check(0) == Applied::CHECKING, "the first flip is left out");
      c.DriverSays(0x000);
      Check(c.Check(0) == Applied::DRIVER_OFF, "NV_VRR_ACTIVE 0: not on, at once");
      CStartCheck d;
      d.Start(0, vrr, 120.0);
      d.DriverSays(0x101);
      d.DriverSays(0x101);
      d.DriverSays(0x100);
      d.DriverSays(0x101);
      Check(d.Check(s) == Applied::DRIVER_OFF, "one flip without VRR among the first ones: not on");
      // a property that is not there (another driver) changes nothing: the vblanks decide
      CStartCheck e;
      e.Start(0, vrr, 120.0);
      e.KernelSays(1);
      e.DriverSays(std::nullopt);
      Check(!e.DriverKnown() && e.Check(run(e, vrr, 120.0, true, 1.1)) == Applied::FOLLOWS,
            "no NV_VRR_ACTIVE: the vblanks of amdgpu and i915 decide as before");
      CStartCheck f;
      f.Start(0, vrr, 120.0);
      f.KernelSays(0);
      f.DriverSays(0x101);
      f.DriverSays(0x101);
      Check(f.Check(0) == Applied::NOT_KEPT, "VRR_ENABLED read back 0 comes first");
      CStartCheck g;
      g.Start(0, 120.0, 120.0);
      g.DriverSays(0x100);
      g.DriverSays(0x100);
      Check(g.Check(0) == Applied::SAME_RATE, "the rate of the mode: nothing to tell, whatever the driver says");
    }
    // ---- the signal, by the out-fences of the flips, where the driver says VRR is active
    // (FenceInterval()): a fixed refresh puts each flip on the mode's next refresh
    {
      const double lgRate = 2 * 24000.0 / 1001.0; // 47.952 Hz in a mode of 119.998 Hz (the LG C5, 1.1e)
      const auto drive = [&](CStartCheck& c, bool follows, int flips)
      {
        const double asked = 1e9 / lgRate;
        const double refresh = 1e9 / 119.998;
        double lastShown = 0.0;
        for (int i = 0; i < flips; ++i)
        {
          const double t = i * asked;
          // VRR: the flip at its time, within +-200 us; fixed: at the first refresh at or after it
          const double shown =
              follows ? t + ((i % 3) - 1) * 200000.0 : std::ceil(t / refresh - 1e-9) * refresh;
          if (i > 0)
            c.FenceInterval(static_cast<int64_t>(std::llround((shown - lastShown) - asked)));
          lastShown = shown;
        }
      };
      CStartCheck c;
      c.Start(0, lgRate, 119.998);
      for (int i = 0; i < 4; ++i)
        c.DriverSays(0x101);
      Check(c.FenceTells() && c.FixedFenceOffShare() > 0.99,
            "47.952 Hz in 119.998 Hz: a fixed refresh puts every interval off by 4.2 ms, the fences tell");
      Check(c.Check(s / 10) == Applied::CHECKING, "the driver says on: the signal is still to be measured");
      drive(c, true, 60);
      Check(c.Check(s + s / 4) == Applied::FOLLOWS, "the flips at the times asked: on");
      CStartCheck d;
      d.Start(0, lgRate, 119.998);
      for (int i = 0; i < 4; ++i)
        d.DriverSays(0x101);
      drive(d, false, 60);
      Check(d.Check(s + s / 4) == Applied::OUTPUT_FIXED,
            "the driver says on, but the flips came at the 120 Hz of the mode: not on (" +
                std::to_string(100.0 * d.FenceOffShare()) + " % off)");
      Check(std::string(AppliedName(Applied::OUTPUT_FIXED)) == "fixed", "... shown as not on");
      CStartCheck e;
      e.Start(0, lgRate, 119.998);
      for (int i = 0; i < 4; ++i)
        e.DriverSays(0x101);
      Check(e.Check(4 * s) == Applied::CHECKING && e.Check(5 * s) == Applied::UNMEASURED,
            "no fence read in 5 s: not taken as on");
      // the G60SD: 71.928 Hz in 359.999 Hz, nearly 5 refreshes a picture: a fixed refresh is off in one
      // interval of 200, which the fences cannot tell; the driver's word holds
      CStartCheck f;
      f.Start(0, 3 * 24000.0 / 1001.0, 359.999);
      Check(!f.FenceTells() && f.FixedFenceOffShare() < 0.01,
            "71.928 Hz in 359.999 Hz: nearly a whole multiple, not told apart");
    }
    // ---- no mode at the video's rate: Automatic() and AutoForDisplay()
    {
      const double film = 24000.0 / 1001.0;
      Check(!WholeMultipleOf(119.999, film), "119.999 Hz is no whole multiple of 23.976");
      Check(WholeMultipleOf(119.999, 24.0), "... but is of 24");
      Check(WholeMultipleOf(23.975758, film), "the mode of 23.976 of the LG C5 (23.975758 Hz) is");
      Check(WholeMultipleOf(120000.0 / 1001.0, film), "119.88 Hz is 5 times 23.976");
      Check(!WholeMultipleOf(359.999, film) && !WholeMultipleOf(239.957, film),
            "the 359.999 and 239.957 Hz of the Samsung G60SD are not");
      Check(!WholeMultipleOf(0.0, film) && !WholeMultipleOf(120.0, 0.0), "no rate: no");
      Check(!WholeMultipleOf(24.0 * 17, 24.0), "more than 16 times: no");
      // every mode of the Samsung G60SD (2560x1440 and 1920x1080, by its EDID) and of the LG C5
      // (3840x2160 to 1280x720)
      const double samsung[] = {119.999, 360.0, 239.957, 119.998, 59.999, 359.99,
                                239.93,  120.0, 119.88,  60.0,    59.94};
      const double c5[] = {120.0, 119.88, 100.0, 60.0, 59.94, 50.0, 30.0, 29.97, 25.0, 24.0, 23.976};
      Check(!DisplayHasModeFor(samsung, film), "the G60SD has no mode of 23.976 or 47.952 (119.88 is 5x)");
      Check(DisplayHasModeFor(c5, film), "the LG C5 has 23.976");
      Check(DisplayHasModeFor(samsung, 60.0), "the G60SD has 59.999 Hz for 60 fps (within 200 ppm)");
      Check(!DisplayHasModeFor(samsung, 25.0), "... and nothing for 25 fps");
      Check(DisplayHasModeFor(std::vector<double>{}, film), "no mode known: taken as having one");
      Check(AutoForDisplay(true, true, samsung), "the G60SD, the setting on, VRR declared: its menus keep VRR");
      Check(!AutoForDisplay(false, true, samsung), "... not with the setting off (as 1.1d)");
      Check(!AutoForDisplay(true, false, samsung), "... not without VRR");
      Check(!AutoForDisplay(true, true, c5), "the LG C5: it has modes of 23.976, its mode sets stay");
      Check(Automatic(true, true, 359.999, film, false), "23.976 in the 359.999 Hz that Kodi kept: VRR");
      Check(!Automatic(false, true, 359.999, film, false), "with the setting off: as 1.1d, nothing");
      Check(!Automatic(true, false, 359.999, film, false), "Kodi chose another mode: nothing");
      Check(!Automatic(true, true, 119.999, 24.0, false), "24 fps in 119.999 Hz: shown evenly, nothing");
      Check(!Automatic(true, true, 120.0, 60.0, false), "60 fps in 120 Hz: nothing");
      Check(Automatic(true, true, 120.0, 25.0, false), "25 fps in 120 Hz, no mode of 25 or 50: VRR");
      // the LG C5 of the test of 2026-11-18 with the menus left in 2560x1440 by the Samsung: Kodi's
      // default whitelist kept 2560x1440 at 119.998 Hz for a 1920x1080 video at 23.976 (it does not
      // leave the resolution of the menus for it), and 1.1e took VRR there; the display has 3840x2160
      // at 23.976, so never VRR
      Check(!Automatic(true, true, 119.998, film, DisplayHasModeFor(c5, film)),
            "the LG C5 with the menus at 2560x1440: it has a mode at 23.976 elsewhere, no VRR");
      // the automatic query goes through Decide() as the setting does: the G60SD on the 4080
      Query q = C5On4080(static_cast<float>(film));
      q.vrrMin = 48;
      q.vrrMax = 360;
      q.guiRefresh = 359.999f;
      q.guiLines = 1440;
      const Verdict v = Decide(q);
      Check(v.use && v.multiple == 3 && Near(v.rate, 3 * film, 1e-3),
            "the G60SD (VRR 48 to 360 Hz) at 359.999 Hz: 23.976 at 71.928 Hz, each picture 3 times");
      q.video = WholeMultipleVideo::HDR10PLUS;
      Check(!Decide(q).use, "... not for HDR10+ where the driver has no slot for it next to the VRR packet");
      // (fixA) Ada and Blackwell: NV_HDR10PLUS_WITH_VRR 1 (9082): HDR10+ in its own slot next to the EMP
      q.hdr10PlusWithVrr = true;
      const Verdict h = Decide(q);
      Check(h.use && h.multiple == 3, "HDR10+ on the G60SD of an RTX 4080 SUPER (Ada): at its own rate with VRR");
      // (fixV) Dolby Vision that does not go out natively goes out as HDR10, HDR10+ or SDR: taken as HDR10+ is
      // (Avatar, Dolby Vision profile 7, rebuilt and sent as HDR10+ to the G60SD in the test of 2026-11-19, went at
      // a fixed 359.999 Hz because Dolby Vision was refused whatever went out)
      q.video = WholeMultipleVideo::DOLBY_VISION;
      const Verdict dv = Decide(q);
      Check(dv.use && dv.multiple == 3 && Near(dv.rate, 3 * film, 1e-3),
            "Dolby Vision sent as HDR10+ on the G60SD of an RTX 4080 SUPER: at its own rate with VRR, 71.928 Hz");
      q.dvMayBeHlg = true;
      Check(!Decide(q).use && std::string(Decide(q).why).find("HLG") != std::string::npos,
            "... not where it may go out as HLG (profile 8.4), which says so");
      q.dvMayBeHlg = false;
      q.hdr10PlusWithVrr = false;
      Check(!Decide(q).use && std::string(Decide(q).why).find("NV_HDR10PLUS_WITH_VRR") != std::string::npos,
            "... nor on Turing or Ampere, where HDR10+ has no slot next to the VRR packet");
      q.hdr10PlusWithVrr = true;
      q.nativeDV = true;
      Check(!Decide(q).use && std::string(Decide(q).why) == NativeDolbyVisionWhy(),
            "native Dolby Vision never, whatever the slots, and it says why");
      q.nativeDV = false;
      q.video = WholeMultipleVideo::HLG;
      Check(!Decide(q).use, "... nor HLG");
    }
    // ---- (fixV) native Dolby Vision with "Always" on the LG C5 (menus at 3840x2160 and 120 Hz): never at the fixed
    // 120 Hz of the menus. Asked before Kodi's choice, it is refused; it is not asked after it; its flips are never
    // paced; and its transport has no mode at the 120 Hz of the menus, so Kodi's choice is the change of mode to its
    // rate (the test of 2026-11-19, 02:51:29: "cannot go at its own rate with VRR (\"Always\": ...)", then 3840x2160 at
    // 23.976 Hz with the Dolby Vision signal)
    {
      for (const double fps : {24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 50.0, 60000.0 / 1001.0, 60.0})
      {
        Query q = C5On4080(static_cast<float>(fps));
        q.video = WholeMultipleVideo::DOLBY_VISION;
        q.nativeDV = true;
        q.hdr10PlusWithVrr = true;
        const Verdict v = Decide(q);
        Check(!v.use && v.why && std::string(v.why) == NativeDolbyVisionWhy() && v.multiple == 0,
              "LG C5, Always, native Dolby Vision at " + std::to_string(fps) + " fps: refused, Kodi's choice");
        // (the transport mode of that rate exists, and the 120 Hz of the menus is none)
        Check(DVBRIDGE::IsTransportMode(3840, 2160, fps, false) && !DVBRIDGE::IsTransportMode(3840, 2160, 120.0, false),
              "... its transport mode is at its own rate, never at the 120 Hz of the menus");
      }
      Check(!AskedAfterKodi(true, false) && !AskedAfterKodi(false, true) && AskedAfterKodi(false, false),
            "native Dolby Vision and 3D are not asked again after Kodi's choice");
      Check(!Paced(true, true, true, true, true), "native Dolby Vision: no flip of it is paced");
      // with the setting at "Where the display has no mode at the video's rate" too
      Query q = C5On4080(24000.0f / 1001.0f);
      q.video = WholeMultipleVideo::DOLBY_VISION;
      q.nativeDV = true;
      q.guiRefresh = 359.999f;
      q.vrrMin = 48;
      q.vrrMax = 360;
      Check(!Decide(q).use, "native Dolby Vision on a display without modes of 24 Hz: refused as well");
    }
    // ---- (fixV) "Always", the mode of the menus set with HDMI VRR off: the video keeps the mode it got
    // (RefusedForModeSetOnly(): the LG C5, 2026-11-19 02:50:09, changed to 23.976 Hz and then back to 120 Hz)
    {
      Query q = C5On4080(24000.0f / 1001.0f);
      q.hdmiVrrOffInMode = true;
      Check(!Decide(q).use && RefusedForModeSetOnly(q), "refused only for the mode set: held for the video");
      q.video = WholeMultipleVideo::HLG;
      Check(!RefusedForModeSetOnly(q), "refused for another reason too: not held for it");
      q.video = WholeMultipleVideo::SDR;
      q.hdmiVrrOffInMode = false;
      Check(Decide(q).use && !RefusedForModeSetOnly(q), "not refused: nothing held");
      q.hdmiVrrOffInMode = true;
      q.nativeDV = true;
      Check(!RefusedForModeSetOnly(q), "native Dolby Vision: refused for itself, nothing held");
    }
    // ---- (fixV) the rate of the flips that the player information says (CFlipRate, ShownRefresh())
    {
      const double asked = 3 * 24000.0 / 1001.0; // the G60SD: 71.928 Hz
      const double period = 1e9 / asked;
      CFlipRate r;
      Check(r.Hz() == 0.0, "nothing measured: no rate");
      // flips at the times asked, within +-450 us (the out-fences of the test of 2026-11-19: p1 -445, p99 465 us),
      // one late by a whole period now and then, and one slot missed (asked two periods apart) every 50
      for (int i = 0; i < 300; ++i)
      {
        const bool missed = i % 50 == 49;
        const double askedNs = missed ? 2 * period : period;
        double actual = askedNs + ((i % 3) - 1) * 450000.0;
        if (i % 40 == 7)
          actual += period; // a flip that came a refresh late
        r.Add(static_cast<int64_t>(std::llround(actual)), static_cast<int64_t>(std::llround(askedNs)), period);
        if (i == CFlipRate::MIN_INTERVALS - 2)
          Check(r.Hz() == 0.0, "fewer than 24 intervals: no rate yet");
      }
      Check(std::fabs(r.Hz() - asked) < 0.1, "flips at 71.928 Hz with jitter, late flips and missed slots: " +
                                                 std::to_string(r.Hz()) + " Hz measured");
      Check(!r.Add(static_cast<int64_t>(2 * period), static_cast<int64_t>(2 * period), period),
            "an interval asked two slots apart (a missed slot) does not count");
      Check(!r.Add(0, static_cast<int64_t>(period), period) && !r.Add(1000, 1000, 0.0), "nothing to measure: no");
      r.Reset();
      Check(r.Count() == 0 && r.Hz() == 0.0, "reset");
      // a fixed refresh of 120 Hz under flips asked at 47.952 Hz (VRR not on): intervals of 16.7 and 25 ms, whose
      // median is no rate of the video; the player information shows the mode's then anyway (not FOLLOWS)
      const double lg = 2 * 24000.0 / 1001.0;
      for (int i = 0; i < 96; ++i)
        r.Add(static_cast<int64_t>(i % 2 ? 25000000 : 16666667), static_cast<int64_t>(1e9 / lg), 1e9 / lg);
      Check(std::fabs(r.Hz() - lg) > lg * SHOWN_AGREES, "a fixed 120 Hz does not measure as 47.952 Hz");

      // what the player information says
      const Shown s1 = ShownRefresh(Applied::FOLLOWS, 359.999, asked, 71.93);
      Check(s1.vrr && Near(s1.hz, asked), "VRR follows, measured 71.93: 71.928 Hz (VRR), as the monitor's 72 Hz");
      const Shown s2 = ShownRefresh(Applied::FOLLOWS, 120.0, lg, 47.95);
      Check(s2.vrr && Near(s2.hz, lg), "the LG C5: 47.952 Hz (VRR), as the TV's 47 Hz");
      const Shown s3 = ShownRefresh(Applied::FOLLOWS, 359.999, asked, 60.0);
      Check(s3.vrr && Near(s3.hz, 60.0), "measured far from the rate asked: the measure, never the rate asked");
      Check(!ShownRefresh(Applied::FOLLOWS, 359.999, asked, 0.0).vrr &&
                Near(ShownRefresh(Applied::FOLLOWS, 359.999, asked, 0.0).hz, 359.999),
            "VRR follows, not measured yet: the mode's");
      for (const Applied a : {Applied::NONE, Applied::CHECKING, Applied::SAME_RATE, Applied::FIXED,
                              Applied::DRIVER_OFF, Applied::OUTPUT_FIXED, Applied::UNMEASURED})
        Check(!ShownRefresh(a, 359.999, asked, 71.93).vrr && Near(ShownRefresh(a, 359.999, asked, 71.93).hz, 359.999),
              "not FOLLOWS (" + std::string(AppliedName(a)) + "): the mode's");
      Check(KODI::UTILS::VrrModeText(2560, 1440, asked) == "2560x1440 @ 71.928 Hz (VRR)" &&
                KODI::UTILS::VrrModeText(3840, 2160, lg) == "3840x2160 @ 47.952 Hz (VRR)" &&
                KODI::UTILS::VrrModeText(3840, 2160, 0.0).empty(),
            "the mode row: \"2560x1440 @ 71.928 Hz (VRR)\"");
    }
    // ---- (fixV) the VRR row of the player information (RowValue())
    {
      RowFacts f;
      f.capable = 1;
      f.heldOff = 1;
      f.heldBySetting = true;
      Check(std::string(RowValue(f)) == "setting", "SDR with the settings at their defaults: off by the settings");
      f.playingNoMode = true;
      Check(std::string(RowValue(f)) == "nomode",
            "the G60SD with VRR off, 23.976 fps at 359.999 Hz: no mode at the video's rate");
      f.capable = 0;
      Check(std::string(RowValue(f)) == "setting", "... not said where the display offers no VRR");
      f.capable = 1;
      f.playingNoMode = false;
      f.heldBySetting = false;
      Check(std::string(RowValue(f)) == "held", "held off for HDR10+ (Turing, Ampere)");
      f.nativeDV = true;
      Check(std::string(RowValue(f)) == "dv", "native Dolby Vision: it changes the mode");
      f.heldBySetting = true;
      f.playingNoMode = true;
      Check(std::string(RowValue(f)) == "dv", "... whatever else");
      f = RowFacts();
      f.applied = Applied::FOLLOWS;
      f.multiple = 3;
      Check(std::string(RowValue(f)) == "repeats", "VRR on, each picture 3 times: said with how many and why");
      f.multiple = 1;
      Check(std::string(RowValue(f)) == "follows", "VRR on, each picture once");
      f.applied = Applied::DRIVER_OFF;
      Check(std::string(RowValue(f)) == "fixed", "VRR did not come on");
      f.refusedNoMode = true;
      Check(std::string(RowValue(f)) == "nomode", "... in a mode without the video's rate");
      f.applied = Applied::CHECKING;
      Check(std::string(RowValue(f)) == "checking", "checking");
      f = RowFacts();
      f.enabled = 1;
      f.capable = 1;
      Check(std::string(RowValue(f)) == "on", "VRR_ENABLED on the CRTC: on");
      f.enabled = 0;
      Check(std::string(RowValue(f)) == "offered", "offered, not used");
      f.capable = 0;
      Check(std::string(RowValue(f)) == "none", "not offered");
      f.capable.reset();
      Check(std::string(RowValue(f)) == "unknown", "not said");
    }
    // ---- the three values of the setting (Mode), and the boolean of the 1.1 test builds taken over
    {
      Check(ModeOf(0) == Mode::OFF && ModeOf(1) == Mode::NO_MODE && ModeOf(2) == Mode::ALWAYS,
            "the values of videoscreen.hadesvrrvideomode");
      Check(ModeOf(-1) == Mode::OFF && ModeOf(3) == Mode::OFF, "a value that is none of them: off");
      Check(!TakesBeforeKodi(Mode::OFF) && !TakesBeforeKodi(Mode::NO_MODE) && TakesBeforeKodi(Mode::ALWAYS),
            "only always is asked before Kodi's choice of a mode");
      const double c5[] = {120.0, 119.88, 100.0, 60.0, 59.94, 50.0, 30.0, 29.97, 25.0, 24.0, 23.976};
      const double g60sd[] = {119.999, 360.0, 239.957, 119.998, 59.999, 359.99, 239.93, 120.0, 119.88, 60.0, 59.94};
      Check(!VrrInModeSets(Mode::OFF, true, g60sd) && !VrrInModeSets(Mode::OFF, true, c5),
            "off: no mode set leaves VRR to the driver for it, on any display");
      Check(VrrInModeSets(Mode::NO_MODE, true, g60sd) && !VrrInModeSets(Mode::NO_MODE, true, c5),
            "where the display has no mode at the video's rate: the G60SD, not the LG C5");
      Check(VrrInModeSets(Mode::ALWAYS, true, g60sd) && VrrInModeSets(Mode::ALWAYS, true, c5),
            "always: every display");
      // the boolean (on was "always" in 1.1e) and the new setting at its default (off), when the settings load
      const int deflt = static_cast<int>(Mode::OFF);
      using KODI::SETTINGS::HADES::Migrate;
      const auto on = Migrate(deflt, deflt, true, ModeFromOld, ModeToOld);
      Check(on.mode == static_cast<int>(Mode::ALWAYS) && on.modeChanged && on.old && !on.oldChanged,
            "the boolean on (1.1e): always");
      const auto off = Migrate(deflt, deflt, false, ModeFromOld, ModeToOld);
      Check(off.mode == deflt && !off.modeChanged && !off.old && !off.oldChanged, "the boolean off or not saved: off");
      // a value chosen in this build stays, and the boolean follows it (on only for always)
      const auto noMode = Migrate(static_cast<int>(Mode::NO_MODE), deflt, false, ModeFromOld, ModeToOld);
      Check(noMode.mode == static_cast<int>(Mode::NO_MODE) && !noMode.modeChanged && !noMode.old,
            "where the display has no mode at the video's rate stays; the boolean stays off");
      const auto always = Migrate(static_cast<int>(Mode::ALWAYS), deflt, false, ModeFromOld, ModeToOld);
      Check(always.mode == static_cast<int>(Mode::ALWAYS) && always.old && always.oldChanged,
            "always stays, and the boolean is set on for the builds before");
      // the user sets it back to off after the update: the boolean is off again, so off stays at the next start
      const auto back = Migrate(deflt, deflt, ModeToOld(deflt), ModeFromOld, ModeToOld);
      Check(back.mode == deflt && !back.modeChanged, "set back to off: off at the next start");
    }
    Check(std::string(AppliedName(Applied::FIXED)) == "fixed" &&
              std::string(AppliedName(Applied::NOT_KEPT)) == "fixed" &&
              std::string(AppliedName(Applied::UNMEASURED)) == "fixed" &&
              std::string(AppliedName(Applied::DRIVER_OFF)) == "fixed" &&
              std::string(AppliedName(Applied::FOLLOWS)) == "follows" &&
              std::string(AppliedName(Applied::CHECKING)) == "checking" &&
              std::string(AppliedName(Applied::SAME_RATE)) == "samerate" &&
              std::string(AppliedName(Applied::NONE)).empty(),
          "the values of hades.out.vrr");
  }

  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
