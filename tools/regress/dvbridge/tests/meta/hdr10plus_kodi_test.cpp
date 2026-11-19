// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: the decisions of Kodi for HDR10+, from the headers of the tree as they are:
//  V  CHDR10PlusOutput::VersionFits() and Next(): a payload only of a version the display takes
//  E  KODI::UTILS::Hdr10PlusBlockVersion(): the block of any version, read from the raw EDID
//  N  CDVPlusModeStart::NeededFor(), For(), WaitNs(), KeepProvisional()
//  I  the decision sequence of Dolby Vision sent as HDR10+ against what i915 does (i915_model.c, the kernel's own
//     intel_hdr10plus.h): the commit of the mode without metadata, then metadata (1.0 RC4): every commit with HDR10+
//     refused, HDR10 after CHDR10PlusOutput::REFUSED_AFTER; with CDVPlusModeStart: HDR10+ from the commit of the mode
//     on, nothing refused; nvidia-drm and amdgpu (any flip takes HDR10+): the same requests as 1.0 RC4
//  R  CHdmiVrrOff::Wanted(), Stage(), Without(), RefreshMilliHz()
// usage: hdr10plus_kodi_test. (The I part also models the decisions of 1.0 RC4, which had no mode start, and
// checks that they fail where the new ones do not.)
#include "i915_model.h"
#include "utils/EdidHdr10Plus.h"
#include "windowing/gbm/DVPlusModeStart.h"
#include "windowing/gbm/HDR10PlusOutput.h"
#include "windowing/gbm/HdmiVrrOff.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::GBM;
using Payload = CHDR10PlusOutput::Payload;

static int g_failures, g_checks;
#define CHECK(c, ...) \
  do \
  { \
    ++g_checks; \
    if (!(c)) \
    { \
      ++g_failures; \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__); \
      std::printf("\n"); \
    } \
  } while (0)

static Payload MakePayload(int version, uint8_t seed)
{
  Payload p{};
  p[0] = 0x8B;
  p[1] = 0x84;
  p[2] = 0x90;
  p[3] = static_cast<uint8_t>((version & 3) << 6);
  for (std::size_t i = 4; i < p.size(); ++i)
    p[i] = static_cast<uint8_t>(seed + i);
  p[26] = 0x40;
  return p;
}

// ---------------------------------------------------------------- V
static void TestVersion()
{
  for (int pv = 0; pv < 4; ++pv)
    for (int rv = -1; rv < 4; ++rv)
    {
      const bool want = rv >= 0 && pv <= (rv < 1 ? rv : 1);
      CHECK(CHDR10PlusOutput::VersionFits(MakePayload(pv, 1), rv) == want,
            "V1 payload of version %d to a display of version %d: %s", pv, rv, want ? "goes" : "does not");
      CHDR10PlusOutput out;
      CHDR10PlusOutput::Output o;
      o.pq = o.display = o.driver = true;
      o.receiverVersion = rv;
      const auto step = out.Next(o, MakePayload(pv, 2));
      CHECK(step == (want ? CHDR10PlusOutput::Step::SEND : CHDR10PlusOutput::Step::NOTHING),
            "V2 Next() for version %d to %d", pv, rv);
    }
  // a payload that was queued and then one of a version the display does not take: taken away
  CHDR10PlusOutput out;
  CHDR10PlusOutput::Output o;
  o.pq = o.display = o.driver = true;
  o.receiverVersion = 0;
  CHECK(out.Next(o, MakePayload(0, 3)) == CHDR10PlusOutput::Step::SEND, "V3 version 0 to version 0 goes");
  out.Queued(5, MakePayload(0, 3));
  CHECK(out.Next(o, MakePayload(1, 4)) == CHDR10PlusOutput::Step::CLEAR,
        "V3 then version 1 to version 0: the one queued is taken away (HDR10)");
  CHECK(CHDR10PlusOutput::PayloadVersion(MakePayload(1, 0)) == 1 &&
            CHDR10PlusOutput::PayloadVersion(MakePayload(0, 0)) == 0,
        "V4 the version of a payload is PB4 bits 7 and 6");
}

