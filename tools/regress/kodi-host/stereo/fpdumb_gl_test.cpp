// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// fpdumb_gl_test.cpp (issue #33, 1.2.3): the driver's dumb buffers of an HDMI 3D frame-packed mode of nvidia-drm without
// GBM (OwnScanout.cpp of the tree under test, COwnScanout::Memory::DUMB), on Mesa's llvmpipe (surfaceless EGL, OpenGL
// ES 3), with the DRM device and the GBM device of NVIDIA stood in for by this file:
//  - the DRM file is a memfd: a dumb buffer is a slot of it, its pitch the driver's (nv_drm_dumb_create(): the width
//    times 4, rounded up to 256), DRM_IOCTL_MODE_MAP_DUMB gives the offset of its slot, and mmap() maps that slot;
//    drmModeAddFB2WithModifiers() and drmModeRmFB() are counted and their arguments kept; GBM makes small buffers that
//    keep their user data, and calls its destroy callback when they go (as Mesa's libgbm does)
//  - the dma-buf of a dumb buffer is refused (as where NVIDIA's EGL cannot import one): the frames are copied by the
//    processor (CopiesOnCpu()), the copy checked before use (CopyProbe(): the probe pixels in the last row of the
//    buffer, the first row black)
//  - what is made: three dumb buffers of 2048x2205 and 32 bits, each with a framebuffer of its own made of its handle,
//    DRM_FORMAT_MOD_LINEAR said (DRM_MODE_FB_MODIFIERS), its pitch of 8192, AR24; a small buffer of GBM standing for
//    each, whose user data is that framebuffer (drm_fb::own: handle, size, pitch, modifier); GBM never imports anything
//  - a frame drawn as Kodi draws a frame-packed one, swapped: in the dumb buffer that goes to the screen, from its first
//    line, the left eye, 45 black lines, the right eye, the same bytes as the frame (B, G, R, A), at the driver's pitch
//  - everything goes with them: each framebuffer removed, each dumb buffer destroyed, each small buffer of GBM; and
//    where a step fails (no dumb buffer, no framebuffer, no small buffer) nothing is left of what was made, and the
//    step is said
//  - RowHolds() as it judges a row of 8 bits, B, G, R, A and R, G, B, A
//  - the cost of the copy of a frame by the processor on this host (CopyRowsFlipped(): 1080p and 720p frame packing;
//    printed, not judged)
// Prints a FAIL line for each failed check and "<n> failed" last.
#include "rendering/OutputFramebuffer.h"
#include "windowing/gbm/OwnScanout.h"
#include "windowing/gbm/StereoGap.h"
#include "windowing/gbm/drm/DRMFramebuffer.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

using namespace KODI::WINDOWING::GBM;

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

// ---- the stand-ins of the DRM device and of NVIDIA's GBM
constexpr size_t SLOT = 32u << 20; // (a dumb buffer of 2048x2205 is 18063360 bytes)
constexpr int SLOTS = 8;
int g_drm = -1;
struct Fake
{
  int createFail = 0; // the n-th CREATE_DUMB fails (0: none)
  bool addFail = false;
  bool gbmFail = false;
  int creates = 0;
  std::set<uint32_t> live; // dumb handles
  std::vector<drm_mode_create_dumb> made;
  struct Fb
  {
    uint32_t width, height, format, handle, pitch, offset, flags;
    uint64_t modifier;
  };
  std::map<uint32_t, Fb> fbs; // live framebuffers
  int added = 0, removed = 0, destroyed = 0, gbmMade = 0, gbmGone = 0, imports = 0;
  uint32_t nextFb = 100;
};
Fake g;

struct FakeBo
{
  gbm_device* device;
  void* data = nullptr;
  void (*destroy)(gbm_bo*, void*) = nullptr;
};
} // namespace

