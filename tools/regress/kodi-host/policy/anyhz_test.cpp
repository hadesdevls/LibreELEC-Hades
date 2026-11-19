/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (any-refresh stream): host test of the decisions that Dolby Vision and HDR take at any
 * refresh rate of the GUI, and of how many times the display changes at the start and the end of a
 * video:
 *  - CDVPresentStall (DVPresentStall.h): frames that the kernel takes but whose flips do not come
 *    end native output;
 *  - DVBRIDGE::IsTransportMode(), RefreshRateKeepsOrdinary(), OutputOfStream()
 *    (DVPlaybackPolicy.h): GUI modes at 100, 119.88, 120 and 144 Hz;
 *  - CModeChangeHold (ModeChangeHold.h): its decisions, each condition in both directions, its
 *    lifecycle, and a model of the commits of each transition that counts the changes of the
 *    display, before (the code before) and after (this stream), on nvidia-drm, amdgpu and i915.
 * Built with -I of the Kodi tree; each check prints FAIL on its own line; the last line says PASSED.
 */
#include "cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h"
#include "windowing/gbm/DVPresentStall.h"
#include "windowing/gbm/DisplayLook.h"
#include "windowing/gbm/ModeChangeHold.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using KODI::WINDOWING::GBM::CDisplayLook;
using KODI::WINDOWING::GBM::CDVPresentStall;
using KODI::WINDOWING::GBM::CHeldModeAnnouncement;
using KODI::WINDOWING::GBM::CModeChangeHold;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

// ---------------------------------------------------------------------------------------------
// CDVPresentStall
// ---------------------------------------------------------------------------------------------
static void StallTests()
{
  using V = CDVPresentStall::Verdict;
  {
    // a film at any rate of the transport, frames in time, and hiccups well under the threshold
    for (const double period : {1000.0 / 23.976, 1000.0 / 24, 1000.0 / 25, 1000.0 / 29.97, 1000.0 / 50,
                                1000.0 / 59.94, 1000.0 / 60})
    {
      CDVPresentStall s;
      bool gaveUp = false;
      for (int i = 0; i < 20000; ++i)
      {
        const double ms = i % 997 == 0 ? 1500.0 : (i % 13 == 0 ? 2.5 * period : period);
        gaveUp |= s.Presented(true, ms) != V::NONE;
      }
      Check(!gaveUp, "frames in time (and hiccups of 1.5 s) never end native output, period " +
                         std::to_string(period));
    }
  }
  {
    // nvidia-drm: a flip that does not come is completed after 3 s; the first is told, the second
    // ends native output
    CDVPresentStall s;
    Check(s.Presented(true, 3000) == V::STALLED, "nvidia-drm: the first 3 s frame stalls");
    Check(s.Stalls() == 1, "one stall counted");
    Check(s.Presented(true, 3000) == V::GIVE_UP, "nvidia-drm: the second 3 s frame in a row ends it");
    Check(s.Stalls() == 0, "after the end, nothing counted");
  }
  {
    // the kernel's helpers (amdgpu, i915): 10 s each; one is enough
    CDVPresentStall s;
    Check(s.Presented(true, 10000) == V::GIVE_UP, "helpers: one 10 s frame ends it");
  }
  {
    // a frame in time between two stalls: they are not in a row
    CDVPresentStall s;
    Check(s.Presented(true, 3000) == V::STALLED, "stall 1");
    Check(s.Presented(true, 41.7) == V::NONE, "a frame in time");
    Check(s.Stalls() == 0, "a frame in time starts again");
    Check(s.Presented(true, 3000) == V::STALLED, "stall 1 again, not the end");
  }
  {
    // the first frame of a native output and frames that carry a modeset do not count, either way
    CDVPresentStall s;
    Check(s.Presented(false, 10000) == V::NONE, "a modeset of 10 s does not count");
    Check(s.Presented(true, 3000) == V::STALLED, "stall after it");
    Check(s.Presented(false, 40) == V::NONE, "a modeset in time between them: not counted");
    Check(s.Stalls() == 1, "... and does not start again");
    Check(s.Presented(true, 3000) == V::GIVE_UP, "the second stall in a row of plain frames ends it");
  }
  {
    // the thresholds
    CDVPresentStall s;
    Check(s.Presented(true, CDVPresentStall::STALL_MS - 0.01) == V::NONE, "just under 2 s: in time");
    Check(s.Presented(true, CDVPresentStall::STALL_MS) == V::STALLED, "2 s: a stall");
    s.Reset();
    Check(s.Stalls() == 0, "Reset() forgets");
    Check(s.Presented(true, CDVPresentStall::GIVE_UP_MS - 0.01) == V::STALLED, "just under 5 s: a stall");
    s.Reset();
    Check(s.Presented(true, CDVPresentStall::GIVE_UP_MS) == V::GIVE_UP, "5 s: the end at once");
    s.Reset();
    Check(s.Presented(true, std::nan("")) == V::NONE, "no time: not a stall");
  }
}

