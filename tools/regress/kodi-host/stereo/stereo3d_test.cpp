// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: a 3D video and the Dolby Vision bridge (xbmc/utils/Stereo3DVideo.h of the tree): how a
// 3D video goes out (videoscreen.hades3dcolour), which 3D pictures the bridge draws (where the eyes are split, every
// picture, as the same picture in 2D, so that the rules of 2D decide how it is mapped), the views of a packed picture cut at the seam and placed in the eye Kodi draws, the timing
// of the views of a frame, the gamut of SDR in 3D (SdrWide.h: only a 3D mode that puts both eyes into one picture
// keeps BT.709), Dolby Vision profile 20 in 3D (MultiviewChoice.h: KeptToOneView()), and the callers in the tree.
// -DOLD_RULE: the rules before (no 3D picture drawn by the bridge, profile 20 always one view, every 3D mode BT.709),
// which must fail.
//
// Prints "ok:"/"FAIL:" lines and "<n> failed".
#include "cores/VideoPlayer/MultiviewChoice.h"
#include "utils/SdrWide.h"
#include "utils/Stereo3DVideo.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace S = KODI::UTILS::STEREO3D;
namespace W = KODI::UTILS::SDRWIDE;
using M = RenderStereoMode;
using V = RenderStereoView;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

static bool Near(float a, float b)
{
  return std::fabs(a - b) < 1e-3f;
}

#ifdef OLD_RULE
// the rules before: no 3D picture drawn by the bridge, profile 20 kept to one view, every 3D mode in BT.709
static bool Takes(bool, S::Choice, int, M)
{
  return false;
}
static bool Kept(int profile, int compatibility, bool)
{
  return profile == 20 && compatibility == 0;
}
static bool StereoRefused(M mode)
{
  return mode != M::OFF;
}
#else
static bool Takes(bool noPicture, S::Choice, int, M mode)
{
  // (the choice for 3D and the gamut of SDR decide what the picture goes out as, not who draws it)
  return S::BridgeTakes(noPicture, mode);
}
static bool Kept(int profile, int compatibility, bool rebuilt)
{
  return KODI::MULTIVIEW::KeptToOneView(profile, compatibility, rebuilt);
}
static bool StereoRefused(M mode)
{
  return S::MixesEyes(mode);
}
#endif