// ---------------------------------------------------------------- E
static std::vector<uint8_t> Edid(const std::vector<std::vector<uint8_t>>& blocks, int exts = 1)
{
  std::vector<uint8_t> e(128 * (1 + exts), 0);
  static const uint8_t magic[8] = {0, 255, 255, 255, 255, 255, 255, 0};
  std::memcpy(e.data(), magic, 8);
  e[126] = static_cast<uint8_t>(exts);
  uint8_t* x = e.data() + 128;
  x[0] = 0x02;
  x[1] = 0x03;
  unsigned pos = 4;
  for (const auto& b : blocks)
  {
    std::memcpy(x + pos, b.data(), b.size());
    pos += b.size();
  }
  x[2] = static_cast<uint8_t>(pos);
  return e;
}

static void TestEdid()
{
  using KODI::UTILS::Hdr10PlusBlockVersion;
  const std::vector<uint8_t> vdb{0x43, 93, 95, 96};
  for (int v = 0; v < 4; ++v)
    CHECK(Hdr10PlusBlockVersion(Edid({vdb, {0xE5, 0x01, 0x8B, 0x84, 0x90, static_cast<uint8_t>(v)}})) == v,
          "E1 a block of version %d", v);
  CHECK(Hdr10PlusBlockVersion(Edid({vdb})) == -1, "E2 no block");
  CHECK(Hdr10PlusBlockVersion(Edid({{0xE5, 0x01, 0x46, 0xD0, 0x00, 0x01}})) == -1, "E3 the Dolby OUI");
  CHECK(Hdr10PlusBlockVersion(Edid({{0xE4, 0x01, 0x8B, 0x84, 0x90}, vdb})) == -1, "E4 a block without the version byte");
  CHECK(Hdr10PlusBlockVersion(Edid({{0xE5, 0x01, 0x8B, 0x84, 0x90, 0x00}, {0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01}})) == 1,
        "E5 two blocks: the highest version");
  CHECK(Hdr10PlusBlockVersion(Edid({{0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01}, {0xE5, 0x01, 0x8B, 0x84, 0x90, 0x00}})) == 1,
        "E5 two blocks, the higher first: the highest version");
  CHECK(Hdr10PlusBlockVersion(Edid({{0xE5, 0x01, 0x8B, 0x84, 0x90, 0x04}})) == 0, "E6 bits 1 and 0 only");
  CHECK(Hdr10PlusBlockVersion(Edid({{0x65, 0x01, 0x8B, 0x84, 0x90, 0x01}})) == -1, "E7 tag 3 with those bytes");
  auto e = Edid({{0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01}});
  e[0] = 1;
  CHECK(Hdr10PlusBlockVersion(e) == -1, "E8 no EDID header");
  e = Edid({{0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01}});
  e.pop_back();
  CHECK(Hdr10PlusBlockVersion(e) == -1, "E9 not a whole number of blocks");
  e = Edid({{0xE5, 0x01, 0x8B, 0x84, 0x90, 0x01}});
  e[128 + 2] = 9;
  CHECK(Hdr10PlusBlockVersion(e) == -1, "E10 the block runs past the data blocks");
}

