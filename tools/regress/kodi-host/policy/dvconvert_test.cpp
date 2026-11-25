// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of what decides and places a Dolby Vision picture that is reconstructed and sent as HDR10: the
// rule and the geometry of DVPlaybackPolicy.h, and the text of DVPlaybackInfo.h
// (xbmc/cores/VideoPlayer/DVDCodecs/Video). Build: g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined
// -I <work repo>/xbmc/cores/VideoPlayer/DVDCodecs/Video dvconvert_test.cpp
#include "DVPlaybackInfo.h"
#include "DVPlaybackPolicy.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using DVBRIDGE::ConversionEligible;
using DVBRIDGE::ConversionPlace;
using DVBRIDGE::PlaceConversion;
using Setting = DVBRIDGE::ConversionSetting;

static int failures = 0;

static void Check(const char* what, bool ok)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++failures;
}

static bool Near(float a, float b)
{
  return std::fabs(a - b) < 0.01f;
}

// the place of the whole of a picture, or of a part of it
static bool Is(const ConversionPlace& p, float sx0, float sy0, float sx1, float sy1, int x, int y, int w, int h)
{
  return p.valid && Near(p.srcX0, sx0) && Near(p.srcY0, sy0) && Near(p.srcX1, sx1) && Near(p.srcY1, sy1) &&
         p.dstX == x && p.dstY == y && p.dstWidth == w && p.dstHeight == h;
}

// the rule is usable where a constant is needed (profile, then what the record says its base layer is
// compatible with: 0 nothing, 1 HDR10, 2 SDR, 4 HLG, 6 Blu-ray)
static_assert(ConversionEligible(Setting::NEEDED, 5, 0, false, false, false, false));
static_assert(!ConversionEligible(Setting::NEEDED, 7, 6, true, false, false, false));

