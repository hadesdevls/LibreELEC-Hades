// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (issue #34): a 3D video gone back to 2D, and the Dolby Vision bridge.
//
// Where the processor cannot keep up with both views of an MVC or MV-HEVC video (or the decoder of both cannot be had),
// the stream is opened again for its base view alone, and its pictures say "mono" (KODI::MULTIVIEW::
// BASE_ONLY_STEREO_MODE, CDVDVideoCodecFFmpeg::GetPicture()). That picture must be decided by the bridge as the same
// picture of the stream played in 2D from its start, whose stereo mode is empty: a Dolby Vision 3D film of profile 20
// (base layer IPT-PQ-c2) is rebuilt in 2D as in 3D, never shown as its base layer (green and purple).
//
// The test takes the readers of the stereo mode of a picture on its way to the bridge, from the tree (KODI_TREE):
//   - KODI::UTILS::STEREO3D::IsStereo(), PackingOf() and ViewSize() (Stereo3DVideo.h), and Kodi's
//     GetFlagsStereoMode() (RenderFlags.cpp, compiled with the test: how the renderer cuts the picture), give the
//     same answers for the picture gone back to 2D as for the one in 2D from the start, and the answers of before for
//     every picture of a 3D layout;
//   - every read of a picture's stereo mode in the decisions of the bridge (WinSystemGbmGLESContext.cpp,
//     WinSystemGbmGLESStereo.cpp) and in the renderer (LinuxRendererGLES.cpp) goes through one of those, except
//     native Dolby Vision and the HDR10 tunnel, which keep a 3D output out by any stereo mode, as designed; so every
//     decision of the bridge is the same for both pictures;
//   - the decoder marks the base view alone so.
// -DOLD_RULE: the rule before (any stereo mode not empty is a 3D picture), which must fail.
// Prints "ok:"/"FAIL:" lines and "<n> failed".
#include "cores/VideoPlayer/MultiviewChoice.h"
#include "cores/VideoPlayer/VideoRenderers/RenderFlags.h"
#include "utils/Stereo3DVideo.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace S = KODI::UTILS::STEREO3D;