// ---------------------------------------------------------------- N
static void TestModeStart()
{
  struct M
  {
    const char* driver;
    int w, h;
    double hz;
    bool want;
  } modes[] = {
      {"i915", 3840, 2160, 23.976, true}, {"i915", 3840, 2160, 24.0, true},  {"i915", 3840, 2160, 25.0, true},
      {"i915", 3840, 2160, 29.97, true},  {"i915", 3840, 2160, 30.0, true},  {"i915", 4096, 2160, 24.0, true},
      {"xe", 3840, 2160, 23.976, true},   {"i915", 3840, 2160, 50.0, false}, {"i915", 3840, 2160, 59.94, false},
      {"i915", 1920, 1080, 24.0, false},  {"i915", 2560, 1440, 60.0, false}, {"amdgpu", 3840, 2160, 23.976, false},
      {"nvidia-drm", 3840, 2160, 23.976, false}, {"i915", 3840, 2160, 0, false}, {"", 3840, 2160, 24.0, false},
  };
  for (const M& m : modes)
    CHECK(CDVPlusModeStart::NeededFor(m.driver, m.w, m.h, m.hz) == m.want, "N1 %s %dx%d %.3f: %s", m.driver, m.w,
          m.h, m.hz, m.want ? "needed" : "not needed");
  CHECK(CDVPlusModeStart::For(true, true, false) == CDVPlusModeStart::Then::WAIT, "N2 needed, the commit of the mode");
  CHECK(CDVPlusModeStart::For(false, true, false) == CDVPlusModeStart::Then::AS_BEFORE, "N2 not needed");
  CHECK(CDVPlusModeStart::For(true, false, false) == CDVPlusModeStart::Then::AS_BEFORE, "N2 not the commit of the mode");
  CHECK(CDVPlusModeStart::For(true, true, true) == CDVPlusModeStart::Then::AS_BEFORE, "N2 HDR10+ went out already");
  CHECK(CDVPlusModeStart::WaitNs(41708333) == 83416666, "N3 two periods at 23.976 Hz");
  CHECK(CDVPlusModeStart::WaitNs(16683350) == 33366700, "N3 two periods at 59.94 Hz");
  CHECK(CDVPlusModeStart::WaitNs(0) == CDVPlusModeStart::WAIT_MAX_NS, "N3 no period: the most");
  CHECK(CDVPlusModeStart::WaitNs(100000000) == CDVPlusModeStart::WAIT_MAX_NS, "N3 100 ms at most");
  CHECK(CDVPlusModeStart::After(true, true) == CDVPlusModeStart::Outcome::MEASURED, "N4 measured first");
  CHECK(CDVPlusModeStart::After(false, true) == CDVPlusModeStart::Outcome::PROVISIONAL, "N4 then provisional");
  CHECK(CDVPlusModeStart::After(false, false) == CDVPlusModeStart::Outcome::NONE, "N4 else none");
  CHECK(CDVPlusModeStart::KeepProvisional(true, false, 0, 48), "N5 kept until measured");
  CHECK(!CDVPlusModeStart::KeepProvisional(true, true, 0, 48), "N5 not once the module sends");
  CHECK(!CDVPlusModeStart::KeepProvisional(true, false, 48, 48), "N5 not after lost pictures");
  CHECK(!CDVPlusModeStart::KeepProvisional(false, false, 0, 48), "N5 not without one");
}

// ---------------------------------------------------------------- I
enum class Driver
{
  I915,
  ANY, // a flip takes HDR10+ and the AVI InfoFrame does not change (amdgpu, nvidia-drm on RTX 50, other modes)
  NVIDIA_AVI, // a flip takes HDR10+, and the first one puts the CTA VIC in the AVI InfoFrame (the HDR10+ patches of nvidia-drm: kept)
};

struct Run
{
  std::vector<uint64_t> requests; // HDR10PLUS_VSIF of the request of each frame (0 none)
  std::vector<bool> kernelHas; // the kernel kept a payload after the frame
  int refusedTests = 0;
  int silent = 0;
  bool refused = false; // CHDR10PlusOutput took the driver as refusing
  int firstKept = -1;
  bool modeCommitWithout = false;
  int aviChangesAfterMode = 0; // NVIDIA_AVI: the AVI InfoFrame changed in a commit without a mode set
};

/*
 * A Dolby Vision video sent as HDR10+: frame 0 is the commit of the mode that the hold releases (modeset, HDR on);
 * the picture of frame f is measured by frame f + latency (its own measurement in the wait at the mode: latency 0);
 * l1: its RPU has a level 1 block. dvPath false: a film's own HDR10+, a payload with every frame from frame 0.
 * modeStart: the decisions of CDVPlusModeStart (false: those of 1.0 RC4, which has none).
 */
