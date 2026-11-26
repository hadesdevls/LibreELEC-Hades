// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// fpown_gl_test.cpp (issue #33): the frame of an HDMI 3D frame-packed mode of nvidia-drm in buffers that Kodi
// allocates itself, where the driver refuses the window's (FramePackedOwn.h, OwnScanout.cpp and StereoGap.h of the
// tree under test), on Mesa's llvmpipe (surfaceless EGL, OpenGL ES 3):
//  - when Kodi makes its own buffers: only for a frame-packed mode of nvidia-drm, with OpenGL ES, and only where the
//    driver refused the window's buffer in its test (MakeOwn()); never for a 2D mode or another driver
//  - which buffers the frame goes out in, and what the two answers say of the refusal (Decide())
//  - the lines of 1080p24 frame packing (1920x2205: the left eye, a gap of 45 lines, the right eye from line 1125)
//    and of 720p (1280x1470, 30 lines), the width that puts the second eye where NVKMS takes a surface (2048 at 4
//    bytes a pixel for 1080p, DRMStereo.h), and which buffers hold the frame (HoldsFrame())
//  - the checks of Kodi's own buffers of 8 bits (COwnScanout::Probe(), FlipProbe()) on renderbuffers of RGBA8, the
//    10-bit ones as before, and that a probe of 10 bits refuses a buffer of 8
//  - a frame drawn as Kodi draws a frame-packed one (each eye under a scissor of its own, the lines between them
//    left as they were and cleared by STEREO_GAP::Clear()) and flipped by COwnScanout::FlipBlit() into the buffer
//    that goes to the screen: from its first line, the left eye, 45 black lines, the right eye
//  - the driver's dumb buffers wherever GBM's own were not made or not all taken (TryDumb()), and the copy of the
//    processor into them where EGL cannot draw into them (CopyRowsFlipped(): the rows read back, the bottom one
//    first, written top first at the driver's pitch, red and blue where the format lays them): the same frame,
//    read back and copied into a dumb buffer of AR24 with a pitch of 8192 bytes, the same lines
// A renderbuffer holds its row 0 first in memory, as a buffer imported into EGL does (OwnScanout.h): row m of the
// buffer that goes to the screen is the line m that the display shows.
// Prints a FAIL line for each failed check and "<n> failed" last.
#include "windowing/gbm/FramePackedOwn.h"
#include "windowing/gbm/OwnScanout.h"
#include "windowing/gbm/StereoGap.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

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

S::Mode Mode1080p24FramePacked()
{
  S::Mode m;
  m.clock = 74250;
  m.hdisplay = 1920;
  m.hsyncStart = 2558;
  m.hsyncEnd = 2602;
  m.htotal = 2750;
  m.vdisplay = 1080;
  m.vsyncStart = 1084;
  m.vsyncEnd = 1089;
  m.vtotal = 1125;
  m.flags = S::FLAG_3D_FRAME_PACKING;
  return m;
}

S::Mode Mode720p60FramePacked()
{
  S::Mode m;
  m.clock = 74250;
  m.hdisplay = 1280;
  m.hsyncStart = 1390;
  m.hsyncEnd = 1430;
  m.htotal = 1650;
  m.vdisplay = 720;
  m.vsyncStart = 725;
  m.vsyncEnd = 730;
  m.vtotal = 750;
  m.flags = S::FLAG_3D_FRAME_PACKING;
  return m;
}

void Gating()
{
  using FP_OWN::Asked;
  check(FP_OWN::AskedOf(std::nullopt) == Asked::NOT_ASKED, "AskedOf(): no answer is not asked");
  check(FP_OWN::AskedOf(true) == Asked::TAKEN, "AskedOf(): taken");
  check(FP_OWN::AskedOf(false) == Asked::REFUSED, "AskedOf(): refused");
  check(FP_OWN::MakeOwn(true, true, Asked::REFUSED),
        "MakeOwn(): a frame-packed mode of nvidia-drm, OpenGL ES, the window's buffer refused: own buffers");
  check(!FP_OWN::MakeOwn(false, true, Asked::REFUSED),
        "MakeOwn(): not for another mode or driver, whatever was answered");
  check(!FP_OWN::MakeOwn(false, true, Asked::TAKEN), "MakeOwn(): not for another mode or driver (taken)");
  check(!FP_OWN::MakeOwn(false, true, Asked::NOT_ASKED), "MakeOwn(): not for another mode or driver (not asked)");
  check(!FP_OWN::MakeOwn(true, false, Asked::REFUSED), "MakeOwn(): not with desktop OpenGL");
  check(!FP_OWN::MakeOwn(true, true, Asked::TAKEN), "MakeOwn(): not where the window's buffer was taken");
  check(!FP_OWN::MakeOwn(true, true, Asked::NOT_ASKED), "MakeOwn(): not where nothing could be asked");
}

