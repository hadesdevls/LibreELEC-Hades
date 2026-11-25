// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: the HDR InfoFrame that tells the display SDR while an SDR picture goes out in BT.2020 after an
// HDR10 one (CHDRSignal::SdrSignal(), SdrSignalSettled(); CWinSystemGbm::SetSdrSignal()), against a model of the
// request and of the kernel: what the kernel has for HDR_OUTPUT_METADATA after each commit. The case of an LG G5
// that stayed in PQ (CroqueMr's build, dd2c267ccd): without it, the kernel has nothing after the HDR10 picture, at
// the same gamut. Prints "FAIL: ..." for each check that fails, and "SDR-SIGNAL <n> checks, <f> failed".
//
// g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I<kodi>/xbmc -I<kodi>/xbmc/windowing/gbm sdrsignal_test.cpp
#include "HDRSignal.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>

using KODI::UTILS::Eotf;
using KODI::WINDOWING::GBM::CHDRSignal;

namespace
{
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

// What the blobs say: the EOTF each was made with (0: SDR, 2: PQ)
std::map<uint64_t, int> g_eotfOf;
uint64_t g_next = 10;

class Blob
{
public:
  Blob() = default;
  explicit Blob(int eotf) : m_id(g_next++) { g_eotfOf[m_id] = eotf; }
  Blob(Blob&& o) noexcept : m_id(std::exchange(o.m_id, 0)) {}
  Blob& operator=(Blob&& o) noexcept
  {
    Reset();
    m_id = std::exchange(o.m_id, 0);
    return *this;
  }
  ~Blob() { Reset(); }
  void Reset()
  {
    if (m_id)
      g_eotfOf.erase(m_id);
    m_id = 0;
  }
  bool IsValid() const { return m_id != 0; }
  uint64_t Get() const { return m_id; }

private:
  uint64_t m_id = 0;
};

// The request: what it queues for HDR_OUTPUT_METADATA; the blob it makes is of the EOTF asked for
struct Out
{
  int eotf = 0;
  std::optional<uint64_t> pending;
  Blob Make() const { return Blob(eotf); }
  bool Queue(uint64_t value)
  {
    pending = value;
    return true;
  }
  void Modeset() const {}
  void Along() const {}
};

// The window system and the kernel
struct System
{
  Eotf eotf = Eotf::TRADITIONAL_SDR;
  Blob blob;
  std::optional<uint64_t> queued;
  std::optional<CHDRSignal::Before<Blob>> before;
  uint64_t request = 1;
  uint64_t sdrId = 0;
  uint64_t kernel = 0;
  CHDRSignal::Display display{true, true, true, true};
  CHDRSignal::SdrFacts facts{true, false};
  Out out;

  // a commit that the driver takes (or refuses), then the settle of the next frame
  void Commit(bool taken = true)
  {
    if (taken && out.pending)
      kernel = *out.pending;
    out.pending.reset();
    ++request;
    CHDRSignal::Settle(eotf, blob, queued, before, request, [this] { return std::optional(kernel); });
    CHDRSignal::SdrSignalSettled(sdrId, before.has_value(), queued);
  }
  // SetHDR() of a picture: PQ, or SDR (none)
  void Picture(bool pq)
  {
    out.eotf = 2;
    CHDRSignal::Display d = display;
    d.eotf = true;
    CHDRSignal::Request(eotf, blob, queued, pq ? std::optional(Eotf::PQ) : std::nullopt, d, false, out,
                        &before, request);
  }
  // SetHDR() without a picture: the end of a video
  void End() { CHDRSignal::Request(eotf, blob, queued, std::nullopt, display, true, out, &before, request); }
  // SetSdrWide(): an SDR picture goes out in BT.2020 now, or no more
  const char* Wide(bool on)
  {
    out.eotf = 0;
    return CHDRSignal::SdrSignal(on, eotf, blob, queued, sdrId, facts, display, out, &before, request);
  }
  int KernelEotf() const
  {
    const auto it = g_eotfOf.find(kernel);
    return kernel == 0 ? -1 : it == g_eotfOf.end() ? -2 : it->second;
  }
};
} // namespace