// ---------------------------------------------------------------------------------------------
// DVPlaybackPolicy.h, at the refresh rates of a GUI
// ---------------------------------------------------------------------------------------------
static void PolicyTests()
{
  for (const double rate : {24000.0 / 1001, 24.0, 25.0, 30000.0 / 1001, 30.0, 50.0, 60000.0 / 1001, 60.0})
  {
    Check(DVBRIDGE::IsTransportMode(3840, 2160, rate, false), "transport at " + std::to_string(rate));
    Check(!DVBRIDGE::RefreshRateKeepsOrdinary(true, rate, false),
          "a fixed rate of the transport does not keep DV off: " + std::to_string(rate));
  }
  for (const double rate : {48.0, 100.0, 120000.0 / 1001, 120.0, 144.0, 23.9, 61.0})
  {
    Check(!DVBRIDGE::IsTransportMode(3840, 2160, rate, false), "no transport at " + std::to_string(rate));
    Check(DVBRIDGE::RefreshRateKeepsOrdinary(true, rate, false),
          "a fixed GUI rate of " + std::to_string(rate) + " Hz keeps DV off, and says why");
    Check(!DVBRIDGE::RefreshRateKeepsOrdinary(false, rate, false),
          "with the adjustment on, the GUI rate " + std::to_string(rate) + " is not the reason");
    Check(!DVBRIDGE::RefreshRateKeepsOrdinary(true, rate, true),
          "an interlaced or 3D mode is not this reason: " + std::to_string(rate));
  }
  using O = DVBRIDGE::OpenedOutput;
  Check(DVBRIDGE::OutputOfStream(true, true, true) == O::NATIVE_DV, "native DV first");
  Check(DVBRIDGE::OutputOfStream(false, true, false) == O::HDR, "an HDR type: HDR");
  Check(DVBRIDGE::OutputOfStream(false, false, true) == O::HDR, "PQ or HLG transfer: HDR");
  Check(DVBRIDGE::OutputOfStream(false, false, false) == O::ORDINARY, "SDR: ordinary");
}

// ---------------------------------------------------------------------------------------------
// CModeChangeHold: decisions and lifecycle
// ---------------------------------------------------------------------------------------------
static CModeChangeHold::Change Possible()
{
  CModeChangeHold::Change c;
  c.atomic = true;
  c.active = true;
  c.sameWindow = true;
  c.mode3D = false;
  c.stereoBusy = false;
  c.captured = false;
  c.timingChanges = true;
  return c;
}

static void HoldDecisionTests()
{
  using Video = CModeChangeHold::Video;
  const CModeChangeHold::Change ok = Possible();
  Check(CModeChangeHold::StartHolds(Video::HDR, ok), "START holds for HDR");
  Check(CModeChangeHold::StartHolds(Video::NATIVE_DV, ok), "START holds for native DV");
  Check(!CModeChangeHold::StartHolds(Video::ORDINARY, ok), "START does not hold for SDR");
  Check(!CModeChangeHold::StartHolds(Video::NONE, ok), "START does not hold without a video");
  // each condition, the other way
  const auto flipped = [&](auto set, const char* what) {
    CModeChangeHold::Change c = ok;
    set(c);
    Check(!CModeChangeHold::StartHolds(Video::HDR, c), std::string("START: not with ") + what);
    Check(!CModeChangeHold::StartHolds(Video::NATIVE_DV, c), std::string("START DV: not with ") + what);
    Check(!CModeChangeHold::StopHolds(true, true, c), std::string("STOP: not with ") + what);
  };
  flipped([](auto& c) { c.atomic = false; }, "legacy modesetting");
  flipped([](auto& c) { c.active = false; }, "the output off");
  flipped([](auto& c) { c.sameWindow = false; }, "another window size");
  flipped([](auto& c) { c.mode3D = true; }, "a 3D mode");
  flipped([](auto& c) { c.stereoBusy = true; }, "3D output busy");
  flipped([](auto& c) { c.captured = true; }, "native DV captured");
  flipped([](auto& c) { c.timingChanges = false; }, "the same mode");
  Check(CModeChangeHold::StopHolds(true, true, ok), "STOP holds with HDR on the screen and a mode to follow");
  Check(!CModeChangeHold::StopHolds(false, true, ok), "STOP: not without HDR on the screen");
  Check(!CModeChangeHold::StopHolds(true, false, ok), "STOP: not without a change of mode to follow");

  // the end of native DV joins the next mode with every backend (no backend in the decision), with
  // the conditions of K57: captured, active, atomic, same window, no 3D mode, another mode
  CModeChangeHold::Change dv = ok;
  dv.captured = true;
  Check(CModeChangeHold::DVRestoreJoins(dv), "DV end joins the next mode");
  dv.stereoBusy = true;
  Check(CModeChangeHold::DVRestoreJoins(dv), "DV end joins whatever the bookkeeping of 3D (as K57)");
  for (int i = 0; i < 6; ++i)
  {
    CModeChangeHold::Change c = ok;
    c.captured = true;
    const char* what[] = {"not captured", "legacy", "off", "another window", "3D mode", "same mode"};
    switch (i)
    {
      case 0: c.captured = false; break;
      case 1: c.atomic = false; break;
      case 2: c.active = false; break;
      case 3: c.sameWindow = false; break;
      case 4: c.mode3D = true; break;
      default: c.timingChanges = false; break;
    }
    Check(!CModeChangeHold::DVRestoreJoins(c), std::string("DV end does not join: ") + what[i]);
  }
}

