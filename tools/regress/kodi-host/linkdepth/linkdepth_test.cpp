/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): host test of the depth of the link (windowing/gbm/LinkDepth.h).
 *  1. The decision: LinkDepthWanted(), LinkDepthFor() and the helpers that say what a request
 *     carries, for SDR, SDR of more than 8 bits, HDR10, HLG, HDR10+, native Dolby Vision, the
 *     setting "HDR output depth" and each output (nvidia-drm HDMI, nvidia-drm DisplayPort, i915
 *     HDMI, amdgpu). The cases of the test of HDRDepthFor() are in it.
 *  2. A model of the commits of the start and the end of a video, in the order in which the
 *     window system makes them (SetFullScreen(), CModeChangeHold, SetVideoOutput(), SetHDR(),
 *     the end of native Dolby Vision and its join with the next mode, K57), with the real
 *     CModeChangeHold and LinkDepth.h, run with and without the depth: the depth adds no commit,
 *     a DV exit with a change of mode stays one commit, an HDR10 start or stop adds no commit and
 *     no commit that sets the head only for the depth, and the depth on the screen is the one
 *     wanted, at GUI rates of 60, 119.88 and 120 Hz.
 * Built with -I <Kodi tree>/xbmc; each failure prints FAIL; the last line says PASSED or FAILED.
 */
#include "windowing/gbm/LinkDepth.h"
#include "windowing/gbm/ModeChangeHold.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

using namespace KODI::WINDOWING::GBM;

