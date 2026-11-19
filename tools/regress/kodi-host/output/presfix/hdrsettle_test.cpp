// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: the HDR state of CWinSystemGbm (CHDRSignal: Request(), Requeued(), Settle())
// against a model of CDRMAtomic and of the kernel: what the kernel has for HDR_OUTPUT_METADATA after commits that
// the driver takes, refuses (its test: the frame goes out with the request before, which the kernel has already),
// or fails, with the queue again of a refused request (K22's 8-bit output) and two changes before one commit.
//
// After the commit that carried a change and the next frame, the state has to be the kernel's: GetEotf() says
// HDR only where the kernel signals it, the value that a modeset queues again is the kernel's, and the only blob
// alive is the one the kernel has (nothing leaked, nothing the kernel holds freed).
//
// With -DOLD the window system does what it did before (no Before, no Settle()): the checks of a refused commit
// must fail there.
//
// presfix3: and with a held change of mode (CHeldModeAnnouncement, D6) that the driver refuses together with the
// HDR of the video: the mode goes out alone, then the HDR (OUTPUT_AFTER queues it again). SettleHDR() between the
// two took the HDR back (the kernel had the state before it), and OUTPUT_AFTER queued that again: the video went
// on without HDR. Against the code before (no CHeldModeAnnouncement::Alone()) those checks must fail.
//
// g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I<kodi>/xbmc -I<kodi>/xbmc/windowing/gbm hdrsettle_test.cpp
#include "HDRSignal.h"
#include "ModeChangeHold.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

using KODI::UTILS::Eotf;
using KODI::WINDOWING::GBM::CHDRSignal;
using KODI::WINDOWING::GBM::CHeldModeAnnouncement;

namespace
{
// CWinSystemGbm::SettleHDR() leaves the state as it is while the mode of a held change goes out alone (presfix3);
// a tree without CHeldModeAnnouncement::Alone() settles it then
template<typename A>
bool ModeGoesAlone(const A& announce)
{
  if constexpr (requires { announce.Alone(); })
    return announce.Alone();
  else
    return false;
}

int g_failures = 0, g_checks = 0;
void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

struct FakeBlob
{
  static inline std::set<uint32_t> live;
  static inline uint32_t next = 100;
  uint32_t id = 0;
  FakeBlob() = default;
  explicit FakeBlob(bool made)
  {
    if (made)
      live.insert(id = next++);
  }
  ~FakeBlob() { Reset(); }
  FakeBlob(const FakeBlob&) = delete;
  FakeBlob& operator=(const FakeBlob&) = delete;
  FakeBlob(FakeBlob&& o) noexcept : id(std::exchange(o.id, 0)) {}
  FakeBlob& operator=(FakeBlob&& o) noexcept
  {
    if (this != &o)
    {
      Reset();
      id = std::exchange(o.id, 0);
    }
    return *this;
  }
  uint32_t Get() const { return id; }
  bool IsValid() const { return id != 0; }
  void Reset()
  {
    if (id)
      live.erase(id);
    id = 0;
  }
};

// CDRMAtomic and the kernel, for HDR_OUTPUT_METADATA: the request being filled (its value, if one was added),
// the front of the queue (the last request taken), and the kernel's value
struct Drm
{
  std::optional<uint64_t> filling;
  uint64_t generation = 0;
  uint64_t kernel = 0;
  bool readable = true;
  // the mode (MODE_ID) of the request being filled, if one was added, and the kernel's; whether the driver refuses
  // a change of mode and of HDR_OUTPUT_METADATA in one commit (and takes each alone: the premise of D6)
  std::optional<int> fillingMode;
  int kernelMode = 60;
  bool refuseTogether = false;
  enum class Commit { TAKEN, TEST_REFUSED, FAILED };
  Commit Decide() const
  {
    const bool modeChange = fillingMode && *fillingMode != kernelMode;
    const bool hdrChange = filling && *filling != kernel;
    return refuseTogether && modeChange && hdrChange ? Commit::TEST_REFUSED : Commit::TAKEN;
  }
  void Finish() { filling.reset(); fillingMode.reset(); ++generation; }
  void Flip(Commit c)
  {
    if (c == Commit::TAKEN && filling)
      kernel = *filling;
    if (c == Commit::TAKEN && fillingMode)
      kernelMode = *fillingMode;
    // (refused test: the front goes out, whose values the kernel has; failed: nothing goes out)
    Finish();
  }
};

struct Out
{
  Drm& drm;
  bool make = true;
  FakeBlob Make() const { return FakeBlob(make); }
  bool Queue(uint64_t value)
  {
    drm.filling = value;
    return true;
  }
  void Modeset() {}
  void Along() {}
};

// the window system: m_eotf, m_hdrBlob, m_queuedHdrMetadata, m_hdrBefore
struct WinSystem
{
  Drm drm;
  Eotf eotf = Eotf::TRADITIONAL_SDR;
  FakeBlob blob;
  std::optional<uint64_t> queued;
#ifndef OLD
  std::optional<CHDRSignal::Before<FakeBlob>> before;
#endif
  int refusedLogs = 0;
  // D6: the held change of mode as CWinSystemGbm tells of it (m_modeAnnounce), its mode, and whether the next
  // commit carries the mode (AllowModeset())
  CHeldModeAnnouncement announce;
  int mode = 60;
  bool modeset = false;