static void HoldLifecycleTests()
{
  using Kind = CModeChangeHold::Kind;
  using End = CModeChangeHold::End;
  {
    CModeChangeHold h;
    Check(h.Commits(0), "nothing held: commits");
    h.Start(1000);
    Check(h.Holding() == Kind::START, "START held");
    Check(!h.Commits(1000), "held at its start");
    Check(!h.Commits(1000 + CModeChangeHold::START_MS - 1), "held until its deadline");
    Check(h.Configured(), "configured ends it");
    Check(h.Holding() == Kind::NONE, "not held after");
    const auto t = h.Take();
    Check(t.first == Kind::START && t.second == End::CONFIGURED, "told: START, configured");
    Check(h.Take().second == End::NONE, "told once");
    Check(h.Commits(2000), "commits after");
  }
  {
    CModeChangeHold h;
    h.Start(0);
    Check(h.Commits(CModeChangeHold::START_MS), "START: commits at its deadline");
    const auto t = h.Take();
    Check(t.first == Kind::START && t.second == End::DEADLINE, "told: START, deadline");
    Check(!h.Configured(), "a late configure ends nothing");
  }
  {
    CModeChangeHold h;
    h.Start(0);
    Check(h.ModeChange(), "another change of mode drops START");
    Check(h.Take().second == End::CANCELLED, "told: cancelled (the held mode is not merged)");
  }
  {
    CModeChangeHold h;
    h.Stop(0);
    Check(h.Holding() == Kind::STOP, "STOP held");
    Check(!h.Commits(CModeChangeHold::STOP_MS - 1), "STOP held until its deadline");
    Check(h.ModeChange(), "the change of mode ends STOP");
    const auto t = h.Take();
    Check(t.first == Kind::STOP && t.second == End::MODE, "told: STOP, with the mode");
  }
  {
    CModeChangeHold h;
    h.Stop(0);
    Check(h.Configured(), "a renderer configured again ends STOP (a video goes on)");
    Check(h.Take().second == End::CANCELLED, "told: cancelled");
  }
  {
    CModeChangeHold h;
    h.Stop(0);
    Check(h.Commits(CModeChangeHold::STOP_MS), "STOP: commits at its deadline");
    Check(h.Take().second == End::DEADLINE, "told: deadline");
  }
  {
    CModeChangeHold h;
    Check(!h.Cancel(), "nothing to cancel");
    h.Start(0);
    Check(h.Cancel(), "cancel");
    Check(h.Commits(1), "commits after a cancel");
    h.Stop(0);
    h.Start(10);
    Check(h.Holding() == Kind::START, "a new hold replaces the one before");
  }
}

// ---------------------------------------------------------------------------------------------
// The model: the commits of a transition, and the changes of the display they make
// ---------------------------------------------------------------------------------------------
enum class Backend
{
  NVIDIA,
  AMDGPU,
  I915,
};
static const char* Name(Backend b)
{
  return b == Backend::NVIDIA ? "nvidia" : b == Backend::AMDGPU ? "amdgpu" : "i915";
}

enum class Content
{
  SDR,
  HDR10,
  HLG,
  DV,
};
static const char* Name(Content c)
{
  return c == Content::SDR ? "SDR" : c == Content::HDR10 ? "HDR10" : c == Content::HLG ? "HLG" : "DV";
}

struct Output
{
  double rate = 0; // the mode
  bool hdr = false; // HDR signalling (HDR_OUTPUT_METADATA)
  bool dv = false; // the transport of native DV (8 bpc, Dolby VSIF)
};

// What makes the TV set itself up again (read in the drivers, the K57 tests and decision 0028):
// another mode on every backend; the transport on or off on every backend (amdgpu and i915: the
// tunnel property forces a full modeset; nvidia-drm: 8 bpc, dithering and the VSIF are a modeset);
// HDR signalling on or off on amdgpu (amdgpu_dm forces a modeset to enter or leave HDR) and i915
// (the Colorspace property, BT.2020, is a modeset); not on nvidia-drm, where Kodi's HDR10 keeps the
// driver's depth (an assumption: earlier tests timed an HDR10 end at one change, 1.5 s).
static bool DisplayChanges(Backend b, const Output& from, const Output& to)
{
  if (std::fabs(from.rate - to.rate) > 0.005 || from.dv != to.dv)
    return true;
  return b != Backend::NVIDIA && from.hdr != to.hdr;
}

struct Model
{
  Backend backend;
  bool after; // this stream's behaviour; else the code before's
  Output screen; // what the display has
  int changes = 0;
  int commits = 0;
  void Commit(const Output& o)
  {
    ++commits;
    if (DisplayChanges(backend, screen, o))
      ++changes;
    screen = o;
  }
};

