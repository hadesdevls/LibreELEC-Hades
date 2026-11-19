/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): the modes of the display in use read again
 * while Kodi runs (windowing/gbm/ModeListRefresh.h). The rule: after a refresh, every choice of a
 * resolution is the one a start makes with the new list. The real header of the tree decides;
 * what Kodi does around it is transcribed from the tree:
 *  - CDRMUtils::GetResolutionInfo(): the size, the refresh rate (clock * 1000 / (htotal * vtotal),
 *    halved for interlace) and the flags of a mode;
 *  - CDRMUtils::GetModes() and CWinSystemGbm::UpdateResolutions(): a resolution for each mode
 *    without a 3D layout, from RES_CUSTOM (17) on, its strId the place of its mode; RES_DESKTOP
 *    (16) a copy of the last one whose size, rate and flags are those of the desktop mode;
 *  - CDRMUtils::FindPreferredMode(), at a start without a mode left by the console;
 *  - CDisplaySettings::GetStringFromResolution(), GetResolutionFromString() and
 *    FindBestMatchingResolution() (square_error(), the first best from RES_DESKTOP on), and the
 *    choice of CApplication::CreateGUI();
 *  - a start: the desktop, the current resolution of the settings, the interface set in it.
 * Policies run on the same changes of a display: KEEP (the code before: the list of the start stays),
 * BY_INDEX (the list made again, the numbers kept), IDENTITY (a first fix: each resolution kept by the
 * timing of its mode where it is offered) and TREE. Only TREE must give what a start gives.
 */
#include "windowing/gbm/ModeListRefresh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace MODELIST = KODI::WINDOWING::GBM::MODELIST;

static int g_failures = 0;
static int g_checks = 0;
static bool g_quiet = false; //!< passes are not printed
static bool g_silent = false; //!< nothing is printed: another policy is being counted
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
    ++g_failures;
  if (!g_silent && (!ok || !g_quiet))
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
}

// The fields of drmModeModeInfo
struct Mode
{
  uint32_t clock = 0;
  uint16_t hdisplay = 0, hsync_start = 0, hsync_end = 0, htotal = 0, hskew = 0;
  uint16_t vdisplay = 0, vsync_start = 0, vsync_end = 0, vtotal = 0, vscan = 0;
  uint32_t vrefresh = 0;
  uint32_t flags = 0;
  uint32_t type = 0;
  std::string name;
};

constexpr uint32_t PHSYNC = 1u << 0, PVSYNC = 1u << 2, INTERLACE = 1u << 4;
constexpr uint32_t FP = 1u << 14, TAB = 7u << 14, SBS = 8u << 14;
constexpr uint32_t PREFERRED = 1u << 3, DRIVER = 1u << 6;
constexpr uint32_t MASK3D = 0x1fu << 14;

Mode M(const char* name, uint32_t clock, uint16_t h, uint16_t hs, uint16_t he, uint16_t ht,
       uint16_t v, uint16_t vs, uint16_t ve, uint16_t vt, uint32_t hz, uint32_t flags = 0)
{
  Mode m;
  m.name = name;
  m.clock = clock;
  m.hdisplay = h;
  m.hsync_start = hs;
  m.hsync_end = he;
  m.htotal = ht;
  m.vdisplay = v;
  m.vsync_start = vs;
  m.vsync_end = ve;
  m.vtotal = vt;
  m.vrefresh = hz;
  m.flags = PHSYNC | PVSYNC | flags;
  m.type = DRIVER;
  return m;
}
Mode Pref(Mode m)
{
  m.type |= PREFERRED;
  return m;
}
Mode With3D(Mode m, uint32_t layout, const char* name)
{
  m.flags |= layout;
  m.name = name;
  return m;
}

