/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC3): a change of mode held for the output of a
 * video (CModeChangeHold, START; kodi-9284) whose commit, with the HDR signalling of the video,
 * the driver refuses. The real CHeldModeAnnouncement of the tree (ModeChangeHold.h) decides; the
 * window system and the driver are transcribed:
 *  - HoldModeChange(): the mode is selected and a modeset allowed (SetVideoMode(): m_need_modeset),
 *    the announcement Held();
 *  - the renderer of the video is configured: SetColorimetry()/SetHDR() queue their properties
 *    and allow a modeset (SetActive(true)), and the hold ends (CONFIGURED);
 *  - each frame of the ordinary output: AnnounceHeldModeChange() (Before()), CDRMAtomic::FlipPage()
 *    (a modeset allowed when one is pending, which it clears; the request committed, or, when the
 *    driver refuses its test, the request before it with the framebuffer of the frame), then
 *    HeldModeChangeCommitted(): the code before the fix passes true whatever happened; the tree passes whether
 *    the commit went out as asked (CDRMAtomic::LastCommitRefused()) and acts on what After() says.
 * The driver here refuses the mode and the HDR metadata in one commit (as a link that cannot carry
 * that mode at that depth would), and takes each of them alone.
 */
#include "windowing/gbm/ModeChangeHold.h"

#include <cstdio>
#include <set>
#include <string>
#include <type_traits>

using KODI::WINDOWING::GBM::CHeldModeAnnouncement;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

// the tree's After() tells what to do; the code before the fix's returns nothing
template<typename A>
concept Acts = requires(A a) {
  a.After(true, false);
  requires !std::is_void_v<decltype(a.After(true, false))>;
};

template<typename A>
struct SystemT
{
  A announce;
  bool needModeset = false;
  std::string selected = "A"; //!< the mode Kodi selected
  std::set<std::string> queued; //!< the request being filled
  std::string screenMode = "A";
  bool screenHdr = false;
  int lost = 0; //!< OnLostDevice() calls, before a commit that changed the mode
  int modeChanges = 0;
  bool hdrQueuedLast = false; //!< m_queuedHdrMetadata

  bool DriverTakes(bool modeset, const std::set<std::string>& props) const
  {
    return !(modeset && selected != screenMode && props.count("HDR"));
  }
  void Frame()
  {
    if (announce.Before(false))
      ++lost;
    const bool modeset = needModeset;
    needModeset = false;
    bool refused = false;
    if (DriverTakes(modeset, queued))
    {
      if (modeset && selected != screenMode)
      {
        screenMode = selected;
        ++modeChanges;
      }
      if (queued.count("HDR"))
        screenHdr = true;
    }
    else
      refused = true; // the request before it, with this frame's framebuffer: nothing changes
    queued.clear();
    if constexpr (Acts<A>)
    {
      switch (announce.After(!refused, false, refused))
      {
        case A::Then::MODE_ALONE:
          needModeset = true; // AllowModeset()
          break;
        case A::Then::OUTPUT_AFTER:
          // RequeueOutputProperties()
          if (hdrQueuedLast)
            queued.insert("HDR");
          needModeset = true;
          break;
        default:
          break;
      }
    }
    else
      announce.After(true, false);
  }
};
using System = SystemT<CHeldModeAnnouncement>;

int main()
{
  std::printf("tree: %s\n", Acts<CHeldModeAnnouncement> ? "CHeldModeAnnouncement::After() acts"
                                                         : "the code before the fix");
  {
    System s;
    // HoldModeChange(): mode B for an HDR10 video, held
    s.selected = "B";
    s.needModeset = true;
    s.announce.Held();
    // the renderer is configured: its HDR signalling is queued, the hold ends
    s.queued.insert("HDR");
    s.hdrQueuedLast = true;
    s.needModeset = true;
    for (int i = 0; i < 6; ++i)
      s.Frame();
    std::printf("model: mode on the screen %s, HDR %s, %d change(s) of mode, told %d time(s)\n",
                s.screenMode.c_str(), s.screenHdr ? "on" : "off", s.modeChanges, s.lost);
    Check(s.screenMode == "B",
          "a held change of mode whose commit with the output of the video is refused goes out "
          "after it (alone), not taken as made");
    Check(s.screenHdr, "... and the output of the video (its HDR signalling) after it");
    Check(s.lost >= 2, "... the change that goes out alone is told before its commit, as the first "
                       "one was");
  }
  {
    // the driver takes the commit of both: nothing else is done (one commit, one announcement)
    System s;
    s.selected = "B";
    s.needModeset = true;
    s.announce.Held();
    s.queued.insert("SDR-colorimetry");
    s.needModeset = true;
    for (int i = 0; i < 6; ++i)
      s.Frame();
    Check(s.screenMode == "B" && s.modeChanges == 1 && s.lost == 1,
          "a held change that the driver takes: one commit, told once, nothing queued again");
  }
  {
    // a restoration of the ordinary output that carries a held change and is refused (a commit of
    // its own: nothing went out in its place, and it is made again later with what it carried) is
    // not told again, as before
    CHeldModeAnnouncement a;
    a.Held();
    a.Before(false);
    a.After(false, false);
    Check(a.Pending() == CHeldModeAnnouncement::Step::NONE,
          "a refused restoration that carried a held change: not told again (as before)");
  }
  std::printf(g_failures ? "HELD-MODE-TEST FAILED (%d)\n" : "HELD-MODE-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