// A video of this content starts at its rate, from the GUI at gui Hz, the refresh rate adjusted to
// the video; its renderer is configured configureMs after VideoPlayer set its mode (never: < 0)
static Model Start(Backend b, bool after, double gui, double video, Content content, int64_t configureMs)
{
  Model m{b, after, {gui, false, false}};
  const Output videoOut{video, content == Content::HDR10 || content == Content::HLG, content == Content::DV};
  CModeChangeHold hold;
  CModeChangeHold::Change c = Possible();
  c.timingChanges = std::fabs(gui - video) > 0.005;
  const auto kind = content == Content::DV  ? CModeChangeHold::Video::NATIVE_DV
                    : content == Content::SDR ? CModeChangeHold::Video::ORDINARY
                                              : CModeChangeHold::Video::HDR;
  // SetFullScreen() for the mode VideoPlayer chose: committed at once, or held
  Output pending{video, false, false};
  if (after && CModeChangeHold::StartHolds(kind, c))
    hold.Start(0);
  else if (c.timingChanges)
    m.Commit(pending);
  // frames of the GUI every 10 ms until the renderer is configured; then the output of the video
  // goes with the next commit (native DV: its first transport frame)
  for (int64_t t = 10; t <= 6000; t += 10)
  {
    if (configureMs >= 0 && t == configureMs)
    {
      hold.Configured();
      pending = videoOut;
      // the next commit carries all that is pending
      if (hold.Commits(t))
        m.Commit(pending);
      continue;
    }
    if (hold.Commits(t) && (pending.rate != m.screen.rate || pending.hdr != m.screen.hdr ||
                            pending.dv != m.screen.dv))
      m.Commit(pending);
  }
  return m;
}

// A video of this content at its rate stops; Kodi goes back to the GUI at gui Hz modeMs after the end
// of its output (never: < 0)
static Model Stop(Backend b, bool after, double gui, double video, Content content, int64_t modeMs)
{
  const Output videoOut{video, content == Content::HDR10 || content == Content::HLG, content == Content::DV};
  Model m{b, after, videoOut};
  CModeChangeHold hold;
  CModeChangeHold::Change c = Possible();
  // (what the window system foresees: the GUI mode is another one; whether that change comes is
  // modeMs)
  c.timingChanges = std::fabs(gui - video) > 0.005;
  Output pending{video, false, false};
  bool dvDeferred = false;
  if (content == Content::DV)
  {
    // EndDVBridgeAfterVideo(): K57 joins on nvidia-drm only before this stream, on every backend after
    CModeChangeHold::Change dv = c;
    dv.captured = true;
    const bool joins = (after || b == Backend::NVIDIA) && CModeChangeHold::DVRestoreJoins(dv);
    if (joins)
      dvDeferred = true; // waits DV_END_WAIT (2 s) for the change of mode
    else
      m.Commit(pending); // the transport ends in the mode of the video (nvidia: off and on)
  }
  else if (after)
  {
    // SetVideoOutput(nullptr): HoldEndOfVideo()
    if (CModeChangeHold::StopHolds(videoOut.hdr, true, c))
      hold.Stop(0);
  }
  for (int64_t t = 10; t <= 6000; t += 10)
  {
    if (modeMs >= 0 && t == modeMs)
    {
      // SetFullScreen(): EndModeHoldForModeChange(), the K57 join, then the commit of the mode
      hold.ModeChange();
      dvDeferred = false;
      pending = {gui, false, false};
      m.Commit(pending);
      continue;
    }
    if (dvDeferred)
    {
      if (t >= 2000)
      {
        dvDeferred = false;
        m.Commit(pending); // no change of mode came: restored in the mode of the video
      }
      continue;
    }
    if (hold.Commits(t) && (pending.rate != m.screen.rate || pending.hdr != m.screen.hdr ||
                            pending.dv != m.screen.dv))
      m.Commit(pending);
  }
  return m;
}

