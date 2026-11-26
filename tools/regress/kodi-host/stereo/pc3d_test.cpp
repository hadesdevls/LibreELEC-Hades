// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// pc3d_test.cpp (1.2.2: 3D on PCs, the reports of 1.2.1 and 1.2): what the tree under test decides, and where it
// decides it in the source, for
//  - the subtitles of a 3D output side by side and top and bottom: libass is given Kodi's pixel ratio as it is
//    (SubtitlesStyle.h, LibassPixelAspect()). Kodi's pixel ratio is the width of a pixel over its height as it is
//    shown (Resolution.cpp: the aspect of the display over that of its pixels); in a 3D output it is 2 side by side
//    and 0.5 top and bottom (GraphicContext.cpp, ResInfo()); libass draws its glyphs that much narrower (libass
//    0.17.5, ass.h: "1.0 means square pixels, 0 means default"; ass_render.c: scale.x / par_scale_x). The ratio was
//    turned around as 2 - ratio (Kodi 20 to 22): 0 side by side, libass's default, the glyphs twice as wide on the
//    display; 1.5 for 2 top and bottom. -DOLD_RULE computes it as before: those checks fail.
//    Each eye: a glyph of h lines is drawn w * h / ratio columns wide, and shown ratio times wider: the same
//    aspect as in 2D (checked for 2D, side by side and top and bottom)
//  - the badge of Dolby Atmos (HadesPlayerInfo.h, AudioBadge()): "+ objects" passed through, "no Atmos" decoded
//  - holding Start on a game controller (joystick.xml): the player information, in FullscreenVideo and
//    FullscreenInfo, the short press as it was
//  - deep colour above 340 MHz on i915 and xe (DeepColourLock.h): which links are looked at (3840x2160 at 24 Hz in
//    10 bits, 370.9 MHz, scrambled: yes; 8 bits, nvidia-drm, a mode capped already, 2160p60: no), what is done with
//    what the display's SCDC says, and where QueueLinkDepth() and LookForDeepLock() hold the mode at 8 bits
//  - a 3D Dolby Vision video in the player information: no choice of native output, the reason "stereo", the label
//    of the choice as the settings say, the line of the log (in the source)
//  - "How 3D video goes out" at Automatic for each display (Stereo3DVideo.h): SDR until Check this setup found HDR in
//    3D on the display, HDR where it did; BT.2020 asked for; told once a display; the three states in the player
//    information; each display by its own key (DisplayModeMemory.h), another's never taken; where Kodi reads and
//    keeps it (in the source)
//  - a 3D playback that stops: 3D off with the change of resolution of the menus, one mode set (GraphicContext.cpp);
//    the half layouts at 2160p on nvidia-drm where the EDID declares them, not at 24 to 30 Hz (WinSystemGbm.cpp)
// Prints a FAIL line for each failed check, then "<n> checks" and "<n> failed".
#include "cores/VideoPlayer/DVDSubtitles/SubtitlesStyle.h"
#include "utils/HadesPlayerInfo.h"
#include "utils/Stereo3DVideo.h"
#include "windowing/gbm/DisplayModeMemory.h"
#include "windowing/gbm/DeepColourLock.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

// the body of a function of a source, from its signature to its closing brace
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

double Par(float ratio)
{
#ifdef OLD_RULE
  return static_cast<double>((ratio - 2.0f) * -1);
#else
  return KODI::SUBTITLES::STYLE::LibassPixelAspect(ratio);
#endif
}

