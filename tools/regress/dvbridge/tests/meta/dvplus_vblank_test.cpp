// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: CDVPlusVBlank (xbmc/windowing/gbm/DVPlusVBlank.h of the tree), the time
// of the last vblank for Dolby Vision sent as HDR10+, with the two ioctls replaced by recorders that answer as a
// driver would: i915 and amdgpu with CRTC_GET_SEQUENCE; a driver whose CRTC_GET_SEQUENCE fails and whose legacy ioctl
// answers (each CRTC index 0 to 7: the request names it in its high-CRTC bits as drm_wait_vblank_ioctl() reads them);
// nvidia-drm with its defaults (EOPNOTSUPP from both); the errors kept for the log.
// (1.0 RC4 asked the legacy ioctl alone, and the case of a driver whose legacy query fails and CRTC_GET_SEQUENCE
// answers failed there; the runner proves the checks with mutants of DVPlusVBlank.h.)
#include "windowing/gbm/DVPlusVBlank.h"

#include <cerrno>
#include <cstdio>
#include <vector>

using KODI::WINDOWING::GBM::CDVPlusVBlank;

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

// the pipe that drm_wait_vblank_ioctl() of Linux 7.3 reads from a request type (no leases)
static int KernelPipe(uint32_t type)
{
  const uint32_t high = type & 0x3e;
  return high ? static_cast<int>(high >> 1) : ((type & 0x20000000) ? 1 : 0);
}

struct Driver
{
  int seqError = 0; // errno of CRTC_GET_SEQUENCE (0: answers)
  int legacyError = 0; // errno of WAIT_VBLANK
  uint64_t seqNs = 0;
  long sec = 0, usec = 0;
  int crtcIndexOfId(uint32_t id) const { return static_cast<int>(id) - 40; } // ids 40.. are indices 0..
  std::vector<uint32_t> seqAsked, legacyTypes;
};

static int64_t Ask(Driver& d, uint32_t crtcId, uint32_t index, CDVPlusVBlank::Errors* e)
{
  return CDVPlusVBlank::Last(
      [&d](uint32_t id, uint64_t* seq, uint64_t* ns)
      {
        d.seqAsked.push_back(id);
        if (d.seqError)
          return d.seqError;
        *seq = 1234;
        *ns = d.seqNs;
        return 0;
      },
      [&d](uint32_t type, long* sec, long* usec)
      {
        d.legacyTypes.push_back(type);
        if (d.legacyError)
          return d.legacyError;
        *sec = d.sec;
        *usec = d.usec;
        return 0;
      },
      crtcId, index, e);
}

int main()
{
  // T1 amdgpu / i915: CRTC_GET_SEQUENCE answers: its time, by the CRTC's id, legacy not asked
  {
    Driver d;
    d.seqNs = 7000123456789;
    CDVPlusVBlank::Errors e;
    const int64_t t = Ask(d, 43, 3, &e);
    CHECK(t == 7000123456789 && d.seqAsked.size() == 1 && d.seqAsked[0] == 43 && d.legacyTypes.empty() &&
              e.sequence == 0 && e.legacy == 0,
          "T1 CRTC_GET_SEQUENCE by the id of the CRTC (index 3): its time in ns, the legacy ioctl not asked");
  }
  // T2 a driver whose legacy query fails and CRTC_GET_SEQUENCE answers (what Hades cannot rule out on amdgpu)
  {
    Driver d;
    d.seqNs = 9000000000001;
    d.legacyError = EINVAL;
    CDVPlusVBlank::Errors e;
    const int64_t t = Ask(d, 41, 1, &e);
    CHECK(t == 9000000000001, "T2 the legacy ioctl fails, CRTC_GET_SEQUENCE answers: the vblank is read (%lld)",
          static_cast<long long>(t));
  }
  // T3 CRTC_GET_SEQUENCE fails: the legacy ioctl, for each index the pipe that the kernel reads
  {
    int wrong = 0;
    for (uint32_t index = 0; index < 8; ++index)
    {
      Driver d;
      d.seqError = EINVAL;
      d.sec = 7000;
      d.usec = 123456;
      CDVPlusVBlank::Errors e;
      const int64_t t = Ask(d, 40 + index, index, &e);
      const bool ok = t == 7000123456000LL && d.legacyTypes.size() == 1 && (d.legacyTypes[0] & 1) &&
                      KernelPipe(d.legacyTypes[0]) == static_cast<int>(index)
                      && e.sequence == EINVAL && e.legacy == 0
          ;
      if (!ok)
        ++wrong;
    }
    CHECK(wrong == 0, "T3 the legacy ioctl names CRTC index 0..7 as the kernel reads it, relative, its time (%d wrong)",
          wrong);
  }
  // T4 nvidia-drm with its defaults: neither; both errors kept
  {
    Driver d;
    d.seqError = EOPNOTSUPP;
    d.legacyError = EOPNOTSUPP;
    CDVPlusVBlank::Errors e;
    const int64_t t = Ask(d, 40, 0, &e);
    CHECK(t == 0, "T4 nvidia-drm: no vblank");
    CHECK(e.sequence == EOPNOTSUPP && e.legacy == EOPNOTSUPP, "T4 both errors kept for the log");
  }
  // T5 a time of 0 is none (a CRTC whose vblanks never ran), from either ioctl
  {
    Driver d;
    d.seqNs = 0;
    d.sec = 0;
    d.usec = 0;
    CDVPlusVBlank::Errors e;
    CHECK(Ask(d, 40, 0, &e) == 0 && e.sequence == EINVAL && e.legacy == EINVAL,
          "T5 times of 0 count as none, and as errors");
    Driver z;
    CDVPlusVBlank::Errors f;
    z.seqNs = 0;
    z.sec = 5;
    z.usec = 0;
    CHECK(Ask(z, 40, 0, &f) == 5000000000LL, "T5 CRTC_GET_SEQUENCE gives 0: the legacy time");
    Driver n;
    CDVPlusVBlank::Errors g;
    n.seqNs = 1;
    const int64_t t = Ask(n, 0, 0, &g);
    CHECK(t == 0 && n.seqAsked.empty() && g.sequence == ENODEV,
          "T6 no CRTC id: CRTC_GET_SEQUENCE is not asked, ENODEV kept");
  }
  CHECK(CDVPlusVBlank::LegacyType(0) == 0x1 && CDVPlusVBlank::LegacyType(1) == 0x3 &&
            CDVPlusVBlank::LegacyType(2) == 0x5 && CDVPlusVBlank::LegacyType(31) == 0x3f,
        "T7 the request types");
  std::printf("%s: %d checks, %d failed\n", g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures != 0;
}
