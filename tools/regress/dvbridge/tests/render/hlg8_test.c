// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): an 8-bit HLG picture sent as Dolby Vision
// (dvbridge_gl_widen.h, DVBRIDGE_BASE_HLG_TO_PQ) on the software OpenGL ES (zink on lavapipe), with the bridge of a
// tree and the libplacebo of the build. The same picture is given to the bridge twice: as the 8-bit NV12 planes of a
// VAAPI surface, widened by dvbridge_gl_widen_nv12(), and as the P010 planes of the same picture in 10 bits (each code
// v as v << 2, at the top of 16 bits: (v << 2) << 6). Everything after the planes must be the same bytes:
//
//   - the widened planes hold exactly 256 v for every sample (checked against the codes, not against the other side),
//     on a chart of HLG brought to 8 bits and on a picture that has every code in each plane;
//   - the picture drawn (HLG converted to ST 2084, the intermediate picture of the transport, floats) and the
//     transport packed with its metadata (RGBA8, the bytes that go out), frame after frame, by two renderers that
//     were made alike: one given the widened planes, the other the P010 ones;
//   - the measurement of its metadata (dvbridge_measure_submit_ex(), hlg), every value;
//   - the GL state of the caller (framebuffers, program, viewport, blending, scissor, dithering) kept by the pass;
//   - the rule before: the NV12 planes given as they are, which the bridge refuses (a base layer of 8 bits where its
//     RPU says 10).
//
// Usage: hlg8_test <HLG chart, 3840x2160 yuv420p10le, limited range>
// Prints "ok:"/"FAIL:" lines and "PASSED|FAILED: <n> failed".
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_gl_widen.h"
#include "dvbridge_measure.h"
#include "dvbridge_measured.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"

#include "pack_sources.h"

#define W 3840
#define H 2160
#define CW (W / 2)
#define CH (H / 2)

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

// ------------------------------------------------------------------------------------------------- the pictures
// an 8-bit 4:2:0 picture: its luma and its two chroma planes
struct pic8
{
  uint8_t* y;
  uint8_t* u;
  uint8_t* v;
};

static GLuint Texture(GLenum iformat, GLenum format, GLenum type, int w, int h, const void* data, int align)
{
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, align);
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint)iformat, w, h, 0, format, type, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return t;
}

// the planes of a VAAPI NV12 surface (R8, RG8) and of the same picture as a P010 one (R16, RG16, (v << 2) << 6)
static void Upload(const struct pic8* p, struct dvbridge_gl_plane nv12[3], struct dvbridge_gl_plane p010[3])
{
  uint8_t* uv8 = malloc((size_t)CW * CH * 2);
  uint16_t* y16 = malloc((size_t)W * H * 2);
  uint16_t* uv16 = malloc((size_t)CW * CH * 4);
  for (size_t i = 0; i < (size_t)CW * CH; ++i)
  {
    uv8[2 * i] = p->u[i];
    uv8[2 * i + 1] = p->v[i];
    uv16[2 * i] = (uint16_t)((p->u[i] << 2) << 6);
    uv16[2 * i + 1] = (uint16_t)((p->v[i] << 2) << 6);
  }
  for (size_t i = 0; i < (size_t)W * H; ++i)
    y16[i] = (uint16_t)((p->y[i] << 2) << 6);
  memset(nv12, 0, 3 * sizeof(*nv12));
  memset(p010, 0, 3 * sizeof(*p010));
  nv12[0] = (struct dvbridge_gl_plane){Texture(GL_R8, GL_RED, GL_UNSIGNED_BYTE, W, H, p->y, 1), W, H};
  nv12[1] = (struct dvbridge_gl_plane){Texture(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, CW, CH, uv8, 2), CW, CH};
  p010[0] = (struct dvbridge_gl_plane){Texture(0x822A, GL_RED, GL_UNSIGNED_SHORT, W, H, y16, 2), W, H};
  p010[1] = (struct dvbridge_gl_plane){Texture(0x822C, GL_RG, GL_UNSIGNED_SHORT, CW, CH, uv16, 4), CW, CH};
  free(uv8);
  free(y16);
  free(uv16);
}

// a 16-bit texture read back (RGBA unsigned shorts); 0 where it cannot be
static int ReadU16(GLuint texture, int w, int h, uint16_t* out)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  while (glGetError() != GL_NO_ERROR)
    ;
  glPixelStorei(GL_PACK_ALIGNMENT, 2);
  if (ok)
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT, out);
  ok = ok && glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  return ok;
}

