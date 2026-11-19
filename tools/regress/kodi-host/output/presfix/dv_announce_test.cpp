/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): whether the modesets of native Dolby
 * Vision output (its first transport frame, and the restoration of the ordinary output after it)
 * are told to the other subsystems (OnLostDevice() before the commit, OnResetDisplay() after it)
 * like a change of mode. Only those are what makes the audio engine close its HDMI sink
 * (ActiveAE.cpp:774-783, DISPLAYLOST); without it the sink is written through the modeset (the
 * issue #3 of the fork: 3 s of stop on i915).
 *
 * The real CHeldModeAnnouncement of the tree (ModeChangeHold.h) decides. Where it is told to the
 * window system is transcribed from the tree (WinSystemGbmGLESContext.cpp: PresentRender() before
 * the frame that sets up the transport, EndDVBridge() before the restoration; WinSystemGbm.cpp:
 * FlipDVBridgePage(), HoldModeChange(), SetFullScreen()). the code before the fix has no Modeset(): only a held
 * change of mode is told there, and the test fails.
 */
#include "windowing/gbm/ModeChangeHold.h"

#include <cstdio>
#include <string>

using KODI::WINDOWING::GBM::CHeldModeAnnouncement;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

template<typename A>
concept TellsModesets = requires(A a) { a.Modeset(); };

template<typename A>
struct WindowT
{
  A announce;
  bool lost = false; //!< m_dispReset: told lost, not told back yet
  int told = 0; //!< OnLostDevice() calls that told
  int modesets = 0; //!< full modesets committed
  int untold = 0; //!< ... of those, without the subsystems told before them
  bool transportLive = false;

  void Modeset()
  {
    if constexpr (TellsModesets<A>)
      announce.Modeset();
  }
  void OnLostDevice()
  {
    if (!lost)
    {
      lost = true;
      ++told;
    }
  }
  void SendDisplayReset() { lost = false; } // (the delay past)
  //! a commit; modeset: it is a full modeset
  void Commit(bool transport, bool modeset, bool accepted)
  {
    if (announce.Before(transport))
      OnLostDevice();
    if (modeset && accepted)
    {
      ++modesets;
      untold += !lost;
    }
    announce.After(accepted, transport);
  }
  //! PresentRender(), a frame of native output
  void TransportFrame(bool accepted = true)
  {
    if (!transportLive)
      Modeset();
    Commit(true, !transportLive, accepted);
    transportLive = transportLive || accepted;
    SendDisplayReset();
  }
  //! EndDVBridge(); withMode: K57's restoration with the change of mode (SetFullScreen() has told)
  void Restore(bool withMode = false, bool tell = true)
  {
    if (withMode)
      OnLostDevice();
    if (tell && transportLive && !withMode)
      Modeset();
    Commit(false, true, true); // FlipDVBridgePage(restore): AnnounceHeldModeChange(false) inside
    transportLive = false;
  }
  //! HoldModeChange() for a native Dolby Vision video that opens
  void HoldMode() { announce.Held(); }
};
using Window = WindowT<CHeldModeAnnouncement>;

int main()
{
  std::printf("tree: %s\n", TellsModesets<CHeldModeAnnouncement> ? "CHeldModeAnnouncement::Modeset()"
                                                                 : "the code before the fix");
  {
    Window w; // the refresh rate not adjusted, the GUI already at 2160p: no change of mode
    w.TransportFrame();
    for (int i = 0; i < 100; ++i)
      w.TransportFrame();
    w.Restore();
    w.SendDisplayReset();
    Check(w.modesets == 2 && w.untold == 0,
          "native output without a change of mode: its start and its end are told");
  }
  {
    Window w; // a change of mode held for the video: told once, with the first frame
    w.HoldMode();
    w.TransportFrame();
    w.SendDisplayReset();
    Check(w.modesets == 1 && w.untold == 0 && w.told == 1,
          "a held change of mode with the first frame: told once, as before");
  }
  {
    Window w; // K57: the end joined with the change of mode to the GUI
    w.TransportFrame();
    w.SendDisplayReset();
    w.Restore(true);
    Check(w.untold == 0 && w.told == 2,
          "the end of native output with the change of mode: told once for it");
  }
  {
    Window w; // a failure in the middle of a video: the ordinary output, at once
    w.TransportFrame();
    w.SendDisplayReset();
    w.Restore();
    Check(w.untold == 0, "the return to the ordinary output after a failure is told");
  }
  {
    Window w; // the second native output of a list (the first ended with the change of mode)
    w.TransportFrame();
    w.SendDisplayReset();
    w.Restore(true);
    w.SendDisplayReset();
    w.TransportFrame();
    w.SendDisplayReset();
    Check(w.untold == 0, "the second video of a list: its start is told");
  }
  {
    Window w; // the first frame refused, then the restoration
    w.TransportFrame(false);
    w.TransportFrame(false);
    w.Restore();
    Check(w.untold == 0 && w.told <= 2,
          "a refused first frame is told once, and the restoration after it");
  }
  {
    Window w; // the window goes (Kodi quits): not told
    w.TransportFrame();
    w.SendDisplayReset();
    const int before = w.told;
    w.Restore(false, false);
    Check(w.told == before, "a restoration while the window goes is not told");
  }
  std::printf(g_failures ? "DV-ANNOUNCE-TEST FAILED (%d)\n" : "DV-ANNOUNCE-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