  void Settle()
  {
#ifndef OLD
    if (ModeGoesAlone(announce))
      return;
    if (CHDRSignal::Settle(eotf, blob, queued, before, drm.generation,
                           [this] { return drm.readable ? std::optional<uint64_t>(drm.kernel) : std::nullopt; }) ==
        CHDRSignal::Settled::REFUSED)
      ++refusedLogs;
#endif
  }
  void SetHDR(std::optional<Eotf> want)
  {
    Settle();
    CHDRSignal::Display display{true, true, true, true};
    Out out{drm};
#ifdef OLD
    CHDRSignal::Request(eotf, blob, queued, want, display, !want, out);
#else
    CHDRSignal::Request(eotf, blob, queued, want, display, !want, out, &before, drm.generation);
#endif
  }
  void Requeue()
  {
    if (queued)
    {
      drm.filling = *queued;
#ifndef OLD
      CHDRSignal::Requeued(before, drm.generation);
#endif
    }
  }
  void FlipPage(Drm::Commit c)
  {
    Settle();
    drm.Flip(c);
  }
  // CWinSystemGbm::FlipPage() as CWinSystemGbmGLESContext::PresentRender() makes it while a held change of mode
  // waits: told before the commit (AnnounceHeldModeChange()), the mode in it where a modeset is allowed, the
  // driver's answer, and HeldModeChangeCommitted(!fellBack, false, fellBack) after it
  void FlipPageHeld()
  {
    Settle();
    announce.Before(false);
    if (modeset)
    {
      drm.fillingMode = mode;
      modeset = false;
    }
    const Drm::Commit c = drm.Decide();
    drm.Flip(c);
    const bool fellBack = c == Drm::Commit::TEST_REFUSED;
    switch (announce.After(!fellBack, false, fellBack))
    {
      case CHeldModeAnnouncement::Then::MODE_ALONE:
        modeset = true; // AllowModeset()
        break;
      case CHeldModeAnnouncement::Then::OUTPUT_AFTER:
        Requeue(); // RequeueOutputProperties(): ... and AllowModeset()
        modeset = true;
        break;
      default:
        break;
    }
  }
};

// the state after the next frame, against the kernel
void Truth(WinSystem& w, const std::string& what)
{
  w.FlipPage(Drm::Commit::TAKEN); // the next frame (nothing new queued)
  const bool hdr = w.eotf != Eotf::TRADITIONAL_SDR;
  Check(hdr == (w.drm.kernel != 0), what + ": GetEotf() says " + (hdr ? "HDR" : "no HDR") + ", the kernel " +
                                        (w.drm.kernel ? "signals HDR" : "signals none"));
  Check(w.queued.value_or(0) == w.drm.kernel, what + ": what a modeset queues again is the kernel's value");
  const bool blobOk = w.drm.kernel ? (FakeBlob::live == std::set<uint32_t>{static_cast<uint32_t>(w.drm.kernel)})
                                   : FakeBlob::live.empty();
  Check(blobOk, what + ": the only blob alive is the kernel's (" + std::to_string(FakeBlob::live.size()) + " alive)");
}
} // namespace