// CTA-861 timings of a 4K TV
const Mode UHD60 = M("2160p60", 594000, 3840, 4016, 4104, 4400, 2160, 2168, 2178, 2250, 60);
const Mode UHD5994 = M("2160p59.94", 593407, 3840, 4016, 4104, 4400, 2160, 2168, 2178, 2250, 60);
const Mode UHD50 = M("2160p50", 594000, 3840, 4896, 4984, 5280, 2160, 2168, 2178, 2250, 50);
const Mode UHD30 = M("2160p30", 297000, 3840, 4016, 4104, 4400, 2160, 2168, 2178, 2250, 30);
const Mode UHD2997 = M("2160p29.97", 296703, 3840, 4016, 4104, 4400, 2160, 2168, 2178, 2250, 30);
const Mode UHD25 = M("2160p25", 297000, 3840, 4896, 4984, 5280, 2160, 2168, 2178, 2250, 25);
const Mode UHD24 = M("2160p24", 297000, 3840, 5116, 5204, 5500, 2160, 2168, 2178, 2250, 24);
const Mode UHD23976 = M("2160p23.976", 296703, 3840, 5116, 5204, 5500, 2160, 2168, 2178, 2250, 24);
const Mode FHD60 = M("1080p60", 148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 60);
const Mode FHD5994 = M("1080p59.94", 148352, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 60);
const Mode FHD50 = M("1080p50", 148500, 1920, 2448, 2492, 2640, 1080, 1084, 1089, 1125, 50);
const Mode FHD24 = M("1080p24", 74250, 1920, 2558, 2602, 2750, 1080, 1084, 1089, 1125, 24);
const Mode FHD23976 = M("1080p23.976", 74176, 1920, 2558, 2602, 2750, 1080, 1084, 1089, 1125, 24);
const Mode FHD60I = M("1080i60", 74250, 1920, 2008, 2052, 2200, 1080, 1084, 1094, 1125, 30, INTERLACE);
const Mode HD60 = M("720p60", 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 60);
const Mode FHD24FP = With3D(FHD24, FP, "1080p24 frame packing");
const Mode FHD24TAB = With3D(FHD24, TAB, "1080p24 top-and-bottom");
const Mode FHD60SBS = With3D(FHD60, SBS, "1080p60 side-by-side");
// A monitor of another make: the same sizes, other timings (VESA reduced blanking)
const Mode QHD60 = M("1440p60", 241500, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1481, 60);
const Mode FHD60RB = M("1080p60 CVT-RB", 138500, 1920, 1968, 2000, 2080, 1080, 1083, 1088, 1111, 60);

// ---- what Kodi does around the header (transcribed) ----

constexpr int RES_DESKTOP = 16;
constexpr int RES_CUSTOM = 17;
constexpr unsigned F_INTERLACED = 1, F_PROGRESSIVE = 4, F_MODEMASK = 1 | 8 | 16;

struct Info // the fields of RESOLUTION_INFO that the choices read
{
  int w = 0, h = 0;
  float refresh = 0;
  unsigned flags = 0;
  int place = -1; // strId
};

bool SameInfo(const Info& a, const Info& b)
{
  return a.w == b.w && a.h == b.h && a.refresh == b.refresh && a.flags == b.flags;
}

// CDRMUtils::GetResolutionInfo()
Info InfoOf(const Mode& m, int place)
{
  Info i;
  i.w = m.hdisplay;
  i.h = m.vdisplay;
  if (m.clock && m.htotal && m.vtotal)
  {
    double numerator = static_cast<double>(m.clock) * 1000;
    double denominator = static_cast<double>(m.vtotal) * m.htotal;
    if (m.flags & INTERLACE)
      denominator /= 2;
    i.refresh = static_cast<float>(numerator / denominator);
  }
  else
    i.refresh = static_cast<float>(m.vrefresh);
  i.flags = (m.flags & INTERLACE) ? F_INTERLACED : F_PROGRESSIVE;
  i.place = place;
  return i;
}

// CDRMUtils::FindPreferredMode(), as it is in the tree, on a list
int FindPreferredModeTranscribed(const std::vector<Mode>& modes)
{
  int foundAt = -1;
  for (int i = 0, area = 0; i < static_cast<int>(modes.size()); i++)
  {
    const Mode* current_mode = &modes[i];
    if (current_mode->flags & MASK3D)
      continue;
    if (current_mode->type & PREFERRED)
    {
      foundAt = i;
      break;
    }
    auto current_area = current_mode->hdisplay * current_mode->vdisplay;
    if (current_area > area)
    {
      foundAt = i;
      area = current_area;
    }
  }
  return foundAt;
}

// CWinSystemGbm::UpdateResolutions() with GetModes(): desktop is the desktop mode's resolution
std::vector<Info> Make(const std::vector<Mode>& modes, const Info& desktop)
{
  std::vector<Info> list(RES_CUSTOM);
  for (int i = 0; i < static_cast<int>(modes.size()); ++i)
  {
    if (modes[i].flags & MASK3D)
      continue;
    const Info res = InfoOf(modes[i], i);
    list.push_back(res);
    if (SameInfo(res, desktop))
      list[RES_DESKTOP] = res; // (no break: the last one)
  }
  return list;
}

// CDisplaySettings::GetStringFromResolution() of a resolution other than the desktop one
std::string StringOf(const Info& i)
{
  char text[64];
  std::snprintf(text, sizeof(text), "%05d%05d%09.5f%sstd", i.w, i.h, static_cast<double>(i.refresh),
                (i.flags & F_INTERLACED) ? "i" : "p");
  return text;
}

float square_error(float x, float y)
{
  float yonx = (x > 0) ? y / x : 0;
  float xony = (y > 0) ? x / y : 0;
  return std::max(yonx, xony);
}