// samples of a widened plane that are not 256 v (components: 1 luma, 2 chroma)
static long WidenedWrong(const struct dvbridge_gl_plane* plane, const uint8_t* a, const uint8_t* b, int w, int h)
{
  uint16_t* got = malloc((size_t)w * h * 8);
  if (!ReadU16(plane->texture, w, h, got))
  {
    free(got);
    return -1;
  }
  long wrong = 0;
  for (size_t i = 0; i < (size_t)w * h; ++i)
  {
    wrong += got[4 * i] != (uint16_t)(a[i] << 8);
    if (b)
      wrong += got[4 * i + 1] != (uint16_t)(b[i] << 8);
  }
  free(got);
  return wrong;
}

// ------------------------------------------------------------------------------------------------- drawing
struct gl
{
  pl_opengl gl;
  pl_log log;
  struct dvbridge_gl_packer* packer;
  GLuint fbo, color;
  float* floats[2];
  uint8_t* bytes[2];
};

static int ReadFloat(pl_tex tex, pl_gpu gpu, float* out)
{
  const GLuint t = tex ? pl_opengl_unwrap(gpu, tex, NULL, NULL, NULL) : 0;
  if (!t)
    return 0;
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
  while (glGetError() != GL_NO_ERROR)
    ;
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out);
  const int ok = glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  return ok;
}

// one picture drawn by a renderer from planes of a layout: the intermediate picture (floats) and the transport
// packed with its metadata (RGBA8) read back into slot s; 0 where it was not drawn
static int DrawOne(struct gl* g, struct dvbridge_renderer* r, enum dvbridge_gl_layout layout,
                   const struct dvbridge_gl_plane planes[3], const void* meta, size_t bytes, double pts, int s,
                   const char** notDrawn)
{
  struct dvbridge_gl_frame f = {0};
  *notDrawn = NULL;
  if (!dvbridge_gl_frame_import(&f, g->gl->gpu, layout, W, H, planes, AVCHROMA_LOC_LEFT))
  {
    *notDrawn = "not imported";
    return 0;
  }
  const struct dvbridge_geometry geometry = {W, H, 0, 0, W, H, {0}};
  int ok = dvbridge_render_rgb(r, &f.frame, meta, bytes, pts, NAN, geometry);
  if (!ok)
    *notDrawn = dvbridge_render_not_drawn(r);
  ok = ok && ReadFloat(dvbridge_render_texture(r), g->gl->gpu, g->floats[s]);
  ok = ok && dvbridge_render_pack(r, g->packer, g->fbo, W, H, true, DVBRIDGE_COVER_NONE);
  if (ok)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g->bytes[s]);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ok = glGetError() == GL_NO_ERROR;
  }
  if (ok)
    dvbridge_render_commit(r);
  dvbridge_gl_frame_release(&f);
  return ok;
}

// ------------------------------------------------------------------------------------------------- measuring
struct measured
{
  int done;
  int blocks_w, blocks_h, window[2];
  float* rows;
  float* columns;
  size_t nrows, ncolumns;
};

static void Measured(const struct dvbridge_measured_raw* raw, uint64_t tag, void* context)
{
  struct measured* m = (struct measured*)context + (tag - 1);
  m->blocks_w = raw->blocks_w;
  m->blocks_h = raw->blocks_h;
  m->window[0] = raw->window[0];
  m->window[1] = raw->window[1];
  m->nrows = (size_t)raw->blocks_h * DVBRIDGE_MEASURED_BANDS * 4;
  m->ncolumns = (size_t)raw->blocks_w * DVBRIDGE_MEASURED_COLUMN_BANDS;
  m->rows = malloc(m->nrows * sizeof(float));
  m->columns = malloc(m->ncolumns * sizeof(float));
  memcpy(m->rows, raw->rows, m->nrows * sizeof(float));
  memcpy(m->columns, raw->columns, m->ncolumns * sizeof(float));
  m->done = 1;
}

