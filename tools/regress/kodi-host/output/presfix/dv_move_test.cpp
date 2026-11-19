/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): how many times the display changes
 * when native Dolby Vision output passes from one video to the next one with another mode.
 *
 * The window system and the drivers are transcribed (WinSystemGbmGLESContext.cpp: SetFullScreen(),
 * SetVideoOutput(), EndDVBridgeAfterVideo(), EndDVBridge(), PresentRender(); WinSystemGbm.cpp:
 * FlipDVBridgePage(): nvidia-drm restores the ordinary output by turning the head off and on, two
 * changes, unless the restoration goes with a change of mode; LinuxRendererGLES: UnInit() and
 * Configure(); RenderManager: a file opened over another keeps its renderer when the pictures have
 * the same parameters, and reconfigures it otherwise; the mode then comes from UpdateResolution()).
 * With the tree's windowing/gbm/DVModeMove.h the real CDVModeMove decides where the tree asks it;
 * the code before the fix has none of it.
 *
 * A change of the display: a commit that changes the mode, or that starts or ends the transport.
 * GUI mode: 2160p at 59.94 Hz; the setting "adjust the refresh rate" on start and stop.
 */
#if __has_include("windowing/gbm/DVModeMove.h")
#include "windowing/gbm/DVModeMove.h"
#define HAVE_FIX 1
using KODI::WINDOWING::GBM::CDVModeMove;
#else
#define HAVE_FIX 0
#endif

#include <cstdio>
#include <string>

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

struct Window
{
  bool nvidia = true;
  // the screen
  std::string mode = "M60";
  bool transport = false;
  int changes = 0;
  // Kodi
  std::string selected = "M60";
  std::string gui = "M60";
  bool needModeset = false;
  bool captured = false;
  bool live = false;
  bool deferred = false;
  bool handOver = false;
  bool movePending = false;
  bool rendererNative = false;
  bool refuseTransport = false; //!< the driver refuses the frames that carry the mode
  int refusedFrames = 0;

  void Commit(bool transportAfter, bool withModeAlso = true)
  {
    const bool modeChanges = needModeset && withModeAlso && selected != mode;
    if (modeChanges || transportAfter != transport)
      ++changes;
    if (needModeset && withModeAlso)
    {
      mode = selected;
      needModeset = false;
    }
    transport = transportAfter;
  }
  bool EndDVBridge(bool withMode)
  {
    deferred = false;
    handOver = false;
    if (!captured)
      return true;
    if (live && nvidia && !withMode && !movePending)
      ++changes; // turned off and on: the off first
    Commit(false);
    captured = false;
    live = false;
    movePending = false;
    return true;
  }
  void RendererUnInit()
  {
    // EndDVBridgeAfterVideo(): waits for the change of mode to the GUI mode when there is one
    if (captured && gui != mode)
      deferred = true;
    else
      EndDVBridge(false);
    rendererNative = false;
  }
  void RendererConfigure(bool native)
  {
    rendererNative = native;
    // SetVideoOutput(&picture)
    bool handed = false;
#if HAVE_FIX
    const bool waiting = deferred || handOver;
    if (waiting && CDVModeMove::HandsOver(captured, true, rendererNative, true, waiting))
    {
      deferred = false;
      handOver = false;
      handed = true;
    }
#endif
    if (!handed && captured)
      EndDVBridge(false);
    if (native && !captured)
    {
      captured = true; // BeginDVBridge(): the output is captured, the transport not yet sent
      live = false;
    }
  }
  //! SetFullScreen(res); nativeOpens: VideoPlayer told of it for a native video that opens
  void SetFullScreen(const std::string& res, bool nativeOpens = false)
  {
    if (res == selected)
      return; // CGraphicContext: the same resolution
#if HAVE_FIX
    CDVModeMove::State s;
    s.captured = captured;
    s.transportLive = live;
    s.joins = captured;
    s.transportMode = true;
    s.rendererNative = rendererNative;
    s.endWaits = deferred;
    s.nativeOpens = nativeOpens;
    if (CDVModeMove::Moves(s))
    {
      selected = res;
      needModeset = true;
      movePending = true;
      if (deferred)
      {
        deferred = false;
        handOver = true;
      }
      return;
    }
    if ((deferred || movePending) && captured)
#else
    (void)nativeOpens;
    if (deferred && captured)
#endif
    {
      // K57: the restoration with the change of mode, in one commit
      selected = res;
      needModeset = true;
      EndDVBridge(true);
      return;
    }
    EndDVBridge(false);
    selected = res;
    needModeset = true;
    Commit(false); // CWinSystemGbm::SetFullScreen(): FlipPage()
  }
  //! a pass of the render loop
  void Frame()
  {
    // BeginDVBridge(): a renderer that draws natively captures the output again
    if (!captured && rendererNative)
    {
      captured = true;
      live = false;
    }
    if (captured && rendererNative)
    {
      if (refuseTransport && needModeset)
      {
        // refused: after 24 of them, the ordinary output (DVBridgeFrameShown())
        if (++refusedFrames >= 24)
        {
          // DisableNativeDVOutput(), and the renderer is configured again for the ordinary
          // output with the next picture
          rendererNative = false;
          EndDVBridge(false);
        }
        return;
      }
      Commit(true);
      live = true;
      movePending = false;
      handOver = false;
      return;
    }
    if (!captured && needModeset)
      Commit(transport);
  }
  void Frames(int n)
  {
    for (int i = 0; i < n; ++i)
      Frame();
  }
  void Deadline()
  {
    // PresentRender(): the end that waited, or the output kept for a video, with no renderer
    if (deferred)
      EndDVBridge(false); // (the ordinary output in the mode of the video)
    if (handOver)
      EndDVBridge(true);
  }
};

