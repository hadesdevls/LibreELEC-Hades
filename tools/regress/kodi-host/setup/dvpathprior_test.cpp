// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades: after an update of the build or of the driver, the first Dolby Vision video starts from what the last build
// found with the same GPU (xbmc/windowing/gbm/DVPathChoice.h: DVPathGpu(), CDVPathMemory::Prior()), not from the
// defaults: on the integrated GPU of a Ryzen 9 7950X3D a FEL stream drawn directly (the default) takes 35 to 37 ms of
// the 41.7 of a refresh, composed 30, and the first video after every update stuttered until the two ways had been
// compared again (README, "Known limits"). Checked: the GPU of real GL_RENDERER strings of Mesa (radeonsi, iris, zink,
// llvmpipe) and NVIDIA with their versions changed, the memory with entries of two GPUs and of Hades up to 1.1 (no
// gpu), what a new build is given, and a CDVPathChoice that recalls it: composed from its first frame. With -DOLD_RULE
// the test carries the rule of 1.1 (nothing for a key that has nothing), which must fail.
#include "DVPathChoice.h"

#include <cstdio>
#include <string>

using namespace KODI::WINDOWING::GBM;

static int fails = 0;
static int checks = 0;

static void Check(bool ok, const std::string& what)
{
  ++checks;
  if (!ok)
  {
    ++fails;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

// what a new session of a build is given for a key (DVPathRecall(): its own, else the prior of its GPU)
static bool Given(const std::string& all, uint64_t key, uint64_t gpu, std::string& plain, std::string& layered)
{
  if (CDVPathMemory::Get(all, key, plain, layered))
    return true;
#ifdef OLD_RULE
  (void)gpu;
  return false;
#else
  return CDVPathMemory::Prior(all, gpu, plain, layered);
#endif
}

int main()
{
  // ---- the GPU of a GL_RENDERER, for any build
  const struct
  {
    const char* renderer;
    const char* gpu;
  } gpus[] = {
      {"AMD Radeon Graphics (radeonsi, raphael_mendocino, LLVM 21.1.8, DRM 3.64, 7.3.5)",
       "AMD Radeon Graphics (radeonsi, raphael_mendocino)"},
      {"AMD Radeon Graphics (radeonsi, raphael_mendocino, LLVM 22.1.0, DRM 3.66, 7.4.0-rc4)",
       "AMD Radeon Graphics (radeonsi, raphael_mendocino)"},
      {"AMD Radeon RX 550 / 550 Series (radeonsi, polaris12, ACO, DRM 3.64, 7.3.5)",
       "AMD Radeon RX 550 / 550 Series (radeonsi, polaris12, ACO)"},
      {"Mesa Intel(R) Graphics (ADL GT2)", "Mesa Intel(R) Graphics (ADL GT2)"},
      {"Mesa Intel(R) Arc(TM) B570 Graphics (BMG G21)", "Mesa Intel(R) Arc(TM) B570 Graphics (BMG G21)"},
      {"NVIDIA GeForce RTX 4080 SUPER/PCIe/SSE2", "NVIDIA GeForce RTX 4080 SUPER/PCIe/SSE2"},
      {"llvmpipe (LLVM 21.1.8, 256 bits)", "llvmpipe (256 bits)"},
      {"zink Vulkan 1.4(llvmpipe (LLVM 21.1.8, 256 bits) (MESA_LLVMPIPE))",
       "zink Vulkan 1.4(llvmpipe (LLVM 21.1.8, 256 bits) (MESA_LLVMPIPE))"},
      {"", ""},
      {"odd (", "odd ("},
  };
  for (const auto& g : gpus)
    Check(DVPathGpu(g.renderer) == g.gpu,
          std::string("GPU of '") + g.renderer + "': '" + DVPathGpu(g.renderer) + "', '" + g.gpu + "' expected");
  Check(DVPathGpu("AMD Radeon Graphics (radeonsi, raphael_mendocino, LLVM 21.1.8, DRM 3.64, 7.3.5)") !=
            DVPathGpu("AMD Radeon Graphics (radeonsi, phoenix, LLVM 21.1.8, DRM 3.64, 7.3.5)"),
        "two chips are two GPUs");

  // ---- the memory
  const uint64_t gpuA = DVPathKey("gpu\n" + DVPathGpu("AMD Radeon Graphics (radeonsi, raphael_mendocino, LLVM 21.1.8, "
                                                        "DRM 3.64, 7.3.5)"));
  const uint64_t gpuB = DVPathKey("gpu\nNVIDIA GeForce RTX 4080 SUPER/PCIe/SSE2");
  const uint64_t a11 = DVPathKey("AMD ... 1.1"), a12 = DVPathKey("AMD ... 1.2"), a13 = DVPathKey("AMD ... next");
  const uint64_t b11 = DVPathKey("NVIDIA ... 1.1");
  // the AMD GPU found composed for FEL streams (30 ms) on one build; the NVIDIA one direct on another
  std::string all = CDVPathMemory::Put("", a11, gpuA, "", "s0,3000");
  all = CDVPathMemory::Put(all, b11, gpuB, "s1,800", "s1,900");
  std::string plain, layered;
  Check(CDVPathMemory::Get(all, a11, plain, layered) && plain.empty() && layered == "s0,3000",
        "the key of a build gives what it put: '" + all + "'");
  Check(!CDVPathMemory::Get(all, a12, plain, layered), "the key of another build has nothing of its own");
  // a new build of the AMD PC
  Check(Given(all, a12, gpuA, plain, layered) && layered == "s0,3000",
        "a new build of the same GPU is given what the last one found");
  Check(!Given(all, DVPathKey("other"), DVPathKey("gpu\nanother GPU"), plain, layered),
        "another GPU is given nothing");
  {
    // ... and draws its first FEL frames composed (the default would be direct, the slower way there)
    std::string p, l;
    CDVPathChoice fresh;
    const bool given = Given(all, a12, gpuA, p, l) && fresh.Recall(l);
    Check(given && !fresh.Direct(), "the first frame of the new build is composed");
    CDVPathChoice none;
    Check(none.Direct(), "with nothing given the first frame would be drawn directly (the default)");
  }
  // what the new build finds replaces the old build's (the best prior for the next one), and keeps the other GPU
  all = CDVPathMemory::Put(all, a12, gpuA, "", "s0,2900");
  Check(!CDVPathMemory::Get(all, a11, plain, layered), "the old build of the same GPU is replaced: '" + all + "'");
  Check(CDVPathMemory::Get(all, b11, plain, layered) && plain == "s1,800", "the other GPU is kept");
  Check(Given(all, a13, gpuA, plain, layered) && layered == "s0,2900", "the next build is given the newest");

  // ---- entries of Hades up to 1.1 ("<key>=..."): read by their key, no prior for another key, kept beside new ones
  const std::string old = "0123456789abcdef=s1,900/s0,3100";
  Check(CDVPathMemory::Get(old, 0x0123456789abcdefULL, plain, layered) && layered == "s0,3100",
        "an entry of 1.1 is read by its key");
  Check(!CDVPathMemory::Prior(old, gpuA, plain, layered), "an entry of 1.1 is no prior");
  {
    const std::string both = CDVPathMemory::Put(old, a12, gpuA, "", "s0,2900");
    Check(both == CDVPathMemory::Put("", a12, gpuA, "", "s0,2900") + " " + old, "an entry of 1.1 is kept: '" + both + "'");
  }
  // ---- at most four entries, the last put first; what is not well formed is dropped
  {
    std::string m;
    for (int n = 0; n < 6; ++n)
      m = CDVPathMemory::Put(m, DVPathKey("k" + std::to_string(n)), DVPathKey("g" + std::to_string(n)), "s1,100", "");
    int entries = 1;
    for (char c : m)
      entries += c == ' ';
    Check(entries == 4, "four entries at most: " + std::to_string(entries));
    Check(CDVPathMemory::Get(m, DVPathKey("k5"), plain, layered) && !CDVPathMemory::Get(m, DVPathKey("k0"), plain, layered),
          "the newest kept, the oldest dropped");
    const std::string bad = "0123456789abcdef.0123=s1,1/ zz=1/2 0123456789abcdef.0123456789abcdeX=s1,1/ " + m;
    Check(CDVPathMemory::Put(bad, DVPathKey("k9"), DVPathKey("g9"), "s1,1", "").find("zz") == std::string::npos,
          "what is not well formed is dropped");
    Check(!CDVPathMemory::Prior("0123456789abcdef.0123=s1,1/", gpuA, plain, layered), "a short gpu is not read");
  }

  std::printf("DVPATHPRIOR: %d checks, %d failed\n", checks, fails);
  if (fails)
    return 1;
  std::printf("DVPATHPRIOR-TEST PASSED\n");
  return 0;
}