static void ModelTests()
{
  const double guis[] = {60.0, 100.0, 120000.0 / 1001, 120.0, 144.0};
  const double videos[] = {24000.0 / 1001, 24.0, 25.0, 30000.0 / 1001, 50.0, 60000.0 / 1001};
  int lines = 0;
  for (const Backend b : {Backend::NVIDIA, Backend::AMDGPU, Backend::I915})
    for (const Content content : {Content::SDR, Content::HDR10, Content::HLG, Content::DV})
      for (const double gui : guis)
        for (const double video : videos)
        {
          const bool modeChanges = std::fabs(gui - video) > 0.005;
          const bool hdr = content == Content::HDR10 || content == Content::HLG;
          const bool dv = content == Content::DV;
          // the start, the renderer configured 400 ms after the mode was set
          const Model sb = Start(b, false, gui, video, content, 400);
          const Model sa = Start(b, true, gui, video, content, 400);
          const int setUp = dv || (hdr && b != Backend::NVIDIA) ? 1 : 0; // what the output adds
          const int startBefore = (modeChanges ? 1 : 0) + setUp;
          const int startAfter = modeChanges || setUp ? 1 : 0;
          char what[160];
          std::snprintf(what, sizeof(what), "%s %s GUI %.3f -> %.3f", Name(b), Name(content), gui, video);
          Check(sb.changes == startBefore,
                std::string("start before: ") + what + " " + std::to_string(sb.changes));
          Check(sa.changes == startAfter,
                std::string("start after, one change of the display: ") + what + " " +
                    std::to_string(sa.changes));
          Check(sa.screen.rate == video && sa.screen.hdr == hdr && sa.screen.dv == dv,
                std::string("start after: the output of the video is on the screen: ") + what);
          // the stop, the GUI mode set 800 ms after the end of the output of the video
          const Model eb = Stop(b, false, gui, video, content, 800);
          const Model ea = Stop(b, true, gui, video, content, 800);
          const int tearDown = (dv && b != Backend::NVIDIA) || (hdr && b != Backend::NVIDIA) ? 1 : 0;
          const int stopBefore = (modeChanges ? 1 : 0) + (modeChanges ? tearDown : setUp);
          const int stopAfter = modeChanges || setUp ? 1 : 0;
          Check(eb.changes == stopBefore,
                std::string("stop before: ") + what + " " + std::to_string(eb.changes));
          Check(ea.changes == stopAfter,
                std::string("stop after, one change of the display: ") + what + " " +
                    std::to_string(ea.changes));
          Check(ea.screen.rate == gui && !ea.screen.hdr && !ea.screen.dv,
                std::string("stop after: the GUI on the screen: ") + what);
          if (sa.changes != sb.changes || ea.changes != eb.changes)
          {
            if (lines++ < 12)
              std::printf("model: %-40s start %d -> %d, stop %d -> %d\n", what, sb.changes,
                          sa.changes, eb.changes, ea.changes);
          }
        }
  std::printf("model: %d transitions of 360 change the display fewer times\n", lines);
  // fail-closed: the deadlines give what the code before gives
  for (const Backend b : {Backend::NVIDIA, Backend::AMDGPU, Backend::I915})
  {
    const Model late = Start(b, true, 120, 24, Content::HDR10, 3500);
    const Model lateBefore = Start(b, false, 120, 24, Content::HDR10, 3500);
    Check(late.changes == lateBefore.changes,
          std::string("a renderer configured after the deadline: as before, ") + Name(b));
    Check(late.screen.rate == 24 && late.screen.hdr, "... and the output of the video in the end");
    const Model never = Start(b, true, 120, 24, Content::HDR10, -1);
    Check(never.changes == 1 && never.screen.rate == 24 && !never.screen.hdr,
          std::string("no renderer at all: the mode alone at the deadline, ") + Name(b));
    const Model noMode = Stop(b, true, 120, 24, Content::HDR10, -1);
    const Model noModeBefore = Stop(b, false, 120, 24, Content::HDR10, -1);
    Check(noMode.changes == noModeBefore.changes && !noMode.screen.hdr,
          std::string("no change of mode after the stop: as before, ") + Name(b));
    const Model dvNoMode = Stop(b, true, 120, 24, Content::DV, -1);
    Check(dvNoMode.changes == 1 && !dvNoMode.screen.dv,
          std::string("DV: no change of mode after the stop: restored in the mode of the video, ") +
              Name(b));
  }
}

// ---------------------------------------------------------------------------------------------
// CDisplayLook: the display looked at when each video starts
// ---------------------------------------------------------------------------------------------
// An EDID of the model: 512 bytes whose first ones stand for what it says (byte 0: 0xAA when it
// can be read; 1: Dolby Vision Standard; 2: Dolby Vision of any kind; 3: HDR10; 4: a variant)
static std::vector<uint8_t> MakeEdid(bool dvStandard, bool dvAny, bool hdr10, int variant,
                                     bool readable = true)
{
  std::vector<uint8_t> e(512);
  for (size_t i = 0; i < e.size(); ++i)
    e[i] = static_cast<uint8_t>(i * 7 + 3);
  e[0] = readable ? 0xAA : 0x00;
  e[1] = dvStandard;
  e[2] = dvAny || dvStandard;
  e[3] = hdr10;
  e[4] = static_cast<uint8_t>(variant);
  return e;
}

struct Caps
{
  bool dvStandard = false;
  bool dvAny = false;
  bool hdr10 = false;
  bool operator==(const Caps&) const = default;
};

static bool Parse(const std::vector<uint8_t>& e, Caps& caps)
{
  if (e.size() < 5 || e[0] != 0xAA)
    return false;
  caps = {e[1] != 0, e[2] != 0, e[3] != 0};
  return true;
}

enum class Driver
{
  AMDGPU, //!< the kernel's EDID property follows a hot-plug (amdgpu's detection reads it)
  PROBED, //!< nvidia-drm, i915: the property has the new EDID once the connector is probed
};

// The look of CWinSystemGbm::CheckDisplayChange(), over a model of the kernel and the display
struct Session
{
  Driver driver = Driver::AMDGPU;
  bool watchHears = true; // the following of displays hears the hot-plug events
  std::vector<uint8_t> real; // what the display answers now
  std::vector<uint8_t> property; // the EDID property of the connector
  unsigned int hotplugs = 0;
  // Kodi
  std::vector<uint8_t> known;
  bool hasInfo = false;
  Caps dvInfo, hdrInfo;
  bool dvSeen = false, disabled = false, unread = false;
  unsigned int lookHotplugs = 0;
  // what the looks did
  unsigned int probes = 0, currents = 0, parses = 0, adoptions = 0, dvChanges = 0;

