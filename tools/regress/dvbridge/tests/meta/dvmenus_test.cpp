// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
/*
 * LibreELEC Hades, TEST ONLY (host): the decisions of Dolby Vision for the menus (windowing/gbm/DVMenus.h) and of
 * the move of native output into another mode with them (windowing/gbm/DVModeMove.h), from the tree.
 *
 *  1. CDVMenus::Start(): every reason, in its order; Stays(); AfterVideo(); the wait after failures; what the log
 *     is told.
 *  2. CDVModeMove::Moves() without the menus is what it was (every combination of the fields it had), and with them
 *     moves into a mode of the menus and into the mode of a native video, never into the mode of another video.
 *  3. A model of the window system (SetFullScreen(), SetVideoOutput(), the renderer's UnInit(), EndDVBridge(), the
 *     frames of the menus), as the tree has it with the menus, counting the changes of the TV's picture mode (into
 *     and out of Dolby Vision, into and out of HDR) and of the HDMI mode, for videos played from the menus; the
 *     same videos without the setting (as 1.0 RC4) for comparison.
 */
#include "windowing/gbm/DVMenus.h"
#include "windowing/gbm/DVModeMove.h"

#include <cstdio>
#include <string>

using KODI::WINDOWING::GBM::CDVMenus;
using KODI::WINDOWING::GBM::CDVModeMove;
using Why = CDVMenus::Why;

