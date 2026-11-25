// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): a 3D picture drawn by the Dolby Vision bridge a view
// at a time (KODI::UTILS::STEREO3D in Kodi), on the software OpenGL ES (zink on lavapipe), with the bridge of a tree
// and the libplacebo of the build. Two different pictures are put side by side (as Kodi puts the two views of MV-HEVC,
// 7680x2160 for two 3840x2160 views) or one above the other, in the textures of 10-bit 4:2:0 that Kodi's renderer
// uploads (DVBRIDGE_YUV420P10); each view is copied out (dvbridge_gl_part_copy()) and drawn into the place of its eye,
// and what is drawn there is read back and compared, value for value, with the same picture drawn alone, in 2D, at
// the same place:
//
//   - Dolby Vision profile 5 (the base layer in IPT-PQ-c2, one RPU for both views, as FFmpeg gives both views of
//     profile 20 the RPU of the base view), rebuilt as HDR10, into the eyes of side by side (half) at 2160p and of
//     frame packing at 1080p (the eyes one above the other with 45 lines between them);
//   - the same mapped to SDR in BT.2020 (a 3D video sent as SDR in BT.2020);
//   - an HDR10 picture beside another, mapped to SDR in BT.709;
//   - each eye is its own view, not the other one;
//   - the copy refuses a part of half a chroma sample, and a picture of two 3840x2160 views is wider than the bridge
//     takes in whole (dvbridge_gl_frame_import());
//   - the rule before, a view drawn as a crop of the picture of two (on views of 1920x1080, which fit whole): what it
//     draws at the seam is not the picture in 2D (the other view's samples are read across it), which the copy fixes.
//
// Usage: stereo3d_test <p5 RPU (AVDOVIMetadata)> <p5 base A> <p5 base B> <HDR10 A> <HDR10 B> (each yuv420p10le,
//        3840x2160; the HDR10 ones limited range, BT.2020, mastered at 4000 cd/m2)
// Prints "ok:"/"FAIL:" lines and "PASSED|FAILED: <n> failed".
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_part.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"

#define W 3840
#define H 2160
#define N ((size_t)W * H)

static int g_failures;

static void Check(int ok, const char* what)
{
  printf("%s %s\n", ok ? "ok:  " : "FAIL:", what);
  if (!ok)
    ++g_failures;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

static void* ReadAll(const char* path, size_t* size)
{
  FILE* f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  void* p = malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n)
  {
    free(p);
    p = NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return p;
}

// --------------------------------------------------------------------------------------------------- the pictures
// A picture of 10-bit 4:2:0 in the three R16 textures that Kodi's renderer makes of a picture the processor decoded
// (CLinuxRendererGLES::CreatePlanarYUVTexture(): the samples as they are, the 10 bits at the bottom)
struct pic
{
  GLuint tex[3];
  int w, h;
};

static GLuint Plane(int w, int h, const uint16_t* data)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

// one of the planes (0 Y, 1 U, 2 V) of a yuv420p10le picture of width x height
static const uint16_t* PlaneOf(const uint16_t* p, int width, int height, int i)
{
  return i == 0 ? p : i == 1 ? p + (size_t)width * height : p + (size_t)width * height * 5 / 4;
}

// a window of a picture (x, y, w, h; even) as a picture of its own
static uint16_t* Cut(const uint16_t* p, int width, int height, int x, int y, int w, int h)
{
  uint16_t* out = malloc((size_t)w * h * 3);
  for (int i = 0; i < 3; ++i)
  {
    const int d = i ? 2 : 1;
    const uint16_t* src = PlaneOf(p, width, height, i);
    uint16_t* dst = (uint16_t*)PlaneOf(out, w, h, i);
    for (int r = 0; r < h / d; ++r)
      memcpy(dst + (size_t)r * (w / d), src + (size_t)(y / d + r) * (width / d) + x / d, (size_t)(w / d) * 2);
  }
  return out;
}

// two pictures of width x height side by side (sbs) or one above the other, as one
static uint16_t* Pack(const uint16_t* a, const uint16_t* b, int width, int height, int sbs)
{
  const int pw = sbs ? 2 * width : width, ph = sbs ? height : 2 * height;
  uint16_t* out = malloc((size_t)pw * ph * 3);
  for (int i = 0; i < 3; ++i)
  {
    const int d = i ? 2 : 1;
    const int w = width / d, h = height / d;
    uint16_t* dst = (uint16_t*)PlaneOf(out, pw, ph, i);
    for (int v = 0; v < 2; ++v)
    {
      const uint16_t* src = PlaneOf(v ? b : a, width, height, i);
      for (int r = 0; r < h; ++r)
      {
        const size_t to = sbs ? (size_t)r * (pw / d) + (size_t)v * w : (size_t)(v * h + r) * (pw / d);
        memcpy(dst + to, src + (size_t)r * w, (size_t)w * 2);
      }
    }
  }
  return out;
}

static struct pic Upload(const uint16_t* p, int width, int height)
{
  struct pic out = {{0, 0, 0}, width, height};
  for (int i = 0; i < 3; ++i)
    out.tex[i] = Plane(i ? width / 2 : width, i ? height / 2 : height, PlaneOf(p, width, height, i));
  return out;
}

static void Planes(const struct pic* p, struct dvbridge_gl_plane planes[3])
{
  for (int i = 0; i < 3; ++i)
    planes[i] = (struct dvbridge_gl_plane){p->tex[i], i ? p->w / 2 : p->w, i ? p->h / 2 : p->h};
}

// ---------------------------------------------------------------------------------------------------- drawing
struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_renderer* r;
  const void* meta;
  size_t bytes;
  struct dvbridge_plain plain;
};