  void Boot(std::vector<uint8_t> edid)
  {
    real = property = known = edid;
    Caps c;
    hasInfo = !edid.empty() && Parse(edid, c);
    dvInfo = hdrInfo = hasInfo ? c : Caps{};
    dvSeen = hasInfo && c.dvAny;
  }
  void Change(std::vector<uint8_t> edid, bool hpd = true)
  {
    real = std::move(edid);
    if (!hpd)
      return;
    if (driver == Driver::AMDGPU)
      property = real;
    if (watchHears)
      ++hotplugs;
  }
  void VideoStarts()
  {
    const bool hotplug = hotplugs != lookHotplugs;
    lookHotplugs = hotplugs;
    const auto how = CDisplayLook::HowToRead(dvSeen, disabled, hotplug);
    if (how == CDisplayLook::Read::PROBE)
    {
      ++probes;
      property = real;
    }
    else
      ++currents;
    const std::vector<uint8_t> edid = property;
    const bool same = !edid.empty() && edid == known;
    Caps c;
    bool parsed = false;
    if (!edid.empty() && !same)
    {
      ++parses;
      parsed = Parse(edid, c);
    }
    const auto outcome = CDisplayLook::Of(how, edid.empty(), same, parsed);
    if (outcome.unread)
    {
      unread = true;
      return;
    }
    if (outcome.read)
      unread = false;
    if (!outcome.adopt)
      return;
    known = edid;
    ++adoptions;
    if (!(c == dvInfo))
      ++dvChanges;
    dvInfo = hdrInfo = c;
    hasInfo = true;
    if (c.dvAny)
      dvSeen = true;
  }
};

static void DisplayLookTests()
{
  const auto dvTv = MakeEdid(true, true, true, 0);
  const auto plainTv = MakeEdid(false, false, true, 1);
  const auto hdmi14 = MakeEdid(false, false, false, 2); // Deep Colour off: an HDMI 1.4 EDID
  const auto lowLatency = MakeEdid(false, true, true, 3); // QFT on: Dolby Vision Low-Latency only
  for (const Driver d : {Driver::AMDGPU, Driver::PROBED})
  {
    const std::string name = d == Driver::AMDGPU ? "amdgpu: " : "nvidia/i915: ";
    {
      // the PC starts while the TV is off: no EDID; the TV is turned on (a hot-plug); a video
      Session s;
      s.driver = d;
      s.Boot({});
      Check(!s.hasInfo && !s.dvSeen, name + "no EDID at the start: nothing known");
      s.VideoStarts();
      Check(s.adoptions == 0 && !s.unread, name + "TV still off: nothing changes, nothing waits");
      s.Change(dvTv);
      s.VideoStarts();
      Check(s.adoptions == 1 && s.dvInfo.dvStandard && s.hdrInfo.hdr10,
            name + "TV on: Dolby Vision and HDR10 at the next video");
      Check(s.probes == 1, name + "... after a probe for the hot-plug");
      s.VideoStarts();
      Check(s.adoptions == 1 && s.parses == 1, name + "the next video: nothing more is parsed");
    }
    {
      // a hot-plug that the following of displays did not hear: amdgpu's property has the new
      // EDID all the same; with nvidia-drm and i915 it is seen once a hot-plug is heard
      Session s;
      s.driver = d;
      s.watchHears = false;
      s.Boot({});
      s.Change(dvTv);
      s.VideoStarts();
      if (d == Driver::AMDGPU)
        Check(s.adoptions == 1 && s.dvInfo.dvStandard && s.probes == 0,
              name + "unheard hot-plug: the kernel's EDID, without a probe");
      else
        Check(s.adoptions == 0 && s.probes == 0,
              name + "unheard hot-plug: not seen without a probe (the limit of the cheap read)");
    }
    {
      // Deep Colour off when Kodi started (an HDMI 1.4 EDID), turned on
      Session s;
      s.driver = d;
      s.Boot(hdmi14);
      Check(!s.dvSeen && !s.hdrInfo.hdr10, name + "HDMI 1.4 EDID: no DV, no HDR10");
      s.Change(dvTv);
      s.VideoStarts();
      Check(s.dvInfo.dvStandard && s.hdrInfo.hdr10 && s.dvChanges == 1,
            name + "Deep Colour on: DV and HDR10, one change told");
    }
    {
      // the DV TV is replaced by one without DV, and the other way
      Session s;
      s.driver = d;
      s.Boot(dvTv);
      Check(s.dvSeen, name + "a DV TV at the start");
      s.Change(plainTv);
      s.VideoStarts();
      Check(!s.dvInfo.dvStandard && s.hdrInfo.hdr10 && s.adoptions == 1 && s.dvChanges == 1,
            name + "a TV without DV in its place: no native output, HDR10 kept from its EDID");
      s.Change(dvTv);
      s.VideoStarts();
      Check(s.dvInfo.dvStandard && s.dvChanges == 2, name + "the DV TV back");
      s.Change(lowLatency);
      s.VideoStarts();
      Check(!s.dvInfo.dvStandard && s.dvInfo.dvAny && s.dvChanges == 3,
            name + "QFT on: Low-Latency only, seen at the next video");
    }
    {
      // a DV TV that wakes up: the probe finds no EDID; HDR10 is not lost, DV waits a video
      Session s;
      s.driver = d;
      s.Boot(dvTv);
      s.Change({});
      s.VideoStarts();
      Check(s.unread && s.hdrInfo.hdr10 && s.dvInfo.dvStandard && s.adoptions == 0,
            name + "no EDID on a probe: native output waits, nothing is lost");
      s.Change(dvTv);
      s.VideoStarts();
      Check(!s.unread && s.adoptions == 0, name + "the same EDID back: read, nothing adopted");
    }
    {
      // an EDID that cannot be read replaces nothing
      Session s;
      s.driver = d;
      s.Boot(plainTv);
      s.Change(MakeEdid(true, true, true, 9, false));
      s.VideoStarts();
      Check(s.adoptions == 0 && s.hdrInfo.hdr10 && !s.dvInfo.dvStandard,
            name + "an unreadable EDID after a hot-plug: nothing replaced");
      Check(s.unread, name + "... and, probed, native output waits a video");
      s.Change(MakeEdid(true, true, true, 9, false), false);
      Session t = s;
      t.unread = false;
      t.property = t.real; // (the kernel has it, no hot-plug)
      t.VideoStarts();
      Check(t.adoptions == 0 && !t.unread, name + "the same, read from the kernel: nothing waits");
    }
    {
      // an unchanged TV: no extra work beyond the cheap read
      Session plain;
      plain.driver = d;
      plain.Boot(plainTv);
      Session dv;
      dv.driver = d;
      dv.Boot(dvTv);
      for (int i = 0; i < 100; ++i)
      {
        plain.VideoStarts();
        dv.VideoStarts();
      }
      Check(plain.currents == 100 && plain.probes == 0 && plain.parses == 0 && plain.adoptions == 0,
            name + "unchanged TV without DV: 100 reads of the kernel's EDID, no probe, no parse");
      Check(dv.probes == 100 && dv.parses == 0 && dv.adoptions == 0,
            name + "unchanged DV TV: probed as before, nothing parsed");
    }
  }
  {
    // after a failure, a display without DV is probed (as before)
    Session s;
    s.Boot(plainTv);
    s.disabled = true;
    s.VideoStarts();
    Check(s.probes == 1, "after a failure: probed");
  }
  // the decisions alone, each way
  using R = CDisplayLook::Read;
  Check(CDisplayLook::HowToRead(false, false, false) == R::CURRENT, "nothing new: the kernel's EDID");
  Check(CDisplayLook::HowToRead(true, false, false) == R::PROBE, "DV seen: probed");
  Check(CDisplayLook::HowToRead(false, true, false) == R::PROBE, "a failure: probed");
  Check(CDisplayLook::HowToRead(false, false, true) == R::PROBE, "a hot-plug: probed");
  for (const R how : {R::PROBE, R::CURRENT})
  {
    const auto same = CDisplayLook::Of(how, false, true, false);
    Check(!same.adopt && same.read && !same.unread, "the same EDID: read, nothing adopted");
    const auto other = CDisplayLook::Of(how, false, false, true);
    Check(other.adopt && other.read, "another EDID that can be read: adopted");
    const auto none = CDisplayLook::Of(how, true, false, false);
    Check(!none.adopt && !none.read && none.unread == (how == R::PROBE),
          "no EDID: nothing adopted; waits only when probed");
    const auto bad = CDisplayLook::Of(how, false, false, false);
    Check(!bad.adopt && !bad.read && bad.unread == (how == R::PROBE),
          "an EDID that cannot be read: nothing adopted; waits only when probed");
  }
  // the cost of a look that finds the same EDID, on this host, besides the call to the kernel
  // (drmModeGetConnectorCurrent(): no read of the display; there is no DRM device here)
  {
    const auto a = MakeEdid(false, false, true, 1);
    const auto b = a;
    unsigned int same = 0;
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int N = 1000000;
    for (int i = 0; i < N; ++i)
    {
      const auto how = CDisplayLook::HowToRead(false, false, (i & 1023) == 1);
      const bool eq = !b.empty() && b == a;
      same += CDisplayLook::Of(how, b.empty(), eq, false).read ? 1 : 0;
    }
    const double ns =
        std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / N;
    std::printf("display look: %.1f ns a look besides the kernel call (512-byte EDID compared)\n", ns);
    Check(same == N && ns < 50000, "a look that finds the same EDID costs less than 50 us here");
  }
}