static int g_failures = 0, g_checks = 0;
static void Check(bool ok, const std::string& what)
{
  g_checks++;
  if (!ok)
  {
    g_failures++;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

static CDVMenus::State Good()
{
  CDVMenus::State s;
  s.setting = true;
  s.standard = true;
  s.window = true;
  s.possible = true;
  return s;
}

static void Decisions()
{
  Check(CDVMenus::Start(Good(), false) == Why::NONE, "everything right: they start");
  struct Case
  {
    const char* name;
    void (*spoil)(CDVMenus::State&);
    Why why;
  };
  const Case cases[] = {
      {"setting off", [](CDVMenus::State& s) { s.setting = false; }, Why::SETTING},
      {"native off", [](CDVMenus::State& s) { s.nativeOff = true; }, Why::NATIVE_OFF},
      {"disabled", [](CDVMenus::State& s) { s.disabled = true; }, Why::DISABLED},
      {"video", [](CDVMenus::State& s) { s.video = true; }, Why::VIDEO},
      {"asleep", [](CDVMenus::State& s) { s.asleep = true; }, Why::ASLEEP},
      {"not looked", [](CDVMenus::State& s) { s.looked = false; }, Why::LOOK},
      {"busy", [](CDVMenus::State& s) { s.busy = true; }, Why::BUSY},
      {"regions", [](CDVMenus::State& s) { s.regions = true; }, Why::REGIONS},
      {"stereo", [](CDVMenus::State& s) { s.stereo = true; }, Why::STEREO},
      {"window", [](CDVMenus::State& s) { s.window = false; }, Why::WINDOW},
      {"signal", [](CDVMenus::State& s) { s.standard = false; }, Why::SIGNAL},
      {"display", [](CDVMenus::State& s) { s.possible = false; }, Why::DISPLAY},
  };
  for (const auto& c : cases)
  {
    auto s = Good();
    c.spoil(s);
    Check(CDVMenus::Start(s, false) == c.why, std::string("Start(): ") + c.name);
    Check(*CDVMenus::Text(c.why) != 0, std::string("a text for ") + c.name);
  }
  Check(CDVMenus::Start(Good(), true) == Why::FAILED, "waiting after a failure: not started");
  // the order: the first reason wins (the setting before all, a video before what it leaves)
  {
    auto s = Good();
    s.setting = false;
    s.video = true;
    s.possible = false;
    Check(CDVMenus::Start(s, true) == Why::SETTING, "the setting is told before anything else");
    s.setting = true;
    Check(CDVMenus::Start(s, true) == Why::VIDEO, "a video before what the display says");
    s.video = false;
    s.busy = true;
    Check(CDVMenus::Start(s, true) == Why::BUSY, "what a video leaves before the display");
  }
  // what ends them
  Check(CDVMenus::Stays(Good(), false, false) == Why::NONE, "they stay");
  Check(CDVMenus::Stays(Good(), true, false) == Why::LOOK, "a hot-plug event ends them");
  Check(CDVMenus::Stays(Good(), false, true) == Why::VIDEO, "a game ends them");
  {
    auto s = Good();
    s.asleep = true;
    Check(CDVMenus::Stays(s, false, false) == Why::ASLEEP, "the display put to sleep ends them");
    s = Good();
    s.setting = false;
    Check(CDVMenus::Stays(s, false, false) == Why::SETTING, "the setting turned off ends them");
    s = Good();
    s.nativeOff = true;
    Check(CDVMenus::Stays(s, false, false) == Why::NATIVE_OFF, "native output turned off ends them");
    s = Good();
    s.stereo = true;
    Check(CDVMenus::Stays(s, false, false) == Why::STEREO, "a stereoscopic GUI ends them");
    // a video plays, or what it leaves: their own transitions, not Stays()
    s = Good();
    s.video = true;
    s.busy = true;
    s.possible = false;
    Check(CDVMenus::Stays(s, false, false) == Why::NONE, "a video's transitions are not Stays()'s");
  }
  // after a native video
  CDVMenus::After a;
  a.setting = a.standard = a.live = a.window = a.modeAfter = true;
  Check(CDVMenus::AfterVideo(a, false), "a native video hands the output back to the menus");
  Check(!CDVMenus::AfterVideo(a, true), "... not while they wait after a failure");
  const struct
  {
    const char* name;
    bool CDVMenus::After::*field;
    bool bad;
  } after[] = {{"setting", &CDVMenus::After::setting, false},     {"native off", &CDVMenus::After::nativeOff, true},
               {"standard", &CDVMenus::After::standard, false},   {"live", &CDVMenus::After::live, false},
               {"window", &CDVMenus::After::window, false},       {"mode after", &CDVMenus::After::modeAfter, false},
               {"asleep", &CDVMenus::After::asleep, true},        {"regions", &CDVMenus::After::regions, true},
               {"hotplug", &CDVMenus::After::hotplug, true}};
  for (const auto& f : after)
  {
    auto b = a;
    b.*f.field = f.bad;
    Check(!CDVMenus::AfterVideo(b, false), std::string("AfterVideo() refuses: ") + f.name);
  }
  // the wait after failures
  CDVMenus p;
  Check(!p.Waiting(0), "no failure: no wait");
  p.Failed(1000);
  Check(p.Waiting(1000 + CDVMenus::FIRST_WAIT_MS - 1) && !p.Waiting(1000 + CDVMenus::FIRST_WAIT_MS),
        "after the first failure, 30 s");
  p.Failed(100000);
  Check(p.Waiting(100000 + CDVMenus::SECOND_WAIT_MS - 1) && !p.Waiting(100000 + CDVMenus::SECOND_WAIT_MS),
        "after the second, 5 minutes");
  p.Failed(900000);
  Check(p.Waiting(900000 + 100LL * 3600 * 1000), "after the third, until forgiven");
  p.Forgive();
  Check(!p.Waiting(900001) && p.Failures() == 0, "forgiven: tried again");
  // the log: a reason once, until it changes or is asked again
  Check(p.Tell(Why::DISPLAY) && !p.Tell(Why::DISPLAY) && p.Tell(Why::WINDOW) && p.Tell(Why::DISPLAY),
        "a reason is told when it changes");
  p.TellAgain();
  Check(p.Tell(Why::DISPLAY), "... and again after TellAgain()");
}

// the formula of CDVModeMove::Moves() before the menus (1.0 RC4)
static bool OldMoves(const CDVModeMove::State& s)
{
  return s.captured && s.transportLive && !s.disabled && s.joins && s.transportMode && !s.linkFromScratch &&
         ((s.rendererNative && !s.endWaits) || (s.endWaits && s.nativeOpens));
}

static void Moves()
{
  unsigned same = 0, all = 0;
  for (unsigned m = 0; m < (1u << 10); m++)
  {
    CDVModeMove::State s;
    s.captured = m & 1;
    s.transportLive = m & 2;
    s.disabled = m & 4;
    s.joins = m & 8;
    s.transportMode = m & 16;
    s.rendererNative = m & 32;
    s.endWaits = m & 64;
    s.nativeOpens = m & 128;
    s.linkFromScratch = m & 256;
    s.videoOpens = m & 512;
    s.menus = false;
    all++;
    same += CDVModeMove::Moves(s) == OldMoves(s);
  }
  Check(same == all, "without the menus, Moves() is what it was in all " + std::to_string(all) + " cases");
  CDVModeMove::State s;
  s.captured = s.transportLive = s.joins = s.transportMode = true;
  s.menus = true;
  Check(CDVModeMove::Moves(s), "the menus move into another mode of the menus");
  s.videoOpens = true;
  Check(!CDVModeMove::Moves(s), "... not into the mode of a video that does not go out natively");
  s.nativeOpens = true;
  Check(CDVModeMove::Moves(s), "... into the mode of a native video that opens");
  for (int f = 0; f < 5; f++)
  {
    auto t = s;
    const char* what = f == 0 ? "disabled" : f == 1 ? "not live" : f == 2 ? "not joined" : f == 3 ? "no transport mode" : "link from scratch";
    if (f == 0) t.disabled = true;
    if (f == 1) t.transportLive = false;
    if (f == 2) t.joins = false;
    if (f == 3) t.transportMode = false;
    if (f == 4) t.linkFromScratch = true;
    Check(!CDVModeMove::Moves(t), std::string("the menus do not move: ") + what);
  }
  Check(CDVModeMove::HandsOver(true, true, true, true, true), "a native video in the same surface takes them over");
  Check(!CDVModeMove::HandsOver(true, true, false, true, true) && !CDVModeMove::HandsOver(true, true, true, false, true),
        "... not one that does not draw natively, nor one in another surface");
}

// --- the model: the screen and the TV's picture mode, the window system as the tree has it
enum class Signal
{
  SDR,
  HDR10,
  DV
};
struct Model
{
  bool menusSetting = false;
  // the screen
  std::string mode = "M60";
  Signal signal = Signal::SDR;
  int hdmiChanges = 0, pictureChanges = 0;
  // Kodi
  std::string selected = "M60", gui = "M60";
  bool needModeset = false, captured = false, live = false, menus = false, rendererNative = false;
  bool movePending = false, deferred = false;
  bool held = false; //!< a change of mode for a video that opens waits for its output (CModeChangeHold, START)
  Signal pending = Signal::SDR; //!< the signalling of the ordinary output, as Kodi asks for it

  void Commit(Signal to)
  {
    const bool modeChanges = needModeset && selected != mode;
    if (modeChanges || to != signal)
      hdmiChanges++;
    if (to != signal)
      pictureChanges++;
    if (needModeset)
      mode = selected;
    needModeset = false;
    signal = to;
  }
  void EndDVBridge(bool withMode)
  {
    menus = false;
    deferred = false;
    if (!captured)
      return;
    if (!withMode && !movePending)
      needModeset = false; // (nvidia: off and on in the mode on the screen: one change of the link)
    Commit(pending);
    captured = live = movePending = false;
  }
  //! a frame of the GUI (PresentRender())
  void GuiFrame()
  {
    if (menus)
    {
      Commit(Signal::DV);
      live = true;
      movePending = false;
      return;
    }
    if (captured)
      return;
    // DVMenusUpdate(): no video, nothing left of one (a hold is BUSY)
    if (menusSetting && !held && !rendererNative && signal == Signal::SDR && pending == Signal::SDR &&
        mode == gui)
    {
      captured = true;
      menus = true;
      Commit(Signal::DV);
      live = true;
      return;
    }
    // (while a change of mode waits for a video, the GUI goes out alone in the mode on the screen)
    if (!held && (needModeset || pending != signal))
      Commit(pending);
  }
  //! SetFullScreen(res): told by VideoPlayer for a video that opens (videoOpens, nativeOpens)
  void SetFullScreen(const std::string& res, bool videoOpens, bool nativeOpens)
  {
    if (res == selected)
      return;
    CDVModeMove::State s;
    s.captured = captured;
    s.transportLive = live;
    s.joins = captured;
    s.transportMode = true;
    s.rendererNative = rendererNative;
    s.endWaits = deferred;
    s.nativeOpens = nativeOpens;
    s.menus = menus;
    s.videoOpens = videoOpens;
    if (CDVModeMove::Moves(s))
    {
      selected = res;
      needModeset = true;
      movePending = true;
      if (deferred)
        deferred = false;
      return;
    }
    if ((deferred || movePending || menus) && captured)
    {
      selected = res;
      needModeset = true;
      EndDVBridge(true);
      held = videoOpens; // (HoldsModeForVideo() then holds it, with the mode on the screen already)
      return;
    }
    EndDVBridge(false);
    selected = res;
    needModeset = true;
    // a video's change of mode waits for its output (CModeChangeHold, START); the GUI's goes out alone
    held = videoOpens;
    if (!videoOpens)
      Commit(pending);
  }
  //! the renderer of a video is configured (SetVideoOutput(&picture), and its output)
  void Configure(bool native, Signal hdr)
  {
    rendererNative = native;
    held = false;
    if (native && captured && (menus || deferred))
    {
      menus = false; // DVMenusHandToVideo() / the hand-over of CDVModeMove
      deferred = false;
      return;
    }
    if (captured)
      EndDVBridge(false);
    if (native)
    {
      captured = true;
      Commit(Signal::DV); // the first frame carries the transport (and a held mode)
      live = true;
      return;
    }
    pending = hdr;
    Commit(pending); // the HDR signalling with the mode that waited for it (START)
  }
  //! the renderer goes (UnInit(): EndDVBridgeAfterVideo(), SetHDR(nullptr), SetVideoOutput(nullptr))
  void UnInit()
  {
    if (captured && rendererNative)
    {
      if (menusSetting)
        menus = true; // DVMenusAfterVideo()
      else if (gui != mode || selected != gui)
        deferred = true; // EndDVBridgeAfterVideo(): waits for the change of mode
      else
        EndDVBridge(false);
    }
    rendererNative = false;
    pending = Signal::SDR; // (an HDR video's end waits for the change of mode that follows: STOP)
  }
  void PlayVideo(const std::string& rate, bool native, Signal hdr)
  {
    SetFullScreen(rate, true, native);
    GuiFrame(); // the GUI while the video opens
    Configure(native, hdr);
    UnInit();
    SetFullScreen(gui, false, false);
    GuiFrame();
    GuiFrame();
  }
};

static void Counts(bool setting, const char* what, const std::string& rate, bool native, Signal hdr, int hdmi,
                   int picture)
{
  Model m;
  m.menusSetting = setting;
  m.GuiFrame(); // the menus start (with the setting)
  const int h0 = m.hdmiChanges, p0 = m.pictureChanges;
  m.PlayVideo(rate, native, hdr);
  const int h = m.hdmiChanges - h0, p = m.pictureChanges - p0;
  char line[256];
  std::snprintf(line, sizeof(line), "%-34s %s: %d HDMI change(s), %d of the picture mode (expected %d, %d)%s", what,
                setting ? "menus in DV " : "SDR menus   ", h, p, hdmi, picture,
                m.signal == (setting ? Signal::DV : Signal::SDR) && m.mode == "M60" ? "" : " - NOT BACK");
  std::printf("%s\n", line);
  Check(h == hdmi && p == picture && m.signal == (setting ? Signal::DV : Signal::SDR) && m.mode == "M60", line);
}

int main()
{
  Decisions();
  Moves();
  std::printf("changes of the display for one video played from the menus (GUI at 2160p60):\n");
  // a native Dolby Vision video
  Counts(false, "Dolby Vision at 23.976 Hz", "M24", true, Signal::DV, 2, 2);
  Counts(true, "Dolby Vision at 23.976 Hz", "M24", true, Signal::DV, 2, 0);
  Counts(false, "Dolby Vision at 60 Hz", "M60", true, Signal::DV, 2, 2);
  Counts(true, "Dolby Vision at 60 Hz", "M60", true, Signal::DV, 0, 0);
  // an HDR10 video, and an SDR one
  Counts(false, "HDR10 at 23.976 Hz", "M24", false, Signal::HDR10, 2, 2);
  Counts(true, "HDR10 at 23.976 Hz", "M24", false, Signal::HDR10, 4, 4);
  Counts(false, "SDR at 23.976 Hz", "M24", false, Signal::SDR, 2, 0);
  Counts(true, "SDR at 23.976 Hz", "M24", false, Signal::SDR, 3, 2);
  Counts(true, "SDR at 60 Hz", "M60", false, Signal::SDR, 2, 2);
  std::printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "ALL PASSED", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