extern "C"
{
int drmIoctl(int fd, unsigned long request, void* arg)
{
  if (fd != g_drm)
  {
    errno = EBADF;
    return -1;
  }
  if (request == DRM_IOCTL_MODE_CREATE_DUMB)
  {
    auto* c = static_cast<drm_mode_create_dumb*>(arg);
    if (++g.creates == g.createFail || g.live.size() >= SLOTS)
    {
      errno = ENOMEM;
      return -1;
    }
    uint32_t handle = 1;
    while (g.live.contains(handle))
      ++handle;
    c->handle = handle;
    // (nv_drm_dumb_create(): the pitch rounded up to the pitch alignment of NVKMS, 256)
    c->pitch = ((c->width * ((c->bpp + 7) / 8)) + 255) / 256 * 256;
    c->size = (static_cast<uint64_t>(c->height) * c->pitch + 4095) / 4096 * 4096;
    g.live.insert(handle);
    g.made.push_back(*c);
    return 0;
  }
  if (request == DRM_IOCTL_MODE_MAP_DUMB)
  {
    auto* m = static_cast<drm_mode_map_dumb*>(arg);
    if (!g.live.contains(m->handle))
    {
      errno = ENOENT;
      return -1;
    }
    m->offset = static_cast<uint64_t>(m->handle - 1) * SLOT;
    return 0;
  }
  if (request == DRM_IOCTL_MODE_DESTROY_DUMB)
  {
    auto* d = static_cast<drm_mode_destroy_dumb*>(arg);
    if (!g.live.erase(d->handle))
    {
      errno = ENOENT;
      return -1;
    }
    ++g.destroyed;
    return 0;
  }
  errno = EINVAL;
  return -1;
}

int drmPrimeHandleToFD(int, uint32_t, uint32_t, int* fd)
{
  // (NVIDIA's EGL taking no dma-buf of a dumb buffer is what this host can show: the copy of the processor)
  *fd = -1;
  errno = ENOSYS;
  return -1;
}

int drmModeAddFB2WithModifiers(int fd,
                               uint32_t width,
                               uint32_t height,
                               uint32_t format,
                               const uint32_t handles[4],
                               const uint32_t pitches[4],
                               const uint32_t offsets[4],
                               const uint64_t modifier[4],
                               uint32_t* id,
                               uint32_t flags)
{
  if (fd != g_drm || g.addFail || !g.live.contains(handles[0]))
  {
    errno = EINVAL;
    return -EINVAL;
  }
  *id = g.nextFb++;
  g.fbs[*id] = {width, height, format, handles[0], pitches[0], offsets[0], flags, modifier[0]};
  ++g.added;
  return 0;
}

int drmModeRmFB(int fd, uint32_t id)
{
  if (fd != g_drm || !g.fbs.erase(id))
  {
    errno = ENOENT;
    return -ENOENT;
  }
  ++g.removed;
  return 0;
}

gbm_bo* gbm_bo_create(gbm_device* device, uint32_t, uint32_t, uint32_t, uint32_t)
{
  if (g.gbmFail)
  {
    errno = EINVAL;
    return nullptr;
  }
  ++g.gbmMade;
  return reinterpret_cast<gbm_bo*>(new FakeBo{device});
}

void gbm_bo_set_user_data(gbm_bo* bo, void* data, void (*destroy)(gbm_bo*, void*))
{
  auto* b = reinterpret_cast<FakeBo*>(bo);
  b->data = data;
  b->destroy = destroy;
}

void* gbm_bo_get_user_data(gbm_bo* bo)
{
  return reinterpret_cast<FakeBo*>(bo)->data;
}

gbm_device* gbm_bo_get_device(gbm_bo* bo)
{
  return reinterpret_cast<FakeBo*>(bo)->device;
}

int gbm_device_get_fd(gbm_device*)
{
  return g_drm;
}

void gbm_bo_destroy(gbm_bo* bo)
{
  auto* b = reinterpret_cast<FakeBo*>(bo);
  if (b->destroy)
    b->destroy(bo, b->data);
  ++g.gbmGone;
  delete b;
}

gbm_bo* gbm_bo_import(gbm_device*, uint32_t, void*, uint32_t)
{
  // (NVIDIA's GBM refuses a dumb buffer: what 1.2.2 met. Nothing of the new path asks it)
  ++g.imports;
  errno = EINVAL;
  return nullptr;
}
} // extern "C"