namespace
{
int g_checks = 0;
int g_failures = 0;
void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

constexpr uint64_t NV_MAX = 12; // nvidia-drm: "max bpc" 8 to 12 (NV_DRM_MAX_BPC_DEFAULT)
constexpr uint64_t I915_MAX = 12; // i915 HDMI: 8 to 12
constexpr uint64_t AMD_MAX = 16; // amdgpu: 8 to 16

enum class Driver
{
  NVIDIA_HDMI,
  NVIDIA_DP, // a DisplayPort sink (not a dual-mode connector driven as HDMI)
  I915_HDMI,
  AMDGPU, // not managed
};

const char* Name(Driver d)
{
  switch (d)
  {
    case Driver::NVIDIA_HDMI:
      return "nvidia HDMI";
    case Driver::NVIDIA_DP:
      return "nvidia DP";
    case Driver::I915_HDMI:
      return "i915 HDMI";
    case Driver::AMDGPU:
      return "amdgpu";
  }
  return "?";
}

LinkDepthOutput OutputOf(Driver d)
{
  // as CWinSystemGbm::ReadLinkDepthOutput() reads them
  switch (d)
  {
    case Driver::NVIDIA_HDMI:
      return {true, true, NV_MAX};
    case Driver::NVIDIA_DP:
      return {true, false, NV_MAX};
    case Driver::I915_HDMI:
      return {true, true, I915_MAX};
    case Driver::AMDGPU:
      return {false, false, AMD_MAX};
  }
  return {};
}

// ------------------------------------------------------------------------------------- 1
void DecisionTests()
{
  const LinkDepthOutput nv = OutputOf(Driver::NVIDIA_HDMI);
  const LinkDepthOutput dp = OutputOf(Driver::NVIDIA_DP);
  const LinkDepthOutput intel = OutputOf(Driver::I915_HDMI);
  const LinkDepthOutput amd = OutputOf(Driver::AMDGPU);

  // what is wanted
  Check(LinkDepthWanted(nv, LinkContent::SDR, 0) == 8, "nvidia HDMI: SDR of 8 bits at 8");
  Check(LinkDepthWanted(nv, LinkContent::SDR, 8) == 8, "nvidia HDMI: SDR at 8 whatever the HDR setting");
  Check(LinkDepthWanted(nv, LinkContent::SDR_DEEP, 0) == NV_MAX, "nvidia HDMI: SDR of 10 bits keeps the driver's choice");
  Check(LinkDepthWanted(nv, LinkContent::SDR_DEEP, 8) == NV_MAX, "nvidia HDMI: SDR of 10 bits, not held by the HDR setting");
  Check(LinkDepthWanted(nv, LinkContent::HDR, 0) == NV_MAX, "nvidia HDMI: HDR (HDR10, HLG, HDR10+) automatic: the driver's choice");
  Check(LinkDepthWanted(nv, LinkContent::HDR, 8) == 8, "nvidia HDMI: HDR with the setting at 8 bits: 8");
  Check(LinkDepthWanted(nv, LinkContent::DV, 0) == 8, "nvidia HDMI: native DV: 8 (its transport)");
  Check(LinkDepthWanted(dp, LinkContent::SDR, 0) == NV_MAX, "nvidia DP: SDR keeps the driver's choice");
  Check(LinkDepthWanted(dp, LinkContent::HDR, 8) == 8, "nvidia DP: HDR with the setting at 8 bits: 8");
  Check(LinkDepthWanted(intel, LinkContent::SDR, 0) == 8, "i915 HDMI: SDR of 8 bits at 8");
  Check(LinkDepthWanted(intel, LinkContent::SDR_DEEP, 0) == I915_MAX, "i915 HDMI: SDR of 10 bits keeps 12");
  Check(LinkDepthWanted(intel, LinkContent::HDR, 0) == I915_MAX, "i915 HDMI: HDR keeps 12");

  // the held depth, as nvidia-drm reads "max bpc"
  Check(LinkHeldAt8(8) && LinkHeldAt8(9) && !LinkHeldAt8(10) && !LinkHeldAt8(12) && !LinkHeldAt8(16),
        "held at 8 below 10");

  // amdgpu: never touched
  for (const LinkContent c : {LinkContent::SDR, LinkContent::SDR_DEEP, LinkContent::HDR, LinkContent::DV})
    for (const uint64_t cur : {8u, 16u})
      for (const bool head : {false, true})
      {
        const LinkDepthStep s = LinkDepthFor(amd, c, 8, cur, head);
        Check(!s.maxBpc && !s.waits && !s.raises, "amdgpu: max bpc is never touched");
      }
  // no range at all
  Check(!LinkDepthFor({true, true, 0}, LinkContent::SDR, 0, 12, true).maxBpc, "no range: not touched");

  // the steps on nvidia HDMI
  {
    LinkDepthStep s = LinkDepthFor(nv, LinkContent::SDR, 0, 12, true);
    Check(s.maxBpc == std::optional<uint64_t>{8} && !s.waits && !s.raises,
          "SDR at Kodi's first commit (the console left 12): 8 with it");
    s = LinkDepthFor(nv, LinkContent::SDR, 0, 12, false);
    Check(!s.maxBpc && s.waits, "SDR in a commit that does not set the head: the lower depth waits");
    s = LinkDepthFor(nv, LinkContent::SDR, 0, 8, false);
    Check(!s.maxBpc && !s.waits && !s.raises, "SDR already at 8: nothing");
    s = LinkDepthFor(nv, LinkContent::HDR, 0, 8, true);
    Check(s.maxBpc == std::optional<uint64_t>{NV_MAX} && !s.raises,
          "HDR10 start (sets the head): up to the driver's choice in that request");
    s = LinkDepthFor(nv, LinkContent::HDR, 0, 10, true);
    Check(!s.maxBpc, "HDR, \"max bpc\" 10 or 12: the same for nvidia-drm, nothing");
    s = LinkDepthFor(nv, LinkContent::SDR_DEEP, 0, 8, false);
    Check(s.maxBpc == std::optional<uint64_t>{NV_MAX} && s.raises,
          "SDR of 10 bits without a change of mode: up, and the request becomes a modeset");
    s = LinkDepthFor(nv, LinkContent::SDR_DEEP, 0, 8, true);
    Check(s.maxBpc == std::optional<uint64_t>{NV_MAX} && !s.raises,
          "SDR of 10 bits with its change of mode: up in it, no extra modeset");
    s = LinkDepthFor(nv, LinkContent::DV, 0, 8, true);
    Check(!s.maxBpc, "native DV ahead from SDR at 8: nothing");
  }

  // the cases of HDRDepthFor(), as they are now: a sequence of requests of SetHDR()
  {
    // automatic: HDR at the driver's choice; on DP (SDR not held) it is never touched
    for (const bool hdr : {false, true})
    {
      const LinkDepthStep s = LinkDepthFor(dp, hdr ? LinkContent::HDR : LinkContent::SDR, 0, NV_MAX, true);
      Check(!s.maxBpc && !s.raises && !s.waits, "nvidia DP, automatic: max bpc as it is");
    }
    // 8 bits on DP: an HDR10 video, then the GUI
    LinkDepthStep s = LinkDepthFor(dp, LinkContent::HDR, 8, NV_MAX, true);
    Check(s.maxBpc == std::optional<uint64_t>{8}, "nvidia DP, 8 bits: HDR at 8");
    s = LinkDepthFor(dp, LinkContent::HDR, 8, 8, false);
    Check(!s.maxBpc && !s.raises, "nvidia DP, 8 bits: the next request of the same video, nothing");
    s = LinkDepthFor(dp, LinkContent::SDR, 8, 8, true);
    Check(s.maxBpc == std::optional<uint64_t>{NV_MAX} && !s.raises,
          "nvidia DP, 8 bits: the end of HDR puts the driver's choice back");
    s = LinkDepthFor(dp, LinkContent::SDR, 8, NV_MAX, true);
    Check(!s.maxBpc, "nvidia DP, 8 bits: SDR is not held there");
    // set again where native DV put back another value
    s = LinkDepthFor(dp, LinkContent::HDR, 8, NV_MAX, true);
    Check(s.maxBpc == std::optional<uint64_t>{8}, "nvidia DP, 8 bits: set again with each HDR");
    // the setting changed to automatic while held
    s = LinkDepthFor(dp, LinkContent::HDR, 0, 8, true);
    Check(s.maxBpc == std::optional<uint64_t>{NV_MAX}, "nvidia DP: a change to automatic lets the link go");
    // HDMI, 8 bits: HDR and SDR both at 8, nothing changes between them
    s = LinkDepthFor(nv, LinkContent::HDR, 8, 8, true);
    Check(!s.maxBpc && !s.waits, "nvidia HDMI, 8 bits: HDR after SDR at 8, nothing");
    s = LinkDepthFor(nv, LinkContent::SDR, 8, 8, true);
    Check(!s.maxBpc && !s.waits, "nvidia HDMI, 8 bits: SDR after HDR at 8, nothing");
  }

  // what a request carries
  Check(LinkContentOfPicture(false, false, false) == LinkContent::SDR, "no picture: the GUI");
  Check(LinkContentOfPicture(false, true, true) == LinkContent::SDR, "no picture: the GUI, whatever else");
  Check(LinkContentOfPicture(true, true, false) == LinkContent::HDR, "a picture with HDR sent: HDR");
  Check(LinkContentOfPicture(true, true, true) == LinkContent::HDR, "a picture of 10 bits with HDR sent: HDR");
  Check(LinkContentOfPicture(true, false, true) == LinkContent::SDR_DEEP, "SDR of 10 bits");
  Check(LinkContentOfPicture(true, false, false) == LinkContent::SDR, "SDR of 8 bits");
  Check(LinkContentOfVideo(true, true, true) == LinkContent::DV, "the video that opens: native DV");
  Check(LinkContentOfVideo(false, true, true) == LinkContent::HDR, "the video that opens: HDR sent");
  Check(LinkContentOfVideo(false, false, true) == LinkContent::SDR_DEEP, "the video that opens: SDR of 10 bits (or HDR not sent)");
  Check(LinkContentOfVideo(false, false, false) == LinkContent::SDR, "the video that opens: SDR of 8 bits");
  Check(HDRRequestSetsHead(false, true, false) && HDRRequestSetsHead(true, false, false),
        "HDR on or off sets the head");
  Check(!HDRRequestSetsHead(true, true, false) && !HDRRequestSetsHead(false, false, false),
        "HDR that stays as it was does not");
  Check(HDRRequestSetsHead(false, false, true), "a change of mode that waits for the request does");
}

// ------------------------------------------------------------------------------------- 2
enum class Content
{
  SDR8,
  SDR10,
  HDR10,
  HLG,
  HDR10PLUS,
  DV,
};

const char* Name(Content c)
{
  switch (c)
  {
    case Content::SDR8:
      return "SDR 8-bit";
    case Content::SDR10:
      return "SDR 10-bit";
    case Content::HDR10:
      return "HDR10";
    case Content::HLG:
      return "HLG";
    case Content::HDR10PLUS:
      return "HDR10+";
    case Content::DV:
      return "DV";
  }
  return "?";
}

bool IsHdr(Content c)
{
  return c == Content::HDR10 || c == Content::HLG || c == Content::HDR10PLUS;
}

// what the display has, or what a request carries
struct Screen
{
  double rate = 60;
  bool hdr = false;
  bool dv = false;
  uint64_t bpc = 8;
};

// the head is set again by what changes (read in the drivers): another mode, HDR on or off
// (nvidia-drm: the HDR InfoFrame and the colorimetry are a modeset of NVKMS on every GPU that has
// no flip-synchronized InfoFrames, i.e. Turing to Blackwell; i915: Colorspace and HDR metadata set
// mode_changed; amdgpu: entering and leaving HDR is a full modeset), the transport of native DV on
// or off, and "max bpc" (a modeset in the DRM core: connectors_changed)
bool SetsHead(const Screen& a, const Screen& b, bool depthToo)
{
  if (std::fabs(a.rate - b.rate) > 0.0005 || a.hdr != b.hdr || a.dv != b.dv)
    return true;
  return depthToo && LinkHeldAt8(a.bpc) != LinkHeldAt8(b.bpc);
}

struct Request
{
  std::optional<double> mode; // a mode selected that a commit has not carried
  std::optional<bool> hdr;
  std::optional<bool> dv;
  std::optional<uint64_t> bpc;
  bool modeset = false;
  bool Empty() const { return !mode && !hdr && !dv && !bpc && !modeset; }
};

struct Model
{
  Driver driver = Driver::NVIDIA_HDMI;
  //! the behaviour of the fix, or before it (max bpc only for DV and the HDR setting)
  bool withDepth = true;
  int hdrSetting = 0;
  bool floatBuffer = true; // nvidia: a video of 10 bits is drawn in a half-float buffer
  Screen screen;
  Request req;
  bool bufferDeep = false;
  int commits = 0;
  int heads = 0; // commits that set the head again
  int depthOnly = 0; // ... only for the depth
  std::string log;