void Subtitles()
{
  // libass's par as it uses it: 0 is its default, which with no storage size is square (ass_render.c)
  const auto used = [](double par) { return par == 0.0 ? 1.0 : par; };
  // a glyph of 100 lines that is 60 columns wide on square pixels: its columns in the frame, and its width on the
  // display, where each pixel is shown 'ratio' times as wide as tall
  const auto shownAspect = [&](float ratio)
  {
    const double columns = 60.0 / used(Par(ratio));
    return columns * static_cast<double>(ratio) / 100.0;
  };
  check(std::fabs(shownAspect(1.0f) - 0.6) < 1e-9, "2D, square pixels: the glyph as it is");
  check(std::fabs(shownAspect(2.0f) - 0.6) < 1e-9, "side by side (pixel ratio 2): the glyph of its aspect, not twice as wide",
        std::to_string(shownAspect(2.0f)));
  check(std::fabs(shownAspect(0.5f) - 0.6) < 1e-9, "top and bottom (pixel ratio 0.5): the glyph of its aspect",
        std::to_string(shownAspect(0.5f)));
  check(std::fabs(shownAspect(16.0f / 9.0f / (4096.0f / 2160.0f)) - 0.6) < 1e-6,
        "4096x2160 on a 16:9 display (pixel ratio 0.9375): the glyph of its aspect");
  check(KODI::SUBTITLES::STYLE::LibassPixelAspect(0.0f) == 1.0 &&
            KODI::SUBTITLES::STYLE::LibassPixelAspect(-1.0f) == 1.0,
        "a ratio that is not known: square, never libass's default");

  // where it is decided, and that what it rests on is so in the tree
  const std::string libass = Read("xbmc/cores/VideoPlayer/DVDSubtitles/DVDSubtitlesLibass.cpp");
  check(Has(libass, "ass_set_pixel_aspect(m_renderer, LibassPixelAspect(opts.m_par));") &&
            !Has(libass, "(opts.m_par - 2.0f)"),
        "DVDSubtitlesLibass.cpp: libass is given LibassPixelAspect(), not 2 - ratio");
  const std::string gc = Body(Read("xbmc/windowing/GraphicContext.cpp"),
                              "const RESOLUTION_INFO CGraphicContext::ResInfo(RESOLUTION res, bool output) const");
  check(Has(gc, "if (m_stereoMode == RenderStereoMode::SPLIT_VERTICAL)") && Has(gc, "info.fPixelRatio     *= 2;") &&
            Has(gc, "if (m_stereoMode == RenderStereoMode::SPLIT_HORIZONTAL)") && Has(gc, "info.fPixelRatio     /= 2;"),
        "GraphicContext.cpp: the pixel ratio of each eye is 2 side by side and 0.5 top and bottom");
  check(Has(Read("xbmc/windowing/Resolution.cpp"), "fPixelRatio = aspect ? aspect / (((float)width) / height) : 1.0f;"),
        "Resolution.cpp: the pixel ratio is the aspect of the display over that of its pixels (wider than 1: wide)");
  const std::string overlays = Read("xbmc/cores/VideoPlayer/VideoRenderers/OverlayRenderer.cpp");
  check(Has(overlays, "  rOpts.m_par = resolution.pixelRatio;") &&
            Has(overlays, "  resolution.pixelRatio = resInfo.fPixelRatio;"),
        "OverlayRenderer.cpp: libass is given the pixel ratio of the resolution of the eye (GetResInfo())");
}