namespace
{
gbm_device* const DEVICE = reinterpret_cast<gbm_device*>(0x1000);

void Reset()
{
  g = Fake{};
}

const uint8_t* Memory(uint32_t handle)
{
  static uint8_t* whole = nullptr;
  if (!whole)
    whole = static_cast<uint8_t*>(
        mmap(nullptr, SLOT * SLOTS, PROT_READ | PROT_WRITE, MAP_SHARED, g_drm, 0));
  return whole + static_cast<size_t>(handle - 1) * SLOT;
}

void Rows()
{
  const uint8_t probeBgra[16] = {3, 2, 1, 255, 254, 129, 128, 255, 128, 1, 254, 255, 129, 254, 3, 255};
  const uint8_t probeRgba[16] = {1, 2, 3, 255, 128, 129, 254, 255, 254, 1, 128, 255, 3, 254, 129, 255};
  const uint8_t black[16] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
  std::string failed;
  check(COwnScanout::RowHolds(probeBgra, DRM_FORMAT_ARGB8888, true, "last", failed),
        "RowHolds(): the probes in AR24 (B, G, R, A)", failed);
  check(COwnScanout::RowHolds(probeRgba, DRM_FORMAT_XBGR8888, true, "last", failed),
        "RowHolds(): the probes in XB24 (R, G, B, A)", failed);
  failed.clear();
  check(!COwnScanout::RowHolds(probeRgba, DRM_FORMAT_ARGB8888, true, "last", failed) &&
            failed.find("in its last row, pixel 0 component 0") != std::string::npos,
        "RowHolds(): the probes of R, G, B, A in AR24 refused, and said where", failed);
  failed.clear();
  check(COwnScanout::RowHolds(black, DRM_FORMAT_ARGB8888, false, "first", failed) &&
            !COwnScanout::RowHolds(probeBgra, DRM_FORMAT_ARGB8888, false, "first", failed),
        "RowHolds(): black, and the probes are not black");
  failed.clear();
  check(!COwnScanout::RowHolds(black, DRM_FORMAT_ABGR2101010, false, "first", failed) && !failed.empty(),
        "RowHolds(): no row of a format of more than 8 bits");
}

// The frame as Kodi draws it in a frame-packed mode (as fpown_gl_test.cpp): the left eye at the top of the picture, the
// right eye iHeight + iBlanking lines down, the lines between them cleared by STEREO_GAP::Clear()
void DrawFramePacked(int width, int height)
{
  constexpr int EYE_W = 1920, EYE_H = 1080, GAP = 45;
  glDisable(GL_SCISSOR_TEST);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, height - EYE_H, EYE_W, EYE_H);
  glClearColor(200 / 255.0f, 10 / 255.0f, 20 / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glScissor(0, 0, EYE_W, EYE_H);
  glClearColor(10 / 255.0f, 200 / 255.0f, 30 / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);
  const auto gap = STEREO_GAP::GapOf(true, false, EYE_W, EYE_H, GAP, EYE_W, height);
  check(gap.has_value(), "GapOf(): the lines between the eyes");
  if (gap)
    STEREO_GAP::Clear(*gap);
  (void)width;
}

void Dumb(EGLDisplay display)
{
  constexpr int W = 2048, H = 2205;
  Reset();
  std::string failed;
  auto own = COwnScanout::Create(DEVICE, display, W, H, DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR}, true, true,
                                 failed, COwnScanout::Memory::DUMB, g_drm);
  check(own != nullptr, "Create(DUMB): made with the copy of the processor where EGL takes no dma-buf", failed);
  if (!own)
    return;
  check(own->MemoryKind() == COwnScanout::Memory::DUMB && own->CopiesOnCpu() &&
            own->CopyReason().find("dma-buf") != std::string::npos,
        "Create(DUMB): the frames copied by the processor, and why", own->CopyReason());
  check(own->Modifier() == DRM_FORMAT_MOD_LINEAR && own->Format() == DRM_FORMAT_ARGB8888,
        "Create(DUMB): linear, AR24");
  // (1.2.3: read back through pixel buffers, a band at a time, and that copy checked before use like the other)
  check(own->CopiesInBands(), "Create(DUMB): the frames read back through pixel buffers, in bands");
  check(g.made.size() == COwnScanout::BUFFERS && g.imports == 0, "three dumb buffers, and nothing imported into GBM");
  bool made = true;
  for (const auto& c : g.made)
    made = made && c.width == W && c.height == H && c.bpp == 32 && c.pitch == 8192;
  check(made, "each dumb buffer 2048x2205 of 32 bits, its pitch 8192 (the driver's)");
  check(g.added == 3 && g.fbs.size() == 3 && g.gbmMade == 3, "a framebuffer of each, and a small buffer of GBM for each");
  bool fbs = true;
  for (const auto& [id, fb] : g.fbs)
    fbs = fbs && fb.width == W && fb.height == H && fb.format == DRM_FORMAT_ARGB8888 && fb.pitch == 8192 &&
          fb.offset == 0 && fb.modifier == DRM_FORMAT_MOD_LINEAR && (fb.flags & DRM_MODE_FB_MODIFIERS);
  check(fbs, "each framebuffer made of its handle, AR24, pitch 8192, offset 0, DRM_FORMAT_MOD_LINEAR said "
             "(DRM_MODE_FB_MODIFIERS): what nvidia-drm asks of a frame-packed one");
  std::set<uint32_t> handles;
  for (unsigned int i = 0; i < COwnScanout::BUFFERS; ++i)
  {
    gbm_bo* bo = own->BufferAt(i);
    const auto* fb = bo ? static_cast<const drm_fb*>(gbm_bo_get_user_data(bo)) : nullptr;
    const drm_fb::Own* o = OwnFramebufferOf(bo);
    const bool known = fb && o && fb->bo == bo && g.fbs.contains(fb->fb_id) && g.fbs[fb->fb_id].handle == o->handle &&
                       o->width == W && o->height == H && o->pitch == 8192 && o->modifier == DRM_FORMAT_MOD_LINEAR &&
                       fb->format == DRM_FORMAT_ARGB8888;
    check(known, "buffer " + std::to_string(i + 1) +
                     ": its small buffer of GBM holds the framebuffer of the dumb one and what it is made of");
    if (o)
      handles.insert(o->handle);
  }
  check(handles.size() == 3, "three dumb buffers, one for each");
  // (black, opaque, before anything goes out through them: the copy's check leaves them so)
  bool black = true;
  for (uint32_t h : handles)
  {
    const uint8_t* m = Memory(h);
    for (size_t row : {size_t{0}, size_t{1124}, size_t{H - 1}})
      for (int x = 0; x < W; x += 97)
      {
        const uint8_t* p = m + row * 8192 + x * 4;
        black = black && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255;
      }
  }
  check(black, "every dumb buffer opaque black before use");