// CDisplaySettings::GetResolutionFromString() and FindBestMatchingResolution()
int ResolutionFromString(const std::vector<Info>& list, const std::string& s)
{
  if (s == "DESKTOP")
    return RES_DESKTOP;
  if (s.size() < 20)
    return RES_DESKTOP;
  const int width = static_cast<int>(std::strtol(s.substr(0, 5).c_str(), nullptr, 10));
  const int height = static_cast<int>(std::strtol(s.substr(5, 5).c_str(), nullptr, 10));
  const float refresh = static_cast<float>(std::strtod(s.substr(10, 9).c_str(), nullptr));
  unsigned flags = s.substr(19, 1) == "i" ? F_INTERLACED : 0;
  int best = RES_DESKTOP;
  float bestScore = 1e30f;
  flags &= F_MODEMASK;
  for (int r = RES_DESKTOP; r < static_cast<int>(list.size()); ++r)
  {
    const Info& info = list[r];
    if ((info.flags & F_MODEMASK) != flags)
      continue;
    float score = 10 * (square_error(static_cast<float>(info.w), static_cast<float>(width)) +
                        square_error(static_cast<float>(info.h), static_cast<float>(height)) +
                        square_error(info.refresh, refresh));
    if (score < bestScore)
    {
      bestScore = score;
      best = r;
    }
  }
  return best;
}

const Mode* ModeOf(const std::vector<Info>& list, const std::vector<Mode>& modes, int res)
{
  if (res < RES_DESKTOP || res >= static_cast<int>(list.size()))
    return nullptr;
  const int place = list[res].place;
  if (place < 0 || place >= static_cast<int>(modes.size()))
    return nullptr;
  return &modes[place];
}

// Kodi running on a display
struct Kodi
{
  std::vector<Mode> modes; // of the connector, as Kodi read it
  std::vector<Info> res;
  Mode screen; // the mode on the screen (CDRMUtils::m_mode)
  int gfx = RES_DESKTOP; // CGraphicContext::m_Resolution
  int current = RES_DESKTOP; // CDisplaySettings::m_currentResolution
  std::string setting = "DESKTOP"; // videoscreen.screenmode
  bool restartTold = false;
  int modesets = 0;
};

int DisplayResolution(const Kodi& k)
{
  int r = ResolutionFromString(k.res, k.setting);
  if (r < RES_DESKTOP || r >= static_cast<int>(k.res.size()))
    r = RES_DESKTOP;
  return r;
}

// A start of Kodi on that display (InitWindowSystem(), CreateGUI())
Kodi Start(const std::vector<Mode>& modes, const std::string& setting)
{
  Kodi k;
  k.modes = modes;
  k.setting = setting;
  const int desktop = FindPreferredModeTranscribed(modes);
  k.res = Make(modes, InfoOf(modes[desktop], desktop));
  k.current = DisplayResolution(k);
  k.gfx = k.current;
  k.screen = *ModeOf(k.res, modes, k.gfx);
  return k;
}

std::string SettingOf(const Mode& m)
{
  return StringOf(InfoOf(m, 0));
}

enum class Policy
{
  KEEP,
  BY_INDEX,
  IDENTITY,
  TREE,
};

void Refresh(Kodi& k, const std::vector<Mode>& now, Policy policy)
{
  if (policy == Policy::KEEP)
    return;
  if (policy == Policy::BY_INDEX)
  {
    const int desktop = FindPreferredModeTranscribed(now);
    k.modes = now;
    k.res = Make(now, InfoOf(now[desktop], desktop));
    return;
  }
  if (policy == Policy::IDENTITY)
  {
    // the first fix: the desktop resolution, the current one and the mode on the screen kept by the
    // timing of their modes where they are offered
    const Mode* desktopMode = ModeOf(k.res, k.modes, RES_DESKTOP);
    const Mode* currentMode = ModeOf(k.res, k.modes, k.current);
    const int deskAt = desktopMode ? MODELIST::PlaceOf(now, *desktopMode) : -1;
    const int curAt = currentMode ? MODELIST::PlaceOf(now, *currentMode) : -1;
    const int scrAt = MODELIST::PlaceOf(now, k.screen);
    const int desktop = deskAt >= 0 ? deskAt : FindPreferredModeTranscribed(now);
    const bool currentWasDesktop = k.current == RES_DESKTOP;
    const bool gfxWasDesktop = k.gfx == RES_DESKTOP;
    k.modes = now;
    k.res = Make(now, InfoOf(now[desktop], desktop));
    const auto resOf = [&k](int place)
    {
      for (int i = RES_CUSTOM; i < static_cast<int>(k.res.size()); ++i)
        if (k.res[i].place == place)
          return i;
      return -1;
    };
    k.current = curAt < 0 ? DisplayResolution(k)
                          : (currentWasDesktop && deskAt >= 0 ? RES_DESKTOP : resOf(curAt));
    if (scrAt >= 0)
      k.gfx = gfxWasDesktop && deskAt == scrAt ? RES_DESKTOP : resOf(scrAt);
    else
    {
      k.gfx = k.current;
      k.screen = *ModeOf(k.res, now, k.gfx);
      ++k.modesets;
    }
    return;
  }

  // TREE: CWinSystemGbm::RefreshModes()
  const MODELIST::Plan plan = MODELIST::MakePlan(k.modes, now);
  if (plan.kind == MODELIST::Plan::Kind::SAME)
    return;
  if (plan.kind == MODELIST::Plan::Kind::REFUSE)
  {
    k.restartTold = true;
    return;
  }
  if (plan.desktop < 0 || plan.desktop >= static_cast<int>(now.size()))
  {
    k.restartTold = true; // (GetModeInfo() fails: told as a restart)
    return;
  }
  const int screenAt = MODELIST::PlaceOf(now, k.screen);
  const Mode* screen = screenAt >= 0 ? &now[screenAt] : &k.screen;
  k.modes = now;
  k.res = Make(now, InfoOf(now[plan.desktop], plan.desktop));
  k.current = DisplayResolution(k);
  const Mode* chosen = ModeOf(k.res, now, k.current);
  if (MODELIST::ScreenStays(screen, chosen))
  {
    k.gfx = k.current;
    k.screen = *screen;
    return;
  }
  k.gfx = k.current;
  if (chosen)
    k.screen = *chosen;
  ++k.modesets;
}

