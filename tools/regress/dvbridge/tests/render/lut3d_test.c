// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image): the 3D LUT of a display (dvbridge_lut.h,
// dvbridge_render_set_lut()) on the software OpenGL ES (zink on lavapipe), with the bridge of a tree and the
// libplacebo of the build. lut3d_check.py makes the LUT files (make) and checks what this writes (check).
//
//   - every LUT file is read as lut3d_check.py made it, or refused (the line "parse <file>: ..."), with grid
//     points of each LUT that is read, by which the orientation of its table is checked, and points of its 1D LUT
//     (a .cube of LUT_1D_SIZE alone or as Resolve's shaper, ArgyllCMS's .cal; the lines "curve ...");
//   - the paths that take the LUT: the PQ chart mapped to SDR (the reference display) and to a projector of
//     150 cd/m2 (HDR10 adapted, with and without the display's black), drawn as it is (clamp_picture()), kept as
//     10-bit codes, and a Dolby Vision picture in Low-Latency (with and without the display's black): written for
//     lut3d_check.py (every third pixel of every third row) with the LUT and without it; the 1D LUTs on SDR (a .cal,
//     a steep curve, one of 5000 points) and Resolve's shaper on the projector;
//   - an identity LUT gives the picture without it: compared here over the whole picture, the floats and the
//     codes of 10 and 12 bits;
//   - what never takes it: native Dolby Vision (TV-led) and HDR over the tunnel, with a LUT set: the same bytes
//     as without, over the whole picture;
//   - a LUT set and then taken away gives the picture of none, to the bit;
//   - a LUT whose shader cannot be made: the picture is drawn without it, and the renderer says so;
//   - the time of a render with the LUT and without it (informative).
//
// Usage: lut3d_test <chart-pq.yuv> <chroma location> <LUT folder> <out folder> [<picture folder of ref_dump>]
// Prints "ok:"/"FAIL:" lines, "parse"/"point"/"time" lines, and "PASSED|FAILED: <n> failed".
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libplacebo/opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_lut.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#include "layers.h"
#include "upload.h"

#define W 3840
#define H 2160
#define N ((size_t)W * H)
#define STRIDE 3

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
  void* p = malloc((size_t)n + 1);
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n)
  {
    free(p);
    p = NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return p;
}

static double NowMs(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static int ReadFloats(GLuint texture, float* into)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, into);
  const int read = glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  while (glGetError() != GL_NO_ERROR)
    ;
  return read;
}

// a picture kept as 10-bit codes (RGB10_A2): read as the codes, made floats code / 1023
static int ReadCodes(GLuint texture, float* into)
{
  GLuint fb = 0;
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  uint32_t* words = malloc(N * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, words);
  const int read = words && glGetError() == GL_NO_ERROR;
  for (size_t i = 0; read && i < N; ++i)
  {
    into[i * 4] = (float)(words[i] & 1023u) / 1023.0f;
    into[i * 4 + 1] = (float)((words[i] >> 10) & 1023u) / 1023.0f;
    into[i * 4 + 2] = (float)((words[i] >> 20) & 1023u) / 1023.0f;
    into[i * 4 + 3] = 1.0f;
  }
  free(words);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fb);
  while (glGetError() != GL_NO_ERROR)
    ;
  return read;
}

// every STRIDE-th pixel of every STRIDE-th row, RGB floats, for lut3d_check.py
static int Write(const char* dir, const char* name, const float* p)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.f32", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f)
    return 0;
  int ok = 1;
  for (size_t y = 0; y < H && ok; y += STRIDE)
    for (size_t x = 0; x < W && ok; x += STRIDE)
      ok = fwrite(p + (y * W + x) * 4, sizeof(float), 3, f) == 3;
  return fclose(f) == 0 && ok;
}

// how two pictures differ over the whole of them: floats that differ, the largest difference, and the codes of
// 10 and of 12 bits that differ (round(v (2^n - 1)), as a copy into a window of that depth makes them)
struct apart
{
  size_t floats, codes10, codes12;
  double most;
};

