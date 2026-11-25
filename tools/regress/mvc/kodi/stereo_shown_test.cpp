// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// What the player information says of the 3D output (xbmc/windowing/gbm/drm/DRMStereo.h, STEREO::ShownOf()) and the
// raster it gives a frame-packed mode (STEREO::ScanoutHeight()): what goes out, not what was asked for.
// Prints one line per check, "FAIL ..." for each that fails, and "STEREO-SHOWN <n> checks, <f> failed".
#include "DRMStereo.h"

#include <cstdio>

using namespace KODI::WINDOWING::GBM::STEREO;

namespace
{
int checks = 0;
int failed = 0;

const char* Name(Shown s)
{
  switch (s)
  {
    case Shown::NONE:
      return "none";
    case Shown::FRAME_PACKING:
      return "frame packing";
    case Shown::TOP_AND_BOTTOM:
      return "top-and-bottom";
    case Shown::SIDE_BY_SIDE_HALF:
      return "side-by-side";
    case Shown::OTHER:
      return "other";
    case Shown::OVER_UNDER_UNTOLD:
      return "over/under, not told";
    case Shown::SIDE_BY_SIDE_UNTOLD:
      return "side by side, not told";
  }
  return "?";
}

void Expect(const char* what, Shown got, Shown want)
{
  ++checks;
  if (got != want)
  {
    ++failed;
    std::printf("FAIL %s: %s (want %s)\n", what, Name(got), Name(want));
  }
  else
    std::printf("ok %s: %s\n", what, Name(got));
}

void ExpectInt(const char* what, int got, int want)
{
  ++checks;
  if (got != want)
  {
    ++failed;
    std::printf("FAIL %s: %d (want %d)\n", what, got, want);
  }
  else
    std::printf("ok %s: %d\n", what, got);
}
} // namespace

int main()
{
  // the mode the output is in comes first, whatever the InfoFrame and the drawing
  Expect("in a frame-packed mode", ShownOf(Layout::FRAME_PACKING, Layout::NONE, true, true),
         Shown::FRAME_PACKING);
  Expect("in a frame-packed mode, an InfoFrame of before still there",
         ShownOf(Layout::FRAME_PACKING, Layout::SIDE_BY_SIDE_HALF, true, true), Shown::FRAME_PACKING);
  Expect("in a frame-packed mode while the eyes stop being split",
         ShownOf(Layout::FRAME_PACKING, Layout::NONE, false, false), Shown::FRAME_PACKING);
  Expect("in a top-and-bottom mode", ShownOf(Layout::TOP_AND_BOTTOM, Layout::NONE, true, true),
         Shown::TOP_AND_BOTTOM);
  Expect("in a side-by-side (half) mode", ShownOf(Layout::SIDE_BY_SIDE_HALF, Layout::NONE, true, false),
         Shown::SIDE_BY_SIDE_HALF);
  Expect("in a mode of another layout", ShownOf(Layout::OTHER, Layout::NONE, true, true), Shown::OTHER);
  // a 2D mode: the vendor InfoFrame that the driver has from Kodi
  Expect("2D mode, InfoFrame top-and-bottom", ShownOf(Layout::NONE, Layout::TOP_AND_BOTTOM, true, true),
         Shown::TOP_AND_BOTTOM);
  Expect("2D mode, InfoFrame side-by-side", ShownOf(Layout::NONE, Layout::SIDE_BY_SIDE_HALF, true, false),
         Shown::SIDE_BY_SIDE_HALF);
  // a 2D mode and nothing told: what the driver left after it refused frame packing (issue 33)
  Expect("frame packing refused: 2D mode, eyes over/under, nothing told",
         ShownOf(Layout::NONE, Layout::NONE, true, true), Shown::OVER_UNDER_UNTOLD);
  Expect("2D mode, eyes side by side, nothing told", ShownOf(Layout::NONE, Layout::NONE, true, false),
         Shown::SIDE_BY_SIDE_UNTOLD);
  Expect("2D", ShownOf(Layout::NONE, Layout::NONE, false, false), Shown::NONE);
  Expect("2D, the halves not drawn split but said to be over/under",
         ShownOf(Layout::NONE, Layout::NONE, false, true), Shown::NONE);

  // the raster of a frame-packed mode: both eyes and the lines between them, as the driver scans it out
  Mode p24; // CTA-861 1080p24
  p24.clock = 74250;
  p24.hdisplay = 1920;
  p24.hsyncStart = 2558;
  p24.hsyncEnd = 2602;
  p24.htotal = 2750;
  p24.vdisplay = 1080;
  p24.vsyncStart = 1084;
  p24.vsyncEnd = 1089;
  p24.vtotal = 1125;
  Mode fp24 = p24;
  fp24.flags = FLAG_3D_FRAME_PACKING;
  ExpectInt("1080p24 frame packing: lines", ScanoutHeight(fp24), 2205);
  ExpectInt("1080p24 in 2D: lines", ScanoutHeight(p24), 1080);
  Mode p60; // CTA-861 720p60
  p60.clock = 74250;
  p60.hdisplay = 1280;
  p60.htotal = 1650;
  p60.vdisplay = 720;
  p60.vtotal = 750;
  p60.flags = FLAG_3D_FRAME_PACKING;
  ExpectInt("720p60 frame packing: lines", ScanoutHeight(p60), 1470);
  Mode tab = p24;
  tab.flags = FLAG_3D_TOP_AND_BOTTOM;
  ExpectInt("1080p24 top-and-bottom: lines", ScanoutHeight(tab), 1080);

  std::printf("STEREO-SHOWN %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