// The driver's dumb buffers after GBM's (TryDumb()), and the copy of the processor into them (CopyRowsFlipped())
void Dumb()
{
  using FP_OWN::Asked;
  check(FP_OWN::TryDumb(false, Asked::NOT_ASKED), "TryDumb(): GBM's could not be made: the driver's dumb buffers");
  check(FP_OWN::TryDumb(true, Asked::REFUSED), "TryDumb(): GBM's refused: the driver's dumb buffers");
  check(FP_OWN::TryDumb(true, Asked::NOT_ASKED), "TryDumb(): GBM's not asked: the driver's dumb buffers");
  check(!FP_OWN::TryDumb(true, Asked::TAKEN), "TryDumb(): GBM's taken: nothing else is made");

  check(COwnScanout::MemoryIsBgra(DRM_FORMAT_ARGB8888) == std::optional<bool>(true) &&
            COwnScanout::MemoryIsBgra(DRM_FORMAT_XRGB8888) == std::optional<bool>(true),
        "MemoryIsBgra(): AR24 and XR24 lie as B, G, R, A");
  check(COwnScanout::MemoryIsBgra(DRM_FORMAT_ABGR8888) == std::optional<bool>(false) &&
            COwnScanout::MemoryIsBgra(DRM_FORMAT_XBGR8888) == std::optional<bool>(false),
        "MemoryIsBgra(): AB24 and XB24 lie as R, G, B, A");
  check(!COwnScanout::MemoryIsBgra(DRM_FORMAT_ABGR2101010) && !COwnScanout::MemoryIsBgra(DRM_FORMAT_ABGR16161616F),
        "MemoryIsBgra(): nothing for a format of more than 8 bits");

  // 3 x 2 pixels read back (the bottom row first), into a buffer whose rows are 16 bytes apart (4 of padding)
  const uint8_t rgba[2][12] = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, {21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}};
  std::vector<uint8_t> rows;
  for (const auto& r : rgba)
    rows.insert(rows.end(), r, r + 12);
  std::vector<uint8_t> buffer(32, 0xee);
  check(COwnScanout::CopyRowsFlipped(rows.data(), 3, 2, false, DRM_FORMAT_ARGB8888, buffer.data(), 16),
        "CopyRowsFlipped(): RGBA rows into AR24");
  // top row of the buffer = the last row read back, red and blue exchanged; the padding untouched
  const uint8_t topAr24[12] = {23, 22, 21, 24, 27, 26, 25, 28, 31, 30, 29, 32};
  const uint8_t bottomAr24[12] = {3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12};
  check(std::equal(topAr24, topAr24 + 12, buffer.begin()) && std::equal(bottomAr24, bottomAr24 + 12, buffer.begin() + 16),
        "CopyRowsFlipped(): the bottom row read back is the bottom row of the buffer, red and blue exchanged for AR24");
  check(buffer[12] == 0xee && buffer[15] == 0xee && buffer[28] == 0xee && buffer[31] == 0xee,
        "CopyRowsFlipped(): each row at the pitch of the buffer, the bytes past the width left alone");

  std::fill(buffer.begin(), buffer.end(), 0xee);
  check(COwnScanout::CopyRowsFlipped(rows.data(), 3, 2, false, DRM_FORMAT_XBGR8888, buffer.data(), 16) &&
            std::equal(rgba[1], rgba[1] + 12, buffer.begin()) && std::equal(rgba[0], rgba[0] + 12, buffer.begin() + 16),
        "CopyRowsFlipped(): RGBA rows into XB24 as they are, flipped");
  std::fill(buffer.begin(), buffer.end(), 0xee);
  check(COwnScanout::CopyRowsFlipped(rows.data(), 3, 2, true, DRM_FORMAT_XRGB8888, buffer.data(), 16) &&
            std::equal(rgba[1], rgba[1] + 12, buffer.begin()) && std::equal(rgba[0], rgba[0] + 12, buffer.begin() + 16),
        "CopyRowsFlipped(): rows read as BGRA into XR24 as they are, flipped");
  check(!COwnScanout::CopyRowsFlipped(rows.data(), 3, 2, false, DRM_FORMAT_ABGR2101010, buffer.data(), 16) &&
            !COwnScanout::CopyRowsFlipped(rows.data(), 3, 2, false, DRM_FORMAT_ARGB8888, buffer.data(), 11),
        "CopyRowsFlipped(): no copy into a format of more than 8 bits, or at a pitch shorter than a row");
}

