// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): the geometry bench.
//
// Draws a picture through Kodi's Dolby Vision bridge (tools/dvbridge of the tree under test) on a software OpenGL ES
// (zink on lavapipe) at a given place of the 3840x2160 raster, and writes what the bridge sends, so that the
// transports of a scaled picture can be compared with references outside the GPU (geom_measure.py).
//   <out>/<tag>-direct.rgba    the transport of the direct way (RGBA8 3840x2160, rows as read back)
//   <out>/<tag>-composed.rgba  the transport packed from the composed way (dvbridge_render_pack())
//   <out>/<tag>-ccm_{y,cb,cr}.u16  with the source transport: the composed planes (h of CCM 001 5.4.3.3, 4:2:0)
//   <out>/<tag>-info.txt       what the renderer said, and the colour metadata of the RPU (to decode the transports)
// The picture: fid:<dir> (a picture dir of ref_dump: both layers) or raw:<meta.bin>:<base layer yuv420p10le>:<w>:<h>
// :<chroma location> (base layer only). Settings: GB_STEP (1; 2: every second sample of the layers), GB_RECT
// x,y,w,h (the place in the raster; default: the picture's own size at 0,0 with GB_NATIVE=1, else the whole
// raster), GB_SOURCE (1: the source transport asked for), GB_CCM (1), GB_NOEL (0), GB_VIEW x0,y0,x1,y1 (the part of
// the picture shown, where the bridge under test takes it: DVBRIDGE_GEOMETRY_VIEW).
// Built with GB_ORACLE defined, every transport is also read by the independent reader of CCM 001
// (ccm001_oracle.h). Usage: geom_bench <out dir> <tag> <picture>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/pixfmt.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#include "dvbridge_source.h"
#include "layers.h"
#include "upload.h"

#include "pack_sources.h"
#ifdef GB_ORACLE
#include "ccm001_oracle.h"
#endif

#define W 3840
#define H 2160

static int envi(const char* name, int fallback)
{
  const char* v = getenv(name);
  return v && *v ? atoi(v) : fallback;
}

static void Log(void* priv, enum pl_log_level level, const char* msg)
{
  (void)priv;
  if (level <= PL_LOG_WARN)
    fprintf(stderr, "libplacebo[%d]: %s\n", level, msg);
}

static int g_failures;

static int WriteFile(const char* dir, const char* tag, const char* what, const void* data, size_t size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s-%s", dir, tag, what);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  const int ok = fwrite(data, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

static int ReadRgba8(GLuint fbo, uint8_t* into)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, into);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return glGetError() == GL_NO_ERROR;
}

static int ReadFloatTexture(GLuint texture, int w, int h, float* into)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  while (glGetError() != GL_NO_ERROR)
    ;
  if (ok)
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, into);
  ok = ok && glGetError() == GL_NO_ERROR;
  // (a 16-bit normalized texture, as the composed planes are with PL_DOVI_CCM_PLANES_UNORM16, is read as unsigned
  // shorts)
  if (!ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
  {
    unsigned short* s = malloc((size_t)w * h * 8);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT, s);
    ok = s && glGetError() == GL_NO_ERROR;
    for (size_t i = 0; ok && i < (size_t)w * h * 4; ++i)
      into[i] = s[i] / 65535.0f;
    free(s);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  while (glGetError() != GL_NO_ERROR)
    ;
  return ok;
}

static void Fill(GLuint fbo, float v)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(v, v, v, v);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// a raw base layer as a fid_picture (no enhancement layer)
static int LoadRaw(const char* spec, struct fid_picture* p)
{
  char buf[2048];
  snprintf(buf, sizeof buf, "%s", spec);
  char* parts[5];
  int n = 0;
  for (char* s = strtok(buf, ":"); s && n < 5; s = strtok(NULL, ":"))
    parts[n++] = s;
  if (n != 5)
    return 0;
  memset(p, 0, sizeof(*p));
  const int w = atoi(parts[2]), h = atoi(parts[3]);
  p->bl_loc = atoi(parts[4]);
  FILE* f = fopen(parts[1], "rb");
  if (!f)
    return 0;
  const size_t cw = (size_t)(w + 1) / 2, ch = (size_t)(h + 1) / 2;
  const size_t counts[3] = {(size_t)w * h, cw * ch, cw * ch};
  int ok = 1;
  for (int c = 0; c < 3; ++c)
  {
    p->bl[c].w = c ? (int)cw : w;
    p->bl[c].h = c ? (int)ch : h;
    p->bl[c].v = malloc(counts[c] * 2);
    ok = ok && p->bl[c].v && fread(p->bl[c].v, 2, counts[c], f) == counts[c];
  }
  fclose(f);
  void* m = NULL;
  f = fopen(parts[0], "rb");
  if (!ok || !f)
    return 0;
  fseek(f, 0, SEEK_END);
  p->meta_size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  m = aligned_alloc(64, (p->meta_size + 63) / 64 * 64);
  ok = m && fread(m, 1, p->meta_size, f) == p->meta_size;
  fclose(f);
  p->meta = m;
  p->bl_pts = p->el_pts = 1000;
  return ok;
}

