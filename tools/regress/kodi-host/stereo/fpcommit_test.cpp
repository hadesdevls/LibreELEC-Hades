// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// fpcommit_test.cpp (issue #33, 1.2.3), of the tree under test:
//  - FallbackFrame.h: the request that goes out in place of a refused one carries the frame only where its plane can
//    show the frame's buffer, as the kernel checks it (drm_framebuffer_check_src_coords(), copied here as it is in the
//    kernel of the build, 7.4); Khazar321's case (a frame of 2048x2205 in the plane of a 2D mode of 3840x2160:
//    "No space left on device") is not committed, the frame of the same mode is; what is not known goes out as before;
//    a refused change to a frame-packed mode is a warning, any other refusal an error
//  - FramePackingCheck.h: which frame-packed mode the card "3D: frame packing" asks (1080p at 24000/1001 first), what
//    it says first of the two kinds of buffers, the names of the answers and of the mode
// Prints a FAIL line for each failed check and "<n> failed" last.
#include "windowing/gbm/FramePackingCheck.h"
#include "windowing/gbm/drm/FallbackFrame.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace KODI::WINDOWING::GBM;
namespace S = KODI::WINDOWING::GBM::STEREO;
namespace F = KODI::WINDOWING::GBM::FALLBACK_FRAME;
namespace C = KODI::WINDOWING::GBM::FP_CHECK;

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what)
{
  ++checks;
  if (!ok)
  {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

// drivers/gpu/drm/drm_framebuffer.c of the kernel of the build: 0, or -ENOSPC
int KernelCheck(uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h, unsigned int width,
                unsigned int height)
{
  unsigned int fb_width = width << 16;
  unsigned int fb_height = height << 16;
  if (src_w > fb_width || src_x > fb_width - src_w || src_h > fb_height || src_y > fb_height - src_h)
    return -28;
  return 0;
}

uint64_t Fx(uint32_t v)
{
  return static_cast<uint64_t>(v) << 16;
}

void Fallback()
{
  // Khazar321 (RTX 5090, 1.2.2): the request before was the 2D mode of 3840x2160, the frame's buffer 2048x2205
  check(!F::Holds({0, 0, Fx(3840), Fx(2160)}, 2048, 2205),
        "a frame of 2048x2205 in the plane of a mode of 3840x2160: refused by the kernel, not committed");
  check(!F::GoesOut(F::Source{0, 0, Fx(3840), Fx(2160)}, std::make_pair(2048u, 2205u)),
        "GoesOut(): Khazar321's fallback does not go out");
  check(F::Holds({0, 0, Fx(1920), Fx(2205)}, 2048, 2205), "the frame of the frame-packed mode in its own plane: shown");
  check(F::Holds({0, 0, Fx(3840), Fx(2160)}, 3840, 2160), "the frame of the same mode: shown, as always");
  check(F::Holds({0, 0, Fx(1920), Fx(1080)}, 3840, 2160),
        "a smaller plane over a bigger buffer: what the kernel takes, which goes out as before");
  check(F::GoesOut(std::nullopt, std::make_pair(2048u, 2205u)) && F::GoesOut(F::Source{0, 0, Fx(3840), Fx(2160)}, std::nullopt),
        "GoesOut(): what is not known goes out, as before");
  // every case as the kernel judges it (the sizes and offsets that matter: equal, one line or column over, an
  // offset, a fraction of a pixel)
  const uint32_t sizes[] = {1, 719, 720, 1080, 1919, 1920, 2048, 2160, 2205, 3840};
  int differ = 0, cases = 0;
  for (uint32_t fw : sizes)
    for (uint32_t fh : sizes)
      for (uint32_t sw : sizes)
        for (uint32_t sh : {1u, 1080u, 2160u, 2205u})
          for (uint32_t off : {0u, 1u, 0x8000u, 1u << 16})
          {
            const uint32_t srcW = sw << 16, srcH = sh << 16;
            const bool kernel = KernelCheck(off, off, srcW, srcH, fw, fh) == 0;
            const bool ours = F::Holds({off, off, srcW, srcH}, fw, fh);
            differ += kernel != ours;
            ++cases;
          }
  check(differ == 0, "Holds() as the kernel judges " + std::to_string(cases) + " cases: " + std::to_string(differ) +
                         " differ");
  check(F::FramePackingRefusal(true, true), "a refused change to a frame-packed mode: a warning");
  check(!F::FramePackingRefusal(false, true) && !F::FramePackingRefusal(true, false) &&
            !F::FramePackingRefusal(false, false),
        "a refused frame in a frame-packed mode, or any other change of mode: an error, as before");
}

S::Mode Mode(uint16_t w, uint16_t h, uint32_t clock, uint16_t htotal, uint16_t vtotal, uint32_t flags)
{
  S::Mode m;
  m.clock = clock;
  m.hdisplay = w;
  m.htotal = htotal;
  m.vdisplay = h;
  m.vtotal = vtotal;
  m.flags = flags;
  return m;
}

void Check()
{
  const uint32_t FP = S::FLAG_3D_FRAME_PACKING;
  const S::Mode p24 = Mode(1920, 1080, 74250, 2750, 1125, FP);
  const S::Mode p23 = Mode(1920, 1080, 74176, 2750, 1125, FP);
  const S::Mode p720 = Mode(1280, 720, 74250, 1650, 750, FP);
  const S::Mode flat = Mode(1920, 1080, 74176, 2750, 1125, 0);
  const S::Mode tab = Mode(1920, 1080, 74176, 2750, 1125, S::FLAG_3D_TOP_AND_BOTTOM);
  check(C::ChooseMode(std::vector<S::Mode>{p720, p24, p23}) == 2, "ChooseMode(): 1080p at 24000/1001 first");
  check(C::ChooseMode(std::vector<S::Mode>{p720, p24}) == 1, "ChooseMode(): then 1080p at 24");
  check(C::ChooseMode(std::vector<S::Mode>{p720}) == 0, "ChooseMode(): then any frame-packed mode");
  check(C::ChooseMode(std::vector<S::Mode>{flat, tab}) == -1 && C::ChooseMode(std::vector<S::Mode>{}) == -1,
        "ChooseMode(): none of a 2D mode or another 3D layout");
  check(C::ChooseMode(std::vector<S::Mode>{flat, tab, p23}) == 2, "ChooseMode(): the frame-packed one among others");
  check(C::ModeName(p23) == "1920x1080@23.976" && C::ModeName(p720) == "1280x720@60.000",
        "ModeName(): " + C::ModeName(p23) + ", " + C::ModeName(p720));
  using A = C::Answer;
  check(std::string(C::Overall(A::NOT_MADE, A::TAKEN)) == "taken" &&
            std::string(C::Overall(A::TAKEN, A::NONE)) == "taken" &&
            std::string(C::Overall(A::REFUSED, A::TAKEN)) == "taken",
        "Overall(): taken where either kind was taken");
  check(std::string(C::Overall(A::NOT_MADE, A::REFUSED)) == "refused" &&
            std::string(C::Overall(A::REFUSED, A::NONE)) == "refused",
        "Overall(): refused where one was refused and none taken");
  check(std::string(C::Overall(A::NOT_MADE, A::NOT_MADE)) == "notasked" &&
            std::string(C::Overall(A::NOT_ASKED, A::NONE)) == "notasked",
        "Overall(): not asked where nothing could be");
  check(std::string(C::Name(A::TAKEN)) == "taken" && std::string(C::Name(A::REFUSED)) == "refused" &&
            std::string(C::Name(A::NOT_MADE)) == "notmade" && std::string(C::Name(A::NOT_ASKED)) == "notasked" &&
            std::string(C::Name(A::NONE)) == "none",
        "Name(): the words that the card of LibreELEC-settings reads");
  check(std::string(C::PROPERTY) == "hades.check.fp.result", "the property the card reads");
}
} // namespace

int main()
{
  Fallback();
  Check();
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