// What has to hold after the display changed to now: what a start on now gives
// \return the number of checks that failed
int Holds(const Kodi& before, const Kodi& k, const std::vector<Mode>& now, const std::string& name)
{
  const int failed = g_failures;
  if (MODELIST::SameList(before.modes, now))
  {
    Check(k.gfx == before.gfx && k.current == before.current && !k.restartTold &&
              k.modesets == 0 && MODELIST::SameTiming(k.screen, before.screen),
          name + ": the same modes, the same one preferred: nothing changes");
    return g_failures - failed;
  }
  const Kodi start = Start(now, before.setting);
  bool sameList = k.res.size() == start.res.size();
  for (std::size_t i = RES_DESKTOP; sameList && i < k.res.size(); ++i)
    sameList = SameInfo(k.res[i], start.res[i]) && k.res[i].place == start.res[i].place;
  Check(sameList, name + ": the resolutions and the desktop one are those of a start");
  Check(k.current == start.current, name + ": the current resolution is that of a start");
  Check(k.gfx == start.gfx, name + ": the interface is in the resolution of a start");
  Check(MODELIST::SameTiming(k.screen, start.screen), name + ": the mode on the screen is that of a start");
  const bool wasThere = MODELIST::SameTiming(before.screen, start.screen);
  Check(k.modesets == (wasThere ? 0 : 1),
        name + (wasThere ? ": the mode on the screen is that one already: no mode set"
                         : ": another mode: set once"));
  Check(!k.restartTold, name + ": no restart told");
  return g_failures - failed;
}

struct Case
{
  const char* name;
  std::vector<Mode> before;
  std::vector<Mode> now;
  std::string setting;
  bool expectRefuse = false;
  bool afterVideo = false; // the interface left in the mode of a video
};

// The LG C5 with HDMI Deep Colour on, and the same input in HDMI 1.4 mode (no 4K above 30 Hz)
const std::vector<Mode> C5 = {Pref(UHD60), UHD5994, UHD50, UHD30, UHD2997, UHD25, UHD24, UHD23976,
                              FHD60, FHD5994, FHD50, FHD24, FHD23976, FHD60I, HD60, FHD24FP,
                              FHD24TAB, FHD60SBS};
const std::vector<Mode> C5_HDMI14 = {Pref(UHD30), UHD2997, UHD25, UHD24, UHD23976, FHD60, FHD5994,
                                     FHD50, FHD24, FHD23976, FHD60I, HD60, FHD24FP, FHD24TAB};

std::vector<Case> Cases()
{
  const std::vector<Mode> monitor = {Pref(QHD60), FHD60RB, HD60};
  std::vector<Mode> otherPreferred = C5;
  otherPreferred[0].type = DRIVER;
  otherPreferred[3].type |= PREFERRED;
  std::vector<Mode> otherDriverBits = C5;
  otherDriverBits[5].type = 0;
  std::vector<Mode> dup = C5_HDMI14;
  dup.push_back(UHD24);
  return {
      {"HDMI 1.4 -> Deep Colour, settings: desktop", C5_HDMI14, C5, "DESKTOP"},
      {"Deep Colour -> HDMI 1.4, settings: desktop", C5, C5_HDMI14, "DESKTOP"},
      {"HDMI 1.4 -> Deep Colour, settings: 1080p60", C5_HDMI14, C5, SettingOf(FHD60)},
      {"Deep Colour -> HDMI 1.4, settings: 1080p60", C5, C5_HDMI14, SettingOf(FHD60)},
      {"Deep Colour -> HDMI 1.4, settings: 2160p60 (gone: the nearest)", C5, C5_HDMI14, SettingOf(UHD60)},
      {"HDMI 1.4 -> Deep Colour, settings: 2160p60 (offered again)", C5_HDMI14, C5, SettingOf(UHD60)},
      {"HDMI 1.4 -> Deep Colour, settings: 2160p30", C5_HDMI14, C5, SettingOf(UHD30)},
      {"HDMI 1.4 -> Deep Colour, settings: 1080i60", C5_HDMI14, C5, SettingOf(FHD60I)},
      {"HDMI 1.4 -> Deep Colour, the interface left in 2160p24 by a video", C5_HDMI14, C5, "DESKTOP", false, true},
      {"Deep Colour -> HDMI 1.4, settings: 1080p60, the interface left in 2160p24 by a video", C5, C5_HDMI14, SettingOf(FHD60), false, true},
      {"a monitor instead of the TV, settings: desktop", C5, monitor, "DESKTOP"},
      {"a monitor instead of the TV, settings: 2160p60", C5, monitor, SettingOf(UHD60)},
      {"the TV instead of the monitor, settings: 1440p60", monitor, C5, SettingOf(QHD60)},
      {"the same modes, another one preferred", C5, otherPreferred, "DESKTOP"},
      {"the same modes, other types but the preferred one", C5, otherDriverBits, "DESKTOP"},
      {"the same modes", C5, C5, SettingOf(FHD24)},
      {"two modes with one timing", C5_HDMI14, dup, "DESKTOP", true},
      {"no mode at all", C5, {}, "DESKTOP", true},
      {"3D modes only", C5, {FHD24FP, FHD24TAB}, "DESKTOP", true},
  };
}