int main()
{
  // ---- which streams
  {
    bool ok = true;
    for (bool layer : {false, true})
      for (int compatible : {0, 1, 2, 4, 6})
        ok &= ConversionEligible(Setting::NEEDED, 5, compatible, layer, false, false, false) &&
              ConversionEligible(Setting::ALSO_ENHANCEMENT, 5, compatible, layer, false, false, false);
    Check("profile 5, whose base layer is no picture by itself, is reconstructed with either setting", ok);
    Check("... and so is profile 10 (AV1) when its base layer is compatible with nothing",
          ConversionEligible(Setting::NEEDED, 10, 0, false, false, false, false) &&
              ConversionEligible(Setting::ALSO_ENHANCEMENT, 10, 0, false, false, false, false));
    ok = true;
    for (int profile : {0, 4, 7, 8, 9, 10, 20})
      for (int compatible : {0, 1, 2, 4, 6})
        for (bool layer : {false, true})
          ok &= !ConversionEligible(Setting::NEEDED, profile, compatible, layer, false, false, false) ||
                ((profile == 10 || profile == 20) && compatible == 0);
    Check("no other stream is, unless it is asked for: their base layer is played as before", ok);
    Check("asked for, profile 7 with its enhancement layer is; without one, and any other profile, is not",
          ConversionEligible(Setting::ALSO_ENHANCEMENT, 7, 6, true, false, false, false) &&
              !ConversionEligible(Setting::ALSO_ENHANCEMENT, 7, 6, false, false, false, false) &&
              !ConversionEligible(Setting::ALSO_ENHANCEMENT, 8, 1, true, false, false, false) &&
              !ConversionEligible(Setting::ALSO_ENHANCEMENT, 10, 1, true, false, false, false) &&
              !ConversionEligible(Setting::ALSO_ENHANCEMENT, 4, 2, true, false, false, false) &&
              !ConversionEligible(Setting::ALSO_ENHANCEMENT, 0, 0, true, false, false, false));
    ok = true;
    for (int profile : {5, 7, 10})
    {
      ok &= !ConversionEligible(Setting::NEVER, profile, 0, true, false, false, false);
      ok &= !ConversionEligible(Setting::ALSO_ENHANCEMENT, profile, 0, true, true, false, false);
      ok &= !ConversionEligible(Setting::ALSO_ENHANCEMENT, profile, 0, true, false, true, false);
      ok &= !ConversionEligible(Setting::ALSO_ENHANCEMENT, profile, 0, true, false, false, true);
    }
    Check("never when it is turned off, with a software decoder, or with interlaced or stereoscopic video", ok);
    // the same rule says for which streams a picture that comes with the RPU of an earlier one is drawn
    // with it (the decoder asks it)
    ok = true;
    for (int profile : {0, 4, 5, 7, 8, 9, 10, 20})
      for (int compatible : {0, 1, 2, 4, 6})
        ok &= DVBRIDGE::BaseLayerIsNoPicture(profile, compatible) ==
              (profile == 5 || ((profile == 10 || profile == 20) && compatible == 0));
    Check("the base layer is no picture by itself in profile 5, and in profiles 10 and 20 when it is compatible "
          "with nothing (20.0 is IPT-PQ-c2 as Dolby's table of profiles defines it; it was shown as PQ before: "
          "green and purple); in every other stream it is one", ok);
    // Hades: profile 20, Dolby Vision in MV-HEVC: rebuilt and sent as HDR10 where its base layer is no
    // picture (with either setting), never natively; its base view decoded by the processor goes to the bridge as
    // profile 5 does; the subtitles of a 20.0 video are those of a PQ video
    Check("profile 20.0 is reconstructed with either setting, as profile 5",
          ConversionEligible(Setting::NEEDED, 20, 0, false, false, false, false) &&
              ConversionEligible(Setting::NO_PICTURE_ONLY, 20, 0, false, false, false, false) &&
              !ConversionEligible(Setting::NEEDED, 20, 4, false, false, false, false));
    Check("profile 20 never goes out natively; every other profile may",
          !DVBRIDGE::NativeProfile(20) && DVBRIDGE::NativeProfile(5) && DVBRIDGE::NativeProfile(7) &&
              DVBRIDGE::NativeProfile(8) && DVBRIDGE::NativeProfile(10) && DVBRIDGE::NativeProfile(0));
    Check("the pictures of profile 20 that the processor decodes go to the bridge, with their own RPU only",
          DVBRIDGE::CpuPicturesEligible(20, false, true) && !DVBRIDGE::CpuPicturesEligible(20, false, false) &&
              !DVBRIDGE::CpuPicturesEligible(7, true, true) && DVBRIDGE::CpuPicturesEligible(5, false, true));
    Check("only a picture of an HEVC stream can come with the metadata of an earlier one, when it has no RPU as "
          "coded; the metadata of an AV1 picture is its own (seventh review: every AV1 picture counted as one "
          "without its metadata)",
          DVBRIDGE::RpuRepeated(true, false) && !DVBRIDGE::RpuRepeated(true, true) &&
              !DVBRIDGE::RpuRepeated(false, false) && !DVBRIDGE::RpuRepeated(false, true));
  }

  // ---- where the picture goes: the rectangles are Kodi's (source part, place in the window; top left origin)
  {
    Check("a picture that fills the window",
          Is(PlaceConversion(0, 0, 3840, 2160, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160), 0, 0, 3840, 2160, 0, 0,
             3840, 2160));
    Check("a 1920x1080 picture scaled to a 3840x2160 window",
          Is(PlaceConversion(0, 0, 1920, 1080, 0, 0, 3840, 2160, 1920, 1080, 3840, 2160), 0, 0, 1920, 1080, 0, 0,
             3840, 2160));
    Check("a 3840x1608 picture between two bars",
          Is(PlaceConversion(0, 0, 3840, 1608, 0, 276, 3840, 1884, 3840, 1608, 3840, 2160), 0, 0, 3840, 1608, 0,
             276, 3840, 1608));
    Check("a 1440x1080 picture between two pillars",
          Is(PlaceConversion(0, 0, 1440, 1080, 480, 0, 3360, 2160, 1440, 1080, 3840, 2160), 0, 0, 1440, 1080, 480,
             0, 2880, 2160));
    Check("a 3840x2160 picture in a 1920x1080 window (the GUI is limited to it)",
          Is(PlaceConversion(0, 0, 3840, 2160, 0, 0, 1920, 1080, 3840, 2160, 1920, 1080), 0, 0, 3840, 2160, 0, 0,
             1920, 1080));
    // Zoomed to 133 %: Kodi places the picture past the window on every side, 5107.2x2872.8 around its centre.
    const ConversionPlace zoom =
        PlaceConversion(0, 0, 3840, 2160, -633.6f, -356.4f, 4473.6f, 2516.4f, 3840, 2160, 3840, 2160);
    Check("zoomed past the window: the window is filled with the middle of the picture, in the same proportion",
          Is(zoom, 476.39f, 267.97f, 3363.61f, 1892.03f, 0, 0, 3840, 2160) &&
              Near((zoom.srcX1 - zoom.srcX0) / 3840.0f, 3840.0f / 5107.2f) &&
              Near((zoom.srcY1 - zoom.srcY0) / 2160.0f, 2160.0f / 2872.8f));
    Check("shifted down and to the right: what leaves the window is cut off the picture too",
          Is(PlaceConversion(0, 0, 3840, 2160, 100.4f, 50.6f, 3940.4f, 2210.6f, 3840, 2160, 3840, 2160), 0, 0,
             3739.6f, 2109.4f, 100, 51, 3740, 2109));
    Check("shifted up and to the left",
          Is(PlaceConversion(0, 0, 3840, 2160, -100, -50, 3740, 2110, 3840, 2160, 3840, 2160), 100, 50, 3840, 2160,
             0, 0, 3740, 2110));
    Check("a part of the picture that Kodi cut out (a view mode that crops) is the part that is drawn",
          Is(PlaceConversion(0, 276, 3840, 1884, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160), 0, 276, 3840, 1884, 0,
             0, 3840, 2160));
    Check("edges between pixels go to the nearest one",
          Is(PlaceConversion(0, 0, 100, 100, 0.4f, 0.5f, 100.6f, 99.5f, 100, 100, 200, 200), 0, 0, 100, 100, 0, 1,
             101, 99));
    Check("a source part that reaches past the picture is cut to it",
          Is(PlaceConversion(-5, -5, 3845, 2165, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160), 0, 0, 3840, 2160, 0, 0,
             3840, 2160));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    bool none = true;
    none &= !PlaceConversion(0, 0, 3840, 2160, 10, 10, 10.5f, 500, 3840, 2160, 3840, 2160).valid; // no width
    none &= !PlaceConversion(0, 0, 0.5f, 2160, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160).valid;
    none &= !PlaceConversion(0, 0, 3840, 2160, 4000, 0, 5000, 2160, 3840, 2160, 3840, 2160).valid; // outside
    none &= !PlaceConversion(0, 0, 3840, 2160, -5000, 0, -4000, 2160, 3840, 2160, 3840, 2160).valid;
    none &= !PlaceConversion(0, 0, 3840, 2160, 0, 2161, 3840, 4000, 3840, 2160, 3840, 2160).valid;
    none &= !PlaceConversion(0, 0, 3840, 2160, 0, 0, 3840, 2160, 0, 2160, 3840, 2160).valid; // no picture
    none &= !PlaceConversion(0, 0, 3840, 2160, 0, 0, 3840, 2160, 3840, 2160, 3840, 0).valid; // no window
    none &= !PlaceConversion(3840, 2160, 0, 0, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160).valid; // inverted
    none &= !PlaceConversion(0, 0, 3840, 2160, 3840, 2160, 0, 0, 3840, 2160, 3840, 2160).valid;
    for (float bad : {nan, inf, -inf})
    {
      none &= !PlaceConversion(bad, 0, 3840, 2160, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160).valid;
      none &= !PlaceConversion(0, 0, bad, 2160, 0, 0, 3840, 2160, 3840, 2160, 3840, 2160).valid;
      none &= !PlaceConversion(0, 0, 3840, 2160, bad, 0, 3840, 2160, 3840, 2160, 3840, 2160).valid;
      none &= !PlaceConversion(0, 0, 3840, 2160, 0, 0, 3840, bad, 3840, 2160, 3840, 2160).valid;
    }
    Check("nothing to draw: no width, outside the window, no picture, no window, inverted, or not numbers", none);

    // whatever the rectangles, a place that is valid lies in the window and in the picture
    unsigned int seed = 7;
    const auto next = [&seed](float low, float high) {
      seed = seed * 1103515245u + 12345u;
      return low + (high - low) * static_cast<float>((seed >> 8) & 0xffff) / 65535.0f;
    };
    bool inside = true;
    unsigned int valid = 0;
    for (int i = 0; i < 200000; ++i)
    {
      const int pw = 16 + static_cast<int>(next(0, 4000)), ph = 16 + static_cast<int>(next(0, 2200));
      const int ww = 16 + static_cast<int>(next(0, 4000)), wh = 16 + static_cast<int>(next(0, 2200));
      const float sx = next(-50, static_cast<float>(pw)), sy = next(-50, static_cast<float>(ph));
      const float dx = next(-3000, 3000), dy = next(-2000, 2000);
      const ConversionPlace p = PlaceConversion(sx, sy, sx + next(0, 5000), sy + next(0, 3000), dx, dy,
                                                dx + next(0, 9000), dy + next(0, 6000), pw, ph, ww, wh);
      if (!p.valid)
        continue;
      ++valid;
      inside &= p.dstX >= 0 && p.dstY >= 0 && p.dstWidth >= 1 && p.dstHeight >= 1 && p.dstX <= ww - p.dstWidth &&
                p.dstY <= wh - p.dstHeight && p.srcX0 >= 0 && p.srcY0 >= 0 && p.srcX1 <= static_cast<float>(pw) &&
                p.srcY1 <= static_cast<float>(ph) && p.srcX1 - p.srcX0 >= 1 && p.srcY1 - p.srcY0 >= 1;
    }
    std::printf("      200000 random pairs of rectangles: %u give a place\n", valid);
    Check("every place that is given lies in the window, and its part in the picture", inside && valid > 20000);
  }

  // ---- what the player information says of the output
  {
    using I = CDVPlaybackInfo;
    Check("native output is named as before",
          I::Detail("dvhe.07.06", I::HDMI | I::FEL | I::CM4) == "dvhe.07.06 FEL, CM4, HDMI DV" &&
              I::Detail("dvhe.08.06", I::HDMI) == "dvhe.08.06, CM2.9, HDMI DV" && I::Detail("x", 0) == "x");
    Check("sent as HDR10: said so, with FEL when the residual was drawn, and once",
          I::Detail("dvhe.05.06", I::HDR10) == "dvhe.05.06, HDR10" &&
              I::Detail("dvhe.07.06", I::HDR10 | I::FEL) == "dvhe.07.06 FEL, HDR10" &&
              I::Detail("dvhe.07.06 FEL", I::HDR10 | I::FEL) == "dvhe.07.06 FEL, HDR10");
  }
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures != 0;
}