#ifdef DVBRIDGE_SOURCE_ENTRY
// --- GB_CHECK=1: the scaled picture of the source transport against a reference on the CPU: the composer's codes
// scaled with the tables of the bridge (dvbridge_source_axis(), the weights) but with this file's own integer
// arithmetic, then the transport read back compared code by code in the active picture.
static long long FloorDiv(long long a, long long b)
{
  long long q = a / b;
  if ((a % b) != 0 && ((a < 0) != (b < 0)))
    --q;
  return q;
}

static int Clampi(int v, int lo, int hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

// shift of the chroma planes (libplacebo's, luma samples) for an AVChromaLocation
static void ChromaShift(int loc, double* sx, double* sy)
{
  static const double s[7][2] = {{0, 0}, {-0.5, 0}, {0, 0}, {-0.5, -0.5}, {0, -0.5}, {-0.5, 0.5}, {0, 0.5}};
  *sx = s[loc >= 0 && loc < 7 ? loc : 0][0];
  *sy = s[loc >= 0 && loc < 7 ? loc : 0][1];
}

// one axis: in (n_in samples, stride between them, count lines of them) -> out, by a table of n_out positions
static void ColumnsPass(const int* in, int in_w, int in_h, int comps, const int* table, int n_out, long long* out)
{
  for (int y = 0; y < in_h; ++y)
    for (int o = 0; o < n_out; ++o)
    {
      const int* e = table + (size_t)o * DVBRIDGE_SOURCE_ENTRY;
      for (int c = 0; c < comps; ++c)
      {
        long long acc = 0;
        for (int i = 0; i < e[1]; ++i)
          acc += (long long)e[2 + i] * in[((size_t)y * in_w + Clampi(e[0] + i, 0, in_w - 1)) * comps + c];
        out[((size_t)y * n_out + o) * comps + c] = FloorDiv(acc + (1 << 11), 1 << 12);
      }
    }
}

static int RowsPass(const long long* in, int w, int in_h, int comps, const int* table, int n_out, int* out)
{
  for (int o = 0; o < n_out; ++o)
  {
    const int* e = table + (size_t)o * DVBRIDGE_SOURCE_ENTRY;
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < comps; ++c)
      {
        long long acc = 0;
        for (int i = 0; i < e[1]; ++i)
          acc += (long long)e[2 + i] * in[((size_t)Clampi(e[0] + i, 0, in_h - 1) * w + x) * comps + c];
        out[((size_t)o * w + x) * comps + c] = Clampi((int)FloorDiv(acc + (1 << 15), 1 << 16), 0, 4095);
      }
  }
  return 1;
}