  // A frame drawn into the framebuffer that stands for the window, swapped: copied into the buffer chosen for it
  check(own->BindBack(), "BindBack(): a buffer to draw the next frame into");
  GLint bound = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
  check(bound != 0 && static_cast<unsigned int>(bound) == KODI::RENDERING::OutputFramebuffer(),
        "the frame is drawn into the framebuffer that stands for the window");
  DrawFramePacked(W, H);
  check(own->Swap(true), "Swap()");
  gbm_bo* front = own->LockFront();
  const drm_fb::Own* o = OwnFramebufferOf(front);
  check(o != nullptr, "LockFront(): the buffer of the frame, one of the dumb ones");
  if (o)
  {
    const uint8_t* m = Memory(o->handle);
    int wrongLeft = 0, wrongGap = 0, wrongRight = 0, firstWrong = -1;
    for (int row = 0; row < H; ++row)
    {
      const uint8_t* p = m + static_cast<size_t>(row) * 8192;
      uint8_t want[4];
      int* wrong;
      if (row < 1080)
        want[0] = 20, want[1] = 10, want[2] = 200, wrong = &wrongLeft;
      else if (row < 1125)
        want[0] = 0, want[1] = 0, want[2] = 0, wrong = &wrongGap;
      else
        want[0] = 30, want[1] = 200, want[2] = 10, wrong = &wrongRight;
      want[3] = 255;
      for (int x = 0; x < 1920; ++x)
        if (std::memcmp(p + x * 4, want, 3) != 0)
        {
          ++*wrong;
          if (firstWrong < 0)
            firstWrong = row;
          break;
        }
    }
    const std::string first = "first wrong line " + std::to_string(firstWrong);
    check(wrongLeft == 0, "the dumb buffer that goes to the screen: the left eye in lines 0-1079, B, G, R, A",
          std::to_string(wrongLeft) + " lines wrong, " + first);
    check(wrongGap == 0, "... black in the 45 lines 1080-1124", std::to_string(wrongGap) + " lines wrong, " + first);
    check(wrongRight == 0, "... the right eye in lines 1125-2204", std::to_string(wrongRight) + " lines wrong, " + first);
    // (the same bytes as the frame: each pixel of a row of the frame read back, against the buffer)
    std::vector<uint8_t> line(static_cast<size_t>(W) * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(bound));
    int differ = 0;
    for (int row : {0, 7, 1079, 1080, 1124, 1125, 2204})
    {
      // (GL row 0 is the bottom line of the picture)
      glReadPixels(0, H - 1 - row, W, 1, GL_RGBA, GL_UNSIGNED_BYTE, line.data());
      const uint8_t* p = m + static_cast<size_t>(row) * 8192;
      for (int x = 0; x < W; ++x)
        if (p[x * 4] != line[x * 4 + 2] || p[x * 4 + 1] != line[x * 4 + 1] || p[x * 4 + 2] != line[x * 4] ||
            p[x * 4 + 3] != line[x * 4 + 3])
          ++differ;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    check(differ == 0, "the copy is the frame, the same bytes (all 2048 pixels of 7 lines)", std::to_string(differ));
  }
  own->Release(front);
  COwnScanout::UnbindAll();

  // (1.2.3) What the copy costs, as the card "3D: frame packing" measures it: black frames into the first buffer
  double ms = -1.0;
  check(own->MeasureCopy(5, ms) && ms > 0.0, "MeasureCopy(): the copy of a frame timed", std::to_string(ms));
  std::printf("BENCH the copy of a frame-packed frame of 2048x2205 through pixel buffers on llvmpipe (software, not "
              "a card): %.2f ms\n", ms);
  {
    const drm_fb::Own* first = OwnFramebufferOf(own->BufferAt(0));
    const uint8_t* m = first ? Memory(first->handle) : nullptr;
    bool black = m != nullptr;
    for (size_t row : {size_t{0}, size_t{1124}, size_t{H - 1}})
      for (int x = 0; m && x < W; x += 97)
      {
        const uint8_t* p = m + row * 8192 + x * 4;
        black = black && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255;
      }
    check(black, "MeasureCopy(): its black frames copied, opaque black");
  }

  // Everything goes with them
  own.reset();
  check(g.removed == 3 && g.fbs.empty(), "each framebuffer removed when they go", std::to_string(g.removed));
  check(g.destroyed == 3 && g.live.empty(), "each dumb buffer destroyed when they go", std::to_string(g.destroyed));
  check(g.gbmGone == 3, "each small buffer of GBM destroyed when they go");
}

void Failures(EGLDisplay display)
{
  constexpr int W = 2048, H = 2205;
  struct Case
  {
    const char* name;
    int createFail;
    bool addFail;
    bool gbmFail;
    const char* said;
  };
  const Case cases[] = {
      {"the second dumb buffer is not made", 2, false, false, "could not make a dumb buffer of 2048x2205"},
      {"no framebuffer of a dumb buffer", 0, true, false, "made no framebuffer of the dumb buffer"},
      {"no small buffer of GBM", 0, false, true, "could not make the small buffer"},
  };
  for (const Case& c : cases)
  {
    Reset();
    g.createFail = c.createFail;
    g.addFail = c.addFail;
    g.gbmFail = c.gbmFail;
    std::string failed;
    auto own = COwnScanout::Create(DEVICE, display, W, H, DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR}, true, true,
                                   failed, COwnScanout::Memory::DUMB, g_drm);
    check(!own && failed.find(c.said) != std::string::npos, std::string("Create(DUMB), ") + c.name + ": refused, said",
          failed);
    check(g.live.empty() && g.fbs.empty() && g.gbmMade == g.gbmGone,
          std::string("... and nothing left of what was made (") + c.name + ")",
          std::to_string(g.live.size()) + " dumb, " + std::to_string(g.fbs.size()) + " framebuffers, " +
              std::to_string(g.gbmMade - g.gbmGone) + " small buffers");
  }
  Reset();
  std::string failed;
  check(!COwnScanout::Create(DEVICE, display, W, H, DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR}, true, true, failed,
                             COwnScanout::Memory::DUMB, -1) &&
            g.made.empty(),
        "Create(DUMB) without a DRM file: refused, nothing made");
  // A width whose rows the driver pads (2000 pixels: 8000 bytes, a pitch of 8192): the framebuffer at the driver's
  // pitch, never at the width's
  Reset();
  failed.clear();
  {
    auto own = COwnScanout::Create(DEVICE, display, 2000, 16, DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR}, true, true,
                                   failed, COwnScanout::Memory::DUMB, g_drm);
    bool pitched = own && !g.fbs.empty();
    for (const auto& [id, fb] : g.fbs)
      pitched = pitched && fb.pitch == 8192 && fb.width == 2000;
    check(pitched, "a dumb buffer 2000 pixels wide: its framebuffer at the driver's pitch of 8192", failed);
    const drm_fb::Own* o = own ? OwnFramebufferOf(own->BufferAt(0)) : nullptr;
    check(o && o->pitch == 8192, "... and so it says (drm_fb::own)");
  }
}