// ---------------------------------------------------------------------------------------------
// CHeldModeAnnouncement: the display resources told around the commit that makes a held change
// ---------------------------------------------------------------------------------------------
// The events the display resources see (OnLostDisplay, OnResetDisplay) and the commits, in order:
// "L" lost, "R" reset (back), "C" a commit that changes the display, "c" one that does not (a
// frame of native output refused, or the same mode), "f" a frame of native output that carries
// nothing new
struct Announced
{
  std::string events;
  bool dispReset = false; // CWinSystemGbm::m_dispReset: lost told, back not yet
  bool lostTwice = false;
  void Lost()
  {
    // (OnLostDevice(): once until the display is back)
    if (dispReset)
      return;
    dispReset = true;
    events += 'L';
  }
  void Reset() // SendDisplayReset(), at the end of each pass (no delay set)
  {
    if (dispReset)
    {
      dispReset = false;
      events += 'R';
    }
  }
};

enum class StartCase
{
  HDR, // the output of an HDR video, configured at 400 ms
  DV, // native output, its first frame accepted
  DV_REFUSED, // native output whose frames the driver refuses; restored at 2 s
  NEVER, // no output of a video before the deadline
  STOPPED, // the user stops at 200 ms: Kodi sets the mode of the GUI (the one on the screen)
};