// a picture of planes (w x h) drawn with view into an output of ow x oh, the place of the view read back into out
// (dst_width x dst_height RGBA floats); 0 where it could not be imported, drawn or read
static int Draw(struct gl* g, const struct dvbridge_gl_plane planes[3], int w, int h, struct dvbridge_view view,
                int ow, int oh, int plain, float* out)
{
  struct dvbridge_gl_frame f = {0};
  if (!dvbridge_gl_frame_import(&f, g->gl->gpu, DVBRIDGE_YUV420P10, w, h, planes, AVCHROMA_LOC_LEFT))
    return 0;
  const int drawn = plain ? dvbridge_render_plain(g->r, &f.frame, &g->plain, view, ow, oh)
                          : dvbridge_render_hdr10(g->r, &f.frame, g->meta, g->bytes, 0.0, NAN, view, ow, oh);
  dvbridge_gl_frame_release(&f);
  if (!drawn)
    return 0;
  pl_tex tex = dvbridge_render_hdr10_texture(g->r);
  const GLuint t = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  if (!t)
    return 0;
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(view.dst_x, view.dst_y, view.dst_width, view.dst_height, GL_RGBA, GL_FLOAT, out);
  const int read = glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  return read;
}

// a view of a packed picture copied out and drawn (the way of Kodi now)
static int DrawPart(struct gl* g, struct dvbridge_gl_part* part, const struct pic* packed, int x, int y, int w,
                    int h, struct dvbridge_view view, int ow, int oh, int plain, float* out)
{
  struct dvbridge_gl_plane all[3], mine[3];
  Planes(packed, all);
  if (!dvbridge_gl_part_copy(part, DVBRIDGE_YUV420P10, packed->w, packed->h, all, x, y, w, h, mine))
    return 0;
  return Draw(g, mine, w, h, view, ow, oh, plain, out);
}

static int DrawWhole(struct gl* g, const struct pic* p, struct dvbridge_view view, int ow, int oh, int plain,
                     float* out)
{
  struct dvbridge_gl_plane all[3];
  Planes(p, all);
  return Draw(g, all, p->w, p->h, view, ow, oh, plain, out);
}

// values that differ, and in how many columns of the place
static long Apart(const float* a, const float* b, int w, int h, int* columns)
{
  long n = 0;
  *columns = 0;
  for (int x = 0; x < w; ++x)
  {
    int col = 0;
    for (int y = 0; y < h; ++y)
      for (int c = 0; c < 4; ++c)
        if (memcmp(&a[((size_t)y * w + x) * 4 + c], &b[((size_t)y * w + x) * 4 + c], sizeof(float)) != 0)
        {
          ++n;
          col = 1;
        }
    *columns += col;
  }
  return n;
}

// one 3D case: the eye of view v of a packed picture against the picture alone, and against the other one
static void Eye(struct gl* g, struct dvbridge_gl_part* part, const struct pic* packed, const struct pic* own,
                const struct pic* other, int x, int y, struct dvbridge_view view, int ow, int oh, int plain,
                const char* what)
{
  const size_t n = (size_t)view.dst_width * view.dst_height * 4;
  float* eye = malloc(n * sizeof(float));
  float* ref = malloc(n * sizeof(float));
  float* not = malloc(n * sizeof(float));
  const int d1 = DrawPart(g, part, packed, x, y, own->w, own->h, view, ow, oh, plain, eye);
  const int d2 = DrawWhole(g, own, view, ow, oh, plain, ref);
  const int d3 = DrawWhole(g, other, view, ow, oh, plain, not);
  int cols = 0, colsOther = 0;
  const long apart = d1 && d2 ? Apart(eye, ref, view.dst_width, view.dst_height, &cols) : -1;
  const long other_ = d1 && d3 ? Apart(eye, not, view.dst_width, view.dst_height, &colsOther) : -1;
  char line[512];
  snprintf(line, sizeof(line),
           "%s: drawn into %dx%d at %d,%d of %dx%d: %ld values apart from the picture in 2D at that place (%d "
           "columns); %ld apart from the other view",
           what, view.dst_width, view.dst_height, view.dst_x, view.dst_y, ow, oh, apart, cols, other_);
  Check(d1 && d2 && d3 && apart == 0 && other_ > (long)(n / 10), line);
  free(eye);
  free(ref);
  free(not);
}