void Bench()
{
  for (const auto& [w, h, name] : {std::tuple{2048, 2205, "1080p frame packing"}, {1280, 1470, "720p frame packing"}})
  {
    std::vector<uint8_t> rows(static_cast<size_t>(w) * h * 4, 0x5a);
    std::vector<uint8_t> target(static_cast<size_t>(((w * 4 + 255) / 256) * 256) * h);
    const uint32_t pitch = static_cast<uint32_t>((w * 4 + 255) / 256 * 256);
    for (bool bgra : {true, false})
    {
      COwnScanout::CopyRowsFlipped(rows.data(), w, h, bgra, DRM_FORMAT_ARGB8888, target.data(), pitch);
      const auto start = std::chrono::steady_clock::now();
      constexpr int N = 20;
      for (int i = 0; i < N; ++i)
        COwnScanout::CopyRowsFlipped(rows.data(), w, h, bgra, DRM_FORMAT_ARGB8888, target.data(), pitch);
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / N;
      std::printf("BENCH %s %dx%d, rows read as %s: %.2f ms a frame (into memory of the host, not of a card)\n", name,
                  w, h, bgra ? "BGRA (copied as they are)" : "RGBA (red and blue exchanged)", ms);
    }
  }
}
} // namespace

int main()
{
  g_drm = memfd_create("fake-drm", 0);
  if (g_drm < 0 || ftruncate(g_drm, static_cast<off_t>(SLOT * SLOTS)) != 0)
  {
    std::printf("FAIL: no memfd\n1 failed\n");
    return 1;
  }
  Rows();
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
  Dumb(display);
  Failures(display);
  Bench();
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