void Verdicts()
{
  using FP_OWN::Asked;
  using FP_OWN::Cause;
  using FP_OWN::Use;
  struct Case
  {
    Asked window;
    bool made;
    Asked own;
    Use use;
    Cause cause;
    const char* name;
  };
  const Case cases[] = {
      {Asked::TAKEN, true, Asked::TAKEN, Use::WINDOW, Cause::NONE, "window taken"},
      {Asked::TAKEN, false, Asked::NOT_ASKED, Use::WINDOW, Cause::NONE, "window taken, nothing made"},
      {Asked::NOT_ASKED, true, Asked::TAKEN, Use::WINDOW, Cause::NONE, "window not asked"},
      {Asked::REFUSED, true, Asked::TAKEN, Use::OWN, Cause::BUFFER, "window refused, own taken"},
      {Asked::REFUSED, true, Asked::REFUSED, Use::WINDOW, Cause::NOT_BUFFER, "window refused, own refused"},
      {Asked::REFUSED, false, Asked::TAKEN, Use::WINDOW, Cause::NOT_KNOWN, "window refused, own not made"},
      {Asked::REFUSED, false, Asked::NOT_ASKED, Use::WINDOW, Cause::NOT_KNOWN,
       "window refused, own not made or asked"},
      {Asked::REFUSED, true, Asked::NOT_ASKED, Use::WINDOW, Cause::NOT_KNOWN, "window refused, own not asked"},
  };
  for (const Case& c : cases)
  {
    const FP_OWN::Verdict v = FP_OWN::Decide(c.window, c.made, c.own);
    check(v.use == c.use && v.cause == c.cause, std::string("Decide(): ") + c.name,
          "use " + std::to_string(static_cast<int>(v.use)) + ", cause " +
              std::to_string(static_cast<int>(v.cause)));
  }
  check(std::string(FP_OWN::CauseText(Cause::NOT_BUFFER)).find("not where the buffer is") != std::string::npos,
        "CauseText(): a refusal of both buffers says the buffer is not the cause");
}