static struct apart Apart(const float* a, const float* b)
{
  struct apart d = {0};
  for (size_t i = 0; i < N; ++i)
    for (int c = 0; c < 3; ++c)
    {
      const float x = a[i * 4 + c], y = b[i * 4 + c];
      if (x == y)
        continue;
      ++d.floats;
      d.most = fmax(d.most, fabs((double)x - y));
      d.codes10 += floorf(x * 1023.0f + 0.5f) != floorf(y * 1023.0f + 0.5f);
      d.codes12 += floorf(x * 4095.0f + 0.5f) != floorf(y * 4095.0f + 0.5f);
    }
  return d;
}

static int Same(const float* a, const float* b)
{
  return !memcmp(a, b, N * 16);
}

struct gl
{
  pl_opengl gl;
  pl_log log;
};

// ------------------------------------------------------------------------------------------------- the LUTs
struct luts
{
  struct dvbridge_lut *id17, *id33, *id65, *tint, *crlf, *video, *nuke, *unif, *madvr, *madvrpq;
  // the 1D LUTs: ArgyllCMS's .cal (and one loaded in video levels), a .cube of LUT_1D_SIZE alone (steep, 5000 points,
  // the identity), Resolve's shaper before a 3D LUT
  struct dvbridge_lut *cal, *caltv, *steep, *long1d, *id1d, *shaper;
};

static struct dvbridge_lut* Parse(const char* dir, const char* name, struct dvbridge_lut_range range)
{
  char path[1024], why[256];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  size_t size = 0;
  void* bytes = ReadAll(path, &size);
  struct dvbridge_lut* lut = bytes ? dvbridge_lut_parse(bytes, size, name, range, why, sizeof why) : NULL;
  if (!bytes)
    printf("parse %s: no file\n", name);
  else if (!lut)
    printf("parse %s: refused: %s\n", name, why);
  else
  {
    printf("parse %s: read: %s, %d points, input %g-%g, output %g-%g, signal %s, gamut %s, shaper %s %.9g, output "
           "bits %d, title '%s'\n",
           name, dvbridge_lut_format_name(lut->format), lut->size, (double)lut->in.lo, (double)lut->in.hi,
           (double)lut->out.lo, (double)lut->out.hi, dvbridge_lut_signal_name(lut->signal),
           dvbridge_lut_gamut_name(lut->gamut), lut->shaper_two ? "two" : "uniform", (double)lut->shaper_knee,
           lut->out_bits, lut->title);
    // grid points by which lut3d_check.py checks the orientation of the table (red fastest)
    const int n = lut->size;
    const int pts[5][3] = {{0, 0, 0}, {n - 1, n - 1, n - 1}, {1, 0, 0}, {0, 1, n - 1}, {n - 1, n / 2, 1}};
    for (int k = 0; k < 5; ++k)
    {
      float rgb[3];
      dvbridge_lut_point(lut, pts[k][0], pts[k][1], pts[k][2], rgb);
      printf("point %s %d %d %d: %.9g %.9g %.9g\n", name, pts[k][0], pts[k][1], pts[k][2], (double)rgb[0],
             (double)rgb[1], (double)rgb[2]);
    }
    // ... and points of its 1D LUT, each channel its own
    if (lut->curve)
    {
      const int c = lut->curve_size, at[3] = {0, c / 2 + 1, c - 1};
      for (int k = 0; k < 3; ++k)
        printf("curve %s %d of %d: %.9g %.9g %.9g\n", name, at[k], c, (double)lut->curve[at[k] * 3],
               (double)lut->curve[at[k] * 3 + 1], (double)lut->curve[at[k] * 3 + 2]);
    }
  }
  free(bytes);
  return lut;
}

// ------------------------------------------------------------------------------------------------- drawing
static const struct dvbridge_view WHOLE = {0, 0, (float)W, (float)H, 0, 0, W, H};

static int DrawPlain(struct gl* g, struct dvbridge_renderer* r, const struct pl_frame* source,
                     const struct dvbridge_plain* plain, const struct dvbridge_lut* lut, float* out, int codes)
{
  dvbridge_render_set_lut(r, lut);
  if (!dvbridge_render_plain(r, source, plain, WHOLE, W, H))
    return 0;
  pl_tex tex = dvbridge_render_hdr10_texture(r);
  const GLuint t = tex ? pl_opengl_unwrap(g->gl->gpu, tex, NULL, NULL, NULL) : 0;
  return t && (codes ? ReadCodes(t, out) : ReadFloats(t, out));
}