int main()
{
  using C = Drm::Commit;
  struct Case
  {
    const char* name;
    void (*run)(WinSystem&);
  };
  const Case cases[] = {
      {"SDR to PQ, taken", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); }},
      {"SDR to PQ, its test refused", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TEST_REFUSED); }},
      {"SDR to PQ, its commit failed", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::FAILED); }},
      {"PQ to HLG, refused", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); w.SetHDR(Eotf::HLG); w.FlipPage(C::TEST_REFUSED); }},
      {"PQ to HLG, taken", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); w.SetHDR(Eotf::HLG); w.FlipPage(C::TAKEN); }},
      {"PQ to none (end of the video), refused", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); w.SetHDR(std::nullopt); w.FlipPage(C::TEST_REFUSED); }},
      {"PQ to none, taken", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); w.SetHDR(std::nullopt); w.FlipPage(C::TAKEN); }},
      {"two changes before one commit, taken", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.SetHDR(Eotf::HLG); w.FlipPage(C::TAKEN); }},
      {"two changes before one commit, refused", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.SetHDR(Eotf::HLG); w.FlipPage(C::TEST_REFUSED); }},
      {"refused, queued again (the 8-bit output), taken", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TEST_REFUSED); w.Requeue(); w.FlipPage(C::TAKEN); }},
      {"refused, queued again, refused again", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TEST_REFUSED); w.Requeue(); w.FlipPage(C::TEST_REFUSED); }},
      {"PQ, refused, then a new picture taken", [](WinSystem& w) { w.SetHDR(Eotf::PQ); w.FlipPage(C::TEST_REFUSED); w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); }},
      {"the kernel cannot be read (taken as made)", [](WinSystem& w) { w.drm.readable = false; w.SetHDR(Eotf::PQ); w.FlipPage(C::TAKEN); }},
  };
  for (const auto& c : cases)
  {
    FakeBlob::live.clear();
    {
      WinSystem w;
      c.run(w);
      Truth(w, c.name);
      if (std::string(c.name).find("refused") != std::string::npos && std::string(c.name).find("taken") == std::string::npos)
        Check(w.refusedLogs >= 1, std::string(c.name) + ": the refusal is told in the log");
    }
    Check(FakeBlob::live.empty(), std::string(c.name) + ": no blob left once the window system is gone");
  }
  // presfix3, D6 with SettleHDR(): the change of mode of an HDR video is held, its HDR is asked for, and the
  // commit that makes the change carries both; the driver refuses them together and takes each alone. The mode
  // goes out alone, then the HDR (OUTPUT_AFTER). The video has to end in its mode with its HDR, and the HDR is
  // not refused in between: the refused commit did not carry it alone.
  for (const bool hdrFirst : {true, false})
  {
    const std::string what = std::string("held mode refused with the HDR of the video (") +
                             (hdrFirst ? "HDR asked before the hold" : "HDR asked once the change is held") + ")";
    FakeBlob::live.clear();
    {
      WinSystem w;
      w.drm.refuseTogether = true;
      w.mode = 24;
      if (hdrFirst)
        w.SetHDR(Eotf::PQ);
      w.announce.Held();
      w.modeset = true;
      if (!hdrFirst)
        w.SetHDR(Eotf::PQ);
      for (int i = 0; i < 4; ++i)
        w.FlipPageHeld();
      Truth(w, what);
      Check(w.drm.kernelMode == 24, what + ": the held mode went out");
      Check(w.drm.kernel != 0 && w.eotf == Eotf::PQ,
            what + ": the HDR of the video went out after it (HDR_OUTPUT_METADATA " +
                (w.drm.kernel ? "set" : "0") + "), and GetEotf() says PQ");
      Check(w.refusedLogs == 0, what + ": no refusal of the HDR is told (" + std::to_string(w.refusedLogs) + ")");
    }
    Check(FakeBlob::live.empty(), what + ": no blob left once the window system is gone");
  }
  // the existing behaviour, without a Before: Request() as before (the state changes for good)
  {
    FakeBlob::live.clear();
    Drm drm;
    Eotf eotf = Eotf::TRADITIONAL_SDR;
    FakeBlob blob;
    std::optional<uint64_t> queued;
    Out out{drm};
    CHDRSignal::Display display{true, true, true, true};
    const auto o = CHDRSignal::Request(eotf, blob, queued, Eotf::PQ, display, false, out);
    Check(o.Sent() && eotf == Eotf::PQ && queued && *queued == blob.Get(), "without a Before, Request() as before");
  }
  std::printf("HDRSETTLE-TEST: %d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}