static long ScaleCheck(const char* what, const uint8_t* transport, const uint16_t* cy, const uint16_t* ccb,
                       const uint16_t* ccr, int sw, int sh, const float view[4], int x, int y, int w, int h, int loc,
                       const unsigned margins[4], unsigned count, char* text, size_t room)
{
  const int scw = (sw + 1) / 2, sch = (sh + 1) / 2;
  double shx, shy;
  ChromaShift(loc, &shx, &shy);
  int* t[4];
  const int n[4] = {w, h, w / 2, h};
  for (int i = 0; i < 4; ++i)
    t[i] = malloc((size_t)n[i] * DVBRIDGE_SOURCE_ENTRY * sizeof(int));
  const double sx = ((double)view[2] - view[0]) / w, sy = ((double)view[3] - view[1]) / h;
  int ok = dvbridge_source_axis(t[0], w, view[0], sx, 1, 0.0, 1, DVBRIDGE_SOURCE_LUMA_LANCZOS) &&
           dvbridge_source_axis(t[1], h, view[1], sy, 1, 0.0, 1, DVBRIDGE_SOURCE_LUMA_LANCZOS) &&
           dvbridge_source_axis(t[2], w / 2, view[0], sx, 2, 0.5 + shx, 2, DVBRIDGE_SOURCE_SCALE_CHROMA_LANCZOS) &&
           dvbridge_source_axis(t[3], h, view[1], sy, 1, 0.5 + shy, 2, DVBRIDGE_SOURCE_SCALE_CHROMA_LANCZOS);
  int* ly = malloc((size_t)sw * sh * sizeof(int));
  int* lc = malloc((size_t)scw * sch * 2 * sizeof(int));
  for (size_t i = 0; i < (size_t)sw * sh; ++i)
    ly[i] = cy[i];
  for (size_t i = 0; i < (size_t)scw * sch; ++i)
    lc[i * 2] = ccb[i], lc[i * 2 + 1] = ccr[i];
  long long* cols = malloc((size_t)w * sh * sizeof(long long));
  long long* ccols = malloc((size_t)(w / 2) * sch * 2 * sizeof(long long));
  int* oy = malloc((size_t)w * h * sizeof(int));
  int* oc = malloc((size_t)(w / 2) * h * 2 * sizeof(int));
  ColumnsPass(ly, sw, sh, 1, t[0], w, cols);
  RowsPass(cols, w, sh, 1, t[1], h, oy);
  ColumnsPass(lc, scw, sch, 2, t[2], w / 2, ccols);
  RowsPass(ccols, w / 2, sch, 2, t[3], h, oc);
  long differ_y = 0, differ_c = 0, compared = 0, largest = 0;
  const int x0 = (int)margins[0], x1 = 3840 - (int)margins[1], y0 = (int)margins[2], y1 = 2160 - (int)margins[3];
  for (int r = y0; ok && r < y1; ++r)
    for (int px = x0; px < x1; ++px)
    {
      if (px < x || px >= x + w || r < y || r >= y + h)
        continue;
      // the transport as read back: rows from the bottom (packed with flip_y)
      const uint8_t* b = transport + ((size_t)(2159 - r) * 3840 + px) * 4;
      const int ty = (b[1] << 4) | (b[2] & 15), tc = (b[0] << 4) | (b[2] >> 4);
      const int ey = oy[(size_t)(r - y) * w + (px - x)];
      const int pair = (px - x) >> 1;
      int ec = oc[((size_t)(r - y) * (w / 2) + pair) * 2 + ((px - x) & 1)];
      // the metadata bit is in the LSB of the chroma of the first packets' pixels
      const int meta = (unsigned)(r * 3840 + px) < count * 3072u;
      const int dc = meta ? ((tc >> 1) != (ec >> 1)) : tc != ec;
      differ_y += ty != ey;
      differ_c += dc;
      if (abs(ty - ey) > largest)
        largest = abs(ty - ey);
      ++compared;
    }
  snprintf(text, room, "%s: scaled against the CPU reference: %ld pixels compared, luma differs in %ld (largest %ld), "
           "chroma in %ld%s\n", what, compared, differ_y, largest, differ_c, ok ? "" : " (TABLES FAILED)");
  for (int i = 0; i < 4; ++i)
    free(t[i]);
  free(ly), free(lc), free(cols), free(ccols), free(oy), free(oc);
  return ok ? differ_y + differ_c : -1;
}

#endif

static int Ints(const char* name, int* out, int n)
{
  const char* v = getenv(name);
  if (!v || !*v)
    return 0;
  return sscanf(v, "%d,%d,%d,%d", &out[0], &out[1], &out[2], &out[3]) == n;
}

static int Floats(const char* name, float* out)
{
  const char* v = getenv(name);
  return v && *v && sscanf(v, "%f,%f,%f,%f", &out[0], &out[1], &out[2], &out[3]) == 4;
}