// The start of a video from the GUI at 120 Hz to 24 Hz, with the hold and its announcement as the
// window system makes them; old: told when the change is chosen (before this fix)
static std::string AnnounceStart(StartCase sc, bool old)
{
  Announced a;
  CModeChangeHold hold;
  CHeldModeAnnouncement announce;
  bool modeOnScreen = false; // the held mode is on the display
  bool captured = false;
  // SetFullScreen(): held
  hold.Start(0);
  if (old)
    a.Lost();
  else
    announce.Held();
  for (int64_t t = 10; t <= 4000; t += 10)
  {
    if (sc == StartCase::STOPPED && t == 200)
    {
      // SetFullScreen() for the GUI mode: the hold is dropped, and that change tells of itself
      hold.ModeChange();
      announce.Dropped();
      a.Lost();
      a.events += modeOnScreen ? 'C' : 'c'; // the mode on the screen is the GUI's: no change
      a.Reset();
      break;
    }
    if ((sc == StartCase::HDR || sc == StartCase::DV || sc == StartCase::DV_REFUSED) && t == 400)
    {
      hold.Configured();
      captured = sc != StartCase::HDR;
    }
    if (!hold.Commits(t))
    {
      a.Reset(); // (the gate of PresentRender())
      continue;
    }
    if (captured)
    {
      // a frame of native output, every pass; refused: the mode stays as it was
      const bool accepted = sc == StartCase::DV;
      if (announce.Before(true))
        a.Lost();
      a.events += accepted && !modeOnScreen ? 'C' : accepted ? 'f' : 'c';
      announce.After(accepted, true);
      modeOnScreen = modeOnScreen || accepted;
      if (sc == StartCase::DV_REFUSED && t >= 2400)
      {
        // two seconds of frames that cannot be shown: the ordinary output is restored, with the
        // held mode (FlipDVBridgePage(restore))
        if (announce.Before(false))
          a.Lost();
        a.events += modeOnScreen ? 'c' : 'C';
        announce.After(true, false);
        modeOnScreen = true;
        captured = false;
      }
    }
    else if (!modeOnScreen)
    {
      // an ordinary commit carries the held mode
      if (announce.Before(false))
        a.Lost();
      a.events += 'C';
      announce.After(true, false);
      modeOnScreen = true;
    }
    a.Reset();
    if (modeOnScreen && !captured)
      break;
    if (sc == StartCase::DV && modeOnScreen && t > 600)
      break;
  }
  // (a run of frames that changed nothing is one 'f' or 'c' for reading)
  std::string out;
  for (const char e : a.events)
    if (out.empty() || !((e == 'f' || e == 'c') && out.back() == e))
      out += e;
  return out;
}

// Every change of the display between a loss and its end, told in pairs
static bool Bracketed(const std::string& events)
{
  bool lost = false;
  for (const char e : events)
  {
    if (e == 'L')
    {
      if (lost)
        return false;
      lost = true;
    }
    else if (e == 'R')
    {
      if (!lost)
        return false;
      lost = false;
    }
    else if (e == 'C' && !lost)
      return false;
  }
  return !lost;
}

static void AnnouncementTests()
{
  const struct
  {
    StartCase sc;
    const char* name;
    const char* expected;
  } cases[] = {
      {StartCase::HDR, "HDR start", "LCR"},
      {StartCase::DV, "DV start", "LCRf"},
      {StartCase::DV_REFUSED, "DV start refused, then restored", "LcRcLCR"},
      {StartCase::NEVER, "no output before the deadline", "LCR"},
      {StartCase::STOPPED, "stopped before its output", "LcR"},
  };
  for (const auto& c : cases)
  {
    const std::string after = AnnounceStart(c.sc, false);
    Check(after == c.expected, std::string("announcements, ") + c.name + ": " + after + " (expected " +
                                   c.expected + ")");
    Check(Bracketed(after), std::string("every change between lost and back, in pairs: ") + c.name);
    const std::string before = AnnounceStart(c.sc, true);
    if (c.sc != StartCase::STOPPED)
      Check(!Bracketed(before),
            std::string("told when chosen (before this fix), the change comes after the display "
                        "is back: ") +
                c.name + ": " + before);
  }
  // the decisions, each way
  using S = CHeldModeAnnouncement::Step;
  CHeldModeAnnouncement x;
  Check(!x.Before(false) && !x.Before(true), "nothing held: nothing told");
  x.Held();
  Check(x.Pending() == S::BEFORE_NEXT, "held: told before the next commit");
  Check(x.Before(true) && x.Pending() == S::TOLD, "a frame of native output: told");
  x.After(false, true);
  Check(x.Pending() == S::BEFORE_ORDINARY, "refused: told again before the restoration");
  Check(!x.Before(true), "... not before another frame of native output");
  Check(x.Before(false), "... before the restoration");
  x.After(true, false);
  Check(x.Pending() == S::NONE, "made: nothing more");
  x.Held();
  x.Dropped();
  Check(!x.Before(false), "dropped: another change tells of itself");
  x.Held();
  Check(x.Before(false), "an ordinary commit: told");
  x.After(false, false);
  Check(x.Pending() == S::NONE, "an ordinary commit is not told twice");
}

int main()
{
  StallTests();
  PolicyTests();
  HoldDecisionTests();
  HoldLifecycleTests();
  ModelTests();
  DisplayLookTests();
  AnnouncementTests();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  std::printf(g_failures ? "ANYHZ-TEST FAILED\n" : "ANYHZ-TEST PASSED\n");
  return g_failures ? 1 : 0;
}