static std::string Read(const std::string& path)
{
  std::ifstream f(std::string(KODI_TREE) + "/" + path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

static const M MODES[] = {M::OFF,
                          M::SPLIT_HORIZONTAL,
                          M::SPLIT_VERTICAL,
                          M::ANAGLYPH_RED_CYAN,
                          M::ANAGLYPH_GREEN_MAGENTA,
                          M::ANAGLYPH_YELLOW_BLUE,
                          M::INTERLACED,
                          M::CHECKERBOARD,
                          M::HARDWAREBASED,
                          M::MONO};

int main()
{
  // --- the setting
  Check(S::ChoiceOfSetting(0) == S::Choice::AUTOMATIC && S::ChoiceOfSetting(1) == S::Choice::HDR &&
            S::ChoiceOfSetting(2) == S::Choice::SDR_BT2020 && S::ChoiceOfSetting(3) == S::Choice::SDR_BT709 &&
            S::ChoiceOfSetting(7) == S::Choice::AUTOMATIC && S::ChoiceOfSetting(-1) == S::Choice::AUTOMATIC,
        "the values of videoscreen.hades3dcolour (anything else: Automatic)");
  Check(!S::AsSdr(S::Choice::AUTOMATIC, false) && S::AsSdr(S::Choice::AUTOMATIC, true) &&
            !S::AsSdr(S::Choice::HDR, true) && S::AsSdr(S::Choice::SDR_BT2020, false) &&
            S::AsSdr(S::Choice::SDR_BT709, false),
        "SDR or HDR: Automatic as \"Play HDR videos as SDR\" says; HDR over it; the SDR choices SDR");
  bool containers = true;
  for (int g = 0; g < 3; ++g)
    containers = containers && S::Container(S::Choice::AUTOMATIC, g) == g &&
                 S::Container(S::Choice::HDR, g) == g && S::Container(S::Choice::SDR_BT2020, g) == 1 &&
                 S::Container(S::Choice::SDR_BT709, g) == 0;
  Check(containers, "the gamut of SDR: the setting's for Automatic and HDR, BT.2020 and BT.709 as chosen");

  // --- which 3D pictures the bridge draws
  bool defaultSplit = true, noPictureAll = true, sdrSplit = true, mixedNone = true;
  for (const M mode : MODES)
  {
    noPictureAll = noPictureAll && Takes(true, S::Choice::AUTOMATIC, 0, mode);
    const bool mixes = mode == M::ANAGLYPH_RED_CYAN || mode == M::ANAGLYPH_GREEN_MAGENTA ||
                       mode == M::ANAGLYPH_YELLOW_BLUE || mode == M::INTERLACED || mode == M::CHECKERBOARD ||
                       mode == M::HARDWAREBASED;
    for (const S::Choice c : {S::Choice::SDR_BT2020, S::Choice::SDR_BT709})
      if (mixes)
        mixedNone = mixedNone && !Takes(false, c, S::Container(c, 0), mode);
      else
        sdrSplit = sdrSplit && Takes(false, c, S::Container(c, 0), mode);
    if (mixes)
      mixedNone = mixedNone && !Takes(false, S::Choice::AUTOMATIC, 1, mode) &&
                  !Takes(false, S::Choice::AUTOMATIC, 0, mode) && !Takes(false, S::Choice::HDR, 0, mode);
    else
    {
      sdrSplit = sdrSplit && Takes(false, S::Choice::AUTOMATIC, 1, mode) && Takes(false, S::Choice::AUTOMATIC, 2, mode);
      defaultSplit = defaultSplit && Takes(false, S::Choice::AUTOMATIC, 0, mode) && Takes(false, S::Choice::HDR, 0, mode);
    }
  }
  Check(defaultSplit, "the default (Automatic, BT.709) and HDR: a 3D picture is drawn by the bridge where the eyes are "
                      "split, as the same picture in 2D, so that the rules of 2D decide how it goes out (HDR passed "
                      "through; SDR by the method of the video, the reference method of the bridge by default): 3D is "
                      "mapped as 2D is");
  Check(noPictureAll, "a 3D picture whose base layer is no picture by itself (profile 20) is drawn by the bridge in "
                      "every way of showing 3D");
  Check(sdrSplit, "SDR chosen for 3D, or a gamut wider than BT.709: drawn by the bridge where the eyes are split, in 2D "
                  "of a 3D video and without a 3D mode");
  Check(mixedNone, "... and not where the eyes are put into one picture (anaglyph, interlaced, checkerboard): Kodi's own "
                   "way, which draws through the masks");

  // --- the packing and the views
  Check(S::PackingOf("left_right") == S::Packing::SIDE_BY_SIDE && S::PackingOf("right_left") == S::Packing::SIDE_BY_SIDE &&
            S::PackingOf("top_bottom") == S::Packing::TOP_AND_BOTTOM &&
            S::PackingOf("bottom_top") == S::Packing::TOP_AND_BOTTOM && S::PackingOf("") == S::Packing::NONE &&
            S::PackingOf("mono") == S::Packing::NONE && S::PackingOf("block_lr") == S::Packing::NONE &&
            S::PackingOf("row_interleaved_lr") == S::Packing::NONE &&
            S::PackingOf(KODI::MULTIVIEW::PACKED_STEREO_MODE) == S::Packing::SIDE_BY_SIDE,
        "the packing of a stereo mode as Kodi's renderer cuts it (GetFlagsStereoMode()); MVC and MV-HEVC side by side");
  Check(S::ViewsFit(S::Packing::SIDE_BY_SIDE, 7680, 2160, 4096, 2304) &&
            S::ViewsFit(S::Packing::TOP_AND_BOTTOM, 3840, 4320, 4096, 2304) &&
            S::ViewsFit(S::Packing::SIDE_BY_SIDE, 3840, 1080, 4096, 2304) &&
            !S::ViewsFit(S::Packing::SIDE_BY_SIDE, 8200, 2160, 4096, 2304) &&
            !S::ViewsFit(S::Packing::SIDE_BY_SIDE, 3842, 2160, 4096, 2304) &&
            !S::ViewsFit(S::Packing::TOP_AND_BOTTOM, 1920, 2162, 4096, 2304) &&
            !S::ViewsFit(S::Packing::NONE, 1920, 1080, 4096, 2304),
        "the views fit: two 3840x2160 views side by side or one above the other; not views of odd sizes nor larger");

  S::Part p[2];
  // left eye of a picture of two 3840x2160 views, into an eye of 1920x2160 (side by side (half) of 2160p)
  int n = S::Parts(S::Packing::SIDE_BY_SIDE, 7680, 2160, {0, 0, 3840, 2160}, {0, 0, 1920, 2160}, p);
  Check(n == 1 && p[0].view == 0 && p[0].x == 0 && p[0].size.width == 3840 && p[0].size.height == 2160 &&
            Near(p[0].source.x2, 3840) && Near(p[0].dest.x2, 1920),
        "the left eye: the left view whole, into the eye");
  n = S::Parts(S::Packing::SIDE_BY_SIDE, 7680, 2160, {3840, 0, 7680, 2160}, {0, 0, 1920, 2160}, p);
  Check(n == 1 && p[0].view == 1 && p[0].x == 3840 && Near(p[0].source.x1, 0) && Near(p[0].source.x2, 3840) &&
            Near(p[0].dest.x1, 0) && Near(p[0].dest.x2, 1920),
        "the right eye: the right view, in its own pixels");
  n = S::Parts(S::Packing::SIDE_BY_SIDE, 7680, 2160, {0, 0, 7680, 2160}, {0, 270, 3840, 1350}, p);
  Check(n == 2 && p[0].view == 0 && p[1].view == 1 && Near(p[0].dest.x2, 1920) && Near(p[1].dest.x1, 1920) &&
            Near(p[1].dest.x2, 3840) && Near(p[0].dest.y1, 270) && Near(p[1].dest.y2, 1350),
        "the whole picture (no 3D way of showing it): both views, each into its half of the destination");
  n = S::Parts(S::Packing::SIDE_BY_SIDE, 7680, 2160, {4000, 100, 7000, 2000}, {0, 0, 1920, 2160}, p);
  Check(n == 1 && p[0].view == 1 && Near(p[0].source.x1, 160) && Near(p[0].source.x2, 3160) &&
            Near(p[0].source.y1, 100),
        "a zoom within the right eye's half: the part of the right view, in its pixels");
  n = S::Parts(S::Packing::TOP_AND_BOTTOM, 1920, 2160, {0, 1080, 1920, 2160}, {0, 0, 1920, 1080}, p);
  Check(n == 1 && p[0].view == 1 && p[0].y == 1080 && p[0].size.height == 1080 && Near(p[0].source.y1, 0) &&
            Near(p[0].source.y2, 1080),
        "top and bottom: the bottom view");
  n = S::Parts(S::Packing::TOP_AND_BOTTOM, 1920, 2160, {0, 0, 1920, 2160}, {0, 0, 1920, 1080}, p);
  Check(n == 2 && Near(p[0].dest.y2, 540) && Near(p[1].dest.y1, 540),
        "top and bottom shown whole: each view into its half");
  Check(S::Parts(S::Packing::NONE, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, p) == 0 &&
            S::Parts(S::Packing::SIDE_BY_SIDE, 7680, 2160, {0, 0, 0, 2160}, {0, 0, 1920, 2160}, p) == 0,
        "nothing to draw: not packed, or an empty source");

  // --- the place of a view in the window
  {
    S::Rect src{0, 0, 3840, 2160}, dst{0, 0, 1920, 2160};
    Check(S::EyePlace(src, dst, 1920, 2160, 1920, 0) && Near(dst.x1, 1920) && Near(dst.x2, 3840) &&
              Near(src.x2, 3840),
          "side by side (half): the right eye moved to its place in the window");
    S::Rect src2{0, 0, 3840, 2160}, dst2{0, 0, 1920, 1080};
    Check(S::EyePlace(src2, dst2, 1920, 1080, 0, 1125) && Near(dst2.y1, 1125) && Near(dst2.y2, 2205),
          "frame packing: the right eye below the 45 lines between the eyes");
    S::Rect src3{0, 0, 3840, 2160}, dst3{-480, 0, 2400, 2160};
    Check(S::EyePlace(src3, dst3, 1920, 2160, 1920, 0) && Near(dst3.x1, 1920) && Near(dst3.x2, 3840) &&
              Near(src3.x1, 640) && Near(src3.x2, 3200),
          "a zoom past the eye: cut at the eye's edges, not into the other eye, the source by the same part");
    S::Rect src4{0, 0, 100, 100}, dst4{2000, 0, 2100, 100};
    Check(!S::EyePlace(src4, dst4, 1920, 2160, 0, 0), "nothing of it within the eye: nothing to draw");
  }

  // --- the timing of the views of a frame
  Check(S::RoleOf(M::SPLIT_VERTICAL, V::LEFT, 0, 1) == S::Role::FIRST &&
            S::RoleOf(M::SPLIT_VERTICAL, V::RIGHT, 0, 1) == S::Role::LAST &&
            S::RoleOf(M::SPLIT_HORIZONTAL, V::LEFT, 0, 1) == S::Role::FIRST &&
            S::RoleOf(M::SPLIT_HORIZONTAL, V::RIGHT, 0, 1) == S::Role::LAST &&
            S::RoleOf(M::MONO, V::LEFT, 0, 1) == S::Role::ALONE &&
            S::RoleOf(M::OFF, V::OFF, 0, 2) == S::Role::FIRST && S::RoleOf(M::OFF, V::OFF, 1, 2) == S::Role::LAST &&
            S::RoleOf(M::ANAGLYPH_RED_CYAN, V::LEFT, 0, 1) == S::Role::ALONE &&
            S::RoleOf(M::ANAGLYPH_RED_CYAN, V::RIGHT, 0, 1) == S::Role::SKIPPED &&
            S::RoleOf(M::INTERLACED, V::RIGHT, 0, 1) == S::Role::SKIPPED &&
            S::RoleOf(M::SPLIT_VERTICAL, V::LEFT, 0, 2) == S::Role::FIRST &&
            S::RoleOf(M::SPLIT_VERTICAL, V::LEFT, 1, 2) == S::Role::MIDDLE,
        "the views of a frame timed together: the left eye begins, the right ends; one draw alone; the right eye "
        "skipped where the eyes are mixed");
  Check(S::TimerBegins(S::Role::NONE) && S::TimerEnds(S::Role::NONE) && S::TimerBegins(S::Role::FIRST) &&
            !S::TimerEnds(S::Role::FIRST) && !S::TimerBegins(S::Role::LAST) && S::TimerEnds(S::Role::LAST) &&
            !S::TimerBegins(S::Role::MIDDLE) && !S::TimerEnds(S::Role::MIDDLE),
        "a 2D picture timed as before");

  // --- SDR in BT.2020 or DCI-P3 in 3D (SdrWide.h): the stereo of its facts is a mode that mixes the eyes
  {
    bool ok = true;
    for (const M mode : MODES)
    {
      W::Facts f;
      f.setting = true;
      f.mapped = true;
      f.container = W::Container::BT2020;
      f.display = f.driver = true;
      f.stereo = StereoRefused(mode);
      const bool mixes = S::MixesEyes(mode);
      ok = ok && (W::Check(f) == (mixes ? W::Why::STEREO : W::Why::NONE));
    }
    Check(ok, "SDR in BT.2020 in 3D: in a split 3D mode, in 2D of a 3D video and without a 3D mode it goes out; only "
              "where both eyes are put into one picture it stays BT.709");
  }

  // --- Dolby Vision profile 20 in 3D
  Check(!Kept(20, 0, true) && Kept(20, 0, false) && !Kept(20, 4, false) && !Kept(5, 0, false) &&
            !Kept(8, 1, true),
        "profile 20 with an IPT-PQ-c2 base layer: both views where the bridge rebuilds 3D, one view elsewhere; "
        "profile 20.4 (HLG) and other profiles never kept");
  {
    KODI::MULTIVIEW::Track t;
    t.hevc = true;
    t.allowed = true;
    t.decoder = true;
    t.dvProfile = 20;
    t.dvCompatibility = 0;
    t.rebuiltIn3D = false;
    const bool keptElsewhere = !KODI::MULTIVIEW::TwoViews(t).has_value();
    Check(keptElsewhere, "TwoViews(): profile 20.0 where the bridge cannot rebuild 3D: not two views");
  }

  // --- the callers in the tree
  const std::string ctx = Read("xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  const std::string ren = Read("xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.cpp");
  const std::string vaapi = Read("xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender/RendererVAAPIGLES.cpp");
  const std::string dmx = Read("xbmc/cores/VideoPlayer/DVDDemuxers/DVDDemuxFFmpeg.cpp");
  const std::string ster = Read("xbmc/windowing/gbm/WinSystemGbmGLESStereo.cpp");
  const std::string mv = Read("xbmc/cores/VideoPlayer/MultiviewChoice.h");
  Check(ctx.find("f.stereo = KODI::UTILS::STEREO3D::MixesEyes(") != std::string::npos,
        "SdrWideFacts(): the stereo of the facts is a mode that mixes the eyes");
  Check(ren.find("else if (KODI::UTILS::STEREO3D::MixesEyes(") != std::string::npos,
        "CheckVideoParameters(): SDR in a wide gamut ends only in a mode that mixes the eyes");
  Check(ctx.find("window->StereoPictureTaken(*picture, !picture->doviBaseIsPicture)") != std::string::npos &&
            ctx.find("!picture->stereoMode.empty() && !window->StereoPictureTaken(*picture, false)") != std::string::npos,
        "WantsConversion() and WantsPlainMapping(): a 3D picture where the bridge takes it");
  Check(ctx.find("stream.interlaced, stereoKept,") != std::string::npos,
        "ConversionTargetOf(): a 3D stream decoded for the bridge where it takes it");
  Check(ctx.find("if (layer.stereo != 0 && m_stereoRole == KODI::UTILS::STEREO3D::Role::NONE)\n    return "
                 "RenderStereoParts(") != std::string::npos &&
            ctx.find("{ return RenderPlainMapping(view, plain, from, to, serial); }") != std::string::npos,
        "RenderDVConversion() and RenderPlainMapping(): a 3D picture a view at a time");
  Check(ctx.find("(!reconstructed &&\n       (m_stereoRole != KODI::UTILS::STEREO3D::Role::NONE ||") != std::string::npos &&
            ctx.find("(mapped || m_stereoRole == KODI::UTILS::STEREO3D::Role::NONE) &&") != std::string::npos,
        "a view of a 3D picture that could not be drawn is never replaced by the picture before (the other eye)");
  Check(ster.find("dvbridge_gl_part_copy(&m_stereoParts[parts[i].view]") != std::string::npos &&
            ster.find("S::EyePlace(from, to,") != std::string::npos &&
            ster.find("gfx.StereoCorrection(CPoint(0.0f, 0.0f))") != std::string::npos,
        "RenderStereoParts(): each view copied out and placed in the eye Kodi draws");
  Check(ren.find("layer.stereo = StereoPacking();") != std::string::npos &&
            vaapi.find("layer.stereo = StereoPacking();") != std::string::npos,
        "both renderers say the packing of a 3D picture to the bridge");
  Check(dmx.find("track.rebuiltIn3D = winSystem && winSystem->RebuildsDolbyVisionIn3D();") != std::string::npos &&
            mv.find("KeptToOneView(track.dvProfile, track.dvCompatibility, track.rebuiltIn3D)") != std::string::npos,
        "the demuxer asks whether profile 20 can be rebuilt in 3D here");

  std::printf("%d failed\n", g_failures);
  return g_failures ? 1 : 0;
}