int main(int argc, char** argv)
{
  if (argc != 4)
    return 2;
  const char* dir = argv[1];
  const char* tag = argv[2];
  const char* spec = argv[3];
  const int step = envi("GB_STEP", 1);

  EGLDisplay dpy = EGL_NO_DISPLAY;
  PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get)
    dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, nc = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLConfig cfg = NULL;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &nc) || !nc)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;

  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = Log, .log_level = PL_LOG_WARN));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char*))eglGetProcAddress,
                                                        .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  if (!gl)
    return 1;
  struct dvbridge_renderer* renderer = dvbridge_renderer_create(gl->gpu, log);
  struct dvbridge_gl_packer* packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
  if (!renderer || !packer)
    return 1;
  dvbridge_render_use_ccm_composer(renderer, envi("GB_CCM", 1) != 0);
  dvbridge_render_use_source_transport(renderer, envi("GB_SOURCE", 1) != 0);

  GLuint color = 0, fbo = 0;
  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  const int complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  pl_tex scanout = pl_opengl_wrap(gl->gpu, pl_opengl_wrap_params(.texture = color, .target = GL_TEXTURE_2D,
                                                                 .iformat = GL_RGBA8, .width = W, .height = H));
  uint8_t* bytes = malloc((size_t)W * H * 4);
  if (!complete || !scanout || !bytes)
    return 1;

  struct fid_picture p;
  const int fid = !strncmp(spec, "fid:", 4);
  if (fid ? !fid_load(spec + 4, &p) : !LoadRaw(spec + 4, &p))
  {
    printf("FAIL: the picture %s not loaded\n", spec);
    return 1;
  }
  struct fid_gl_layer bl = {0}, el = {0};
  const int noel = !fid || envi("GB_NOEL", 0);
  if (!fid_upload(&bl, p.bl, p.bl_loc, step) || (!noel && !fid_upload(&el, p.el, p.el_loc, step)))
  {
    printf("FAIL: not uploaded\n");
    return 1;
  }
  struct dvbridge_gl_plane bp[3] = {{bl.tex[0], bl.w, bl.h}, {bl.tex[1], (bl.w + 1) / 2, (bl.h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_plane ep[3] = {{el.tex[0], el.w, el.h}, {el.tex[1], (el.w + 1) / 2, (el.h + 1) / 2}, {0, 0, 0}};
  struct dvbridge_gl_frame b = {0}, e = {0};
  if (!dvbridge_gl_frame_import(&b, gl->gpu, DVBRIDGE_P010, bl.w, bl.h, bp, (enum AVChromaLocation)bl.loc) ||
      (!noel && !dvbridge_gl_frame_import(&e, gl->gpu, DVBRIDGE_P010, el.w, el.h, ep, (enum AVChromaLocation)el.loc)))
  {
    printf("FAIL: not imported\n");
    return 1;
  }
  b.frame.enhancement_layer = noel ? NULL : &e.frame;
  const double pts = p.bl_pts / 1000.0, elPts = noel ? NAN : pts;
  int rect[4] = {0, 0, W, H};
  if (envi("GB_NATIVE", 0))
    rect[2] = bl.w, rect[3] = bl.h;
  Ints("GB_RECT", rect, 4);
  struct dvbridge_geometry geometry = {bl.w, bl.h, rect[0], rect[1], rect[2], rect[3]};
#ifdef DVBRIDGE_GEOMETRY_VIEW
  float view[4] = {0, 0, (float)bl.w, (float)bl.h};
  if (Floats("GB_VIEW", view))
  {
    geometry.view[0] = view[0];
    geometry.view[1] = view[1];
    geometry.view[2] = view[2];
    geometry.view[3] = view[3];
  }
#endif
  const AVDOVIMetadata* meta = p.meta;
  char text[4096];
  int n = 0;
  n += snprintf(text + n, sizeof text - n, "picture %s step %d: %dx%d at %d,%d %dx%d of the raster\n", spec, step,
                bl.w, bl.h, rect[0], rect[1], rect[2], rect[3]);
  const AVDOVIColorMetadata* c = av_dovi_get_color(meta);
  n += snprintf(text + n, sizeof text - n, "ycc_to_rgb");
  for (int i = 0; i < 9; ++i)
    n += snprintf(text + n, sizeof text - n, " %d/%d", c->ycc_to_rgb_matrix[i].num, c->ycc_to_rgb_matrix[i].den);
  n += snprintf(text + n, sizeof text - n, "\nycc_offset");
  for (int i = 0; i < 3; ++i)
    n += snprintf(text + n, sizeof text - n, " %u/%u", c->ycc_to_rgb_offset[i].num, c->ycc_to_rgb_offset[i].den);
  n += snprintf(text + n, sizeof text - n, "\nrgb_to_lms");
  for (int i = 0; i < 9; ++i)
    n += snprintf(text + n, sizeof text - n, " %d/%d", c->rgb_to_lms_matrix[i].num, c->rgb_to_lms_matrix[i].den);
  n += snprintf(text + n, sizeof text - n, "\nsignal_color_space %d\nchroma_location %d\n", c->signal_color_space, bl.loc);

  // the composed way (and with the source transport the composed planes), then the direct way
  Fill(fbo, 0.5f);
  const int rgb = dvbridge_render_rgb(renderer, &b.frame, meta, p.meta_size, pts, elPts, geometry);
  const char* why = NULL;
  const int source = rgb && dvbridge_render_source(renderer, &why);
  unsigned margins[4] = {0}, count = 0;
  if (rgb)
  {
    const struct dvbridge_candidate* cand = dvbridge_render_candidate(renderer);
    dvbridge_packets(cand, &count);
    dvbridge_active_area(cand, margins);
  }
  const int cw = (bl.w + 1) / 2, ch = (bl.h + 1) / 2;
  uint16_t* cpl[3] = {NULL, NULL, NULL}; // the composed planes, kept for GB_CHECK
  if (source)
  {
    unsigned planes[2] = {0, 0};
    if (dvbridge_render_source_planes(renderer, planes))
    {
      float* buf = malloc((size_t)bl.w * bl.h * 16);
      cpl[0] = malloc((size_t)bl.w * bl.h * 2);
      cpl[1] = malloc((size_t)cw * ch * 2);
      cpl[2] = malloc((size_t)cw * ch * 2);
      long off = 0;
      if (ReadFloatTexture(planes[0], bl.w, bl.h, buf))
      {
        for (size_t i = 0; i < (size_t)bl.w * bl.h; ++i)
        {
          const double hv = buf[i * 4] * 4096.0;
          const long r = lround(hv);
          off += fabs(hv - r) > 1e-3;
          cpl[0][i] = (uint16_t)(r < 0 ? 0 : r > 65535 ? 65535 : r);
        }
        WriteFile(dir, tag, "ccm_y.u16", cpl[0], (size_t)bl.w * bl.h * 2);
      }
      if (ReadFloatTexture(planes[1], cw, ch, buf))
        for (int k = 0; k < 2; ++k)
        {
          for (size_t i = 0; i < (size_t)cw * ch; ++i)
          {
            const double hv = buf[i * 4 + k] * 4096.0;
            const long r = lround(hv);
            off += fabs(hv - r) > 1e-3;
            cpl[1 + k][i] = (uint16_t)(r < 0 ? 0 : r > 65535 ? 65535 : r);
          }
          WriteFile(dir, tag, k ? "ccm_cr.u16" : "ccm_cb.u16", cpl[1 + k], (size_t)cw * ch * 2);
        }
      n += snprintf(text + n, sizeof text - n, "composed planes written; samples not on a whole code %ld\n", off);
      free(buf);
    }
  }
  uint8_t* composedBytes = malloc((size_t)W * H * 4);
  int composedOk = 0;
  if (rgb)
  {
    Fill(fbo, 0.25f);
    if (dvbridge_render_pack(renderer, packer, fbo, W, H, true, DVBRIDGE_COVER_NONE) && ReadRgba8(fbo, composedBytes))
      composedOk = WriteFile(dir, tag, "composed.rgba", composedBytes, (size_t)W * H * 4);
    else
      ++g_failures, n += snprintf(text + n, sizeof text - n, "FAIL: the composed way not packed\n");
  }
  else
    ++g_failures, n += snprintf(text + n, sizeof text - n, "FAIL: dvbridge_render_rgb() failed\n");
  Fill(fbo, 0.75f);
  const int direct = dvbridge_render_packed(renderer, &b.frame, meta, p.meta_size, pts, elPts, geometry, scanout, true) &&
                     ReadRgba8(fbo, bytes);
  const char* dwhy = NULL;
  const int dsource = direct && dvbridge_render_source(renderer, &dwhy);
  if (direct)
    WriteFile(dir, tag, "direct.rgba", bytes, (size_t)W * H * 4);
  else
    ++g_failures, n += snprintf(text + n, sizeof text - n, "FAIL: the direct way not drawn\n");
  // GB_REPEAT=N: the direct way drawn N more times, each waited for (glFinish), its wall time (on a software
  // GL: the CPU's time to emulate the GPU, comparable only between runs on the same machine and load)
  const int repeat = envi("GB_REPEAT", 0);
  if (direct && repeat > 0)
  {
    double sum = 0, least = 1e30;
    for (int i = 0; i < repeat; ++i)
    {
      struct timespec t0, t1;
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &t0);
      const int ok = dvbridge_render_packed(renderer, &b.frame, meta, p.meta_size, pts, elPts, geometry, scanout, true);
      glFinish();
      clock_gettime(CLOCK_MONOTONIC, &t1);
      const double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
      if (!ok)
        ++g_failures;
      sum += ms;
      if (ms < least)
        least = ms;
    }
    n += snprintf(text + n, sizeof text - n, "time of the direct way: %d draws, mean %.1f ms, least %.1f ms\n", repeat,
                  sum / repeat, least);
  }
#ifdef GB_ORACLE
  // every transport through the independent reader of CCM 001 (ccm001_oracle.h)
  for (int k = 0; k < 2; ++k)
  {
    const uint8_t* tb = k ? composedBytes : bytes;
    if (!(k ? composedOk : direct))
      continue;
    struct ccm_oracle_opts o;
    ccm_oracle_defaults(&o);
    o.depth = 12;
    static struct ccm_oracle_result res;
    const int ok = ccm_oracle_read(tb, &o, &res);
    char line[512];
    ccm_oracle_describe(&res, line, sizeof line);
    n += snprintf(text + n, sizeof text - n, "%s: the CCM 001 reader: %s: %s\n", k ? "composed" : "direct",
                  ok ? "as the clause says" : "FAIL", line);
    if (!ok)
      ++g_failures;
  }
#endif
  float wv[4] = {0, 0, (float)bl.w, (float)bl.h};
  const int viewed = Floats("GB_VIEW", wv) && (wv[0] != 0 || wv[1] != 0 || wv[2] != bl.w || wv[3] != bl.h);
  const int isScaled = rect[2] != bl.w || rect[3] != bl.h || viewed;
#ifdef DVBRIDGE_SOURCE_ENTRY
  if (envi("GB_CHECK", 0) && isScaled)
  {
    // (the view asked for, also where the bridge under test has none: then it must fail)
    float v[4] = {0, 0, (float)bl.w, (float)bl.h};
    Floats("GB_VIEW", v);
    if (!source || !dsource || !cpl[0] || !cpl[1] || !cpl[2] || !composedOk)
    {
      ++g_failures;
      n += snprintf(text + n, sizeof text - n, "FAIL: GB_CHECK: the scaled picture did not go out in the source transport "
                    "(composed %d, direct %d)\n", source, dsource);
    }
    else
    {
      char line[512];
      const long d1 = ScaleCheck("direct", bytes, cpl[0], cpl[1], cpl[2], bl.w, bl.h, v, rect[0], rect[1], rect[2],
                                 rect[3], bl.loc, margins, count, line, sizeof line);
      n += snprintf(text + n, sizeof text - n, "%s", line);
      const long d2 = ScaleCheck("composed", composedBytes, cpl[0], cpl[1], cpl[2], bl.w, bl.h, v, rect[0], rect[1],
                                 rect[2], rect[3], bl.loc, margins, count, line, sizeof line);
      n += snprintf(text + n, sizeof text - n, "%s", line);
      if (d1 || d2)
        ++g_failures, n += snprintf(text + n, sizeof text - n, "FAIL: GB_CHECK: the transport is not the reference\n");
      else
        n += snprintf(text + n, sizeof text - n, "ok: GB_CHECK: both transports are the CPU reference, code by code\n");
    }
  }
#else
  (void)isScaled;
#endif
  free(composedBytes);
  for (int i = 0; i < 3; ++i)
    free(cpl[i]);
  n += snprintf(text + n, sizeof text - n,
                "composed: source transport %d (%s); direct %d: source transport %d (%s); packets %u; margins %u %u %u %u\n",
                source, why ? why : "-", direct, dsource, dwhy ? dwhy : "-", count, margins[0], margins[1], margins[2],
                margins[3]);
  printf("%s", text);
  WriteFile(dir, tag, "info.txt", text, strlen(text));
  printf("%s %s\n", g_failures ? "GEOMBENCH-FAILED" : "GEOMBENCH-OK", tag);
  fflush(stdout);
  _Exit(g_failures ? 1 : 0);
}