//! native video A at 23.976 Hz on the screen
static Window Playing(bool nvidia)
{
  Window w;
  w.nvidia = nvidia;
  w.SetFullScreen("M24", true); // (first file: a held change, here simply the mode)
  w.Frames(1);
  w.RendererConfigure(true);
  w.Frames(10);
  w.changes = 0;
  return w;
}

int main()
{
  std::printf("tree: %s\n", HAVE_FIX ? "CDVModeMove (DVModeMove.h)" : "the code before the fix");
  for (const bool nvidia : {true, false})
  {
    const std::string d = nvidia ? " (nvidia-drm)" : " (amdgpu, i915)";
    {
      // a file opened over A, the same renderer, at 59.94: the mode from UpdateResolution()
      Window w = Playing(nvidia);
      w.SetFullScreen("M60");
      w.Frames(10);
      std::printf("model: over, same renderer%s: %d change(s)\n", d.c_str(), w.changes);
      Check(w.changes == 1 && w.mode == "M60" && w.transport,
            "a native file opened over another at another rate, same renderer: one change" + d);
    }
    {
      // the next of a list after A ended: VideoPlayer sets the mode before the renderer is made
      Window w = Playing(nvidia);
      w.RendererUnInit();
      w.SetFullScreen("M60", true);
      w.Frames(5);
      w.RendererConfigure(true);
      w.Frames(10);
      std::printf("model: next of a list%s: %d change(s)\n", d.c_str(), w.changes);
      Check(w.changes == 1 && w.mode == "M60" && w.transport,
            "the next native file of a list at another rate: one change" + d);
    }
    {
      // a file opened over A whose pictures make the renderer be configured again
      Window w = Playing(nvidia);
      w.RendererUnInit();
      w.RendererConfigure(true);
      w.Frames(3);
      w.SetFullScreen("M60");
      w.Frames(10);
      std::printf("model: over, renderer made again%s: %d change(s)\n", d.c_str(), w.changes);
      Check(w.changes == 1 && w.mode == "M60" && w.transport,
            "a native file opened over another, renderer configured again: one change" + d);
    }
    {
      // the next of a list at the same rate as A: no change of mode at all
      Window w = Playing(nvidia);
      w.RendererUnInit();
      w.SetFullScreen("M24", true);
      w.RendererConfigure(true);
      w.Frames(10);
      std::printf("model: next at the same rate%s: %d change(s)\n", d.c_str(), w.changes);
      Check(w.changes == 0 && w.transport,
            "the next native file of a list at the same rate: no change" + d);
    }
    {
      // the next of a list is no native video: as before, the restoration with its mode
      Window w = Playing(nvidia);
      w.RendererUnInit();
      w.SetFullScreen("M60", false);
      const bool restoredAtOnce = !w.transport && w.mode == "M60";
      w.RendererConfigure(false);
      w.Frames(10);
      Check(restoredAtOnce && w.changes == 1 && w.mode == "M60" && !w.transport,
            "the next file of a list is not native: the ordinary output with its mode at once, one "
            "change, as before" + d);
    }
    {
      // the next of a list never gets its renderer: the output comes back in the new mode
      Window w = Playing(nvidia);
      w.RendererUnInit();
      w.SetFullScreen("M60", true);
      w.Frames(5);
      w.Deadline();
      w.Frames(5);
      Check(!w.captured && !w.transport && w.mode == "M60" && w.changes == 1,
            "no video takes the output kept for it: the ordinary output in the new mode, in one "
            "commit" + d);
    }
#if HAVE_FIX
    {
      // the driver refuses the frames that carry the mode: fail closed, in the new mode
      Window w = Playing(nvidia);
      w.refuseTransport = true;
      w.SetFullScreen("M60");
      w.Frames(30);
      Check(!w.captured && !w.transport && w.mode == "M60",
            "frames that carry the mode refused: the ordinary output in that mode" + d);
    }
#endif
  }
  std::printf(g_failures ? "DV-MOVE-TEST FAILED (%d)\n" : "DV-MOVE-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
