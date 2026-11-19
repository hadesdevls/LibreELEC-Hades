/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of "HDMI variable refresh rate (VRR)" (videoscreen.hadeshdmivrr), the choice of
 * HDMI VRR in the mode sets that Kodi makes (windowing/gbm/HdmiVrrOff.h, Allowed(), Reason() with Stage()):
 *  - Allowed(): off, the default, unless the display is one where a video goes at its own rate with VRR;
 *  - with "Video at its own rate with VRR" (VRR_VIDEO::VrrInModeSets()): off, its default, every mode set of both
 *    displays of the tests asks for VRR off; "where the display has no mode at the video's rate" only the G60SD's
 *    leave it to the driver; "always" both;
 *  - with the setting on every decision is the one Kodi made before the setting, for every
 *    combination of a mode set, HDR10+, the kernel's value and native Dolby Vision;
 *  - with the setting off every mode set that Kodi stages asks for VRR off, and the reason is the setting;
 *  - native Dolby Vision, a commit without a mode set and a connector without the property are never touched;
 *  - (crash) the end of native Dolby Vision puts back what the settings ask then (AtHandBack()), after which the
 *    mode set of the menus stages nothing; a change of the settings while the menus show goes out at once
 *    (ModesetForSetting()), not during a video, native Dolby Vision or a mode set on its way.
 */
#include "windowing/gbm/HdmiVrrOff.h"
#include "windowing/gbm/VrrVideo.h"

#include <cstdio>
#include <optional>
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

// The decision of CWinSystemGbm::StageHdmiVrrOff(), as it is written there: nothing without the property or a
// mode set; otherwise Stage() with the reason of Reason()
static std::optional<uint64_t> Decide(bool modeset, bool settingAllowsVrr, bool hdr10Plus,
                                      std::optional<uint64_t> kernel, bool nativeDV)
{
  if (!kernel || !modeset)
    return std::nullopt;
  const auto why = CHdmiVrrOff::Reason(settingAllowsVrr, hdr10Plus);
  return CHdmiVrrOff::Stage(true, why != CHdmiVrrOff::Why::NONE, kernel, nativeDV);
}

// What Kodi staged before the setting (1.0): Stage(modeset, hdr10Plus, kernel, nativeDV)
static std::optional<uint64_t> Before(bool modeset, bool hdr10Plus, std::optional<uint64_t> kernel,
                                      bool nativeDV)
{
  return CHdmiVrrOff::Stage(modeset, hdr10Plus, kernel, nativeDV);
}

static std::string Name(std::optional<uint64_t> v)
{
  return v ? std::to_string(*v) : std::string("nothing");
}