static Run Simulate(Driver driver, bool dvPath, int latency, bool l1, bool modeStart, int frames = 40)
{
  bool nvAviCta = false; // NVIDIA_AVI: the AVI InfoFrame carries the CTA VIC (kept once in)
  Run run;
  CHDR10PlusOutput out;
  out.NewVideo();
  out.HeldModeReleased(true);
  i915_state s{};
  s.receiver_version = 1;
  uint64_t request = 0, kernel = 0, nextBlob = 1;
  std::optional<Payload> provisional;
  unsigned int provisionalFrames = 0;
  bool sent = false, modeStartDone = false;
  std::vector<std::optional<Payload>> blobs(1024);
  const Payload provisionalBytes = MakePayload(1, 200);
  for (int f = 0; f < frames; ++f)
  {
    const bool modeset = f == 0;
    // the window's payload for this frame (DVConversionHDR10Plus() before it is drawn)
    std::optional<Payload> p;
    if (!dvPath)
      p = MakePayload(1, static_cast<uint8_t>(f));
    else
    {
      // a measurement of a picture before this one has come (scene-wise the same metadata: one value)
      const bool measuredBefore = f >= std::max(latency, 1);
      if (measuredBefore)
      {
        p = MakePayload(1, 100);
        provisional.reset();
      }
      else if (CDVPlusModeStart::KeepProvisional(provisional.has_value(), false, provisionalFrames, 48))
      {
        ++provisionalFrames;
        p = provisional;
      }
    }
    CHDR10PlusOutput::Output o;
    o.pq = o.display = o.driver = true;
    o.receiverVersion = 1;
    o.alone = !modeset;
    o.withMode = out.WithMode(modeset, 1);
    auto apply = [&](const std::optional<Payload>& payload)
    {
      switch (out.Next(o, payload))
      {
        case CHDR10PlusOutput::Step::SEND:
          blobs[nextBlob] = payload;
          request = nextBlob++;
          out.Queued(request, payload);
          break;
        case CHDR10PlusOutput::Step::CLEAR:
          request = 0;
          out.Queued(0, std::nullopt);
          break;
        case CHDR10PlusOutput::Step::NOTHING:
          break;
      }
    };
    apply(p);
    // ... after it is drawn (DVPlusMeasure()): the first picture in the commit of the mode
    if (dvPath && !p && modeStart && !modeStartDone && !sent)
    {
      modeStartDone = true;
      // NeededFor() of i915 at 2160p23.976 (N1), NeededForNvidia() where nvidia-drm substitutes it (N6)
      const bool needed = driver == Driver::I915 || driver == Driver::NVIDIA_AVI;
      if (CDVPlusModeStart::For(needed, o.withMode, sent) == CDVPlusModeStart::Then::WAIT)
      {
        const bool measured = latency == 0;
        switch (CDVPlusModeStart::After(measured, l1))
        {
          case CDVPlusModeStart::Outcome::MEASURED:
            apply(MakePayload(1, 100));
            break;
          case CDVPlusModeStart::Outcome::PROVISIONAL:
            provisional = provisionalBytes;
            provisionalFrames = 0;
            apply(provisional);
            break;
          case CDVPlusModeStart::Outcome::NONE:
            break;
        }
      }
    }
    run.requests.push_back(request);
    // the commit: its test; one refused with HDR10+ is tested again without it (CDRMAtomic, Without())
    auto commit = [&](uint64_t value)
    {
      if (driver == Driver::ANY)
        return I915_ACCEPTED;
      if (driver == Driver::NVIDIA_AVI)
      {
        if (value && !nvAviCta)
        {
          nvAviCta = true;
          run.aviChangesAfterMode += modeset ? 0 : 1;
        }
        return I915_ACCEPTED;
      }
      i915_state t = s;
      const int r = i915_commit(&t, modeset, modeset, value && blobs[value] ? blobs[value]->data() : nullptr);
      if (r == I915_ACCEPTED)
        s = t;
      return r;
    };
    int r = commit(request);
    if (r != I915_ACCEPTED && request)
    {
      ++run.refusedTests;
      run.silent += r == I915_REFUSED_SILENT;
      out.TestFailed(22);
      request = *CHDR10PlusOutput::Without(request);
      r = commit(request);
    }
    if (r == I915_ACCEPTED)
      kernel = request;
    if (out.Committed(kernel))
      run.refused = true;
    out.HeldModeCommitted();
    run.kernelHas.push_back(kernel != 0);
    if (kernel != 0)
    {
      sent = true;
      if (run.firstKept < 0)
        run.firstKept = f;
    }
  }
  run.modeCommitWithout = out.ModeCommitWithout();
  return run;
}

static bool AllKept(const Run& r, int from)
{
  for (std::size_t i = from; i < r.kernelHas.size(); ++i)
    if (!r.kernelHas[i])
      return false;
  return true;
}

