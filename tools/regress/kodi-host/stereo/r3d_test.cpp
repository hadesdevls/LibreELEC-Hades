// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// r3d_test.cpp (1.2.3: the reports of NTM-3D and Ryudoadema on 1.2.2): what the tree under test decides, and where it
// decides it in the source, for
//  - the pixel aspect ratio that libass is given in a 3D output (SubtitlesStyle.h, LibassPixelAspect()): 1 / Kodi's
//    pixel ratio (its rendering with the libass of the build: libass_par_test.cpp);
//  - a bitmap subtitle kept to its shape on the screen (OverlayShape.h, FitShownScale()): fitted into each eye as the
//    display shows it, the shape of its canvas kept, for 2D, side by side and top and bottom, at 1080p and 2160p, and
//    where OverlayRenderer.cpp takes it;
//  - a 3D playback that stops: the change of resolution of the menus forced (GraphicContext.cpp): the same resolution
//    as the video's left the view at the size of an eye;
//  - a half 3D layout where the display takes it only at fewer lines (DRMStereo.h, Allowed::fewerLines): at Automatic
//    1080p told, at "Automatic, keep 2160p" 2160p untold; the display's own 2160p first; where WinSystemGbm.cpp reads
//    the setting, the setting and its strings;
//  - whether a source is what a buffering waited for (SourceRate.h, SourceLimited()), the counts of the file cache
//    (FileCache.cpp) and where the player takes them (VideoPlayer.cpp).
// Prints a FAIL line for each failed check, then "<n> checks" and "<n> failed".
#include "cores/VideoPlayer/DVDSubtitles/SubtitlesStyle.h"
#include "cores/VideoPlayer/SourceRate.h"
#include "cores/VideoPlayer/VideoRenderers/OverlayShape.h"
#include "windowing/gbm/drm/DRMStereo.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what, const std::string& detail = {})
{
  ++checks;
  if (!ok)
  {
    ++failures;
    std::printf("FAIL: %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
  }
}

std::string Read(const std::string& rel)
{
  std::ifstream in(std::string(KODI_TREE) + "/" + rel);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

bool Has(const std::string& text, const std::string& what)
{
  return text.find(what) != std::string::npos;
}

std::string Body(const std::string& text, const std::string& signature)
{
  const size_t at = text.find(signature);
  if (at == std::string::npos)
    return {};
  const size_t open = text.find('{', at);
  int depth = 0;
  for (size_t i = open; i < text.size(); ++i)
  {
    if (text[i] == '{')
      ++depth;
    else if (text[i] == '}' && --depth == 0)
      return text.substr(open, i - open + 1);
  }
  return {};
}

void Libass()
{
  using KODI::SUBTITLES::STYLE::LibassPixelAspect;
  check(LibassPixelAspect(2.0f) == 0.5, "side by side (Kodi's pixel ratio 2): libass is given 0.5");
  check(LibassPixelAspect(0.5f) == 2.0, "top and bottom (0.5): libass is given 2");
  check(LibassPixelAspect(1.0f) == 1.0, "2D on square pixels: 1");
  check(std::fabs(LibassPixelAspect(0.9375f) - 1.0 / 0.9375) < 1e-9, "4096x2160 on a 16:9 display: 1 / 0.9375");
  check(LibassPixelAspect(0.0f) == 1.0 && LibassPixelAspect(-1.0f) == 1.0, "a ratio not known: square");
  check(Has(Read("xbmc/cores/VideoPlayer/DVDSubtitles/DVDSubtitlesLibass.cpp"),
            "  ass_set_pixel_aspect(m_renderer, LibassPixelAspect(opts.m_par));"),
        "DVDSubtitlesLibass.cpp: libass is given LibassPixelAspect() of the pixel ratio of the eye");
}

void Bitmaps()
{
  using OVERLAY::FitShownScale;
  struct Case
  {
    const char* what;
    float viewW, viewH, srcW, srcH, ratio;
    float w, h; // the scale expected
  };
  const Case cases[] = {
      {"2D 1080p, a canvas of 1920x1080", 1920, 1080, 1920, 1080, 1.0f, 1.0f, 1.0f},
      {"side by side 1080p (an eye of 960x1080), a canvas of 1920x1080", 960, 1080, 1920, 1080, 2.0f, 0.5f, 1.0f},
      {"top and bottom 1080p (an eye of 1920x540), a canvas of 1920x1080", 1920, 540, 1920, 1080, 0.5f, 1.0f, 0.5f},
      {"side by side 2160p (an eye of 1920x2160), a canvas of 1920x1080", 1920, 2160, 1920, 1080, 2.0f, 1.0f, 2.0f},
      {"top and bottom 2160p (an eye of 3840x1080), a canvas of 1920x1080", 3840, 1080, 1920, 1080, 0.5f, 2.0f, 1.0f},
      {"2D 1080p, a 4:3 canvas of 720x576", 1920, 1080, 720, 576, 1.0f, 1.875f, 1.875f},
      {"side by side 1080p, a 4:3 canvas of 720x576", 960, 1080, 720, 576, 2.0f, 0.9375f, 1.875f},
  };
  for (const Case& c : cases)
  {
    const OVERLAY::ScreenArScale s = FitShownScale(c.viewW, c.viewH, c.srcW, c.srcH, c.ratio);
    char d[160];
    std::snprintf(d, sizeof(d), "%.4f x %.4f, %.4f x %.4f expected", s.width, s.height, c.w, c.h);
    check(std::fabs(s.width - c.w) < 1e-5f && std::fabs(s.height - c.h) < 1e-5f, c.what, d);
    // as the display shows it: the shape of the canvas, and within the eye as shown
    const float shownW = c.srcW * s.width * c.ratio, shownH = c.srcH * s.height;
    check(std::fabs(shownW / shownH - c.srcW / c.srcH) < 1e-4f && shownW <= c.viewW * c.ratio + 0.01f &&
              shownH <= c.viewH + 0.01f,
          std::string(c.what) + ": shown in the shape of its canvas, within the eye");
  }
  const OVERLAY::ScreenArScale none = FitShownScale(960, 1080, 1920, 1080, 0.0f);
  check(none.width == 0.5f && none.height == 0.5f, "a ratio not known is square");
  const std::string ov = Body(Read("xbmc/cores/VideoPlayer/VideoRenderers/OverlayRenderer.cpp"),
                              "void CRenderer::GetRenderState(COverlay* o, SRenderState& state) const");
  check(Has(ov, "      const ScreenArScale fit = FitShownScale(\n          m_rv.Width(), m_rv.Height(), source_width, "
                "source_height,\n          CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo().fPixelRatio);") &&
            Has(ov, "      scale_w = fit.width;\n      scale_h = fit.height;"),
        "OverlayRenderer.cpp: a bitmap aligned to the screen with its aspect ratio is fitted as the eye is shown");
}

void Stop()
{
  const std::string gc = Read("xbmc/windowing/GraphicContext.cpp");
  check(Has(gc, "        m_stereoMode = RenderStereoMode::OFF;\n        m_nextStereoMode = RenderStereoMode::OFF;\n") &&
            Has(gc, "        SetVideoResolution(targetResolutionOnStop, true);\n        CServiceBroker::GetGUI()"),
        "GraphicContext.cpp: the menus' resolution after a 3D playback is set forced, with 3D off");
  check(Has(Body(gc, "void CGraphicContext::SetVideoResolutionInternal(RESOLUTION res, bool forceUpdate)"),
            "  if (!forceUpdate && res == lastRes && "),
        "GraphicContext.cpp: a change to the resolution it is in is skipped unless forced (why it must be forced)");
}

void StereoChoice()
{
  using namespace KODI::WINDOWING::GBM::STEREO;
  // the display of NTM-3D (1.2.2): 3D declared up to 1080p, side by side at 1080p60 told by the InfoFrame
  // (nvidia-drm lists no half layout); Kodi's mode 3840x2160 at 60 Hz
  std::vector<Offer> offers(2);
  offers[0].base = 0, offers[0].width = 3840, offers[0].height = 2160, offers[0].refresh = 60.0;
  offers[1].base = 1, offers[1].width = 1920, offers[1].height = 1080, offers[1].refresh = 60.0;
  offers[1].infoFrameSideBySide = true;
  offers[1].infoFrameTopAndBottom = true;
  Allowed automatic;
  automatic.pictureRate = 47.952;
  const KODI::WINDOWING::GBM::STEREO::Choice a = Choose(offers, 0, Arrangement::SIDE_BY_SIDE, automatic);
  check(a.layout == Layout::SIDE_BY_SIDE_HALF && a.base == 1 && a.mode == 1 && a.infoFrame,
        "Automatic, 3D at 1080p only: side by side at 1080p60, told by the InfoFrame");
  const KODI::WINDOWING::GBM::STEREO::Choice t = Choose(offers, 0, Arrangement::TOP_AND_BOTTOM, automatic);
  check(t.layout == Layout::TOP_AND_BOTTOM && t.base == 1 && t.infoFrame, "... top and bottom the same");
  Allowed keep = automatic;
  keep.fewerLines = false;
  check(Choose(offers, 0, Arrangement::SIDE_BY_SIDE, keep).layout == Layout::NONE,
        "Automatic, keep 2160p: 2160p, untold (the display switched by hand)");
  // where the display takes it at 2160p as well, 2160p at both
  offers[0].infoFrameSideBySide = true;
  const KODI::WINDOWING::GBM::STEREO::Choice u = Choose(offers, 0, Arrangement::SIDE_BY_SIDE, automatic);
  check(u.layout == Layout::SIDE_BY_SIDE_HALF && u.base == 0 && u.infoFrame,
        "Automatic, side by side declared at 2160p too: 2160p, told");
  check(Choose(offers, 0, Arrangement::SIDE_BY_SIDE, keep) == u, "Automatic, keep 2160p: the same there");
  check(OUTPUT_OFF == 0 && OUTPUT_AUTOMATIC == 1 && OUTPUT_AUTOMATIC_KEEP_LINES == 2,
        "the values of videoscreen.hades3doutput");

  const std::string ws = Read("xbmc/windowing/gbm/WinSystemGbm.cpp");
  check(Has(Body(ws, "bool CWinSystemGbm::GetStereoOffers("),
            "  allowed.fewerLines = settings->GetInt(CSettings::SETTING_VIDEOSCREEN_HADES3DOUTPUT) !=\n"
            "                       STEREO::OUTPUT_AUTOMATIC_KEEP_LINES;"),
        "WinSystemGbm.cpp: fewer lines by the setting");
  check(Has(ws, "the display takes this 3D layout only up to {} lines at this rate, so it ") &&
            Has(ws, R"(\"Automatic, keep 2160p\" (at )"),
        "WinSystemGbm.cpp: kodi.log says it, both ways");
  check(Has(Read("system/settings/settings.xml"),
            "              <option label=\"59875\">1</option> <!-- automatic -->\n"
            "              <option label=\"62873\">2</option>"),
        "settings.xml: the value Automatic, keep 2160p");
  check(Has(Read("xbmc/settings/dialogs/GUIDialogSettingsBase.cpp"),
            "  if (labelId == 62873)\n    return CGUIUtils::GetHadesString(62873, 54952);"),
        "GUIDialogSettingsBase.cpp: its label translated");
  check(Has(Read("addons/resource.language.en_gb/resources/strings.po"),
            "msgctxt \"#62873\"\nmsgid \"Automatic, keep 2160p\"\n"),
        "the string of the value");
  for (const char* lang : {"en_gb", "es_es", "es_ar", "es_mx"})
  {
    const std::string po =
        Read(std::string("addons/skin.estuary/language/resource.language.") + lang + "/strings.po");
    const bool en = std::string(lang) == "en_gb";
    check(Has(po, std::string("msgctxt \"#54952\"\nmsgid \"Automatic, keep 2160p\"\nmsgstr \"") +
                      (en ? "\"" : "Automático, mantener 2160p\"")) &&
              Has(po, en ? "Automatic, keep 2160p: they stay at 2160p there"
                         : "Automático, mantener 2160p: se quedan a 2160p"),
          std::string("Estuary ") + lang + ": the value and the help");
  }
}

void Source()
{
  using SOURCE_RATE::SourceLimited;
  check(SourceLimited(10000, 8000) && !SourceLimited(10000, 7999),
        "the source is what a buffering waited for: the cache waited for it 80 % of the time or more");
  check(SourceLimited(10000, -1), "where that is not known, as before (the source)");
  const std::string fc = Read("xbmc/filesystem/FileCache.cpp");
  check(Has(fc, "      iRead = m_source->Read(buffer.get(), maxSourceRead);\n      m_sourceWaitUs.fetch_add(") &&
            Has(fc, "        m_sourceBytes.fetch_add(static_cast<uint64_t>(iRead));") &&
            Has(fc, "        m_sourceSeeks.fetch_add(1);") && Has(fc, "    status->sourceCounted = true;"),
        "FileCache.cpp: what the source sent, how long the cache waited for it, its seeks");
  const std::string vp = Body(Read("xbmc/cores/VideoPlayer/VideoPlayer.cpp"), "void CVideoPlayer::WatchBuffering(bool ends)");
  check(Has(vp, "  const int64_t bytes = counted ? static_cast<int64_t>(status.sourceBytes)\n"),
        "VideoPlayer.cpp: a buffering measured by what the cache took from the source");
  check(Has(vp, "  if (m_buffering.told || !limited || !SOURCE_RATE::Slower(ms, sent, status.maxrate))\n    return;"),
        "VideoPlayer.cpp: the user told only where the source is what the player waited for");
  check(Has(Read("xbmc/filesystem/IFileTypes.h"), "  uint64_t maxforward{0};"),
        "IFileTypes.h: every field of SCacheStatus initialised");
}
} // namespace

int main()
{
  Libass();
  Bitmaps();
  Stop();
  StereoChoice();
  Source();
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
