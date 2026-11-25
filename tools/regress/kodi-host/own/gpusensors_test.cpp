/*
 *  Copyright (C) 2026 Hades
 *  This file is part of LibreELEC Hades. It is not in Kodi as Team Kodi publishes it.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

// Host test of the use of an amdgpu GPU in the player information (GPUSensors.h, CGpu): the time of
// its graphics engine on Kodi's work, from the fdinfo of Kodi's DRM files, between two readings, and
// gpu_busy_percent where the fdinfo does not give it. A root of files stands for /sys and /proc/self:
// the DRM device 226:0 is the PCI function 0000:03:00.0 of the driver amdgpu, with gpu_busy_percent
// at 100 (what a Ryzen 5 3400GE showed); the fdinfo has a DRM file of that GPU, the same file again
// (dup(): the same drm-client-id), a file of another amdgpu GPU, one of i915 and one that is no DRM
// file. The clock of the readings is the test's.

#include "GPUSensors.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace KODI::WINDOWING::GBM::GPUSENSORS;
namespace fs = std::filesystem;

static int g_failed = 0;
static int g_checks = 0;

static void Check(bool ok, const std::string& what, const Reading& r)
{
  ++g_checks;
  if (ok)
    std::cout << "ok   " << what << "\n";
  else
  {
    ++g_failed;
    std::cout << "FAIL " << what << ": busy " << (r.busy ? std::to_string(*r.busy) : "none")
              << "; " << r.how << "\n";
  }
}

static void Write(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path) << text;
}

static std::string Drm(const std::string& driver, const std::string& pci, int client,
                       long long gfx, long long compute)
{
  std::string t = "pos:\t0\nflags:\t02100002\nmnt_id:\t26\nino:\t1050\ndrm-driver:\t" + driver +
                  "\ndrm-client-id:\t" + std::to_string(client) + "\ndrm-pdev:\t" + pci +
                  "\npasid:\t32770\ndrm-memory-vram:\t1024 KiB\n";
  if (gfx >= 0)
    t += "drm-engine-gfx:\t" + std::to_string(gfx) + " ns\n";
  if (compute >= 0)
    t += "drm-engine-compute:\t" + std::to_string(compute) + " ns\n";
  t += "drm-engine-dec:\t999999999 ns\n";
  return t;
}

int main()
{
  const fs::path root = fs::temp_directory_path() / ("gpusensors-test-" + std::to_string(getpid()));
  fs::remove_all(root);
  const fs::path pci = root / "sys/devices/pci0000:00/0000:00:08.1/0000:03:00.0";
  fs::create_directories(pci);
  fs::create_directories(root / "sys/bus/pci/drivers/amdgpu");
  fs::create_symlink(root / "sys/bus/pci/drivers/amdgpu", pci / "driver");
  Write(pci / "gpu_busy_percent", "100\n");
  fs::create_directories(root / "sys/dev/char/226:0");
  fs::create_symlink(pci, root / "sys/dev/char/226:0/device");
  const fs::path fdinfo = root / "proc/self/fdinfo";
  long long gfx = 5000000000LL, compute = 0, other = 1000000000LL;
  auto fds = [&]
  {
    Write(fdinfo / "3", "pos:\t0\nflags:\t02000002\nmnt_id:\t15\nino:\t42\n");
    Write(fdinfo / "11", Drm("amdgpu", "0000:03:00.0", 7, gfx, compute));
    Write(fdinfo / "12", Drm("amdgpu", "0000:03:00.0", 7, gfx, compute));
    Write(fdinfo / "13", Drm("amdgpu", "0000:04:00.0", 9, other, -1));
    Write(fdinfo / "14", Drm("i915", "0000:03:00.0", 12, other, -1));
  };
  fds();

  PerfOps perf = PerfOps::System();
  CGpu gpu(root.string(), makedev(226, 0), "", perf);
  int64_t now = 1000000000LL;
  gpu.SetClock([&now] { return now; });

  Check(gpu.NeedsEarlierReading(), "1 the first use of amdgpu is a difference: a reading before it",
        Reading{});
  Reading r = gpu.Read();
  Check(r.busy && *r.busy == 100 && r.how.find("gpu_busy_percent") != std::string::npos &&
            r.how.find("no reading before this one") != std::string::npos,
        "2 the first reading: gpu_busy_percent, saying why", r);
  Check(!gpu.NeedsEarlierReading(), "3 after one reading, no reading before is needed", r);

  // 250 ms of the engine in 500 ms; the other GPU and i915 move a lot, which does not count
  gfx += 250000000LL;
  other += 9000000000LL;
  now += 500000000LL;
  fds();
  r = gpu.Read();
  Check(r.busy && *r.busy == 50 && r.how.find("fdinfo") != std::string::npos &&
            r.how.find("over 500 ms") != std::string::npos,
        "4 250 ms of the engine in 500 ms: 50, from the fdinfo (the file shared by dup() once, the "
        "other GPU and i915 not)",
        r);

  // the compute engine counts too
  compute += 100000000LL;
  now += 1000000000LL;
  fds();
  r = gpu.Read();
  Check(r.busy && *r.busy == 10, "5 100 ms of the compute engine in 1 s: 10", r);

  // no more than 100
  gfx += 2000000000LL;
  now += 1000000000LL;
  fds();
  r = gpu.Read();
  Check(r.busy && *r.busy == 100, "6 2 s of the engine in 1 s: 100, not more", r);

  // a file closed: the time goes back; gpu_busy_percent this once, then from the fdinfo again
  gfx = 100000000LL;
  compute = 0;
  now += 1000000000LL;
  fds();
  r = gpu.Read();
  Check(r.busy && *r.busy == 100 && r.how.find("went back") != std::string::npos,
        "7 the time went back: gpu_busy_percent, saying why", r);
  gfx += 300000000LL;
  now += 1000000000LL;
  fds();
  r = gpu.Read();
  Check(r.busy && *r.busy == 30, "8 then from the fdinfo again: 30", r);

  // no DRM file of this GPU gives the time of an engine: gpu_busy_percent
  fs::remove(fdinfo / "11");
  fs::remove(fdinfo / "12");
  Write(pci / "gpu_busy_percent", "63\n");
  now += 1000000000LL;
  r = gpu.Read();
  Check(r.busy && *r.busy == 63 && r.how.find("gives no time of its graphics engine") != std::string::npos,
        "9 no DRM file of this GPU: gpu_busy_percent, saying why", r);

  // a DRM file of this GPU without the time of an engine (no work yet) is no time
  Write(fdinfo / "11", Drm("amdgpu", "0000:03:00.0", 7, -1, -1));
  now += 1000000000LL;
  r = gpu.Read();
  Check(r.busy && *r.busy == 63, "10 a DRM file with no engine time: gpu_busy_percent", r);

  fs::remove_all(root);
  std::cout << g_checks << " checks, " << g_failed << " failed\n";
  if (g_failed)
    return 1;
  std::cout << "ALL PASSED\n";
  return 0;
}