  LinkDepthOutput Out() const { return OutputOf(driver); }

  uint64_t Current() const { return req.bpc.value_or(screen.bpc); }

  // CWinSystemGbm::QueueLinkDepth()
  void Queue(LinkContent content, bool setsHead)
  {
    if (!withDepth)
    {
      // before: only the HDR setting, in the request that signals HDR, and back with a modeset
      // (HDRDepthFor(), on nvidia-drm with NV_OUTPUT_BPC)
      if (!Out().managed || driver == Driver::I915_HDMI)
        return;
      if (content == LinkContent::HDR && hdrSetting == 8)
        req.bpc = 8;
      else if (content != LinkContent::HDR && LinkHeldAt8(Current()) && setsHead && !dvCaptured)
        req.bpc = Out().maximum;
      return;
    }
    // (the setting "HDR output depth" is read only where it is shown: nvidia-drm)
    const bool nvidia = driver == Driver::NVIDIA_HDMI || driver == Driver::NVIDIA_DP;
    const LinkDepthStep s =
        LinkDepthFor(Out(), content, nvidia ? hdrSetting : 0, Current(), setsHead);
    if (s.maxBpc)
      req.bpc = *s.maxBpc;
    if (s.raises)
      req.modeset = true;
  }

  bool ModePending() const { return req.mode.has_value(); }