void LeaveInVideoMode(Kodi& k)
{
  // after a video with "adjust refresh rate" at its start only: the interface stays in its mode
  for (const Mode& video : {UHD24, FHD24})
  {
    for (int i = RES_CUSTOM; i < static_cast<int>(k.res.size()); ++i)
    {
      if (MODELIST::SameTiming(*ModeOf(k.res, k.modes, i), video))
      {
        k.gfx = i;
        k.screen = video;
        return;
      }
    }
  }
}

int RunCases(Policy policy, const char* label)
{
  const int failed = g_failures;
  for (const Case& c : Cases())
  {
    // (a list that cannot be taken is for the tree alone: the others would index nothing)
    if (c.expectRefuse && policy != Policy::TREE)
      continue;
    Kodi start = Start(c.before, c.setting);
    if (c.afterVideo)
      LeaveInVideoMode(start);
    Kodi k = start;
    Refresh(k, c.now, policy);
    const std::string name = std::string(label) + ": " + c.name;
    if (c.expectRefuse)
    {
      if (policy == Policy::TREE)
      {
        Check(k.restartTold, name + ": refused, a restart told");
        Check(k.gfx == start.gfx && k.current == start.current &&
                  k.modes.size() == start.modes.size() && k.modesets == 0,
              name + ": refused: the resolutions stay as they were");
      }
      continue;
    }
    Holds(start, k, c.now, name);
  }
  return g_failures - failed;
}

// ---- the parts of the header ----

void TestWhen()
{
  using MODELIST::When;
  Check(MODELIST::WhenToLook(false, 1000000, 1000000, nullptr) == When::NOT_DUE,
        "no hot-plug event: nothing is done");
  Check(MODELIST::WhenToLook(false, 0, 0, "x") == When::NOT_DUE,
        "no hot-plug event, something in the way: nothing is done either");
  Check(MODELIST::WhenToLook(true, 2999, 1000000, nullptr) == When::LATER, "still settling: later");
  Check(MODELIST::WhenToLook(true, 3000, 1000000, nullptr) == When::NOW, "settled: now");
  Check(MODELIST::WhenToLook(true, 1000000, 9999, nullptr) == When::LATER, "a video not long ago: later");
  Check(MODELIST::WhenToLook(true, 1000000, 10000, nullptr) == When::NOW, "a video long ago: now");
  Check(MODELIST::WhenToLook(true, 1000000, 1000000, "x") == When::LATER, "something in the way: later");
}