int main()
{
  {
    // HDR10, then an SDR picture of BT.2020 primaries that goes out in BT.2020, then the GUI
    System s;
    s.Picture(true);
    s.Commit();
    Check(s.KernelEotf() == 2, "HDR10: the kernel has the HDR metadata of PQ");
    s.Picture(false); // the next video: SDR, SetHDR() takes the HDR metadata away ...
    const char* why = s.Wide(true); // ... and SDR in BT.2020 starts in the same configuration
    Check(why == nullptr, "SDR in BT.2020 after HDR10: the HDR InfoFrame of SDR is sent");
    s.Commit();
    Check(s.KernelEotf() == 0, "SDR in BT.2020 after HDR10: the kernel has an HDR InfoFrame of the EOTF of SDR "
                               "(not nothing, which an LG G5 took for PQ still)");
    Check(s.eotf == Eotf::TRADITIONAL_SDR, "... and the output is said to be SDR");
    Check(s.sdrId != 0 && s.queued == s.sdrId, "... and it is known as the one that says SDR");
    s.Commit(); // another frame: nothing changes
    Check(s.KernelEotf() == 0 && s.sdrId != 0, "a frame later the kernel still has it");
    s.Wide(true); // asked again (another configuration of the same video): one InfoFrame, the same
    s.Commit();
    Check(s.KernelEotf() == 0 && s.queued == s.sdrId, "asked again: the same InfoFrame stays");
    s.Wide(false); // the video ends
    s.Commit();
    Check(s.kernel == 0, "SDR in BT.2020 ends: the HDR InfoFrame of SDR is taken away");
    Check(s.sdrId == 0, "... and forgotten once the kernel has the end");
    Check(g_eotfOf.empty(), "no blob is left alive");
  }
  {
    // an HDR10 picture after SDR in BT.2020: its own metadata, never replaced by the one of SDR
    System s;
    s.Picture(false);
    s.Wide(true);
    s.Commit();
    Check(s.KernelEotf() == 0, "SDR in BT.2020: the HDR InfoFrame of SDR");
    s.Picture(true); // the next video is HDR10 (SetHDR() before the renderer ends SDR in BT.2020)
    s.Wide(false);   // the end of SDR in BT.2020 in the same configuration takes nothing of the HDR10 one
    s.Commit();
    Check(s.KernelEotf() == 2, "HDR10 after SDR in BT.2020: the kernel has the HDR metadata of PQ");
    s.Wide(true); // SDR in BT.2020 asked while an HDR picture has the property
    s.Commit();
    Check(s.KernelEotf() == 2, "while an HDR picture has the property, the InfoFrame of SDR takes nothing of it");
    Check(s.sdrId == 0, "the id of the InfoFrame of SDR is forgotten once the HDR10 one is the kernel's");
    s.End();
    s.Commit();
    Check(s.kernel == 0, "the end of the HDR10 video: nothing");
  }
  {
    // where it is not sent: the driver tells SDR itself, the settings have no HDR display, the display does not
    // declare the EOTF of SDR; and a commit that the driver refuses
    System n;
    n.facts.driverTells = true;
    n.Picture(true);
    n.Commit();
    n.Picture(false);
    Check(n.Wide(true) != nullptr, "nvidia-drm: not sent, and why is said");
    n.Commit();
    Check(n.kernel == 0, "nvidia-drm: the kernel has nothing (NVKMS tells the display SDR itself)");
    System o;
    o.facts.hdrDisplay = false;
    Check(o.Wide(true) != nullptr && (o.Commit(), o.kernel == 0), "no HDR display in the settings: not sent");
    System e;
    e.display.eotf = false;
    const char* why = e.Wide(true);
    Check(why != nullptr && std::string(why).find("EOTF of SDR") != std::string::npos,
          "a display that does not declare the EOTF of SDR: not sent, and that is why");
    e.Commit();
    Check(e.kernel == 0, "... the kernel has nothing");
    System r;
    r.Wide(true);
    r.Commit(false); // refused
    Check(r.kernel == 0 && r.queued.value_or(0) == 0, "a refused commit: the state is what the kernel has, nothing");
    Check(r.sdrId == 0, "... and the InfoFrame of SDR is not taken as queued");
    r.Wide(true); // asked again with the next configuration
    r.Commit();
    Check(r.KernelEotf() == 0, "asked again after a refusal: sent");
  }
  std::printf("SDR-SIGNAL %d checks, %d failed\n", g_checks, g_failures);
  if (!g_failures)
    std::printf("SDR-SIGNAL-TEST PASSED\n");
  return g_failures ? 1 : 0;
}