void Atmos()
{
  using KODI::UTILS::PLAYERINFO::AudioBadge;
  check(AudioBadge("eac3_ddp_atmos", "", 6) == "DD+ Atmos 5.1 + objects", "E-AC-3 Atmos passed through",
        AudioBadge("eac3_ddp_atmos", "", 6));
  check(AudioBadge("truehd_atmos", "7.1.2", 8) == "TrueHD Atmos 7.1.2 + objects", "TrueHD Atmos with its bed, passed through",
        AudioBadge("truehd_atmos", "7.1.2", 8));
  check(AudioBadge("eac3_ddp_atmos", "", 6, true) == "DD+ 5.1, no Atmos", "E-AC-3 Atmos decoded here",
        AudioBadge("eac3_ddp_atmos", "", 6, true));
  check(AudioBadge("truehd_atmos", "7.1.2", 8, true) == "TrueHD 7.1, no Atmos", "TrueHD Atmos decoded here",
        AudioBadge("truehd_atmos", "7.1.2", 8, true));
  check(AudioBadge("eac3", "", 6) == "Dolby Digital+ 5.1" && AudioBadge("eac3", "", 6, true) == "Dolby Digital+ 5.1" &&
            AudioBadge("dtshd_ma", "", 8) == "DTS-HD MA 7.1",
        "no Atmos: as before, passed through or decoded");
  const std::string win = Read("xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  check(Has(win, "AudioBadge(audio.codecName, bed, audio.channels, info[\"hades.audio.path\"] == \"pcm\");"),
        "the player information: decoded where the sound goes out as PCM");
}

void Gamepad()
{
  const std::string map = Read("system/keymaps/joystick.xml");
  const std::string video =
      map.substr(map.find("<FullscreenVideo>"), map.find("</FullscreenVideo>") - map.find("<FullscreenVideo>"));
  const std::string info =
      map.substr(map.find("<FullscreenInfo>"), map.find("</FullscreenInfo>") - map.find("<FullscreenInfo>"));
  check(Has(video, "<start>Info</start>") && Has(video, "<start holdtime=\"500\">PlayerProcessInfo</start>"),
        "FullscreenVideo: Start is Info, holding it the player information");
  check(Has(info, "<start>Close</start>") && Has(info, "<start holdtime=\"500\">PlayerProcessInfo</start>"),
        "FullscreenInfo: Start closes, holding it the player information");
  size_t holds = 0;
  for (size_t at = video.find("holdtime"); at != std::string::npos; at = video.find("holdtime", at + 1))
    ++holds;
  check(holds == 2, "FullscreenVideo: the hold of B as before and the hold of Start, no other",
        std::to_string(holds));
}

void Info3D()
{
  const std::string win = Read("xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  const std::string available = Body(win, "bool DVBRIDGE::OutputChoiceAvailable(OutputChoice choice)");
  check(Has(available, "return choice == OutputChoice::AUTOMATIC || (!StreamIs3D() && NativeOutputAvailable());") &&
            Has(available, "if (StreamIs3D() && (NativeChoice(choice) || choice == OutputChoice::HDR10PLUS))\n    return false;"),
        "OutputChoiceAvailable(): no choice of native output for a 3D video");
  const std::string opened = Body(win, "void DVBRIDGE::VideoStreamOpened(const CDVDStreamInfo& stream)");
  check(Has(opened, "g_dvStreamStereo = KODI::UTILS::STEREO3D::IsStereo(stream.stereo_mode);"),
        "VideoStreamOpened(): 3D by its stereo mode (mono is 2D)");
  const std::string publish = Body(win, "void CWinSystemGbmGLESContext::PublishDVInfo(bool force)");
  check(Has(publish, "if (!native && reason != \"settings\" && DVBRIDGE::StreamIs3D() && g_dvStreamProfile != 0)\n    reason = \"stereo\";"),
        "PublishDVInfo(): the reason of a 3D Dolby Vision video is stereo");
  check(Has(Read("addons/skin.estuary/xml/Variables.xml"),
            "String.IsEqual(Window(Home).Property(hades.dv.reason),stereo)\">$ADDON[skin.estuary 54927]"),
        "Variables.xml: the reason stereo in words");
  const std::string dialog = Read("xbmc/video/dialogs/GUIDialogVideoSettings.cpp");
  check(Has(dialog, "const bool native3D = DVBRIDGE::NativeOutputAvailable() && DVBRIDGE::StreamIs3D();") &&
            Has(dialog, "GetHadesString(62868, 54926)"),
        "Output of this video: as the settings say, no native Dolby Vision in 3D");
  const std::string policy = Read("xbmc/cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h");
  check(!Has(policy, "natively or reconstructed: it is 3D") && Has(policy, "native Dolby Vision output is not used for this video: it is 3D ("),
        "the log of a 3D Dolby Vision stream: native output not used, the views rebuilt where that is had");
}
} // namespace

void DeepColour()
{
  namespace D = KODI::WINDOWING::GBM::DEEP_LOCK;
  namespace H = KODI::WINDOWING::GBM::HDR_MORE_BITS;
  check(D::DeepGoesScrambled(296703) && D::DeepGoesScrambled(297000),
        "3840x2160 at 24 to 30 Hz: 10 bits go above 340 MHz (370.9 MHz), scrambled");
  check(!D::DeepGoesScrambled(594000) && !D::DeepGoesScrambled(148500) && !D::DeepGoesScrambled(0),
        "2160p60 (scrambled at 8 bits already), 1080p60 (185.6 MHz at 10 bits), no mode: not this case");
  check(!D::DeepGoesScrambled(400000) && !D::DeepGoesScrambled(340001),
        "a mode above 340 MHz at 8 bits, which goes scrambled whatever its depth (500 MHz at 10 bits): not this case");
  check(D::Wanted(true, false, 10, 296703, false), "i915, max bpc 10, 4K24: looked at");
  check(!D::Wanted(true, true, 10, 296703, false) && !D::Wanted(false, false, 10, 296703, false) &&
            !D::Wanted(true, false, 8, 296703, false) && !D::Wanted(true, false, 12, 296703, true),
        "not on nvidia-drm, not where Kodi does not set the depth, not at 8 bits, not for a mode held at 8");
  D::Check c;
  check(D::Next(c) == D::Step::WAIT, "nothing armed: nothing");
  c.armed = true;
  c.sinceMs = 1000;
  check(D::Next(c) == D::Step::WAIT, "armed, before 2.5 s: wait");
  c.sinceMs = 2600;
  check(D::Next(c) == D::Step::READ, "armed, after 2.5 s: read the SCDC");
  c.readInFlight = true;
  check(D::Next(c) == D::Step::WAIT, "a read in flight: wait");
  c.readInFlight = false;
  c.outputOn = false;
  check(D::Next(c) == D::Step::DROP, "the output went off: dropped");
  c.outputOn = true;
  // (Status_Flags_0: clock detected, a channel not locked; TMDS_Config with scrambling and 1/40)
  c.lock = H::LockOf(uint8_t{0x03}, uint8_t{0x03}, true);
  check(D::Next(c) == D::Step::STEP_DOWN, "the display did not lock: 8 bits for the mode");
  c.lock = H::LockOf(uint8_t{0x0f}, uint8_t{0x03}, true);
  check(D::Next(c) == D::Step::KEEP, "the display locked: kept");
  c.lock = H::LockOf(uint8_t{0x0f}, uint8_t{0x00}, true);
  check(D::Next(c) == D::Step::STEP_DOWN, "the display lost the setup of the scrambled link: 8 bits");
  c.lock = H::LockOf(std::nullopt, std::nullopt, true);
  check(D::Next(c) == D::Step::UNKNOWN, "nothing read: not known, kept");

  const std::string gbm = Read("xbmc/windowing/gbm/WinSystemGbm.cpp");
  check(Has(gbm, "  if (deepCapped && content != LinkContent::DV && linkDepth.maximum > 8)\n    linkDepth.maximum = 8;") &&
            Has(gbm, "      LinkDepthFor(linkDepth, content, hdrSetting, current, setsHead, firstCommit);"),
        "QueueLinkDepth(): a mode the display did not lock onto goes out at 8 bits");
  const std::string gles = Read("xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  const std::string look = Body(gles, "void CWinSystemGbmGLESContext::LookForDeepLock()");
  check(Has(look, "m_deepCapped.insert(where);") && Has(look, "m_linkBpc = 8;") &&
            look.find("m_linkBpc = 8;") < look.find("Show(true);"),
        "LookForDeepLock(): the mode held at 8 bits, and the output set again at 8 bits");
  check(Has(look, "ConnectorDdcAdapter(fd, connector->GetName())") && !Has(look, "NV_DDC_I2C_PORT"),
        "LookForDeepLock(): the SCDC read through the connector's own DDC adapter");
}

void Hdr3D()
{
  namespace S = KODI::UTILS::STEREO3D;
  using H = S::HdrIn3D;
  check(S::HdrIn3DOf("hdr") == H::WORKS && S::HdrIn3DOf("sdr") == H::REFUSED &&
            S::HdrIn3DOf("told") == H::NOT_CHECKED && S::HdrIn3DOf("") == H::NOT_CHECKED &&
            S::HdrIn3DOf("HDR") == H::NOT_CHECKED,
        "what is kept of a display: hdr, sdr; anything else (told, nothing) is not checked");
  check(S::AsSdr(S::Choice::AUTOMATIC, H::NOT_CHECKED) && S::AsSdr(S::Choice::AUTOMATIC, H::REFUSED) &&
            !S::AsSdr(S::Choice::AUTOMATIC, H::WORKS),
        "Automatic: SDR until the check found HDR in 3D on the display, HDR where it did");
  check(!S::AsSdr(S::Choice::HDR, H::REFUSED) && !S::AsSdr(S::Choice::HDR, H::NOT_CHECKED) &&
            S::AsSdr(S::Choice::SDR_BT709, H::WORKS) && S::AsSdr(S::Choice::SDR_BT2020, H::WORKS),
        "HDR, SDR in BT.2020 and SDR in BT.709 whatever the check found");
  check(S::Container(S::Choice::AUTOMATIC, 0) == 1 && S::Container(S::Choice::AUTOMATIC, 2) == 1 &&
            S::Container(S::Choice::SDR_BT709, 1) == 0,
        "Automatic asks for BT.2020 (SdrMappedContainer() gives it where the display declares it)");
  check(S::TellsOfCheck(S::Choice::AUTOMATIC, H::NOT_CHECKED, false) &&
            !S::TellsOfCheck(S::Choice::AUTOMATIC, H::NOT_CHECKED, true) &&
            !S::TellsOfCheck(S::Choice::AUTOMATIC, H::REFUSED, false) &&
            !S::TellsOfCheck(S::Choice::AUTOMATIC, H::WORKS, false) &&
            !S::TellsOfCheck(S::Choice::SDR_BT709, H::NOT_CHECKED, false),
        "told of the check once a display, at Automatic, where it was not checked");
  const auto by = [](S::Choice c, H d, bool hdrVideo, bool had)
  { return std::string(S::ByCode(c, d, hdrVideo, had)); };
  check(by(S::Choice::AUTOMATIC, H::NOT_CHECKED, true, true) == "notchecked" &&
            by(S::Choice::AUTOMATIC, H::WORKS, true, true) == "hdrworks" &&
            by(S::Choice::AUTOMATIC, H::REFUSED, true, true) == "nohdr" &&
            by(S::Choice::AUTOMATIC, H::WORKS, false, true).empty() &&
            by(S::Choice::HDR, H::REFUSED, true, false) == "notpossible" &&
            by(S::Choice::SDR_BT709, H::NOT_CHECKED, true, true) == "chosen",
        "the player information: the three states of Automatic, as before for the others");
  KODI::WINDOWING::GBM::DISPLAY_MODES::CDisplayModeMemory memory;
  memory.Parse("GSM / LG TV\thdr\nSNY / VPL-VW290\tsdr\nEPS / EH-TW9400\ttold\n");
  const auto of = [&memory](const std::string& key) { return S::HdrIn3DOf(memory.Of(key).value_or("")); };
  check(of("GSM / LG TV") == H::WORKS && of("SNY / VPL-VW290") == H::REFUSED &&
            of("EPS / EH-TW9400") == H::NOT_CHECKED && of("GSM / LG TV 2") == H::NOT_CHECKED &&
            of("") == H::NOT_CHECKED,
        "each display by its own key: another display's answer is never taken");
  const std::string vars = Read("addons/skin.estuary/xml/Variables.xml");
  check(Has(vars, "hades.out.stereo3dby),notchecked)\">, $ADDON[skin.estuary 54932]") &&
            Has(vars, "hades.out.stereo3dby),hdrworks)\">, $ADDON[skin.estuary 54933]") &&
            Has(vars, "hades.out.stereo3dby),nohdr)\">, $ADDON[skin.estuary 54934]"),
        "Estuary says the three states");
  const std::string ster = Read("xbmc/windowing/gbm/WinSystemGbmGLESStereo.cpp");
  const std::string load = Body(ster, "void CWinSystemGbmGLESContext::LoadStereo3DHdr()");
  check(Has(load, "const std::string key = DisplayModeKey();") && Has(load, "ReadHdrIn3D().Of(key)"),
        "LoadStereo3DHdr(): the answer of the display that plays, by its key");
  const std::string keep = Body(ster, "void CWinSystemGbmGLESContext::KeepStereo3DHdrTold() const");
  check(Has(keep, "if (key.empty() || memory.Of(key) || !memory.Remember(key, S::HDR_IN_3D_TOLD))"),
        "KeepStereo3DHdrTold(): never over what the check found");
  const std::string ctx = Read("xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp");
  check(Has(ctx, "    const bool sdr = S::AsSdr(choice, display);") &&
            Has(ctx, "    LoadStereo3DHdr();") &&
            Has(ctx, "    info[\"hades.out.displaykey\"] = DisplayModeKey();"),
        "HdrAsSdr(): Automatic by the display; read as each video starts; its key published");
  const std::string gles = Read("xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.cpp");
  check(Has(gles, "const char* by = S::ByCode(choice, window->Stereo3DHdr(), hdrVideo, had);"),
        "TellStereoVideo(): the state of Automatic for the player information");
}

void Exit3D()
{
  const std::string gc = Read("xbmc/windowing/GraphicContext.cpp");
  const std::string full = Body(gc, "void CGraphicContext::SetFullScreenVideo(bool bOnOff)");
  check(Has(full, "if (!appPlayer->IsPlayingVideo() && m_nextStereoMode != RenderStereoMode::OFF &&") &&
            Has(full, "CSettings::SETTING_VIDEOPLAYER_QUITSTEREOMODEONSTOP") &&
            Has(full, "        m_stereoMode = RenderStereoMode::OFF;\n        m_nextStereoMode = "
                      "RenderStereoMode::OFF;\n        SetVideoResolution(targetResolutionOnStop, false);"),
        "a 3D playback that stops: 3D off with the change of resolution of the menus, one mode set");
  const std::string sm = Read("xbmc/guilib/StereoscopicsManager.cpp");
  check(Has(sm, "  if (currentMode != mode)\n  {\n    CServiceBroker::GetWinSystem()->GetGfxContext().SetStereoMode(mode);"),
        "ApplyStereoMode(): nothing set where 3D is off already");
  const std::string gbm = Read("xbmc/windowing/gbm/WinSystemGbm.cpp");
  check(Has(gbm, "    const bool aviCtaVic = connector->SupportsProperty(STEREO::NVIDIA_3D_AVI_CTA_VIC);\n"
                 "    for (STEREO::Offer& offer : offers)\n    {\n"
                 "      const uint8_t vic = STEREO::FormatNamed(modes[offer.base], aviCtaVic)\n"
                 "                              ? STEREO::VicOf(modes[offer.base])\n"
                 "                              : uint8_t{0};"),
        "GetStereoOffers(): a half layout at 2160p told by the InfoFrame where the EDID declares it; at 24 to 30 Hz "
        "only where the driver names the format by its CTA-861 number (NV_HDMI_3D_AVI_CTA_VIC), whatever the HDMI "
        "version of the display, as the kernel");
  check(Has(gbm, "  const bool driverTells =\n      !plain ||\n"
                 "      !STEREO::FormatNamed(mode, connector->SupportsProperty(STEREO::NVIDIA_3D_AVI_CTA_VIC));"),
        "the commit tells a half layout at 2160p at 24 to 30 Hz by the rule of the offers");
  check(Has(gbm, "aviCtaVic ? \"at 2160p at 24 to 30 Hz too: the driver names those modes by their CTA-861 \"") &&
            Has(gbm, ": \"not at 2160p at 24 to 30 Hz, which the driver tells by a format number of \"") &&
            !Has(gbm, "HDMI 2.0 one"),
        "kodi.log says whether the half layouts are told at 2160p at 24 to 30 Hz, and why");
  check(Has(gbm, "edidDeclares = STEREO::EdidLayouts(m_stereoEdid);"),
        "kodi.log says what the EDID declares where the driver lists no 3D modes");
}

int main()
{
  Subtitles();
  Atmos();
  Gamepad();
  Info3D();
  DeepColour();
  Hdr3D();
  Exit3D();
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
