// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: a Dolby Vision stream whose base layer is no picture by itself (profile 5,
// AV1 profile 10 compatible with nothing) is rebuilt wherever the output takes HDR10, whatever
// videoscreen.hadesdvashdr10 says (issue #2: profile 5 green and purple with that setting off).
// Built against DVPlaybackPolicy.h of a tree (-I): with the header of the code before the fix it fails, with the fix
// it passes. It names no value of ConversionSetting that the header before does not have: the setting "off"
// is what ConversionSettingOf(false, ...) gives.
//   A every combination of the settings (on/off, enhancement layer, mapping), the output (HDR10, SDR, none),
//     the choice of the video, profile and compatibility, the enhancement layer, software decoding,
//     interlaced, stereoscopic, the tone mapping method and HDR10+, against a model written from the rule
//     (not from the header): what changes from the rule before is only a stream whose base layer is no
//     picture by itself with the setting off, and native measured asked of a stream whose base layer is
//     not HDR10;
//   B the cases by name.
// usage: p5always_test
#include "DVPlaybackPolicy.h"

#include <cstdio>

using namespace DVBRIDGE;

static int g_failures, g_checks;
static void Check(bool ok, const char* what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

namespace
{
struct Case
{
  bool convert, fel, mapped, hdr10Out, sdrOut, reference;
  OutputChoice choice;
  int profile, compatibility;
  bool layer, sw, interlaced, stereo, plus;
};

bool NoPicture(const Case& c)
{
  return c.profile == 5 || (c.profile == 10 && c.compatibility == 0);
}
bool HdrBase(const Case& c)
{
  return (c.profile == 7 && c.compatibility == 6) || (c.profile == 8 && c.compatibility == 1);
}

// the rule, as the commit says it; before: the rule of the code before the fix
ConversionTarget Model(const Case& c, bool before)
{
  const bool kept = c.sw || c.interlaced || c.stereo;
  OutputChoice choice = c.choice;
  if (!before && choice == OutputChoice::MEASURED && !HdrBase(c))
    choice = OutputChoice::AUTOMATIC;
  if (c.hdr10Out)
  {
    if (kept || choice == OutputChoice::KODI || choice == OutputChoice::MEASURED)
      return ConversionTarget::NONE;
    if (choice == OutputChoice::HDR10)
      return ConversionTarget::HDR10;
    if (NoPicture(c) && (c.convert || !before))
      return ConversionTarget::HDR10;
    if (!c.convert)
      return ConversionTarget::NONE;
    if (c.mapped || (c.fel && c.profile == 7 && c.layer) ||
        (c.plus && c.profile == 8 && HdrBase(c) && !c.layer))
      return ConversionTarget::HDR10;
    return ConversionTarget::NONE;
  }
  if (c.sdrOut && !kept && c.choice != OutputChoice::KODI &&
      (NoPicture(c) || (c.reference && HdrBase(c))))
    return ConversionTarget::SDR;
  return ConversionTarget::NONE;
}

ConversionTarget Header(const Case& c)
{
  return ConversionTargetOf(c.hdr10Out, c.sdrOut, ConversionSettingOf(c.convert, c.fel, c.mapped),
                            c.reference, c.choice, c.profile, c.compatibility, c.layer, c.sw,
                            c.interlaced, c.stereo, c.plus);
}

Case Base(int profile, int compatibility, bool layer = false)
{
  Case c{};
  c.convert = false;
  c.hdr10Out = true;
  c.choice = OutputChoice::AUTOMATIC;
  c.profile = profile;
  c.compatibility = compatibility;
  c.layer = layer;
  return c;
}
} // namespace

int main()
{
  // A
  const OutputChoice choices[] = {OutputChoice::AUTOMATIC, OutputChoice::HDR10, OutputChoice::KODI,
                                  OutputChoice::MEASURED};
  const int profiles[] = {0, 4, 5, 7, 8, 9, 10};
  const int compatibilities[] = {0, 1, 2, 4, 6};
  long combinations = 0, wrong = 0, changed = 0, changedNoPictureOff = 0, changedMeasured = 0,
       changedOther = 0;
  for (int s = 0; s < 8; ++s)
    for (int out = 0; out < 3; ++out)
      for (const auto choice : choices)
        for (const int profile : profiles)
          for (const int compatibility : compatibilities)
            for (int flags = 0; flags < 64; ++flags)
            {
              Case c{};
              c.convert = s & 1;
              c.fel = s & 2;
              c.mapped = s & 4;
              c.hdr10Out = out == 1;
              c.sdrOut = out == 2;
              c.choice = choice;
              c.profile = profile;
              c.compatibility = compatibility;
              c.layer = flags & 1;
              c.sw = flags & 2;
              c.interlaced = flags & 4;
              c.stereo = flags & 8;
              c.reference = flags & 16;
              c.plus = flags & 32;
              ++combinations;
              const ConversionTarget got = Header(c);
              const ConversionTarget want = Model(c, false);
              if (got != want)
              {
                if (++wrong <= 8)
                  std::printf("  differs: convert %d fel %d mapped %d out %d choice %d profile %d.%d "
                              "layer %d sw %d interlaced %d stereo %d reference %d plus %d: got %d, "
                              "want %d\n",
                              c.convert, c.fel, c.mapped, out, static_cast<int>(choice), profile,
                              compatibility, c.layer, c.sw, c.interlaced, c.stereo, c.reference,
                              c.plus, static_cast<int>(got), static_cast<int>(want));
              }
              if (want != Model(c, true))
              {
                ++changed;
                if (NoPicture(c) && !c.convert)
                  ++changedNoPictureOff;
                else if (c.choice == OutputChoice::MEASURED && !HdrBase(c))
                  ++changedMeasured;
                else
                  ++changedOther;
              }
            }
  Check(wrong == 0, "A every combination as the rule says");
  Check(changedOther == 0, "A the model changes nothing but what the commit says");
  std::printf("     %ld combinations; %ld changed from the rule before: %ld with the setting off and a base "
              "layer that is no picture, %ld with native measured asked of a base layer that is not HDR10\n",
              combinations, changed, changedNoPictureOff, changedMeasured);

  // B
  Check(ConversionSettingOf(false, false, false) != ConversionSetting::NEVER,
        "B the setting off is not the choice of Kodi's own output (NEVER)");
  Check(Header(Base(5, 0)) == ConversionTarget::HDR10,
        "B profile 5, the setting off, an HDR10 output: rebuilt and sent as HDR10");
  {
    Case c = Base(5, 0);
    c.fel = true;
    c.mapped = true;
    Check(Header(c) == ConversionTarget::HDR10,
          "B profile 5, the setting off with its enhancement layer and mapping settings on: rebuilt");
  }
  Check(Header(Base(10, 0)) == ConversionTarget::HDR10,
        "B AV1 profile 10.0, the setting off: rebuilt and sent as HDR10");
  Check(Header(Base(10, 1)) == ConversionTarget::NONE, "B profile 10.1, the setting off: its base layer");
  Check(Header(Base(7, 6, true)) == ConversionTarget::NONE,
        "B profile 7 with its enhancement layer, the setting off: its base layer");
  {
    Case c = Base(7, 6, true);
    c.fel = true;
    Check(Header(c) == ConversionTarget::NONE,
          "B profile 7 FEL, the setting off and the layer's on (greyed out): its base layer");
    c.mapped = true;
    Check(Header(c) == ConversionTarget::NONE, "B profile 7 FEL, the setting off and mapping on: its base layer");
  }
  Check(Header(Base(8, 1)) == ConversionTarget::NONE, "B profile 8.1, the setting off: its base layer");
  {
    Case c = Base(8, 1);
    c.plus = true;
    Check(Header(c) == ConversionTarget::NONE,
          "B profile 8.1, the setting off, HDR10+ possible: its base layer, not rebuilt for HDR10+");
  }
  Check(Header(Base(8, 4)) == ConversionTarget::NONE, "B profile 8.4, the setting off: its base layer");
  {
    Case c = Base(5, 0);
    c.choice = OutputChoice::KODI;
    Check(Header(c) == ConversionTarget::NONE, "B profile 5 with Kodi's own output chosen: not rebuilt");
    c.hdr10Out = false;
    c.sdrOut = true;
    Check(Header(c) == ConversionTarget::NONE, "B ... nor on an SDR output, as before");
  }
  {
    Case c = Base(5, 0);
    c.convert = true;
    c.choice = OutputChoice::MEASURED;
    Check(Header(c) == ConversionTarget::HDR10,
          "B profile 5 with native measured asked (by another stream of the file): rebuilt as the settings say");
    c.convert = false;
    Check(Header(c) == ConversionTarget::HDR10, "B ... and with the setting off");
  }
  {
    Case c = Base(7, 6, true);
    c.convert = true;
    c.fel = true;
    c.choice = OutputChoice::MEASURED;
    Check(Header(c) == ConversionTarget::NONE,
          "B profile 7 FEL with native measured: played as its base layer, as before");
  }
  {
    Case c = Base(5, 0);
    c.hdr10Out = false;
    c.sdrOut = true;
    Check(Header(c) == ConversionTarget::SDR,
          "B profile 5, the setting off, an output without HDR10: mapped to SDR, as before");
    c.sdrOut = false;
    Check(Header(c) == ConversionTarget::NONE,
          "B profile 5, the setting off, neither HDR10 nor SDR rebuilding here: as Kodi plays it, as before");
  }
  for (int k = 0; k < 3; ++k)
  {
    Case c = Base(5, 0);
    (k == 0 ? c.sw : k == 1 ? c.interlaced : c.stereo) = true;
    Check(Header(c) == ConversionTarget::NONE,
          "B profile 5 decoded in software, interlaced or stereoscopic: never rebuilt, as before");
  }
  // the setting on: as before
  {
    Case c = Base(5, 0);
    c.convert = true;
    Check(Header(c) == ConversionTarget::HDR10, "B profile 5, the setting on: rebuilt, as before");
    Case d = Base(8, 1);
    d.convert = true;
    d.plus = true;
    Check(Header(d) == ConversionTarget::HDR10, "B profile 8.1, the setting on, HDR10+: rebuilt, as before");
  }
  std::printf("%s: %d of %d\n", g_failures ? "FAILED" : "ALL PASSED", g_checks - g_failures, g_checks);
  return g_failures != 0;
}