void TestInTheWay()
{
  MODELIST::Busy none;
  Check(MODELIST::InTheWay(none) == nullptr, "nothing busy: nothing in the way");
  bool MODELIST::Busy::*fields[] = {
      &MODELIST::Busy::stopping,    &MODELIST::Busy::noInterface, &MODELIST::Busy::playing,
      &MODELIST::Busy::output,      &MODELIST::Busy::modeChange,  &MODELIST::Busy::dialog,
      &MODELIST::Busy::calibrating, &MODELIST::Busy::slideshow,   &MODELIST::Busy::stereo,
      &MODELIST::Busy::dark};
  const char* names[] = {"stopping", "no interface", "playing", "output", "mode change",
                         "dialog", "calibrating", "slideshow", "stereo", "dark"};
  for (std::size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
  {
    MODELIST::Busy b;
    b.*fields[i] = true;
    Check(MODELIST::InTheWay(b) != nullptr, std::string("in the way: ") + names[i]);
  }
}

void TestOf()
{
  using MODELIST::Found;
  Check(MODELIST::Of(false, true, false, false) == Found::NOT_CONNECTED, "not connected");
  Check(MODELIST::Of(false, true, true, true) == Found::NOT_CONNECTED, "not connected, whatever else");
  Check(MODELIST::Of(true, false, false, false) == Found::NO_EDID, "no EDID");
  Check(MODELIST::Of(true, true, true, false) == Found::OTHER_MODES,
        "the same EDID, other modes (the driver assessed the link again): taken");
  Check(MODELIST::Of(true, true, true, true) == Found::SAME_EDID, "the same EDID and modes: nothing");
  Check(MODELIST::Of(true, true, false, true) == Found::SAME_MODES, "another EDID, the same modes");
  Check(MODELIST::Of(true, true, false, false) == Found::OTHER_MODES, "another EDID, other modes");
}

// (fixS, 1.1) the Samsung Odyssey G60SD of the tests: its HDMI FRL link could not be assessed when
// nvidia-drm found it at the start of the PC (start 43, "no FRL: the assessment of the link found
// none"), so its modes of more than 600 MHz were not offered; a hot-plug event 16 s later assessed
// it again ("FRL up to 4 lanes at 10 Gb/s") with the same EDID, and Kodi kept the modes of its start
// (120 Hz at most) until a restart. Timings from the kernel log of that start.
const Mode G120 = Pref(M("1440p120 G60SD", 555720, 2560, 2688, 2720, 2880, 1440, 1443, 1453, 1608, 120));
const Mode G360 = M("1440p360 G60SD", 1667170, 2560, 2688, 2720, 2880, 1440, 1443, 1453, 1608, 360);
const Mode G240 = M("1440p240 G60SD", 1111250, 2560, 2688, 2720, 2880, 1440, 1443, 1453, 1608, 240);
const Mode G120CVT = M("1440p120 CVT", 497750, 2560, 2608, 2640, 2720, 1440, 1443, 1448, 1525, 120);
const Mode G60 = M("1440p60 G60SD", 277860, 2560, 2688, 2720, 2880, 1440, 1443, 1453, 1608, 60);
const Mode G1080x360 = M("1080p360 G60SD", 937750, 1920, 2008, 2040, 2160, 1080, 1083, 1093, 1206, 360);
const Mode G1080x120 = M("1080p120", 297000, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 120);

void TestLinkAssessedAgain()
{
  const std::vector<Mode> tmds = {G120, G120CVT, G60, G1080x120};
  const std::vector<Mode> frl = {G120, G360, G240, G120CVT, G60, G1080x360, G1080x120};
  // the menus were set at 360 Hz before (videoscreen.screenmode, and the mode of the display)
  Kodi k = Start(tmds, SettingOf(G360));
  Check(k.screen.clock == G120.clock || k.screen.clock == G120CVT.clock,
        "G60SD, link not assessed at the start: the menus in a mode of 120 Hz, the nearest");
  const bool sameEdid = true;
  const MODELIST::Found found =
      MODELIST::Of(true, true, sameEdid, MODELIST::SameList(k.modes, frl));
  Check(found == MODELIST::Found::OTHER_MODES,
        "G60SD, the link assessed again with the same EDID: the new modes are taken");
  if (found == MODELIST::Found::OTHER_MODES)
    Refresh(k, frl, Policy::TREE);
  Check(k.screen.clock == G360.clock,
        "G60SD: the menus go to 2560x1440 at 360 Hz, the mode of the settings, without a restart");
  // and the same modes again, with the same EDID: nothing at all
  Check(MODELIST::Of(true, true, sameEdid, MODELIST::SameList(frl, frl)) ==
            MODELIST::Found::SAME_EDID,
        "G60SD, the next hot-plug event with the same EDID and modes: nothing is done");
}

void TestParts()
{
  using L = std::vector<Mode>;
  const L a = {UHD60, UHD30, FHD60};
  L driverBits = a;
  driverBits[1].type = 0;
  L preferred = a;
  preferred[1].type |= PREFERRED;
  Check(MODELIST::SameList(a, a), "a list is the same list");
  Check(MODELIST::SameList(a, driverBits), "other types but the preferred one: the same list");
  Check(!MODELIST::SameList(a, preferred), "another one preferred: another list (a start chooses another)");
  Check(!MODELIST::SameList(a, L{UHD30, UHD60, FHD60}), "the same modes, other places: another list");
  Check(!MODELIST::SameList(a, L{UHD60, UHD30}), "one mode less: another list");
  Check(!MODELIST::SameList(a, L{UHD60, UHD30, FHD60, HD60}), "one mode more: another list");
  Check(!MODELIST::SameList(a, L{UHD60, UHD2997, FHD60}), "another clock: another list");
  Check(!MODELIST::SameList(L{FHD60}, L{FHD60RB}), "the same size and rate, another timing: another list");
  Check(!MODELIST::SameList(L{FHD24}, L{FHD24FP}), "a 3D layout: another mode");

  Check(MODELIST::PlaceOf(a, FHD60) == 2, "the place of a mode");
  Check(MODELIST::PlaceOf(a, HD60) == -1, "a mode that is not there");
  Check(MODELIST::PlaceOf(L{FHD60, UHD30, FHD60}, FHD60) == -2, "two with one timing");
  Check(MODELIST::PlaceOf(L{FHD60RB}, FHD60) == -1, "a mode is found by its timing, not by its size");
  Check(MODELIST::PlaceOf(L{UHD5994}, UHD60) == -1, "59.94 is not 60");

  Check(MODELIST::PreferredPlace(L{UHD30, Pref(FHD60), UHD60}) == 1, "the preferred mode");
  Check(MODELIST::PreferredPlace(L{UHD30, FHD60, UHD60}) == 0, "none preferred: the first largest");
  Check(MODELIST::PreferredPlace(L{Pref(FHD24FP), FHD60}) == 1, "a preferred 3D mode is not taken");
  Check(MODELIST::PreferredPlace(L{FHD24FP, FHD24TAB}) == -1, "3D modes only: none");
  Check(MODELIST::PreferredPlace(L{}) == -1, "no mode: none");
  Check(MODELIST::PreferredPlace(L{FHD60, Pref(UHD30), Pref(UHD60)}) == 1, "two preferred: the first");

  using K = MODELIST::Plan::Kind;
  Check(MODELIST::MakePlan(a, a).kind == K::SAME, "the same list: SAME");
  {
    const auto plan = MODELIST::MakePlan(C5_HDMI14, C5);
    Check(plan.kind == K::REMADE && plan.desktop == 0, "1.4 -> Deep Colour: made again, the desktop 2160p60");
  }
  {
    const auto plan = MODELIST::MakePlan(C5, C5_HDMI14);
    Check(plan.kind == K::REMADE && plan.desktop == 0, "Deep Colour -> 1.4: made again, the desktop 2160p30");
  }
  Check(MODELIST::MakePlan(a, L{}).kind == K::REFUSE, "no mode: REFUSE");
  Check(MODELIST::MakePlan(a, L{FHD60, FHD60}).kind == K::REFUSE, "two with one timing: REFUSE");
  Check(MODELIST::MakePlan(a, L{FHD24FP}).kind == K::REFUSE, "3D only: REFUSE");

  Check(MODELIST::ScreenStays(&UHD30, &UHD30), "the screen in the chosen mode: it stays");
  Check(!MODELIST::ScreenStays(&UHD30, &UHD60), "another mode chosen: it is set");
  Check(!MODELIST::ScreenStays(&FHD60, &FHD60RB), "the same size and rate, another timing: it is set");
  Check(!MODELIST::ScreenStays<Mode>(nullptr, &UHD60), "no mode known on the screen: it is set");
  Check(!MODELIST::ScreenStays<Mode>(&UHD60, nullptr), "no mode chosen: it is set");
}

// ---- random lists ----

struct Lcg
{
  uint64_t s;
  uint32_t Next()
  {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(s >> 33);
  }
  uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
};

std::vector<Mode> RandomList(Lcg& rng, const std::vector<Mode>& pool, bool allowDuplicate)
{
  std::vector<Mode> list;
  for (const Mode& m : pool)
    if (rng.Below(3) != 0)
      list.push_back(m);
  for (std::size_t i = list.size(); i > 1; --i)
    std::swap(list[i - 1], list[rng.Below(static_cast<uint32_t>(i))]);
  if (!list.empty() && rng.Below(3) != 0)
    list[rng.Below(static_cast<uint32_t>(list.size()))].type |= PREFERRED;
  if (allowDuplicate && !list.empty() && rng.Below(20) == 0)
    list.push_back(list[rng.Below(static_cast<uint32_t>(list.size()))]);
  return list;
}

void TestRandom(int runs)
{
  const std::vector<Mode> pool = {UHD60, UHD5994, UHD50, UHD30, UHD2997, UHD25, UHD24, UHD23976,
                                  FHD60, FHD5994, FHD50, FHD24, FHD23976, FHD60I, HD60, FHD24FP,
                                  FHD24TAB, FHD60SBS, QHD60, FHD60RB};
  Lcg rng{20261028};
  int planned = 0, remade = 0, refused = 0, same = 0, startMismatch = 0, treeFail = 0,
      keepFail = 0, indexFail = 0, identityFail = 0, stays = 0, sets = 0;
  const bool quiet = g_quiet;
  for (int run = 0; run < runs; ++run)
  {
    std::vector<Mode> before = RandomList(rng, pool, false);
    if (FindPreferredModeTranscribed(before) < 0)
      continue;
    std::vector<Mode> now = rng.Below(10) == 0 ? before : RandomList(rng, pool, true);
    if (rng.Below(15) == 0 && !now.empty())
    {
      now = before; // the same modes, another preferred
      for (auto& m : now)
        m.type &= ~PREFERRED;
      now[rng.Below(static_cast<uint32_t>(now.size()))].type |= PREFERRED;
    }
    if (MODELIST::PreferredPlace(now) != FindPreferredModeTranscribed(now))
      ++startMismatch;

    std::string setting = "DESKTOP";
    if (rng.Below(2) == 0)
    {
      const Mode& m = pool[rng.Below(static_cast<uint32_t>(pool.size()))];
      if (!(m.flags & MASK3D))
        setting = SettingOf(m);
    }
    Kodi start = Start(before, setting);
    if (rng.Below(3) == 0)
    {
      const int pick = RES_CUSTOM + static_cast<int>(rng.Below(static_cast<uint32_t>(
                                        start.res.size() - RES_CUSTOM)));
      start.gfx = pick;
      start.screen = *ModeOf(start.res, before, pick);
    }

    const MODELIST::Plan plan = MODELIST::MakePlan(before, now);
    ++planned;
    bool duplicate = false;
    for (std::size_t i = 0; i < now.size(); ++i)
      for (std::size_t j = i + 1; j < now.size(); ++j)
        duplicate = duplicate || MODELIST::SameTiming(now[i], now[j]);
    const bool sameList = MODELIST::SameList(before, now);
    const bool takeable = !now.empty() && !duplicate && MODELIST::PreferredPlace(now) >= 0;

    g_quiet = true;
    using K = MODELIST::Plan::Kind;
    Check(sameList == (plan.kind == K::SAME), "random: SAME exactly where the list is the same");
    if (!sameList)
    {
      Check(!takeable == (plan.kind == K::REFUSE), "random: REFUSE exactly where the list cannot be taken");
      if (plan.kind == K::REMADE)
        Check(plan.desktop == FindPreferredModeTranscribed(now), "random: the desktop is the start's");
    }
    const int f0 = g_failures;
    Kodi k = start;
    Refresh(k, now, Policy::TREE);
    if (takeable || sameList)
      Holds(start, k, now, "random tree");
    else
      Check(k.restartTold && k.gfx == start.gfx && k.current == start.current,
            "random tree: refused, nothing changed, a restart told");
    treeFail += g_failures - f0;
    if (plan.kind == K::REMADE)
      (k.modesets ? sets : stays)++;
    {
      const int failed = g_failures;
      const int checks = g_checks;
      g_silent = true;
      if (takeable && !sameList)
      {
        Kodi keep = start;
        Refresh(keep, now, Policy::KEEP);
        keepFail += Holds(start, keep, now, "random keep") > 0 ? 1 : 0;
        Kodi idx = start;
        Refresh(idx, now, Policy::BY_INDEX);
        indexFail += Holds(start, idx, now, "random by index") > 0 ? 1 : 0;
        Kodi ident = start;
        Refresh(ident, now, Policy::IDENTITY);
        identityFail += Holds(start, ident, now, "random identity") > 0 ? 1 : 0;
      }
      g_failures = failed;
      g_checks = checks;
      g_silent = false;
    }
    g_quiet = quiet;
    switch (plan.kind)
    {
      case K::SAME: ++same; break;
      case K::REMADE: ++remade; break;
      case K::REFUSE: ++refused; break;
    }
  }
  g_quiet = quiet;
  Check(startMismatch == 0, "random: PreferredPlace() is FindPreferredMode() (" +
                                std::to_string(startMismatch) + " lists differ)");
  Check(treeFail == 0, "random: the tree gives what a start gives on every list (" +
                           std::to_string(treeFail) + " failed checks)");
  std::printf("random: %d plans: %d same, %d made again (%d with the screen as it was, %d with a "
              "mode set), %d refused; lists where a start gives something else with the policy of "
              "the code before (KEEP): %d, a refresh by index: %d, the rule of a first fix (IDENTITY): %d\n",
              planned, same, remade, stays, sets, refused, keepFail, indexFail, identityFail);
  Check(keepFail > 0 && indexFail > 0 && identityFail > 0,
        "random: KEEP, BY_INDEX and IDENTITY are wrong in some cases");
}

int main(int argc, char** argv)
{
  const int runs = argc > 1 ? std::atoi(argv[1]) : 20000;
  TestWhen();
  TestInTheWay();
  TestOf();
  TestLinkAssessedAgain();
  TestParts();

  const auto count = [](Policy policy, const char* label)
  {
    const int failed = g_failures, checks = g_checks;
    g_silent = true;
    const int n = RunCases(policy, label);
    g_failures = failed;
    g_checks = checks;
    g_silent = false;
    return n;
  };
  const int keepFails = count(Policy::KEEP, "KEEP");
  const int indexFails = count(Policy::BY_INDEX, "BY_INDEX");
  const int identityFails = count(Policy::IDENTITY, "IDENTITY");
  std::printf("the cases: checks that fail with the policy of the code before (KEEP): %d; with a refresh "
              "by index: %d; with the rule of a first fix (IDENTITY): %d\n",
              keepFails, indexFails, identityFails);
  Check(keepFails > 0 && indexFails > 0 && identityFails > 0,
        "the cases tell KEEP, BY_INDEX and IDENTITY from the tree");
  RunCases(Policy::TREE, "TREE");

  TestRandom(runs);

  std::printf("%s: %d of %d checks failed\n", g_failures ? "FAILED" : "PASSED", g_failures, g_checks);
  return g_failures ? 1 : 0;
}