int main()
{
  // Allowed(): "HDMI variable refresh rate (VRR)" (off by default), or a display where a video goes at its
  // own rate with VRR ("Video with VRR where the display has no mode at its rate" on, VRR declared, no mode
  // of the rate of films: VRR_VIDEO::AutoForDisplay())
  Check(!CHdmiVrrOff::Allowed(false, false), "Allowed: both off: VRR off");
  Check(CHdmiVrrOff::Allowed(true, false), "Allowed: HDMI VRR on: the driver decides");
  Check(CHdmiVrrOff::Allowed(false, true), "Allowed: a display without modes of 24 Hz: its menus keep VRR");
  Check(CHdmiVrrOff::Allowed(true, true), "Allowed: both");
  Check(CHdmiVrrOff::Reason(CHdmiVrrOff::Allowed(false, false), false) == CHdmiVrrOff::Why::SETTING,
        "the defaults: every mode set asks for VRR off, by the setting");
  Check(CHdmiVrrOff::Reason(CHdmiVrrOff::Allowed(false, true), false) == CHdmiVrrOff::Why::NONE,
        "Video at its own rate with VRR on, HDMI VRR off: the driver decides");
  Check(CHdmiVrrOff::Reason(CHdmiVrrOff::Allowed(false, true), true) == CHdmiVrrOff::Why::HDR10PLUS,
        "Video at its own rate with VRR on, an HDR10+ video: off for HDR10+, as before");
  // Reason(): the truth table of its two inputs
  Check(CHdmiVrrOff::Reason(true, false) == CHdmiVrrOff::Why::NONE, "Reason: setting on, no HDR10+: NONE");
  Check(CHdmiVrrOff::Reason(true, true) == CHdmiVrrOff::Why::HDR10PLUS, "Reason: setting on, HDR10+: HDR10PLUS");
  Check(CHdmiVrrOff::Reason(false, false) == CHdmiVrrOff::Why::SETTING, "Reason: setting off: SETTING");
  Check(CHdmiVrrOff::Reason(false, true) == CHdmiVrrOff::Why::SETTING,
        "Reason: setting off and HDR10+: SETTING (the setting holds for every mode set)");

  const std::optional<uint64_t> kernels[] = {std::nullopt, 0u, 1u, 2u};
  int same = 0;
  for (int m = 0; m < 2 * 2 * 2 * 4; ++m)
  {
    const bool modeset = m & 1, hdr10Plus = m & 2, nativeDV = m & 4;
    const auto kernel = kernels[m >> 3];
    const std::string at = "modeset " + std::to_string(modeset) + ", HDR10+ " + std::to_string(hdr10Plus) +
                           ", native DV " + std::to_string(nativeDV) + ", kernel " + Name(kernel);
    // the setting on: exactly what 1.0 did
    const auto on = Decide(modeset, true, hdr10Plus, kernel, nativeDV);
    const auto before = Before(modeset, hdr10Plus, kernel, nativeDV);
    Check(on == before, "setting on = 1.0 (" + at + "): " + Name(on) + " against " + Name(before));
    same += on == before;
    // the setting off: 1 wherever a mode set is staged and the kernel has something else; never over
    // native Dolby Vision, never without a mode set or the property
    const auto off = Decide(modeset, false, hdr10Plus, kernel, nativeDV);
    const bool may = modeset && kernel && !nativeDV;
    const std::optional<uint64_t> want =
        may && *kernel != 1u ? std::optional<uint64_t>(1u) : std::nullopt;
    Check(off == want, "setting off (" + at + "): " + Name(off) + ", expected " + Name(want));
  }
  // "Video at its own rate with VRR" (VRR_VIDEO::VrrInModeSets()) into Allowed(), with "HDMI variable refresh rate
  // (VRR)" off, its default, on the two displays of the tests: with the setting off, its default, every mode set of
  // both asks for VRR off (no VTEM "VRR on" in the menus); "Where the display has no mode at the video's rate" only
  // on the G60SD, which has no mode of 23.976 Hz; "Always" on both
  {
    using VRR_VIDEO::Mode;
    const double g60sd[] = {119.999, 360.0, 239.957, 119.998, 59.999, 359.99, 239.93, 120.0, 119.88, 60.0, 59.94};
    const double c5[] = {120.0, 119.88, 100.0, 60.0, 59.94, 50.0, 30.0, 29.97, 25.0, 24.0, 23.976};
    const auto why = [](Mode mode, const auto& rates)
    { return CHdmiVrrOff::Reason(CHdmiVrrOff::Allowed(false, VRR_VIDEO::VrrInModeSets(mode, true, rates)), false); };
    Check(why(Mode::OFF, g60sd) == CHdmiVrrOff::Why::SETTING,
          "off (the default), the G60SD: every mode set asks for VRR off, as 1.1d");
    Check(why(Mode::OFF, c5) == CHdmiVrrOff::Why::SETTING, "off (the default), the LG C5: VRR off, as 1.1d");
    Check(why(Mode::NO_MODE, g60sd) == CHdmiVrrOff::Why::NONE,
          "where the display has no mode at the video's rate, the G60SD: the driver decides in its menus");
    Check(why(Mode::NO_MODE, c5) == CHdmiVrrOff::Why::SETTING,
          "where the display has no mode at the video's rate, the LG C5 (23.976 Hz): VRR off, as 1.1d");
    Check(why(Mode::ALWAYS, g60sd) == CHdmiVrrOff::Why::NONE, "always, the G60SD: the driver decides");
    Check(why(Mode::ALWAYS, c5) == CHdmiVrrOff::Why::NONE, "always, the LG C5: the driver decides, as 1.1e on");
    Check(!VRR_VIDEO::VrrInModeSets(Mode::NO_MODE, false, g60sd),
          "where the display has no mode at the video's rate, without VRR declared: VRR off");
    Check(VRR_VIDEO::VrrInModeSets(Mode::ALWAYS, false, c5),
          "always: every mode set leaves it to the driver, as 1.1e on did whatever the display declared");
    // and with "HDMI variable refresh rate (VRR)" on, the driver decides whatever the other one says
    for (const Mode mode : {Mode::OFF, Mode::NO_MODE, Mode::ALWAYS})
      Check(CHdmiVrrOff::Allowed(true, VRR_VIDEO::VrrInModeSets(mode, true, c5)),
            "HDMI VRR on: the driver decides, value " + std::to_string(static_cast<int>(mode)));
  }
  // the scenes of a session, step by step (the kernel keeps what the last mode set left)
  {
    std::optional<uint64_t> kernel = 0u;
    auto step = [&](bool settingAllowsVrr, bool hdr10Plus, bool nativeDV, std::optional<uint64_t> expect,
                    const std::string& what) {
      const auto v = Decide(true, settingAllowsVrr, hdr10Plus, kernel, nativeDV);
      Check(v == expect, what + ": " + Name(v) + ", expected " + Name(expect));
      if (v)
        kernel = *v;
    };
    step(true, false, false, std::nullopt, "GUI at start, setting on: the driver decides, nothing staged");
    step(false, false, false, 1u, "the user turns it off; the next mode set (a video): 1");
    step(false, false, false, std::nullopt, "the GUI after the video, setting off: stays 1, nothing staged");
    step(false, true, false, std::nullopt, "an HDR10+ video, setting off: stays 1");
    step(false, false, true, std::nullopt, "native Dolby Vision: its transport owns it, nothing staged");
    step(true, false, false, 0u, "the user turns it on again; the next mode set: 0, the driver decides");
    step(true, true, false, 1u, "an HDR10+ video, setting on: 1, as in 1.0");
    step(true, false, false, 0u, "the GUI after it: 0 again, as in 1.0");
  }
  // (crash, 1.1) the end of native Dolby Vision puts back what the settings ask now (AtHandBack()), not what it found:
  // the LG C5 on 2026-11-19 10:04:06-07 found 1 ("Always" turned on while the menus showed), gave back 1, and the next
  // frame made a second change of mode for 0
  {
    using VRR_VIDEO::Mode;
    const double c5[] = {120.0, 119.88, 100.0, 60.0, 59.94, 50.0, 30.0, 29.97, 25.0, 24.0, 23.976};
    Check(CHdmiVrrOff::AtHandBack(true) == 0u, "AtHandBack: the settings allow VRR (\"Always\"): 0, one change of mode");
    Check(CHdmiVrrOff::AtHandBack(false) == 1u, "AtHandBack: both off: 1");
    const bool always = CHdmiVrrOff::Allowed(false, VRR_VIDEO::VrrInModeSets(Mode::ALWAYS, true, c5));
    std::optional<uint64_t> kernel = CHdmiVrrOff::AtHandBack(always);
    Check(Decide(true, always, false, kernel, false) == std::nullopt,
          "after the hand-back, the mode set of the menus stages nothing: no second change of mode");
    // a change of the settings while the menus show goes out at once, in a mode set of its own
    Check(CHdmiVrrOff::ModesetForSetting(true, false, false, false), "ModesetForSetting: changed, in the menus: yes");
    Check(!CHdmiVrrOff::ModesetForSetting(false, false, false, false), "ModesetForSetting: nothing changed: no");
    Check(!CHdmiVrrOff::ModesetForSetting(true, true, false, false),
          "ModesetForSetting: a mode set is on its way, which takes it: no");
    Check(!CHdmiVrrOff::ModesetForSetting(true, false, true, false), "ModesetForSetting: native Dolby Vision: no");
    Check(!CHdmiVrrOff::ModesetForSetting(true, false, false, true),
          "ModesetForSetting: a video plays (its end takes it): no");
  }
  std::printf("%d checks (%d of 32 combinations equal to 1.0 with the setting on), %d failures\n%s\n", g_checks,
              same, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