static double TimePlain(struct dvbridge_renderer* r, const struct pl_frame* source, const struct dvbridge_plain* plain,
                        const struct dvbridge_lut* lut)
{
  double best = 1e9;
  dvbridge_render_set_lut(r, lut);
  for (int k = 0; k < 4; ++k)
  {
    glFinish();
    const double t0 = NowMs();
    dvbridge_render_plain(r, source, plain, WHOLE, W, H);
    glFinish();
    best = fmin(best, NowMs() - t0);
  }
  return best;
}

int main(int argc, char** argv)
{
  if (argc != 5 && argc != 6)
    return 2;
  size_t blBytes = 0;
  uint16_t* bl = ReadAll(argv[1], &blBytes);
  const enum AVChromaLocation loc = (enum AVChromaLocation)atoi(argv[2]);
  const char* luts = argv[3];
  const char* out = argv[4];
  const char* pictures = argc == 6 ? argv[5] : NULL;
  if (!bl || blBytes != N * 2 * 3 / 2)
  {
    fprintf(stderr, "inputs\n");
    return 1;
  }

  // --- the LUT files (lut3d_check.py make): read as they are, or refused
  const struct dvbridge_lut_range FULL = {0.0f, 255.0f}, VIDEO = {16.0f, 235.0f};
  struct luts L = {0};
  L.id17 = Parse(luts, "id17.cube", FULL);
  L.id33 = Parse(luts, "id33.cube", FULL);
  L.id65 = Parse(luts, "id65.cube", FULL);
  L.tint = Parse(luts, "tint17.cube", FULL);
  L.crlf = Parse(luts, "tint17-crlf.cube", FULL);
  L.video = Parse(luts, "video17.cube", VIDEO);
  L.nuke = Parse(luts, "nuke17.3dl", FULL);
  L.unif = Parse(luts, "unif9.3dl", FULL);
  L.madvr = Parse(luts, "madvr16.3dlut", FULL);
  L.madvrpq = Parse(luts, "madvrpq8.3dlut", FULL);
  L.cal = Parse(luts, "vcgt256.cal", VIDEO);
  L.caltv = Parse(luts, "vcgt-tv.cal", FULL);
  L.steep = Parse(luts, "steep1d.cube", FULL);
  L.long1d = Parse(luts, "long1d.cube", FULL);
  L.id1d = Parse(luts, "id1d256.cube", FULL);
  L.shaper = Parse(luts, "shaper.cube", FULL);
  static const char* const refused[] = {"bad-1drange.cube", "bad-1dcount.cube", "bad-1d3dcount.cube",
                                        "bad-printer.cal", "bad-cmyk.cal", "bad-fields.cal", "bad-grid.cal",
                                        "bad-header.cal", "bad-value.cal", "bad-domain.cube", "bad-keyword.cube",
                                        "bad-count.cube",
                                        "bad-inputrange.cube", "bad-shaper.3dl", "bad-compressed.3dlut",
                                        "bad-header.3dlut", "empty.cube", "bad-value.cube", "bad-name.txt"};
  int refusedAll = 1;
  for (size_t i = 0; i < sizeof(refused) / sizeof(*refused); ++i)
  {
    struct dvbridge_lut* bad = Parse(luts, refused[i], FULL);
    refusedAll = refusedAll && !bad;
    dvbridge_lut_free(&bad);
  }
  Check(L.id17 && L.id33 && L.id65 && L.tint && L.crlf && L.video && L.nuke && L.unif && L.madvr && L.madvrpq &&
            L.cal && L.caltv && L.steep && L.long1d && L.id1d && L.shaper,
        "every good LUT file read");
  Check(refusedAll, "every file that cannot be read exactly refused, with why");
  if (!L.id17 || !L.id33 || !L.id65 || !L.tint || !L.crlf || !L.video || !L.nuke || !L.unif || !L.madvr ||
      !L.madvrpq || !L.cal || !L.caltv || !L.steep || !L.long1d || !L.id1d || !L.shaper)
  {
    printf("FAILED: %d failed\n", g_failures);
    return 1;
  }
  Check(L.crlf->size == L.tint->size && !memcmp(L.crlf->data, L.tint->data, (size_t)17 * 17 * 17 * 12),
        "a .cube with CRLF, comments, blank lines and indented values: the same table as the plain one");
  Check(L.video->in.lo == 16.0f && L.video->out.hi == 235.0f && !L.video->range_declared,
        "a .cube read in the video levels that the caller says");
  Check(L.madvr->range_declared && L.madvr->in.lo == 16.0f && L.madvr->gamut == DVBRIDGE_LUT_GAMUT_BT709 &&
            L.madvr->signal == DVBRIDGE_LUT_SIGNAL_SDR,
        "madVR: the range, the primaries and the transfer of its parameters (BT.709, video levels, SDR)");
  Check(L.madvrpq->signal == DVBRIDGE_LUT_SIGNAL_PQ && L.madvrpq->gamut == DVBRIDGE_LUT_GAMUT_BT2020 &&
            L.madvrpq->in.lo == 0.0f && L.madvrpq->in.hi == 255.0f,
        "madVR: a LUT of PQ in BT.2020, in full range, said so by its parameters");
  Check(L.nuke->shaper_two && !L.unif->shaper_two && L.nuke->out_bits == 12 && L.unif->out_bits == 10,
        ".3dl: Nuke's shaper of two slopes and a uniform one, the output depths of 12 and 10 bits");
  // the 1D LUTs: alone (the identity of 2 points for the 3D table), or a shaper before one; a .cal in full range
  // whatever the caller says (its values are the device's), the same table with its fields in another order and loaded
  // in video levels
  Check(L.cal->format == DVBRIDGE_LUT_CAL && L.cal->curve_only && L.cal->curve_size == 256 && L.cal->size == 2 &&
            L.cal->range_declared && L.cal->in.lo == 0.0f && L.cal->in.hi == 255.0f && !L.cal->tv_encoding &&
            L.caltv->tv_encoding && L.caltv->curve_size == 256 &&
            !memcmp(L.cal->curve, L.caltv->curve, (size_t)256 * 12),
        ".cal: a 1D LUT alone of 256 points, in full range as its file says, read the same with its fields in another "
        "order and loaded in video levels (TV_OUTPUT_ENCODING)");
  Check(L.steep->curve_only && L.steep->curve_size == 1024 && L.long1d->curve_size == 5000 && L.id1d->curve_only &&
            L.shaper->format == DVBRIDGE_LUT_CUBE && !L.shaper->curve_only && L.shaper->curve_size == 64 &&
            L.shaper->size == 17 && !strcmp(L.steep->title, "a 1D LUT of lut3d_check.py"),
        ".cube: LUT_1D_SIZE alone (1024 and 5000 points, CRLF), and Resolve's shaper of 64 points before a 3D LUT of "
        "17");

  // --- GL
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
  struct dvbridge_renderer* r = g.gl ? dvbridge_renderer_create_hdr10(g.gl->gpu, g.log) : NULL;
  float* off = malloc(N * 16);
  float* with = malloc(N * 16);
  float* again = malloc(N * 16);
  Check(g.gl && r && off && with && again, "libplacebo and the renderer");
  if (!g.gl || !r || !off || !with || !again)
    return 1;
  {
    pl_fmt f32 = pl_find_fmt(g.gl->gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_SAMPLEABLE);
    printf("the GPU: 3D textures up to %u, 32-bit float RGBA sampled: %s\n", g.gl->gpu->limits.max_tex_3d_dim,
           f32 ? f32->name : "none");
  }

  // --- the PQ chart as P010, as Kodi imports a VAAPI surface (as sdrgamut_test.c)
  GLuint tex[2];
  glGenTextures(2, tex);
  {
    uint16_t* y = malloc(N * 2);
    uint16_t* c = malloc(N);
    const uint16_t* u = bl + N;
    const uint16_t* v = u + N / 4;
    for (size_t i = 0; i < N; ++i)
      y[i] = (uint16_t)(bl[i] << 6);
    for (size_t i = 0; i < N / 4; ++i)
    {
      c[i * 2] = (uint16_t)(u[i] << 6);
      c[i * 2 + 1] = (uint16_t)(v[i] << 6);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x822A /* R16 */, W, H, 0, GL_RED, GL_UNSIGNED_SHORT, y);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x822C /* RG16 */, W / 2, H / 2, 0, GL_RG, GL_UNSIGNED_SHORT, c);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(y);
    free(c);
  }
  const struct dvbridge_gl_plane planes[3] = {{tex[0], W, H}, {tex[1], W / 2, H / 2}, {0, 0, 0}};
  struct dvbridge_gl_frame b = {0};
  Check(glGetError() == GL_NO_ERROR && dvbridge_gl_frame_import(&b, g.gl->gpu, DVBRIDGE_P010, W, H, planes, loc),
        "the PQ chart imported (P010)");
  const struct dvbridge_plain plain = {.full_range = false, .max_luma = 4000, .min_luma = 0.005f, .max_cll = 4000,
                                       .max_fall = 400};

  // --- the mappings: SDR (the reference display), a projector of 150 cd/m2 in DCI-P3 (BT.2390), and the same
  // with the display's black (0.05 cd/m2: clamp_to_display() lifts it)
  float xy[8];
  dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, xy);
  const struct dvbridge_hdr10_mapping sdr = dvbridge_sdr_mapping(xy);
  struct dvbridge_hdr10_mapping proj = {0};
  proj.enabled = true;
  proj.peak = 150.0f;
  proj.gamut = DVBRIDGE_HDR10_GAMUT_P3_D65;
  proj.curve = DVBRIDGE_HDR10_CURVE_BT2390;
  proj.source_known = dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, proj.source_xy);
  struct dvbridge_hdr10_mapping projBlack = proj;
  projBlack.black = 0.05f;
  const struct dvbridge_hdr10_mapping none = {0};

  struct way
  {
    const char* name;
    const struct dvbridge_hdr10_mapping* m;
    const struct dvbridge_lut* lut;
    const struct dvbridge_lut* identity;
  };
  // each way: the picture without a LUT, with the identity, with the known LUT (written for lut3d_check.py)
  const struct way ways[] = {
      {"sdr", &sdr, L.tint, L.id33},       {"sdr-video", &sdr, L.video, NULL},  {"sdr-nuke", &sdr, L.nuke, NULL},
      {"sdr-unif", &sdr, L.unif, NULL},    {"sdr-madvr", &sdr, L.madvr, NULL},  {"proj", &proj, L.tint, L.id65},
      {"projblack", &projBlack, L.tint, L.id17}, {"asis", &none, L.tint, L.id33},
      {"sdr-cal", &sdr, L.cal, L.id1d},    {"sdr-steep1d", &sdr, L.steep, NULL}, {"sdr-long1d", &sdr, L.long1d, NULL},
      {"proj-shaper", &proj, L.shaper, NULL},
  };
  for (size_t w = 0; w < sizeof(ways) / sizeof(*ways); ++w)
  {
    const struct way* way = &ways[w];
    char what[256], name[64];
    if (!dvbridge_render_hdr10_mapping(r, way->m))
    {
      snprintf(what, sizeof what, "%s: the mapping taken", way->name);
      Check(0, what);
      continue;
    }
    const int d0 = DrawPlain(&g, r, &b.frame, &plain, NULL, off, 0);
    const int applied0 = dvbridge_render_lut_applied(r);
    const int d1 = DrawPlain(&g, r, &b.frame, &plain, way->lut, with, 0);
    const int applied1 = dvbridge_render_lut_applied(r);
    snprintf(what, sizeof what, "%s: drawn without a LUT (applied %d) and with %s (applied %d)", way->name, applied0,
             dvbridge_lut_format_name(way->lut->format), applied1);
    Check(d0 && d1 && !applied0 && applied1 && !dvbridge_render_lut_failed(r), what);
    if (!d0 || !d1)
      continue;
    snprintf(name, sizeof name, "%s-off", way->name);
    const int w0 = Write(out, name, off);
    snprintf(name, sizeof name, "%s-lut", way->name);
    Check(w0 && Write(out, name, with), "written");
    // the LUT taken away: the picture of none, to the bit
    const int d2 = DrawPlain(&g, r, &b.frame, &plain, NULL, again, 0);
    snprintf(what, sizeof what, "%s: the LUT set and taken away gives the picture of none, to the bit", way->name);
    Check(d2 && Same(off, again) && !dvbridge_render_lut_applied(r), what);
    if (way->identity)
    {
      const int d3 = DrawPlain(&g, r, &b.frame, &plain, way->identity, with, 0);
      const struct apart a = Apart(off, with);
      printf("identity %s %d points: %zu of %zu floats differ (at most %.3g), codes of 10 bits %zu, of 12 bits %zu\n",
             way->name, way->identity->size, a.floats, N * 3, a.most, a.codes10, a.codes12);
      snprintf(what, sizeof what,
               "%s: an identity LUT of %d points is the picture without it: every float within 2^-21 (4 units of the "
               "last place of 1.0 in a float), every code of 10 bits the same",
               way->name, way->identity->size);
      Check(d3 && dvbridge_render_lut_applied(r) && a.most <= 1.0 / 2097152.0 && a.codes10 == 0, what);
    }
  }

  // --- kept as 10-bit codes (a 10-bit window in full range): the LUT before the quantization
  {
    const int codes = dvbridge_render_hdr10_store(r, 1023, false);
    Check(codes, "the pictures kept as 10-bit codes (the GPU has RGB10_A2)");
    if (codes && dvbridge_render_hdr10_mapping(r, &sdr))
    {
      const int d0 = DrawPlain(&g, r, &b.frame, &plain, NULL, off, 1);
      const int d1 = DrawPlain(&g, r, &b.frame, &plain, L.tint, with, 1);
      const int applied = dvbridge_render_lut_applied(r);
      Check(d0 && d1 && applied, "codes: drawn without a LUT and with it");
      if (d0 && d1)
      {
        Check(Write(out, "codes-off", off) && Write(out, "codes-lut", with), "written");
        const int d2 = DrawPlain(&g, r, &b.frame, &plain, L.id33, with, 1);
        const struct apart a = Apart(off, with);
        printf("identity codes 33 points: %zu of %zu codes differ\n", a.floats, N * 3);
        Check(d2 && a.floats == 0, "codes: an identity LUT gives the same 10-bit codes as none, every one");
      }
    }
    dvbridge_render_hdr10_store(r, 0, false);
  }

  // --- a LUT whose shader cannot be made (its table of no size on one axis): drawn without it, and said
  {
    struct dvbridge_lut broken = *L.tint;
    broken.pl.size[2] = 0;
    broken.pl.signature ^= 0x5a5a;
    dvbridge_render_hdr10_mapping(r, &sdr);
    const int d0 = DrawPlain(&g, r, &b.frame, &plain, NULL, off, 0);
    const int d1 = DrawPlain(&g, r, &b.frame, &plain, &broken, with, 0);
    printf("a LUT that fails: drawn %d, applied %d, failed %d\n", d1, dvbridge_render_lut_applied(r),
           dvbridge_render_lut_failed(r));
    Check(d0 && d1 && !dvbridge_render_lut_applied(r) && dvbridge_render_lut_failed(r) && Same(off, with),
          "a LUT that this GPU cannot draw: the picture drawn without it, the same bytes, and the renderer says it "
          "failed");
    // ... and the next LUT set is tried again
    const int d2 = DrawPlain(&g, r, &b.frame, &plain, L.tint, with, 0);
    Check(d2 && dvbridge_render_lut_applied(r) && !dvbridge_render_lut_failed(r),
          "another LUT set after it is applied again");
  }

  // --- the time of a render (informative: zink on lavapipe; the LUT is in the output pass, no pass of its own)
  dvbridge_render_hdr10_mapping(r, &sdr);
  printf("time sdr: without a LUT %.1f ms, with a LUT of 17 points %.1f ms, of 65 points %.1f ms, a .cal %.1f ms, "
         "a shaper and 17 points %.1f ms (best of 4)\n",
         TimePlain(r, &b.frame, &plain, NULL), TimePlain(r, &b.frame, &plain, L.tint),
         TimePlain(r, &b.frame, &plain, L.id65), TimePlain(r, &b.frame, &plain, L.cal),
         TimePlain(r, &b.frame, &plain, L.shaper));
  dvbridge_render_set_lut(r, NULL);
  dvbridge_renderer_destroy(r);

  // --- HDR over the tunnel never takes it (a native renderer: the picture of the transport)
  {
    struct dvbridge_renderer* t = dvbridge_renderer_create(g.gl->gpu, g.log);
    const int on = t && dvbridge_render_set_tunnel(t, true);
    Check(on, "HDR over the tunnel set on a native renderer");
    if (on)
    {
      dvbridge_render_set_lut(t, NULL);
      const int d0 = dvbridge_render_plain(t, &b.frame, &plain, WHOLE, W, H);
      pl_tex tt = dvbridge_render_texture(t);
      const int r0 = d0 && tt && ReadFloats(pl_opengl_unwrap(g.gl->gpu, tt, NULL, NULL, NULL), off);
      dvbridge_render_set_lut(t, L.tint);
      const int d1 = dvbridge_render_plain(t, &b.frame, &plain, WHOLE, W, H);
      tt = dvbridge_render_texture(t);
      const int r1 = d1 && tt && ReadFloats(pl_opengl_unwrap(g.gl->gpu, tt, NULL, NULL, NULL), with);
      Check(r0 && r1 && !dvbridge_render_lut_applied(t) && Same(off, with),
            "HDR over the tunnel with a LUT set: not applied, the same bytes as without");
    }
    dvbridge_renderer_destroy(t);
  }

  // --- Dolby Vision: native (TV-led) never takes it; Low-Latency does
  if (pictures)
  {
    struct fid_picture p;
    if (!fid_load(pictures, &p))
      Check(0, "the Dolby Vision picture of ref_dump");
    else
    {
      struct fid_gl_layer blL = {0}, elL = {0};
      Check(fid_upload(&blL, p.bl, p.bl_loc, 1) && fid_upload(&elL, p.el, p.el_loc, 1), "the layers uploaded");
      struct dvbridge_gl_plane bp[3] = {{blL.tex[0], blL.w, blL.h}, {blL.tex[1], (blL.w + 1) / 2, (blL.h + 1) / 2}, {0, 0, 0}};
      struct dvbridge_gl_plane ep[3] = {{elL.tex[0], elL.w, elL.h}, {elL.tex[1], (elL.w + 1) / 2, (elL.h + 1) / 2}, {0, 0, 0}};
      struct dvbridge_gl_frame db = {0}, de = {0};
      const int imported = dvbridge_gl_frame_import(&db, g.gl->gpu, DVBRIDGE_P010, blL.w, blL.h, bp,
                                                    (enum AVChromaLocation)blL.loc) &&
                           dvbridge_gl_frame_import(&de, g.gl->gpu, DVBRIDGE_P010, elL.w, elL.h, ep,
                                                    (enum AVChromaLocation)elL.loc);
      Check(imported, "the Dolby Vision layers imported");
      db.frame.enhancement_layer = &de.frame;
      struct dvbridge_renderer* d = dvbridge_renderer_create(g.gl->gpu, g.log);
      const struct dvbridge_geometry geometry = {blL.w, blL.h, 0, 0, W, H};
      const double pts = (double)p.bl_pts;
      // draw <lut> into <into>: the picture of the transport; its active area into <area>
      int drawn = 0;
#define DRAW_DV(lutp, into, area)                                                                              \
  do                                                                                                           \
  {                                                                                                            \
    dvbridge_render_set_lut(d, (lutp));                                                                        \
    drawn = dvbridge_render_rgb(d, &db.frame, p.meta, p.meta_size, pts, pts, geometry);                        \
    pl_tex dt = drawn ? dvbridge_render_texture(d) : NULL;                                                     \
    drawn = dt && ReadFloats(pl_opengl_unwrap(g.gl->gpu, dt, NULL, NULL, NULL), (into));                       \
    if (drawn)                                                                                                 \
      dvbridge_active_area(dvbridge_render_candidate(d), (area));                                              \
  } while (0)
      unsigned area[4] = {0};
      if (imported && d)
      {
        // native (TV-led): the same bytes with a LUT set, and not applied
        DRAW_DV(NULL, off, area);
        const int d0 = drawn;
        DRAW_DV(L.tint, with, area);
        Check(d0 && drawn && !dvbridge_render_lut_applied(d) && Same(off, with),
              "native Dolby Vision (TV-led) with a LUT set: not applied, the same bytes as without (the tunnel is "
              "bit-exact)");
        // Low-Latency: to a display of 700 cd/m2 in BT.2020, and with its black (mask_low_latency() lifts it)
        for (int k = 0; k < 2; ++k)
        {
          struct dvbridge_hdr10_mapping ll = {0};
          ll.enabled = true;
          ll.peak = 700.0f;
          ll.gamut = DVBRIDGE_HDR10_GAMUT_BT2020;
          ll.curve = DVBRIDGE_HDR10_CURVE_BT2390;
          ll.black = k ? 0.05f : 0.0f;
          const char* tag = k ? "llblack" : "ll";
          char what[256], name[64];
          if (!dvbridge_render_set_low_latency(d, &ll))
          {
            Check(0, "Low-Latency set");
            continue;
          }
          DRAW_DV(NULL, off, area);
          const int a0 = drawn && !dvbridge_render_lut_applied(d);
          DRAW_DV(L.tint, with, area);
          const int a1 = drawn && dvbridge_render_lut_applied(d);
          snprintf(what, sizeof what, "%s: drawn without a LUT and with it (applied)", tag);
          Check(a0 && a1, what);
          snprintf(name, sizeof name, "%s-off", tag);
          Write(out, name, off);
          snprintf(name, sizeof name, "%s-lut", tag);
          Write(out, name, with);
          printf("active %s: %u %u %u %u\n", tag, area[0], area[1], area[2], area[3]);
          DRAW_DV(L.id33, again, area);
          const struct apart ia = Apart(off, again);
          printf("identity %s 33 points: %zu of %zu floats differ (at most %.3g), codes of 10 bits %zu, of 12 bits "
                 "%zu\n",
                 tag, ia.floats, N * 3, ia.most, ia.codes10, ia.codes12);
          snprintf(what, sizeof what,
                   "%s: an identity LUT is the picture without it: every float within 2^-21, every code of 12 bits "
                   "the same",
                   tag);
          Check(drawn && ia.most <= 1.0 / 2097152.0 && ia.codes12 == 0, what);
          if (!k)
          {
            // the bars where they are not written first (the early mask off: advancedsettings.xml dvearlymask):
            // put back to code 0 after the LUT, the same picture as with them written first
            dvbridge_render_allow_early_mask(d, false);
            DRAW_DV(L.tint, again, area);
            dvbridge_render_allow_early_mask(d, true);
            Check(drawn && dvbridge_render_lut_applied(d) && Same(with, again),
                  "ll: the bars at code 0 with the LUT also where they are not written first (the early mask off): "
                  "the same bytes as with it");
            double t0 = 1e9, t1 = 1e9;
            for (int i = 0; i < 3; ++i)
            {
              glFinish();
              double s = NowMs();
              dvbridge_render_set_lut(d, NULL);
              dvbridge_render_rgb(d, &db.frame, p.meta, p.meta_size, pts, pts, geometry);
              glFinish();
              t0 = fmin(t0, NowMs() - s);
              s = NowMs();
              dvbridge_render_set_lut(d, L.tint);
              dvbridge_render_rgb(d, &db.frame, p.meta, p.meta_size, pts, pts, geometry);
              glFinish();
              t1 = fmin(t1, NowMs() - s);
            }
            printf("time ll: without a LUT %.1f ms, with a LUT of 17 points %.1f ms (best of 3)\n", t0, t1);
          }
        }
        dvbridge_render_set_low_latency(d, NULL);
      }
#undef DRAW_DV
      dvbridge_render_set_lut(d, NULL);
      dvbridge_renderer_destroy(d);
      dvbridge_gl_frame_release(&de);
      dvbridge_gl_frame_release(&db);
      fid_free(&p);
    }
  }

  dvbridge_gl_frame_release(&b);
  dvbridge_lut_free(&L.id17);
  dvbridge_lut_free(&L.id33);
  dvbridge_lut_free(&L.id65);
  dvbridge_lut_free(&L.tint);
  dvbridge_lut_free(&L.crlf);
  dvbridge_lut_free(&L.video);
  dvbridge_lut_free(&L.nuke);
  dvbridge_lut_free(&L.unif);
  dvbridge_lut_free(&L.madvr);
  dvbridge_lut_free(&L.madvrpq);
  dvbridge_lut_free(&L.cal);
  dvbridge_lut_free(&L.caltv);
  dvbridge_lut_free(&L.steep);
  dvbridge_lut_free(&L.long1d);
  dvbridge_lut_free(&L.id1d);
  dvbridge_lut_free(&L.shaper);
  printf("%s: %d failed\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
