// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// fpeyes_gl_test.cpp (1.2.3, the owner's RTX 4080 SUPER: the copy of the processor took 40.45 ms a frame of 1080p24
// frame packing, more than a frame lasts): the frame-packed frame drawn by the GPU into block-linear buffers of NVIDIA
// (OwnScanout.cpp and drm/DRMStereo.h of the tree under test; nvidia-drm-fb.c of the patch of the tree,
// nvidia-open-kernel-modules-9087), on Mesa's llvmpipe (surfaceless EGL, OpenGL ES 3):
//  - IsNvidiaBlockLinear(): the uncompressed block-linear modifiers of NVIDIA that nvidia-drm offers, nothing else
//  - BlockLinearRightEyeRow(): the first line at or after vtotal that begins a block row and lies a multiple of 1024
//    bytes into the buffer, against a count made here line by line, for every block height that nvidia-drm offers,
//    every pitch of 64 to 16384 bytes and the vtotal of every frame-packed mode of CTA-861 (720p 750, 1080p 1125) and
//    others; and the same line as the kernel's nv_drm_frame_packed_block_linear_row() (knl.h, taken from the patch as
//    it is by run-kodi-host.sh), so that Kodi draws the right eye where nvidia-drm reads it
//  - BlockLinearBufferHeight(): every such line and its eye fit in the buffer
//  - EyesBlit(): a frame drawn as Kodi draws a frame-packed one (1080p24: 2048x2205, the right eye from line 1125;
//    720p: 1280x1470 from line 750), each line of its own colour, put by the GPU into a buffer of
//    BlockLinearBufferHeight() lines: the left eye in its lines 0 to 1079, the right eye from the block row, every
//    pixel of every line of both eyes the same bytes as the frame; the lines between them untouched
//  - EyesProbe(): passes on that (it fails on the mutants of EyesBlit() in mutants.py); a plain flip of the whole
//    frame (FlipBlit(), as for a linear buffer) does not put the right eye on the block row
//  - CopyRowsFlipped() with streaming stores: the same bytes as the copy of 1.2.3 before (a pixel at a time, red and
//    blue exchanged in a row of its own: kept here as it was), BGRA and RGBA rows, widths of 1 to 37 pixels and of the
//    frames, pitches with padding, targets aligned and not; the padding untouched. And its cost against that copy on
//    this host (printed, not judged)
// Prints a FAIL line for each failed check and "<n> failed" last.
#include "windowing/gbm/OwnScanout.h"
#include "windowing/gbm/StereoGap.h"
#include "windowing/gbm/drm/DRMStereo.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// The kernel's rule, as the patch of the tree has it (run-kodi-host.sh writes knl.h)
#include "knl.h"