// ------------------------------------------------------------------------------------------------- one picture
static void OnePicture(struct gl* g, struct dvbridge_renderer* wide, struct dvbridge_renderer* ref,
                       struct dvbridge_gl_widen* widen, struct dvbridge_gl_widened* widened, const struct pic8* p,
                       const void* meta, size_t bytes, double pts, const char* name)
{
  char line[512];
  struct dvbridge_gl_plane nv12[3], p010[3], out[3];
  Upload(p, nv12, p010);

  // the widening, and the GL state of the caller kept
  GLuint other = 0;
  glGenFramebuffers(1, &other);
  glBindFramebuffer(GL_FRAMEBUFFER, other);
  glViewport(1, 2, 3, 4);
  glEnable(GL_BLEND);
  glEnable(GL_SCISSOR_TEST);
  glEnable(GL_DITHER);
  glActiveTexture(GL_TEXTURE3);
  const int widenedOk = dvbridge_gl_widen_nv12(widen, widened, W, H, nv12, out);
  GLint fb = 0, vp[4] = {0}, active = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
  glGetIntegerv(GL_VIEWPORT, vp);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  const int kept = fb == (GLint)other && vp[0] == 1 && vp[1] == 2 && vp[2] == 3 && vp[3] == 4 &&
                   glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_DITHER) &&
                   active == GL_TEXTURE3;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &other);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);
  snprintf(line, sizeof line, "%s: widened, and the GL state of the caller (framebuffer, viewport, blending, "
                              "scissor, dithering, texture unit) kept", name);
  Check(widenedOk && kept, line);

  const long wy = widenedOk ? WidenedWrong(&out[0], p->y, NULL, W, H) : -1;
  const long wc = widenedOk ? WidenedWrong(&out[1], p->u, p->v, CW, CH) : -1;
  snprintf(line, sizeof line,
           "%s: every widened sample is 256 v ((v << 2) << 6): %ld of %ld luma and %ld of %ld chroma samples are "
           "not",
           name, wy, (long)W * H, wc, (long)CW * CH * 2);
  Check(wy == 0 && wc == 0, line);

  // the same picture drawn from the widened planes and from the P010 ones
  const char *whyA = NULL, *whyB = NULL;
  const int a = widenedOk && DrawOne(g, wide, DVBRIDGE_P010, out, meta, bytes, pts, 0, &whyA);
  const int b = DrawOne(g, ref, DVBRIDGE_P010, p010, meta, bytes, pts, 1, &whyB);
  long fd = -1, bd = -1;
  if (a && b)
  {
    fd = bd = 0;
    for (size_t i = 0; i < (size_t)W * H * 4; ++i)
    {
      fd += memcmp(&g->floats[0][i], &g->floats[1][i], sizeof(float)) != 0;
      bd += g->bytes[0][i] != g->bytes[1][i];
    }
  }
  snprintf(line, sizeof line,
           "%s: drawn from the widened planes and from the P010 ones (%s / %s): %ld of %ld values of the picture "
           "converted to ST 2084 differ",
           name, a ? "drawn" : (whyA ? whyA : "not drawn"), b ? "drawn" : (whyB ? whyB : "not drawn"), fd,
           (long)W * H * 4);
  Check(a && b && fd == 0, line);
  snprintf(line, sizeof line, "%s: ... and %ld of %ld bytes of the transport packed with its metadata differ",
           name, bd, (long)W * H * 4);
  Check(a && b && bd == 0, line);

  // the measurement of the metadata, from each
  struct dvbridge_measure* m = dvbridge_measure_create();
  struct measured got[2] = {{0}};
  const int window[2] = {0, CW};
  int queued = m && widenedOk &&
               dvbridge_measure_submit_ex(m, out[0].texture, out[1].texture, W, H, window, pts, 1, true) &&
               dvbridge_measure_submit_ex(m, p010[0].texture, p010[1].texture, W, H, window, pts, 2, true);
  for (int i = 0; queued && i < 200 && !(got[0].done && got[1].done); ++i)
  {
    glFinish();
    dvbridge_measure_collect(m, Measured, got);
  }
  const int same = got[0].done && got[1].done && got[0].nrows == got[1].nrows &&
                   got[0].ncolumns == got[1].ncolumns &&
                   !memcmp(got[0].rows, got[1].rows, got[0].nrows * sizeof(float)) &&
                   !memcmp(got[0].columns, got[1].columns, got[0].ncolumns * sizeof(float));
  snprintf(line, sizeof line,
           "%s: the measurement of its metadata (HLG to ST 2084, %d x %d blocks) the same value for value from the "
           "widened planes and from the P010 ones",
           name, got[1].blocks_w, got[1].blocks_h);
  Check(queued && same, line);
  for (int i = 0; i < 2; ++i)
  {
    free(got[i].rows);
    free(got[i].columns);
  }
  if (m)
    dvbridge_measure_destroy(m);

  for (int i = 0; i < 2; ++i)
  {
    glDeleteTextures(1, &nv12[i].texture);
    glDeleteTextures(1, &p010[i].texture);
  }
}