  void Commit(const char* what)
  {
    Screen next = screen;
    if (req.mode)
      next.rate = *req.mode;
    if (req.hdr)
      next.hdr = *req.hdr;
    if (req.dv)
      next.dv = *req.dv;
    if (req.bpc)
      next.bpc = *req.bpc;
    ++commits;
    if (SetsHead(screen, next, true))
    {
      ++heads;
      if (!SetsHead(screen, next, false))
        ++depthOnly;
    }
    char line[160];
    std::snprintf(line, sizeof(line), " [%s: %.3f%s%s %u bpc]", what, next.rate,
                  next.hdr ? " HDR" : "", next.dv ? " DV" : "", static_cast<unsigned>(next.bpc));
    log += line;
    screen = next;
    req = {};
  }

  // a frame of the GUI or of the video: committed where it carries anything
  void Frame(const char* what)
  {
    if (!req.Empty())
      Commit(what);
  }

  // what native DV took from the output when it captured it (CDVBridgeState::Capture())
  bool dvCaptured = false;
  uint64_t dvOriginalBpc = 0;
};

// The start of a video at video Hz from the GUI at gui Hz. switching: "adjust refresh rate" on
// start/stop; off: the video plays in the GUI mode. hdrDisplay: the display takes HDR (the LG C5).
Model Start(Driver d, bool withDepth, int hdrSetting, double gui, double video, Content content,
            bool switching)
{
  Model m;
  m.driver = d;
  m.withDepth = withDepth;
  m.hdrSetting = hdrSetting;
  m.screen = {gui, false, false, 8};
  // the GUI as Kodi left it before this video: SDR at 8 on a held output, the maximum elsewhere
  const LinkDepthOutput out = m.Out();
  m.screen.bpc = withDepth ? LinkDepthWanted(out, LinkContent::SDR, hdrSetting)
                           : (d == Driver::AMDGPU ? AMD_MAX : out.maximum);
  const bool hdr = IsHdr(content);
  const bool dv = content == Content::DV;
  const bool deepStream = content != Content::SDR8; // the HDR and DV streams have 10 bits too
  const double rate = switching ? video : gui;
  const bool modeChanges = std::fabs(rate - gui) > 0.0005;

  // VideoPlayer: SetVideoResolution() for the mode of the stream, which the window system may hold
  // (CModeChangeHold, START) for the output of the video; where it is the mode on the screen,
  // nothing is set
  CModeChangeHold hold;
  if (modeChanges)
  {
    CModeChangeHold::Change c;
    c.atomic = c.active = c.sameWindow = true;
    c.timingChanges = true;
    const auto kind = dv    ? CModeChangeHold::Video::NATIVE_DV
                      : hdr ? CModeChangeHold::Video::HDR
                            : CModeChangeHold::Video::ORDINARY;
    if (CModeChangeHold::StartHolds(kind, c))
    {
      hold.Start(0);
      m.req.mode = rate; // selected, the next commit carries it
    }
    else
    {
      // CWinSystemGbm::SetFullScreen(): the depth for what the mode will carry, with its commit
      m.req.mode = rate;
      m.Queue(LinkContentOfVideo(dv, hdr, deepStream), true);
      m.Commit("mode");
    }
  }

  // the renderer is configured: SetVideoOutput(), SetColorimetry(), SetHDR(); native DV: the
  // transport
  if (dv)
  {
    // CDVBridgeState::Capture() keeps what the output has, and the first transport frame carries
    // the transport (8 bpc) with the mode that waited
    m.dvCaptured = true;
    m.dvOriginalBpc = m.screen.bpc;
    hold.Configured();
    m.req.dv = true;
    m.req.bpc = 8;
    m.req.modeset = true;
    m.Commit("DV in");
  }
  else
  {
    // SetVideoOutput(): a buffer of more than 8 bits for a stream of more (nvidia-drm: half-float,
    // a commit that allows a modeset; i915: 10 bits, a flip)
    const bool nvidia = d == Driver::NVIDIA_HDMI || d == Driver::NVIDIA_DP;
    const bool deepBuffer = deepStream && (!nvidia || m.floatBuffer);
    if (deepBuffer != m.bufferDeep && nvidia)
      m.req.modeset = true;
    m.bufferDeep = deepBuffer;
    const bool before = false;
    if (hdr)
    {
      m.req.hdr = true;
      m.req.modeset = true;
    }
    // CWinSystemGbm::QueuePictureLinkDepth()
    m.Queue(LinkContentOfPicture(true, hdr, deepStream || m.bufferDeep),
            HDRRequestSetsHead(before, hdr, m.ModePending()));
    hold.Configured();
    m.Frame("video");
  }
  // frames: nothing more
  m.Frame("frame");
  return m;
}

// The end of a video at video Hz back to the GUI at gui Hz
Model Stop(Driver d, bool withDepth, int hdrSetting, double gui, double video, Content content,
           bool switching)
{
  // the screen as the start left it
  Model m = Start(d, withDepth, hdrSetting, gui, video, content, switching);
  m.commits = m.heads = m.depthOnly = 0;
  m.log.clear();
  const bool hdr = IsHdr(content);
  const bool dv = content == Content::DV;
  const double rate = switching ? video : gui;
  const bool modeChanges = std::fabs(rate - gui) > 0.0005;
  CModeChangeHold::Change c;
  c.atomic = c.active = c.sameWindow = true;
  c.timingChanges = modeChanges;

  if (dv)
  {
    CModeChangeHold::Change dvc = c;
    dvc.captured = true;
    // EndDVBridgeAfterVideo(): K57, the restoration waits for the change of mode back to the GUI
    const bool joins = switching && CModeChangeHold::DVRestoreJoins(dvc);
    const auto restore = [&](bool withMode, const char* what)
    {
      // FlipDVBridgePage(restore): Stage() puts back what it found, then the depth for what the
      // ordinary output carries (QueueLinkDepth(), the end of the transport sets the head anyway)
      m.req.dv = false;
      m.req.bpc = m.dvOriginalBpc;
      m.Queue(LinkContent::SDR, true);
      m.req.modeset = true;
      if (withMode)
        m.req.mode = gui;
      m.Commit(what);
    };
    if (joins)
    {
      // SetFullScreen() of the GLES context: the mode and the restoration in one commit, then the
      // base SetFullScreen() for the same mode, which carries nothing new
      restore(true, "DV out with the mode");
      m.dvCaptured = false;
      m.req.mode = gui;
      m.Queue(LinkContent::SDR, false);
      m.req.mode.reset(); // the mode on the screen: a flip
      m.Frame("mode again");
    }
    else
    {
      // in the mode of the video; nvidia-drm turns the head off and on (two commits, as before)
      if (d == Driver::NVIDIA_HDMI || d == Driver::NVIDIA_DP)
      {
        restore(false, "DV out, head off");
        m.req.dv = false;
        m.req.bpc = m.screen.bpc;
        m.Queue(LinkContent::SDR, true);
        m.Frame("head on");
      }
      else
        restore(false, "DV out");
      m.dvCaptured = false;
      if (modeChanges)
      {
        m.req.mode = gui;
        m.Queue(LinkContent::SDR, true);
        m.Commit("mode");
      }
    }
    return m;
  }

  // the renderer goes: SetHDR(nullptr), SetColorimetry(nullptr), SetVideoOutput(nullptr), which
  // holds the end of HDR for the change of mode that follows (HoldEndOfVideo(), STOP)
  CModeChangeHold hold;
  const bool signalledBefore = hdr;
  if (hdr)
  {
    m.req.hdr = false;
    m.req.modeset = true;
  }
  m.Queue(LinkContentOfPicture(false, false, false),
          HDRRequestSetsHead(signalledBefore, false, m.ModePending()));
  const bool stopHolds = CModeChangeHold::StopHolds(hdr, switching, c);
  if (stopHolds)
    hold.Stop(0);
  // the GUI's buffer of 8 bits again (nvidia-drm: from half-float, a commit that allows a modeset)
  if (m.bufferDeep && (d == Driver::NVIDIA_HDMI || d == Driver::NVIDIA_DP))
    m.req.modeset = true;
  m.bufferDeep = false;
  if (!stopHolds)
    m.Frame("GUI");
  if (modeChanges)
  {
    // SetFullScreen(): the hold ends with the change of mode, whose commit carries what it held
    hold.ModeChange();
    m.req.mode = gui;
    m.Queue(LinkContent::SDR, true);
    m.Commit("mode");
  }
  m.Frame("GUI");
  return m;
}

// The GUI after a video, on an output that holds SDR at 8: at 8, but after an SDR video of more
// than 8 bits that played in the mode of the GUI, whose lower depth waits for the next change of
// mode (no commit at its end sets the head again)
bool GuiAt8After(Content content, bool modeChanges)
{
  return !(content == Content::SDR10 && !modeChanges);
}

void ModelTests()
{
  const double guis[] = {60.0, 120000.0 / 1001, 120.0};
  const double videos[] = {24000.0 / 1001, 24.0, 50.0, 60000.0 / 1001, 60.0, 120000.0 / 1001};
  const Content contents[] = {Content::SDR8,  Content::SDR10,     Content::HDR10,
                              Content::HLG,   Content::HDR10PLUS, Content::DV};
  int runs = 0;
  for (const Driver d : {Driver::NVIDIA_HDMI, Driver::NVIDIA_DP, Driver::I915_HDMI, Driver::AMDGPU})
    for (const int setting : {0, 8})
      for (const bool switching : {true, false})
        for (const double gui : guis)
          for (const double video : videos)
            for (const Content content : contents)
            {
              if (content == Content::DV && d == Driver::NVIDIA_DP)
                continue; // native DV only on HDMI
              ++runs;
              char what[200];
              std::snprintf(what, sizeof(what), "%s, HDR setting %d, switching %s, GUI %.3f, %s %.3f",
                            Name(d), setting, switching ? "on" : "off", gui, Name(content), video);
              const bool hdr = IsHdr(content);
              const bool dv = content == Content::DV;
              const double rate = switching ? video : gui;
              const bool modeChanges = std::fabs(rate - gui) > 0.0005;
              const LinkDepthOutput out = OutputOf(d);

              const Model sb = Start(d, false, setting, gui, video, content, switching);
              const Model sa = Start(d, true, setting, gui, video, content, switching);
              // the only commit that sets the head for the depth alone: an SDR video of more than 8
              // bits that starts in the mode of the GUI, on an output that holds SDR at 8. On
              // nvidia-drm its half-float buffer is a commit of its own already; on i915 a flip.
              const bool deepRaise = content == Content::SDR10 && !modeChanges && out.sdrAt8;
              const bool nvidia = d == Driver::NVIDIA_HDMI || d == Driver::NVIDIA_DP;
              Check(sa.commits == sb.commits + (deepRaise && !nvidia ? 1 : 0),
                    std::string("start: the depth adds no commit: ") + what + sa.log + " /" + sb.log);
              Check(sa.depthOnly == (deepRaise ? 1 : 0),
                    std::string("start: no head set for the depth alone: ") + what + sa.log);
              Check(sa.heads == sb.heads + (deepRaise ? 1 : 0),
                    std::string("start: the same changes of the display: ") + what + sa.log + " /" +
                        sb.log);
              if (hdr && modeChanges)
                Check(sa.commits == 1, std::string("HDR start with its mode: one commit: ") + what + sa.log);
              if (hdr && !modeChanges)
                Check(sa.commits == 1 && sa.depthOnly == 0,
                      std::string("HDR start in the GUI mode: one commit, which signals HDR: ") + what +
                          sa.log);
              // the depth during the video
              uint64_t wanted = 0;
              if (dv)
                wanted = 8;
              else if (hdr)
                wanted = LinkDepthWanted(out, LinkContent::HDR,
                                         d == Driver::I915_HDMI ? 0 : setting);
              else if (content == Content::SDR10)
                wanted = out.maximum;
              else
                wanted = LinkDepthWanted(out, LinkContent::SDR, setting);
              if (d == Driver::AMDGPU)
                wanted = dv ? 8 : AMD_MAX;
              Check(LinkHeldAt8(sa.screen.bpc) == LinkHeldAt8(wanted),
                    std::string("the depth of the video: ") + what + sa.log);
              Check(sa.screen.hdr == hdr && sa.screen.dv == dv &&
                        std::fabs(sa.screen.rate - rate) < 0.0005,
                    std::string("start: the output of the video on the screen: ") + what + sa.log);
              // HDR, HLG and HDR10+ keep what they had before the fix
              if (hdr)
                Check(LinkHeldAt8(sa.screen.bpc) == LinkHeldAt8(sb.screen.bpc),
                      std::string("HDR keeps its depth: ") + what + sa.log + " /" + sb.log);
              if (dv)
                Check(sa.screen.bpc == 8 && sb.screen.bpc == 8, std::string("DV keeps 8: ") + what);

              const Model eb = Stop(d, false, setting, gui, video, content, switching);
              const Model ea = Stop(d, true, setting, gui, video, content, switching);
              Check(ea.commits == eb.commits,
                    std::string("stop: the depth adds no commit: ") + what + ea.log + " /" + eb.log);
              Check(ea.depthOnly == 0, std::string("stop: no head set for the depth alone: ") + what + ea.log);
              Check(ea.heads == eb.heads,
                    std::string("stop: the same changes of the display: ") + what + ea.log + " /" + eb.log);
              if (dv && modeChanges && switching)
                Check(ea.commits == 1 && ea.heads == 1,
                      std::string("DV exit with the change of mode: one change, in one commit (K57): ") +
                          what + ea.log);
              if (hdr)
                Check(ea.heads == 1, std::string("HDR stop: one change of the display: ") + what + ea.log);
              // the GUI after it
              Check(!ea.screen.hdr && !ea.screen.dv && std::fabs(ea.screen.rate - gui) < 0.0005,
                    std::string("stop: the GUI on the screen: ") + what + ea.log);
              const bool guiAt8 = GuiAt8After(content, modeChanges);
              if (d != Driver::AMDGPU && out.sdrAt8)
                Check(LinkHeldAt8(ea.screen.bpc) == guiAt8,
                      std::string("stop: the depth of the GUI: ") + what + ea.log);
            }
  std::printf("model: %d runs\n", runs);
}
} // namespace


int main()
{
  DecisionTests();
  ModelTests();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  std::printf("%s\n", g_failures ? "LINKDEPTH-TEST FAILED" : "LINKDEPTH-TEST PASSED");
  return g_failures ? 1 : 0;
}