static void TestI915()
{
  // 1.0 RC4: the commit of the mode without metadata, then metadata from frame 1: refused for the whole video
  const Run rc4 = Simulate(Driver::I915, true, 1, true, false);
  CHECK(rc4.refused && rc4.firstKept < 0 && rc4.refusedTests >= 1 && rc4.modeCommitWithout,
        "I1 1.0 RC4 decisions on i915 at 2160p23.976: every commit with HDR10+ refused (%d), HDR10 after 8, the commit of "
        "the mode without metadata (the failure of issue 2 reproduced)",
        rc4.refusedTests);
  const Run nowMeasured = Simulate(Driver::I915, true, 0, true, true);
  CHECK(!nowMeasured.refused && nowMeasured.firstKept == 0 && AllKept(nowMeasured, 0) && nowMeasured.refusedTests == 0 &&
            !nowMeasured.modeCommitWithout,
        "I2 measured in the wait: HDR10+ from the commit of the mode on, nothing refused (refused %d, first %d)",
        nowMeasured.refusedTests, nowMeasured.firstKept);
  const Run nowProvisional = Simulate(Driver::I915, true, 3, true, true);
  CHECK(!nowProvisional.refused && nowProvisional.firstKept == 0 && AllKept(nowProvisional, 0) &&
            nowProvisional.refusedTests == 0,
        "I3 not measured in the wait: provisional metadata in the commit of the mode, kept until measured; nothing "
        "refused (refused %d)",
        nowProvisional.refusedTests);
  CHECK(nowProvisional.requests[0] != nowProvisional.requests[5], "I3 the measurement replaced the provisional");
  const Run nowNone = Simulate(Driver::I915, true, 3, false, true);
  CHECK(nowNone.refused && nowNone.firstKept < 0, "I4 neither measured nor a level 1 block: as 1.0 RC4 (refused)");
  // nvidia-drm where its HDR10+ patches put the CTA VIC in the AVI InfoFrame: with 1.0 RC4's decisions it changes a frame after the
  // mode; with CDVPlusModeStart it goes out with the mode and never changes, measured or provisional
  {
    const Run rc4 = Simulate(Driver::NVIDIA_AVI, true, 1, true, false);
    CHECK(rc4.aviChangesAfterMode == 1 && rc4.firstKept == 1,
          "I7 1.0 RC4 decisions on nvidia-drm at an HDMI VIC mode: the AVI InfoFrame changes after the mode (%d)",
          rc4.aviChangesAfterMode);
    for (int latency : {0, 3})
    {
      const Run now = Simulate(Driver::NVIDIA_AVI, true, latency, true, true);
      CHECK(now.aviChangesAfterMode == 0 && now.firstKept == 0 && AllKept(now, 0),
            "I8 nvidia-drm, latency %d: HDR10+ and the CTA VIC from the commit of the mode on, no change after it",
            latency);
    }
  }
  // nvidia-drm and amdgpu: the requests of 1.0 RC4, frame by frame
  for (int latency : {0, 1, 3})
  {
    const Run before = Simulate(Driver::ANY, true, latency, true, false);
    const Run now = Simulate(Driver::ANY, true, latency, true, true);
    CHECK(before.requests == now.requests && before.kernelHas == now.kernelHas,
          "I5 a driver that takes HDR10+ with a flip, latency %d: the same requests as 1.0 RC4", latency);
  }
  // a film's own HDR10+: the same as 1.0 RC4 on both drivers, and HDR10+ from the commit of the mode
  for (Driver d : {Driver::I915, Driver::ANY})
  {
    const Run before = Simulate(d, false, 0, true, false);
    const Run now = Simulate(d, false, 0, true, true);
    CHECK(before.requests == now.requests && !now.refused && now.firstKept == 0,
          "I6 native HDR10+ (%s): unchanged, from the commit of the mode", d == Driver::I915 ? "i915" : "flip");
  }
}