using namespace KODI::WINDOWING::GBM;
namespace S = KODI::WINDOWING::GBM::STEREO;

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what, const std::string& detail = {})
{
  ++checks;
  if (!ok)
  {
    ++failures;
    std::printf("FAIL: %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
  }
}

S::Mode FramePacked(int hdisplay, int vdisplay, int vtotal)
{
  S::Mode m;
  m.clock = 74250;
  m.hdisplay = static_cast<uint16_t>(hdisplay);
  m.vdisplay = static_cast<uint16_t>(vdisplay);
  m.vtotal = static_cast<uint16_t>(vtotal);
  m.htotal = static_cast<uint16_t>(hdisplay + 300);
  m.flags = S::FLAG_3D_FRAME_PACKING;
  return m;
}

// DRM_FORMAT_MOD_NVIDIA_BLOCK_LINEAR_2D(c, s, g, k, h)
uint64_t Bl(uint64_t c, uint64_t s, uint64_t g, uint64_t k, uint64_t h)
{
  return (uint64_t{0x03} << 56) | 0x10 | (h & 0xf) | ((k & 0xff) << 12) | ((g & 0x3) << 20) | ((s & 0x1) << 22) |
         ((c & 0x7) << 23);
}

// The line counted here: the first one at or after vtotal that is a multiple of the block height and whose offset is a
// multiple of 1024
uint32_t Counted(uint32_t vtotal, uint32_t log2, uint32_t pitch)
{
  const uint32_t block = 8u << log2;
  for (uint32_t row = vtotal; row < vtotal + 64 * block; ++row)
    if (row % block == 0 && (static_cast<uint64_t>(pitch) * row) % 1024 == 0)
      return row;
  return 0;
}

void Modifiers()
{
  for (uint64_t h = 0; h <= 5; ++h)
    check(S::IsNvidiaBlockLinear(Bl(0, 1, 2, 0xfe, h)) && S::IsNvidiaBlockLinear(Bl(0, 1, 0, 0, h)),
          "IsNvidiaBlockLinear(): an uncompressed block-linear modifier, block of " + std::to_string(1 << h) + " GOBs");
  check(!S::IsNvidiaBlockLinear(Bl(0, 1, 2, 0xfe, 6)), "IsNvidiaBlockLinear(): not a block of 64 GOBs");
  check(!S::IsNvidiaBlockLinear(Bl(1, 1, 2, 0xfe, 4)) && !S::IsNvidiaBlockLinear(Bl(7, 1, 2, 0xfe, 4)),
        "IsNvidiaBlockLinear(): not a compressed one");
  check(!S::IsNvidiaBlockLinear(DRM_FORMAT_MOD_LINEAR) && !S::IsNvidiaBlockLinear(DRM_FORMAT_MOD_INVALID),
        "IsNvidiaBlockLinear(): not LINEAR, not INVALID");
  check(!S::IsNvidiaBlockLinear((uint64_t{0x02} << 56) | 0x14), "IsNvidiaBlockLinear(): not another vendor's");
  check(!S::IsNvidiaBlockLinear(uint64_t{0x03} << 56 | 0x04), "IsNvidiaBlockLinear(): not NVIDIA's without bit 4");
  // (the kernel's, from the patch)
  bool same = true;
  for (uint64_t c = 0; c < 8; ++c)
    for (uint64_t h = 0; h < 16; ++h)
    {
      const uint64_t mod = Bl(c, 1, 2, 0xfe, h);
      same = same && S::IsNvidiaBlockLinear(mod) == nv_drm_modifier_is_plain_block_linear(mod);
    }
  same = same && !nv_drm_modifier_is_plain_block_linear(DRM_FORMAT_MOD_LINEAR) &&
         !nv_drm_modifier_is_plain_block_linear((uint64_t{0x02} << 56) | 0x14);
  check(same, "nv_drm_modifier_is_plain_block_linear() of the patch says the same of every compression and height");
}

void Rows()
{
  const S::Mode p1080 = FramePacked(1920, 1080, 1125);
  const S::Mode p720 = FramePacked(1280, 720, 750);
  check(S::BlockLinearRightEyeRow(p1080, Bl(0, 1, 2, 0xfe, 4), 8192, 1024) == 1152,
        "BlockLinearRightEyeRow(): 1080p24, blocks of 16 GOBs, pitch 8192: line 1152");
  check(S::BlockLinearRightEyeRow(p1080, Bl(0, 1, 2, 0xfe, 0), 8192, 1024) == 1128,
        "BlockLinearRightEyeRow(): 1080p24, blocks of one GOB, pitch 8192: line 1128");
  check(S::BlockLinearRightEyeRow(p1080, Bl(0, 1, 2, 0xfe, 5), 7680, 1024) == 1280,
        "BlockLinearRightEyeRow(): 1080p24, blocks of 32 GOBs, pitch 7680: line 1280");
  check(S::BlockLinearRightEyeRow(p720, Bl(0, 1, 2, 0xfe, 4), 5120, 1024) == 768,
        "BlockLinearRightEyeRow(): 720p, blocks of 16 GOBs, pitch 5120: line 768");
  // (a pitch of an odd number of GOBs: the first block row of one GOB, line 1128, is 1128 x 64 x 3 = 216576 bytes in,
  // no multiple of 1024; the next one, 1136, is)
  check(S::BlockLinearRightEyeRow(p1080, Bl(0, 1, 2, 0xfe, 0), 192, 1024) == 1136,
        "BlockLinearRightEyeRow(): a pitch of 3 GOBs, blocks of one GOB: the block row after (1136)");
  check(S::BlockLinearRightEyeRow(p1080, DRM_FORMAT_MOD_LINEAR, 8192, 1024) == 0 &&
            S::BlockLinearRightEyeRow(p1080, Bl(1, 1, 2, 0xfe, 4), 8192, 1024) == 0,
        "BlockLinearRightEyeRow(): nothing for LINEAR or a compressed modifier");
  check(S::BlockLinearRightEyeRow(p1080, Bl(0, 1, 2, 0xfe, 4), 8100, 1024) == 0 &&
            S::BlockLinearRightEyeRow(p1080, Bl(0, 1, 2, 0xfe, 4), 0, 1024) == 0,
        "BlockLinearRightEyeRow(): nothing for a pitch that is no number of GOBs");
  check(S::BlockLinearRightEyeRow(FramePacked(1920, 1080, 1080), Bl(0, 1, 2, 0xfe, 4), 8192, 1024) == 0,
        "BlockLinearRightEyeRow(): nothing for a mode with no lines between the eyes");
  check(S::BlockLinearBufferHeight(p1080) == 2360 && S::BlockLinearBufferHeight(p720) == 1488,
        "BlockLinearBufferHeight(): 1080p24 2360 lines, 720p 1488");

  // Every block height, every pitch of 64 to 16384 bytes, and these vtotals: counted here, Kodi's and the kernel's
  int differ = 0, kernelDiffer = 0, unfit = 0, none = 0;
  std::string first;
  for (int vtotal : {750, 1125, 1250, 1100, 1153, 2250, 1279, 1281})
  {
    const int vdisplay = vtotal == 750 ? 720 : vtotal == 2250 ? 2160 : 1080;
    const S::Mode mode = FramePacked(1920, vdisplay, vtotal);
    for (uint32_t h = 0; h <= 5; ++h)
      for (uint32_t pitch = 64; pitch <= 16384; pitch += 64)
      {
        const uint32_t counted = Counted(static_cast<uint32_t>(vtotal), h, pitch);
        const uint32_t kodi = S::BlockLinearRightEyeRow(mode, Bl(0, 1, 2, 0xfe, h), pitch, 1024);
        const uint32_t kernel = nv_drm_frame_packed_block_linear_row(Bl(0, 1, 2, 0xfe, h), pitch,
                                                                     static_cast<uint32_t>(vtotal));
        none += counted == 0;
        if (kodi != counted && differ++ == 0)
          first = "vtotal " + std::to_string(vtotal) + " h " + std::to_string(h) + " pitch " + std::to_string(pitch) +
                  ": " + std::to_string(kodi) + " for " + std::to_string(counted);
        if (kernel != kodi && kernelDiffer++ == 0)
          first += "; kernel " + std::to_string(kernel) + " for Kodi's " + std::to_string(kodi) + " (vtotal " +
                   std::to_string(vtotal) + " h " + std::to_string(h) + " pitch " + std::to_string(pitch) + ")";
        unfit += kodi != 0 && static_cast<int>(kodi) + vdisplay > S::BlockLinearBufferHeight(mode);
      }
  }
  check(none == 0, "a line was counted for every height and pitch", std::to_string(none));
  check(differ == 0, "BlockLinearRightEyeRow(): the line counted, for 6 heights x 256 pitches x 8 vtotals",
        std::to_string(differ) + " differ, " + first);
  check(kernelDiffer == 0, "nv_drm_frame_packed_block_linear_row() of the patch: the same line as Kodi's, everywhere",
        std::to_string(kernelDiffer) + " differ, " + first);
  check(unfit == 0, "BlockLinearBufferHeight(): the right eye fits from every such line", std::to_string(unfit));
}

// A renderbuffer of RGBA8 and its framebuffer object (row m of it is the line m of a buffer in memory, as an imported
// buffer is: OwnScanout.h)
GLuint Target(int width, int height)
{
  GLuint rb = 0, fb = 0;
  glGenRenderbuffers(1, &rb);
  glBindRenderbuffer(GL_RENDERBUFFER, rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
  check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "a framebuffer of RGBA8");
  return fb;
}

// The colour of line t of the frame, from its top (a different one for each line of either eye)
void LineColour(int t, uint8_t out[3])
{
  out[0] = static_cast<uint8_t>(t & 0xff);
  out[1] = static_cast<uint8_t>(37 * (t >> 8) + 11);
  out[2] = static_cast<uint8_t>((t * 7) & 0xff);
}

// The frame as Kodi draws a frame-packed one: each eye under scissors (here a line at a time, each of its own colour),
// the lines between the eyes cleared by STEREO_GAP::Clear()
void DrawFrame(GLuint frame, int width, int height, int eye, int gapLines)
{
  glBindFramebuffer(GL_FRAMEBUFFER, frame);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  for (int t = 0; t < height; ++t)
  {
    if (t >= eye && t < eye + gapLines)
      continue;
    uint8_t c[3];
    LineColour(t, c);
    glScissor(0, height - 1 - t, width, 1);
    glClearColor(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glDisable(GL_SCISSOR_TEST);
  const auto gap = STEREO_GAP::GapOf(true, false, width, eye, gapLines, width, height);
  check(gap.has_value(), "GapOf(): the lines between the eyes");
  if (gap)
    STEREO_GAP::Clear(*gap);
}

void Eyes(int width, int eye, int vtotal, uint32_t log2)
{
  const int height = eye + vtotal;
  const S::Mode mode = FramePacked(width == 2048 ? 1920 : width, eye, vtotal);
  const uint32_t pitch = static_cast<uint32_t>(width) * 4;
  const int right = static_cast<int>(S::BlockLinearRightEyeRow(mode, Bl(0, 1, 2, 0xfe, log2), pitch, 1024));
  const int lines = S::BlockLinearBufferHeight(mode);
  const std::string name = std::to_string(width) + "x" + std::to_string(height) + ", blocks of " +
                           std::to_string(1 << log2) + " GOBs: right eye from line " + std::to_string(right);
  const COwnScanout::EyeRows eyes{eye, vtotal, right};
  const GLuint frame = Target(width, height);
  const GLuint buffer = Target(width, lines);

  std::string failed;
  check(COwnScanout::EyesProbe(frame, buffer, width, height, eyes, failed), "EyesProbe() passes (" + name + ")",
        failed);

  // The lines between the eyes in the buffer, marked: EyesBlit() leaves them as they are
  glBindFramebuffer(GL_FRAMEBUFFER, buffer);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(77 / 255.0f, 66 / 255.0f, 55 / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFrame(frame, width, height, eye, vtotal - eye);
  COwnScanout::EyesBlit(frame, buffer, width, height, eyes);
  check(glGetError() == GL_NO_ERROR, "EyesBlit(): no GL error (" + name + ")");

  std::vector<uint8_t> want(static_cast<size_t>(width) * 4), got(want.size());
  int wrongLeft = 0, wrongRight = 0, touched = 0, firstWrong = -1;
  for (int m = 0; m < lines; ++m)
  {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, buffer);
    glReadPixels(0, m, width, 1, GL_RGBA, GL_UNSIGNED_BYTE, got.data());
    int t = -1;
    if (m < eye)
      t = m;
    else if (m >= right && m < right + eye)
      t = vtotal + (m - right);
    if (t < 0)
    {
      // (a line that the display engine never reads: as it was)
      for (int x = 0; x < width; ++x)
        if (got[x * 4] != 77 || got[x * 4 + 1] != 66 || got[x * 4 + 2] != 55)
        {
          ++touched;
          break;
        }
      continue;
    }
    // (line t of the frame, from its top: GL's row height - 1 - t)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, frame);
    glReadPixels(0, height - 1 - t, width, 1, GL_RGBA, GL_UNSIGNED_BYTE, want.data());
    uint8_t c[3];
    LineColour(t, c);
    const bool same = want == got && want[0] == c[0] && want[1] == c[1] && want[2] == c[2];
    if (!same)
    {
      ++(m < eye ? wrongLeft : wrongRight);
      if (firstWrong < 0)
        firstWrong = m;
    }
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  check(wrongLeft == 0, "EyesBlit(): the left eye in lines 0-" + std::to_string(eye - 1) + ", every pixel (" + name + ")",
        std::to_string(wrongLeft) + " lines wrong, the first " + std::to_string(firstWrong));
  check(wrongRight == 0,
        "EyesBlit(): the right eye (lines " + std::to_string(vtotal) + "- of the frame) in lines " +
            std::to_string(right) + "-" + std::to_string(right + eye - 1) + ", every pixel (" + name + ")",
        std::to_string(wrongRight) + " lines wrong, the first " + std::to_string(firstWrong));
  check(touched == 0, "EyesBlit(): the lines between the eyes and past the right eye untouched (" + name + ")",
        std::to_string(touched));

  // (EyesProbe() checks EyesBlit() as it is: mutants of EyesBlit() are caught by it, mutants.py. A plain flip of the
  // whole frame, as for a linear buffer, does not put the right eye on the block row)
  glBindFramebuffer(GL_FRAMEBUFFER, buffer);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  DrawFrame(frame, width, height, eye, vtotal - eye);
  COwnScanout::FlipBlit(frame, buffer, width, height);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, buffer);
  uint8_t px[4]{};
  glReadPixels(0, right, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  uint8_t c[3];
  LineColour(vtotal, c);
  check(right == vtotal || px[0] != c[0] || px[1] != c[1] || px[2] != c[2],
        "a plain flip does not put the right eye on the block row (" + name + ")");
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// The copy of 1.2.3 before streaming stores, as it was (OwnScanout.cpp of 4eabe75)
void CopyBefore(const uint8_t* rows, int width, int height, bool rowsBgra, bool targetBgra, uint8_t* target,
                uint32_t pitch)
{
  const size_t rowBytes = static_cast<size_t>(width) * 4;
  const bool swap = targetBgra != rowsBgra;
  std::vector<uint8_t> row(swap ? rowBytes : 0);
  for (int r = 0; r < height; ++r)
  {
    const uint8_t* from = rows + static_cast<size_t>(r) * rowBytes;
    uint8_t* to = target + static_cast<size_t>(height - 1 - r) * pitch;
    if (!swap)
    {
      std::memcpy(to, from, rowBytes);
      continue;
    }
    for (size_t i = 0; i < rowBytes; i += 4)
    {
      row[i] = from[i + 2];
      row[i + 1] = from[i + 1];
      row[i + 2] = from[i];
      row[i + 3] = from[i + 3];
    }
    std::memcpy(to, row.data(), rowBytes);
  }
}

void Copy()
{
  std::mt19937 random(1234);
  int differ = 0, cases = 0;
  std::string first;
  struct Size
  {
    int w, h;
  };
  std::vector<Size> sizes;
  for (int w = 1; w <= 37; ++w)
    sizes.push_back({w, 3});
  sizes.push_back({2048, 9});
  sizes.push_back({1280, 7});
  sizes.push_back({1920, 5});
  for (const Size& s : sizes)
    for (uint32_t extra : {0u, 4u, 60u, 256u})
      for (size_t misalign : {size_t{0}, size_t{4}, size_t{8}})
        for (bool rowsBgra : {true, false})
          for (uint32_t format : {uint32_t{DRM_FORMAT_ARGB8888}, uint32_t{DRM_FORMAT_XBGR8888}})
          {
            const uint32_t pitch = static_cast<uint32_t>(s.w) * 4 + extra;
            std::vector<uint8_t> rows(static_cast<size_t>(s.w) * s.h * 4);
            for (auto& b : rows)
              b = static_cast<uint8_t>(random());
            std::vector<uint8_t> a(misalign + static_cast<size_t>(pitch) * s.h + 16, 0xc3), b = a;
            CopyBefore(rows.data(), s.w, s.h, rowsBgra, *COwnScanout::MemoryIsBgra(format), a.data() + misalign, pitch);
            const bool ok =
                COwnScanout::CopyRowsFlipped(rows.data(), s.w, s.h, rowsBgra, format, b.data() + misalign, pitch);
            ++cases;
            if ((!ok || a != b) && differ++ == 0)
              first = std::to_string(s.w) + "x" + std::to_string(s.h) + " pitch " + std::to_string(pitch) +
                      " at +" + std::to_string(misalign) + (rowsBgra ? " BGRA" : " RGBA") + " into " +
                      (format == DRM_FORMAT_ARGB8888 ? "AR24" : "XB24");
          }
  check(differ == 0,
        "CopyRowsFlipped(): the same bytes as the copy before, padding untouched (" + std::to_string(cases) + " cases)",
        std::to_string(differ) + " differ, the first " + first);
}

void Bench()
{
  for (const auto& [w, h, name] : {std::tuple{2048, 2205, "1080p frame packing"}, {1280, 1470, "720p frame packing"}})
  {
    std::vector<uint8_t> rows(static_cast<size_t>(w) * h * 4, 0x5a);
    const uint32_t pitch = static_cast<uint32_t>((w * 4 + 255) / 256 * 256);
    std::vector<uint8_t> target(static_cast<size_t>(pitch) * h + 64);
    uint8_t* at = target.data() + ((16 - (reinterpret_cast<uintptr_t>(target.data()) & 15)) & 15);
    for (bool bgra : {true, false})
    {
      constexpr int N = 30;
      double before = 0.0, now = 0.0;
      for (int i = 0; i < N + 2; ++i)
      {
        const auto a = std::chrono::steady_clock::now();
        CopyBefore(rows.data(), w, h, bgra, true, at, pitch);
        const auto b = std::chrono::steady_clock::now();
        COwnScanout::CopyRowsFlipped(rows.data(), w, h, bgra, DRM_FORMAT_ARGB8888, at, pitch);
        const auto c = std::chrono::steady_clock::now();
        if (i >= 2)
        {
          before += std::chrono::duration<double, std::milli>(b - a).count();
          now += std::chrono::duration<double, std::milli>(c - b).count();
        }
      }
      std::printf("BENCH %s %dx%d, rows read as %s: before %.2f ms, streaming stores %.2f ms a frame (memory of the "
                  "host, not of a card)\n",
                  name, w, h, bgra ? "BGRA" : "RGBA", before / N, now / N);
    }
  }
}
} // namespace

int main()
{
  Modifiers();
  Rows();
  Copy();
  auto getDisplay =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  EGLDisplay display =
      getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) : EGL_NO_DISPLAY;
  EGLint major = 0, minor = 0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
  {
    std::printf("FAIL: no EGL display without a window (Mesa's surfaceless platform)\n%d failed\n", failures + 1);
    return 1;
  }
  const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RED_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  eglChooseConfig(display, configAttribs, &config, 1, &count);
  const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext context = count ? eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs) : EGL_NO_CONTEXT;
  if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
  {
    std::printf("FAIL: no OpenGL ES 3 context\n%d failed\n", failures + 1);
    return 1;
  }
  std::printf("GL_RENDERER %s, GL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  Eyes(2048, 1080, 1125, 4);
  Eyes(2048, 1080, 1125, 0);
  Eyes(1280, 720, 750, 4);
  Bench();
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