void Geometry()
{
  const S::Mode fp = Mode1080p24FramePacked();
  const auto rows = FP_OWN::RowsOf(fp);
  check(rows.has_value(), "RowsOf(): 1080p24 frame packing has rows");
  if (rows)
  {
    check(rows->leftFirst == 0 && rows->leftEnd == 1080, "RowsOf(1080p24): the left eye in lines 0-1079");
    check(rows->gapFirst == 1080 && rows->gapEnd == 1125 && rows->gapEnd - rows->gapFirst == 45,
          "RowsOf(1080p24): 45 lines between the eyes, 1080-1124");
    check(rows->rightFirst == 1125 && rows->rightEnd == 2205, "RowsOf(1080p24): the right eye in lines 1125-2204");
  }
  check(S::ScanoutHeight(fp) == 2205, "ScanoutHeight(1080p24 frame packing) is 2205");
  const int width = S::FramePackedBufferWidth(fp, 4, S::NVKMS_SURFACE_OFFSET_ALIGNMENT);
  check(width == 2048, "FramePackedBufferWidth(1080p24, 4 bytes): 2048", std::to_string(width));
  check(S::SecondEyeAligned(2048 * 4, fp, 1024) && !S::SecondEyeAligned(1920 * 4, fp, 1024),
        "the second eye of 1080p: at a multiple of 1024 bytes with a pitch of 8192, not of 7680");
  check(static_cast<uint64_t>(2048 * 4) * 1125 == 9216000ull, "the second eye of 1080p begins at byte 9216000");

  const S::Mode fp720 = Mode720p60FramePacked();
  const auto rows720 = FP_OWN::RowsOf(fp720);
  check(rows720 && rows720->leftEnd == 720 && rows720->gapFirst == 720 && rows720->gapEnd == 750 &&
            rows720->rightFirst == 750 && rows720->rightEnd == 1470,
        "RowsOf(720p): the left eye in 0-719, 30 lines, the right eye in 750-1469");
  check(S::FramePackedBufferWidth(fp720, 4, 1024) == 1280, "FramePackedBufferWidth(720p, 4 bytes): 1280");

  S::Mode flat = fp;
  flat.flags = 0;
  check(!FP_OWN::RowsOf(flat), "RowsOf(): none for a 2D mode");
  S::Mode tab = fp;
  tab.flags = S::FLAG_3D_TOP_AND_BOTTOM;
  check(!FP_OWN::RowsOf(tab), "RowsOf(): none for top and bottom (half)");

  check(FP_OWN::HoldsFrame(fp, 2048, 2205), "HoldsFrame(): 2048x2205 holds 1080p24 frame packing");
  check(FP_OWN::HoldsFrame(fp, 1920, 2205), "HoldsFrame(): 1920x2205 holds it");
  check(!FP_OWN::HoldsFrame(fp, 2048, 2204), "HoldsFrame(): a line short does not");
  check(!FP_OWN::HoldsFrame(fp, 1919, 2205), "HoldsFrame(): a column short does not");
  check(!FP_OWN::HoldsFrame(fp, 1920, 1080), "HoldsFrame(): the size of the 2D mode does not");
  check(!FP_OWN::HoldsFrame(flat, 2048, 2205), "HoldsFrame(): nothing for a 2D mode");
}

// a renderbuffer of this internal format and a framebuffer with it; 0 where GL cannot make it complete
GLuint Target(GLenum format, int width, int height, GLuint& renderbuffer)
{
  GLuint fb = 0;
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE)
  {
    glDeleteFramebuffers(1, &fb);
    glDeleteRenderbuffers(1, &renderbuffer);
    renderbuffer = 0;
    return 0;
  }
  return fb;
}

void Formats()
{
  check(COwnScanout::Bits(DRM_FORMAT_ARGB8888) == 8 && COwnScanout::Bits(DRM_FORMAT_XRGB8888) == 8 &&
            COwnScanout::Bits(DRM_FORMAT_ABGR8888) == 8 && COwnScanout::Bits(DRM_FORMAT_XBGR8888) == 8,
        "Bits(): the formats of 8 bits of a window, for the buffers of a frame-packed mode");
  check(COwnScanout::Supported(DRM_FORMAT_ARGB8888) && !COwnScanout::IsFloat(DRM_FORMAT_ARGB8888),
        "Supported(): AR24, not half floats");
  check(COwnScanout::Bits(DRM_FORMAT_ABGR2101010) == 10 && COwnScanout::Bits(DRM_FORMAT_XRGB2101010) == 10 &&
            COwnScanout::Bits(DRM_FORMAT_ABGR16161616F) == 16 && COwnScanout::IsFloat(DRM_FORMAT_ABGR16161616F),
        "Bits(): 10 bits and half floats as before");
  check(!COwnScanout::Supported(DRM_FORMAT_RGB565) && !COwnScanout::Supported(DRM_FORMAT_NV12),
        "Supported(): no other format");
}