int main(int argc, char** argv)
{
  if (argc != 2)
    return 2;
  FILE* f = fopen(argv[1], "rb");
  const size_t n10 = (size_t)W * H * 3 / 2;
  uint16_t* chart = malloc(n10 * 2);
  if (!f || !chart || fread(chart, 2, n10, f) != n10)
  {
    fprintf(stderr, "input\n");
    return 1;
  }
  fclose(f);

  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, nc = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLConfig cfg;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &nc) || !nc)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  printf("renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  struct gl g = {0};
  g.log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  g.gl = pl_opengl_create(g.log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                  .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  if (!g.gl)
    return 1;
  g.packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  glGenTextures(1, &g.color);
  glBindTexture(GL_TEXTURE_2D, g.color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &g.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g.color, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  for (int i = 0; i < 2; ++i)
  {
    g.floats[i] = malloc((size_t)W * H * 16);
    g.bytes[i] = malloc((size_t)W * H * 4);
  }
  // two renderers made alike: one is given the widened planes, the other the P010 ones (each keeps its own
  // metadata serializer, so that both prepare the same frames in the same order)
  struct dvbridge_renderer* wide = dvbridge_renderer_create(g.gl->gpu, g.log);
  struct dvbridge_renderer* ref = dvbridge_renderer_create(g.gl->gpu, g.log);
  struct dvbridge_renderer* before = dvbridge_renderer_create(g.gl->gpu, g.log);
  Check(complete && g.packer && wide && ref && before, "libplacebo, the renderers and the packer");
  if (!complete || !g.packer || !wide || !ref || !before)
    return 1;
  dvbridge_render_set_base_transfer(wide, DVBRIDGE_BASE_HLG_TO_PQ);
  dvbridge_render_set_base_transfer(ref, DVBRIDGE_BASE_HLG_TO_PQ);
  dvbridge_render_set_base_transfer(before, DVBRIDGE_BASE_HLG_TO_PQ);
  size_t bytes = 0;
  struct AVDOVIMetadata* meta = dvbridge_measured_placeholder_hlg(&bytes);
  Check(meta && bytes, "the metadata of an HLG picture sent as Dolby Vision (profile 8.1, a base layer of 10 bits)");
  if (!meta)
    return 1;

  // the chart brought to 8 bits (each code the nearest 8-bit one), and a picture with every code in each plane
  struct pic8 c8 = {malloc((size_t)W * H), malloc((size_t)CW * CH), malloc((size_t)CW * CH)};
  struct pic8 all = {malloc((size_t)W * H), malloc((size_t)CW * CH), malloc((size_t)CW * CH)};
  for (size_t i = 0; i < (size_t)W * H; ++i)
  {
    const unsigned v = (chart[i] + 2u) >> 2;
    c8.y[i] = (uint8_t)(v > 255 ? 255 : v);
    all.y[i] = (uint8_t)((i % W + 3 * (i / W)) & 255);
  }
  for (size_t i = 0; i < (size_t)CW * CH; ++i)
  {
    const unsigned u = (chart[(size_t)W * H + i] + 2u) >> 2, v = (chart[(size_t)W * H * 5 / 4 + i] + 2u) >> 2;
    c8.u[i] = (uint8_t)(u > 255 ? 255 : u);
    c8.v[i] = (uint8_t)(v > 255 ? 255 : v);
    all.u[i] = (uint8_t)((7 * (i % CW) + i / CW) & 255);
    all.v[i] = (uint8_t)((i % CW + 5 * (i / CW)) & 255);
  }

  struct dvbridge_gl_widen widen = {0};
  struct dvbridge_gl_widened widened = {{0}};
  OnePicture(&g, wide, ref, &widen, &widened, &c8, meta, bytes, 0.0, "the HLG chart in 8 bits");
  OnePicture(&g, wide, ref, &widen, &widened, &all, meta, bytes, 1.0 / 24.0, "every code in each plane");

  // the rule before: the NV12 planes given as they are
  {
    struct dvbridge_gl_plane nv12[3], p010[3];
    Upload(&c8, nv12, p010);
    const char* why = NULL;
    const int drawn = DrawOne(&g, before, DVBRIDGE_NV12, nv12, meta, bytes, 2.0 / 24.0, 0, &why);
    char line[400];
    snprintf(line, sizeof line, "the rule before: the 8-bit planes given as they are are not drawn (%s)",
             drawn ? "drawn" : why ? why : "-");
    Check(!drawn && why && strstr(why, "bit depth"), line);
    for (int i = 0; i < 2; ++i)
    {
      glDeleteTextures(1, &nv12[i].texture);
      glDeleteTextures(1, &p010[i].texture);
    }
  }

  dvbridge_gl_widened_free(&widened);
  dvbridge_gl_widen_free(&widen);
  av_free(meta);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