// ---------------------------------------------------------------- N6
static void TestModeStartNvidia()
{
  using namespace KODI::UTILS;
  // a display without UHD_VIC that fits: HDMI VICs 1-4 in its HDMI VSDB, VICs 93-95 and 98 in its VDB, SCDC,
  // HDR10+ and ST 2084 (as a display that declares them both)
  HdmiVicSink fit;
  for (int v : {1, 2, 3, 4})
    fit.hdmiVics.set(v);
  for (int v : {93, 94, 95, 98})
    fit.vics.set(v);
  fit.scdc = fit.hdr10Plus = fit.st2084 = true;
  struct M
  {
    int w, rr;
    bool want;
  } modes[] = {{3840, 23976, true}, {3840, 24000, true}, {3840, 25000, true}, {3840, 29970, true},
               {3840, 30000, true}, {4096, 24000, true}, {4096, 23976, false}, {3840, 50000, false},
               {3840, 59940, false}, {1920, 23976, false}};
  for (const M& m : modes)
    CHECK(CDVPlusModeStart::NeededForNvidia(true, fit, m.w, 2160, m.rr) == m.want,
          "N6 %dx2160 at %d mHz without UHD_VIC: %s", m.w, m.rr, m.want ? "waits" : "as 1.0 RC4");
  for (const M& m : modes)
    CHECK(!CDVPlusModeStart::NeededForNvidia(false, fit, m.w, 2160, m.rr),
          "N6 no NV_HDR10PLUS_AVI_CTA_VIC (RTX 50, amdgpu, i915): never, %dx2160 at %d", m.w, m.rr);
  // with UHD_VIC (the LG C5, the real S90H): only 4096x2160 at 24.000
  HdmiVicSink uhd = fit;
  uhd.uhdVic = true;
  CHECK(!CDVPlusModeStart::NeededForNvidia(true, uhd, 3840, 2160, 23976), "N6 UHD_VIC: 3840 at 23.976 goes by VIC 93");
  CHECK(!CDVPlusModeStart::NeededForNvidia(true, uhd, 3840, 2160, 30000), "N6 UHD_VIC: 3840 at 30 goes by VIC 95");
  CHECK(CDVPlusModeStart::NeededForNvidia(true, uhd, 4096, 2160, 24000), "N6 UHD_VIC: 4096 at 24.000 by HDMI VIC 4");
  // each condition of the display
  auto without = [&fit](auto f)
  {
    HdmiVicSink s = fit;
    f(s);
    return CDVPlusModeStart::NeededForNvidia(true, s, 3840, 2160, 23976);
  };
  CHECK(!without([](HdmiVicSink& s) { s.hdmiVics.reset(3); }), "N6 HDMI VIC 3 not in the HDMI VSDB");
  CHECK(!without([](HdmiVicSink& s) { s.vics.reset(93); }), "N6 VIC 93 not listed");
  CHECK(!without([](HdmiVicSink& s) { s.scdc = false; }), "N6 no SCDC");
  CHECK(!without([](HdmiVicSink& s) { s.hdr10Plus = false; }), "N6 no HDR10+");
  CHECK(!without([](HdmiVicSink& s) { s.st2084 = false; }), "N6 no ST 2084");
  // a progressive 3840x2160 detailed timing at 24.000 (297 MHz): NVKMS takes it, VIC 93 in the AVI already
  HdmiVicSink dtd = fit;
  dtd.dtds.push_back({3840, 2160, 297000, 5500, 2250, false});
  CHECK(!CDVPlusModeStart::NeededForNvidia(true, dtd, 3840, 2160, 24000), "N6 its own DTD at 24.000");
  CHECK(CDVPlusModeStart::NeededForNvidia(true, dtd, 3840, 2160, 23976), "N6 ... not at 23.976");
  // the raw EDID: an HDR static metadata block with ST 2084, with and without static metadata type 1; an HDMI VSDB
  // with HDMI VICs 1 and 3; an HF-VSDB with SCDC and UHD_VIC
  {
    const std::vector<uint8_t> vdb{0x43, 93, 95, 96};
    const std::vector<uint8_t> hdmi{0x6E, 0x03, 0x0C, 0x00, 0x10, 0x00, 0x00, 0x3C, 0x20, 0x00, 0x40, 0x01, 0x03,
                                    0x00, 0x00};
    const std::vector<uint8_t> hf{0x67, 0xD8, 0x5D, 0xC4, 0x01, 0x78, 0x80, 0x08};
    const HdmiVicSink a = HdmiVicSinkOf(Edid({vdb, {0xE3, 0x06, 0x04, 0x01}, hdmi, hf}));
    CHECK(a.st2084 && a.vics[93] && a.vics[95] && !a.vics[94] && a.hdmiVics[1] && a.hdmiVics[3] && !a.hdmiVics[2] &&
              a.scdc && a.uhdVic,
          "N7 the raw EDID: ST 2084 of type 1, VICs, HDMI VICs, SCDC and UHD_VIC");
    const HdmiVicSink b = HdmiVicSinkOf(Edid({vdb, {0xE3, 0x06, 0x04, 0x00}}));
    CHECK(!b.st2084, "N7 ST 2084 without static metadata type 1: no HDR InfoFrame from NVKMS");
    const HdmiVicSink c = HdmiVicSinkOf(Edid({vdb, {0xE3, 0x06, 0x0B, 0x01}}));
    CHECK(!c.st2084, "N7 HLG and SDR without ST 2084");
  }
  CHECK(RRx1kOf(296703, 5500, 2250) == 23976 && RRx1kOf(297000, 5500, 2250) == 24000 &&
            RRx1kOf(296703, 4400, 2250) == 29970,
        "N6 RRx1kOf()");
}