void Probes()
{
  constexpr int W = 2048, H = 2205;
  GLuint rbFrame = 0, rbTarget = 0, rb10 = 0, rb10b = 0, rb565 = 0;
  const GLuint frame = Target(GL_RGBA8, W, H, rbFrame);
  const GLuint target = Target(GL_RGBA8, W, H, rbTarget);
  const GLuint ten = Target(GL_RGB10_A2, 64, 64, rb10);
  const GLuint tenb = Target(GL_RGB10_A2, 64, 64, rb10b);
  const GLuint coarse = Target(GL_RGB565, 64, 64, rb565);
  check(frame && target && ten && tenb && coarse,
        "renderbuffers of RGBA8 2048x2205, RGB10_A2 and RGB565 on this GL");
  if (!frame || !target || !ten || !tenb || !coarse)
    return;
  std::string failed;
  check(!COwnScanout::Probe(coarse, 8, failed) && !failed.empty(),
        "Probe(): a probe of 8 bits refuses a buffer that does not keep 8-bit values, and says why", failed);
  failed.clear();
  check(COwnScanout::Probe(target, 8, failed), "Probe(): a buffer of 8 bits takes 8-bit values back", failed);
  failed.clear();
  check(COwnScanout::FlipProbe(frame, target, W, H, 8, failed),
        "FlipProbe(): a frame of 8 bits flipped by the GPU into a buffer of 8 bits", failed);
  failed.clear();
  check(COwnScanout::Probe(ten, 10, failed), "Probe(): a buffer of 10 bits as before", failed);
  failed.clear();
  check(COwnScanout::FlipProbe(ten, tenb, 64, 64, 10, failed), "FlipProbe(): a frame of 10 bits as before", failed);
  failed.clear();
  check(!COwnScanout::Probe(target, 10, failed) && !failed.empty(),
        "Probe(): a probe of 10 bits refuses a buffer of 8 bits, and says why", failed);
  GLint bound = -1;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
  check(bound == 0, "the probes put back the framebuffer that was bound");
  // (the probes leave the buffers black: what goes to the screen first)
  uint8_t px[4] = {9, 9, 9, 9};
  glBindFramebuffer(GL_FRAMEBUFFER, target);
  glReadPixels(0, H - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  check(px[0] == 0 && px[1] == 0 && px[2] == 0, "the buffer is black after the probes");
  for (GLuint fb : {frame, target, ten, tenb, coarse})
    glDeleteFramebuffers(1, &fb);
  for (GLuint rb : {rbFrame, rbTarget, rb10, rb10b, rb565})
    glDeleteRenderbuffers(1, &rb);
}

// The frame as Kodi draws it in a frame-packed mode: in GL coordinates of the frame (from its bottom), the left eye
// at the top (CGraphicContext::SetStereoView(), the first eye), the right eye iHeight + iBlanking lines down; the
// lines between them keep what an earlier frame left there (white here) until STEREO_GAP::Clear()
void FramePacked()
{
  constexpr int W = 2048, H = 2205, EYE_W = 1920, EYE_H = 1080, GAP = 45;
  GLuint rbFrame = 0, rbTarget = 0;
  const GLuint frame = Target(GL_RGBA8, W, H, rbFrame);
  const GLuint target = Target(GL_RGBA8, W, H, rbTarget);
  check(frame && target, "renderbuffers of the frame and of the buffer that goes to the screen");
  if (!frame || !target)
    return;
  glBindFramebuffer(GL_FRAMEBUFFER, frame);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  // the left eye: Kodi's lines 0-1079, GL rows 1125-2204
  glScissor(0, H - EYE_H, EYE_W, EYE_H);
  glClearColor(200 / 255.0f, 10 / 255.0f, 20 / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  // the right eye: Kodi's lines 1125-2204, GL rows 0-1079
  glScissor(0, 0, EYE_W, EYE_H);
  glClearColor(10 / 255.0f, 200 / 255.0f, 30 / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  // CWinSystemGbmGLESContext::ClearStereoGap(): GapOf() with the eye and the size of the window (m_width, m_height)
  const auto gap = STEREO_GAP::GapOf(true, false, EYE_W, EYE_H, GAP, EYE_W, H);
  check(gap.has_value(), "GapOf(): the lines between the eyes of 1080p frame packing");
  if (gap)
    STEREO_GAP::Clear(*gap);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // COwnScanout::Swap(): the frame flipped into the buffer that goes to the screen
  COwnScanout::FlipBlit(frame, target, W, H);
  check(glGetError() == GL_NO_ERROR, "FlipBlit(): no GL error");

  std::vector<uint8_t> line(static_cast<size_t>(EYE_W) * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, target);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  int wrongLeft = 0, wrongGap = 0, wrongRight = 0, firstWrong = -1;
  for (int row = 0; row < H; ++row)
  {
    glReadPixels(0, row, EYE_W, 1, GL_RGBA, GL_UNSIGNED_BYTE, line.data());
    uint8_t want[3];
    int* wrong;
    if (row < EYE_H)
    {
      want[0] = 200, want[1] = 10, want[2] = 20;
      wrong = &wrongLeft;
    }
    else if (row < EYE_H + GAP)
    {
      want[0] = 0, want[1] = 0, want[2] = 0;
      wrong = &wrongGap;
    }
    else
    {
      want[0] = 10, want[1] = 200, want[2] = 30;
      wrong = &wrongRight;
    }
    for (int x = 0; x < EYE_W; ++x)
      if (line[x * 4] != want[0] || line[x * 4 + 1] != want[1] || line[x * 4 + 2] != want[2])
      {
        ++*wrong;
        if (firstWrong < 0)
          firstWrong = row;
        break;
      }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  const std::string first = "first wrong line " + std::to_string(firstWrong);
  check(wrongLeft == 0, "the buffer that goes to the screen: the left eye in lines 0-1079",
        std::to_string(wrongLeft) + " lines wrong, " + first);
  check(wrongGap == 0, "... black in the 45 lines 1080-1124", std::to_string(wrongGap) + " lines wrong, " + first);
  check(wrongRight == 0, "... the right eye in lines 1125-2204", std::to_string(wrongRight) + " lines wrong, " + first);
  // (the same lines as the rows that FramePackedOwn.h names for the mode)
  const auto rows = FP_OWN::RowsOf(Mode1080p24FramePacked());
  check(rows && rows->gapFirst == EYE_H && rows->gapEnd == EYE_H + GAP && rows->rightEnd == H,
        "RowsOf(): the rows of the frame that was drawn");

  // COwnScanout::CopyOut(): the same frame read back as RGBA and copied by the processor into a dumb buffer of AR24
  // (2048 pixels, a pitch of 8192 bytes as nvidia-drm gives it, B, G, R, A in memory): from its first line, the left
  // eye, 45 black lines, the right eye
  std::vector<uint8_t> read(static_cast<size_t>(W) * H * 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, frame);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, read.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  constexpr uint32_t PITCH = 8192;
  std::vector<uint8_t> dumb(static_cast<size_t>(PITCH) * H, 0x77);
  check(COwnScanout::CopyRowsFlipped(read.data(), W, H, false, DRM_FORMAT_ARGB8888, dumb.data(), PITCH),
        "CopyRowsFlipped(): the frame-packed frame copied into a dumb buffer of AR24");
  int copyLeft = 0, copyGap = 0, copyRight = 0;
  for (int row = 0; row < H; ++row)
  {
    const uint8_t* p = dumb.data() + static_cast<size_t>(row) * PITCH;
    uint8_t want[3];
    int* wrong;
    if (row < EYE_H)
      want[0] = 20, want[1] = 10, want[2] = 200, wrong = &copyLeft;
    else if (row < EYE_H + GAP)
      want[0] = 0, want[1] = 0, want[2] = 0, wrong = &copyGap;
    else
      want[0] = 30, want[1] = 200, want[2] = 10, wrong = &copyRight;
    for (int x = 0; x < EYE_W; ++x)
      if (p[x * 4] != want[0] || p[x * 4 + 1] != want[1] || p[x * 4 + 2] != want[2])
      {
        ++*wrong;
        break;
      }
  }
  check(copyLeft == 0, "the dumb buffer copied into: the left eye in lines 0-1079", std::to_string(copyLeft));
  check(copyGap == 0, "... black in the 45 lines 1080-1124", std::to_string(copyGap));
  check(copyRight == 0, "... the right eye in lines 1125-2204", std::to_string(copyRight));
  glDeleteFramebuffers(1, &frame);
  glDeleteFramebuffers(1, &target);
  glDeleteRenderbuffers(1, &rbFrame);
  glDeleteRenderbuffers(1, &rbTarget);
}
} // namespace

int main()
{
  Gating();
  Dumb();
  Verdicts();
  Geometry();
  Formats();

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
  Probes();
  FramePacked();
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