int main(int argc, char** argv)
{
  if (argc != 6)
    return 2;
  size_t metaBytes = 0, sz[4] = {0};
  void* meta = ReadAll(argv[1], &metaBytes);
  uint16_t* in[4];
  for (int i = 0; i < 4; ++i)
    in[i] = ReadAll(argv[2 + i], &sz[i]);
  for (int i = 0; i < 4; ++i)
    if (!in[i] || sz[i] != N * 3)
    {
      fprintf(stderr, "inputs\n");
      return 1;
    }
  if (!meta)
    return 1;

  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  EGLConfig cfg;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  Check(dvbridge_gl_part_available(), "the copy of a part (glCopyImageSubData) is available");

  struct gl g = {0};
  g.log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  g.gl = pl_opengl_create(g.log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                  .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  g.r = g.gl ? dvbridge_renderer_create_hdr10(g.gl->gpu, g.log) : NULL;
  g.meta = meta;
  g.bytes = metaBytes;
  g.plain = (struct dvbridge_plain){.hlg = false, .full_range = false, .max_luma = 4000.0f, .min_luma = 0.005f,
                                    .max_cll = 4000.0f, .max_fall = 400.0f};
  Check(g.gl && g.r, "libplacebo and the renderer");
  if (!g.gl || !g.r)
    return 1;

  // --- the pictures: two of profile 5, two of HDR10, alone and packed
  struct pic a = Upload(in[0], W, H), b = Upload(in[1], W, H);
  uint16_t* sbsData = Pack(in[0], in[1], W, H, 1);
  uint16_t* tabData = Pack(in[0], in[1], W, H, 0);
  struct pic sbs = Upload(sbsData, 2 * W, H), tab = Upload(tabData, W, 2 * H);
  struct pic ha = Upload(in[2], W, H), hb = Upload(in[3], W, H);
  uint16_t* hsbsData = Pack(in[2], in[3], W, H, 1);
  struct pic hsbs = Upload(hsbsData, 2 * W, H);
  Check(glGetError() == GL_NO_ERROR, "the pictures uploaded (a picture of two 3840x2160 views: 7680x2160)");
  struct dvbridge_gl_part part[2] = {{{0}}};

  // --- Dolby Vision rebuilt as HDR10
  Check(dvbridge_render_hdr10_mapping(g.r, NULL), "no mapping: rebuilt as HDR10");
  // side by side (half) at 2160p: each eye 1920x2160, the right one at x 1920
  const struct dvbridge_view left = {0, 0, W, H, 0, 0, W / 2, H};
  const struct dvbridge_view right = {0, 0, W, H, W / 2, 0, W / 2, H};
  Eye(&g, &part[0], &sbs, &a, &b, 0, 0, left, W, H, 0, "profile 5 rebuilt as HDR10, side by side (half), left eye");
  Eye(&g, &part[1], &sbs, &b, &a, W, 0, right, W, H, 0, "profile 5 rebuilt as HDR10, side by side (half), right eye");
  // frame packing at 1080p: the eyes 1920x1080, the right one below 45 lines (1920x2205), from views one above the other
  const struct dvbridge_view top = {0, 0, W, H, 0, 0, 1920, 1080};
  const struct dvbridge_view bottom = {0, 0, W, H, 0, 1125, 1920, 1080};
  Eye(&g, &part[0], &tab, &a, &b, 0, 0, top, 1920, 2205, 0, "profile 5 rebuilt as HDR10, frame packing, left eye");
  Eye(&g, &part[1], &tab, &b, &a, 0, H, bottom, 1920, 2205, 0, "profile 5 rebuilt as HDR10, frame packing, right eye");
  // a letterboxed eye (a 2.39:1 film within its eye): the picture's place, not the eye's
  const struct dvbridge_view box = {0, 276, W, H - 276, W / 2, 230, W / 2, 1700};
  Eye(&g, &part[1], &sbs, &b, &a, W, 0, box, W, H, 0,
      "profile 5 rebuilt as HDR10, side by side (half), right eye of a cropped picture in its place");

  // --- mapped to SDR in BT.2020
  struct dvbridge_hdr10_mapping m2020 = dvbridge_sdr_mapping(NULL);
  m2020.gamut = DVBRIDGE_HDR10_GAMUT_BT2020;
  Check(dvbridge_render_hdr10_mapping(g.r, &m2020), "a mapping to SDR in BT.2020");
  Eye(&g, &part[0], &sbs, &a, &b, 0, 0, left, W, H, 0, "profile 5 mapped to SDR in BT.2020, left eye");
  Eye(&g, &part[1], &sbs, &b, &a, W, 0, right, W, H, 0, "profile 5 mapped to SDR in BT.2020, right eye");

  // --- HDR10 mapped to SDR in BT.709
  const struct dvbridge_hdr10_mapping m709 = dvbridge_sdr_mapping(NULL);
  Check(dvbridge_render_hdr10_mapping(g.r, &m709), "a mapping to SDR in BT.709");
  Eye(&g, &part[0], &hsbs, &ha, &hb, 0, 0, left, W, H, 1, "HDR10 mapped to SDR in BT.709, left eye");
  Eye(&g, &part[1], &hsbs, &hb, &ha, W, 0, right, W, H, 1, "HDR10 mapped to SDR in BT.709, right eye");

  // --- what the copy refuses, and what the bridge refuses whole
  {
    struct dvbridge_gl_plane all[3], mine[3];
    Planes(&sbs, all);
    struct dvbridge_gl_part odd = {{0}};
    Check(!dvbridge_gl_part_copy(&odd, DVBRIDGE_YUV420P10, sbs.w, sbs.h, all, 1, 0, W, H, mine) &&
              !dvbridge_gl_part_copy(&odd, DVBRIDGE_YUV420P10, sbs.w, sbs.h, all, W, 0, W + 2, H, mine) &&
              !dvbridge_gl_part_copy(&odd, DVBRIDGE_YUV420P10, sbs.w, sbs.h, all, 0, 0, W - 1, H, mine),
          "the copy refuses half a chroma sample and a part past the picture");
    dvbridge_gl_part_free(&odd);
    struct dvbridge_gl_frame f = {0};
    const int whole = dvbridge_gl_frame_import(&f, g.gl->gpu, DVBRIDGE_YUV420P10, sbs.w, sbs.h, all, AVCHROMA_LOC_LEFT);
    if (whole)
      dvbridge_gl_frame_release(&f);
    Check(!whole, "a picture of two 3840x2160 views is wider than the bridge takes in: the views are copied out");
  }

  // --- the rule before: a view drawn as a crop of the picture of two (views of 1920x1080, which fit whole)
  {
    uint16_t* sa = Cut(in[0], W, H, 960, 540, 1920, 1080);
    uint16_t* sb = Cut(in[1], W, H, 960, 540, 1920, 1080);
    uint16_t* sp = Pack(sa, sb, 1920, 1080, 1);
    struct pic small = Upload(sa, 1920, 1080), smallB = Upload(sb, 1920, 1080), packed = Upload(sp, 3840, 1080);
    Check(dvbridge_render_hdr10_mapping(g.r, NULL), "no mapping, again");
    // the left eye of side by side (half) at 1080p: 960x1080
    const struct dvbridge_view eye = {0, 0, 1920, 1080, 0, 0, 960, 1080};
    const struct dvbridge_view cropped = {0, 0, 1920, 1080, 0, 0, 960, 1080};
    const size_t n2 = (size_t)960 * 1080 * 4;
    float* ref = malloc(n2 * sizeof(float));
    float* crop = malloc(n2 * sizeof(float));
    int cols = 0;
    const int d1 = DrawWhole(&g, &small, eye, 1920, 1080, 0, ref);
    const int d2 = DrawWhole(&g, &packed, cropped, 1920, 1080, 0, crop);
    const long apart = d1 && d2 ? Apart(crop, ref, 960, 1080, &cols) : -1;
    char line[400];
    snprintf(line, sizeof(line),
             "the rule before (the left view drawn as a crop of a 3840x1080 picture of two): %ld values apart from the "
             "picture in 2D, in %d of its 960 columns (the other view read across the seam)",
             apart, cols);
    Check(d1 && d2 && apart > 0, line);
    Eye(&g, &part[0], &packed, &small, &smallB, 0, 0, eye, 1920, 1080, 0,
        "... and copied out: the left view of the same picture");
    free(ref);
    free(crop);
    free(sa);
    free(sb);
    free(sp);
  }

  dvbridge_gl_part_free(&part[0]);
  dvbridge_gl_part_free(&part[1]);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