static int g_failures = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = {})
{
  std::printf("%s %s%s%s\n", ok ? "ok:  " : "FAIL:", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
  if (!ok)
    ++g_failures;
}

#ifdef OLD_RULE
static bool IsStereo(const std::string& mode)
{
  return !mode.empty();
}
#else
static bool IsStereo(const std::string& mode)
{
  return S::IsStereo(mode);
}
#endif

static std::string Read(const std::string& path)
{
  std::ifstream f(std::string(KODI_TREE) + "/" + path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// the lines of a text that name a picture's stereo mode
static std::vector<std::string> Reads(const std::string& text)
{
  std::vector<std::string> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    if (line.find("picture->stereoMode") != std::string::npos || line.find("picture.stereoMode") != std::string::npos)
      lines.push_back(line);
  return lines;
}

int main()
{
  const std::string gone = KODI::MULTIVIEW::BASE_ONLY_STEREO_MODE; // the base view alone, back in 2D
  const std::string flat; // the stream in 2D from its start (a file without a stereo mode)

  // --- the readers of the stereo mode
  Check(!IsStereo(gone) && !IsStereo(flat) && gone == "mono",
        "a picture gone back to 2D (\"mono\") is no 3D picture, as one without a stereo mode");
  Check(S::PackingOf(gone) == S::PackingOf(flat) && S::PackingOf(gone) == S::Packing::NONE,
        "... nor packed: the bridge does not cut it into views");
  Check(S::ViewSize(S::PackingOf(gone), 3840, 2160).width == 3840 &&
            S::ViewSize(S::PackingOf(gone), 3840, 2160).height == 2160,
        "... and its view is the whole picture, at its own size (3840x2160: one view, not two side by side)");
  Check(GetFlagsStereoMode(gone) == GetFlagsStereoMode(flat) && GetFlagsStereoMode(gone) == 0u,
        "Kodi's renderer draws it whole, as the picture in 2D from the start (GetFlagsStereoMode(): no stereo flags)");
  bool layouts = true;
  for (const char* m : {"left_right", "right_left", "top_bottom", "bottom_top", "block_lr", "block_rl",
                        "row_interleaved_lr", "col_interleaved_rl", "checkerboard_lr", "anaglyph_cyan_red",
                        "anaglyph_green_magenta"})
    layouts = layouts && IsStereo(m);
  Check(layouts && IsStereo(KODI::MULTIVIEW::PACKED_STEREO_MODE),
        "every picture of a 3D layout is a 3D picture as before (MVC and MV-HEVC side by side among them)");

  // --- the decisions of the bridge: every read of a picture's stereo mode goes through those readers
  const std::string ctx = Read("xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  const std::string ster = Read("xbmc/windowing/gbm/WinSystemGbmGLESStereo.cpp");
  const std::string ren = Read("xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.cpp");
  int through = 0, kept = 0;
  std::string other;
  for (const std::string& text : {ctx, ster, ren})
    for (const std::string& l : Reads(text))
    {
      if (l.find("KODI::UTILS::STEREO3D::IsStereo(picture") != std::string::npos ||
          l.find("PackingOf(picture") != std::string::npos ||
          l.find("GetFlagsStereoMode(picture.stereoMode)") != std::string::npos ||
          l.find("!picture.stereoMode.empty() && picture.stereoMode != \"mono\"") != std::string::npos ||
          l.find("picture.iHeight, picture.stereoMode,") != std::string::npos)
        ++through;
      else if (l.find("!picture->stereoMode.empty(), anyMode,") != std::string::npos ||
               l.find("(picture.iFlags & DVP_FLAG_INTERLACED) != 0, !picture.stereoMode.empty());") !=
                   std::string::npos)
        ++kept;
      else
        other += (other.empty() ? "" : " | ") + l.substr(l.find_first_not_of(' '));
    }
  Check(other.empty() && through >= 12,
        "every read of a picture's stereo mode on its way to the bridge goes through IsStereo(), PackingOf() or the "
        "renderer's flags",
        std::to_string(through) + " reads" + (other.empty() ? std::string() : "; not so: " + other));
  Check(kept == 2, "native Dolby Vision and the HDR10 tunnel keep a 3D output out by any stereo mode, as designed",
        std::to_string(kept) + " of 2");
  // (the readers named above are those of the decisions: WantsConversion() twice, WantsPlainMapping(), HdrAsSdr(),
  // Hdr10AdaptDecide(), StereoPictureTaken())
  Check(ctx.find("(!KODI::UTILS::STEREO3D::IsStereo(picture->stereoMode) ||\n          "
                 "window->StereoPictureTaken(*picture, !picture->doviBaseIsPicture)) &&") != std::string::npos,
        "WantsConversion(): a picture that is no 3D picture is rebuilt as in 2D; a 3D one where the bridge draws it a "
        "view at a time");
  Check(ctx.find("(KODI::UTILS::STEREO3D::IsStereo(picture->stereoMode) &&\n                    "
                 "KODI::UTILS::STEREO3D::ChoosesSdr(window->Stereo3DChoice()))") != std::string::npos,
        "WantsConversion(): the choice of SDR for 3D video only for a 3D picture");
  Check(ctx.find("KODI::UTILS::STEREO3D::IsStereo(picture->stereoMode) &&\n          "
                 "!window->StereoPictureTaken(*picture, false));") != std::string::npos,
        "WantsPlainMapping(): a picture that is no 3D picture is mapped as in 2D");
  Check(ctx.find("p.stereo = KODI::UTILS::STEREO3D::IsStereo(picture.stereoMode);") != std::string::npos &&
            ctx.find("if ((pq || hlg) && KODI::UTILS::STEREO3D::IsStereo(picture.stereoMode) && HdrAsSdrChoosable())") !=
                std::string::npos,
        "Hdr10AdaptDecide() and HdrAsSdr(): the same rule");
  Check(ster.find("const S::Packing packing = S::PackingOf(picture.stereoMode);") != std::string::npos,
        "StereoPictureTaken(): by the packing of the picture");

  // --- the decoder: the base view alone says so
  const std::string dec = Read("xbmc/cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecFFmpeg.cpp");
  Check(dec.find("if (result == VC_PICTURE && m_multiviewFailed && m_hints.multiview)\n    "
                 "pVideoPicture->stereoMode = KODI::MULTIVIEW::BASE_ONLY_STEREO_MODE;") != std::string::npos,
        "CDVDVideoCodecFFmpeg: the pictures of the base view alone of a stream of two views say BASE_ONLY_STEREO_MODE");

  std::printf("%d failed\n", g_failures);
  return g_failures ? 1 : 0;
}