// ---------------------------------------------------------------- R
static void TestVrrOff()
{
  // (as nvidia-drm gives NVKMS the RRx1k of a mode: drm_mode_vrefresh(), rounded to the Hz, times 1000)
  CHECK(CHdmiVrrOff::RefreshMilliHz(594000, 4400, 2250) == 60000, "R1 2160p60 is 60000");
  CHECK(CHdmiVrrOff::RefreshMilliHz(593407, 4400, 2250) == 60000, "R1 2160p59.94 is 60000 (60 Hz rounded)");
  CHECK(CHdmiVrrOff::RefreshMilliHz(594000, 5280, 2250) == 50000, "R1 2160p50 is 50000");
  CHECK(CHdmiVrrOff::RefreshMilliHz(296703, 5500, 2250) == 24000, "R1 2160p23.976 is 24000");
  CHECK(CHdmiVrrOff::RefreshMilliHz(296998, 4400, 1350) == 50000, "R1 49.9997 Hz is 50000 (rounded up)");
  CHECK(CHdmiVrrOff::RefreshMilliHz(293400, 4400, 1350) == 49000, "R1 49.39 Hz is 49000");
  CHECK(CHdmiVrrOff::RefreshMilliHz(0, 4400, 2250) == 0, "R1 no clock");
  int wrong = 0;
  for (int m = 0; m < 64; ++m)
  {
    const bool prop = m & 1, withMode = m & 2, h10 = m & 4, vrr = m & 8;
    const int lines = (m & 16) ? 2160 : 576;
    const int64_t mhz = (m & 32) ? 59940 : 23976;
    const bool want = prop && withMode && h10 && vrr && lines >= 720 && mhz >= 50000;
    if (CHdmiVrrOff::Wanted(prop, withMode, h10, vrr, lines, mhz) != want)
      ++wrong;
  }
  CHECK(wrong == 0, "R2 Wanted(): all 64 combinations (%d wrong)", wrong);
  CHECK(CHdmiVrrOff::Wanted(true, true, true, true, 720, 50000), "R2 at the edge: 720 lines, 50000 mHz");
  CHECK(!CHdmiVrrOff::Wanted(true, true, true, true, 719, 50000), "R2 719 lines");
  CHECK(!CHdmiVrrOff::Wanted(true, true, true, true, 720, 49999), "R2 49999 mHz");
  CHECK(CHdmiVrrOff::Stage(true, true, 0u) == std::optional<uint64_t>(1), "R3 the mode set of the video: 1");
  CHECK(!CHdmiVrrOff::Stage(false, true, 0u), "R3 never without a mode set");
  CHECK(!CHdmiVrrOff::Stage(true, true, 1u), "R3 1 already: nothing");
  CHECK(CHdmiVrrOff::Stage(true, false, 1u) == std::optional<uint64_t>(0), "R3 another mode set: back to 0");
  CHECK(!CHdmiVrrOff::Stage(true, false, 0u), "R3 0 already: nothing");
  CHECK(!CHdmiVrrOff::Stage(true, true, std::nullopt), "R3 no property: nothing");
  CHECK(!CHdmiVrrOff::Stage(false, false, 1u), "R3 a flip after the video: left as it is");
  CHECK(CHdmiVrrOff::Without(1u) == std::optional<uint64_t>(0), "R4 a refused test with it: 0");
  CHECK(!CHdmiVrrOff::Without(0u) && !CHdmiVrrOff::Without(std::nullopt), "R4 a refused test without it: nothing");
  CHECK(std::string(CHdmiVrrOff::PROPERTY) == "NV_HDMI_VRR_OFF", "R5 the property of nvidia-open-kernel-modules-9051");
}

int main()
{
  TestVersion();
  TestEdid();
  TestModeStart();
  TestModeStartNvidia();
  TestI915();
  TestVrrOff();
  std::printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures != 0;
}
