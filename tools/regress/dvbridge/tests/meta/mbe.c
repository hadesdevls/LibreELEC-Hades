// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into Kodi or into an image).
//
// Draws one picture through Kodi's Dolby Vision bridge (tools/dvbridge of the tree under test) on a software
// OpenGL ES (zink on lavapipe) and writes what it gives, for measurements against float64 references.
// The picture: fid:<dir> (a picture dir of ref_dump: both layers; MB_NOEL=1 the base layer alone) or
// raw:<meta.bin>:<base layer yuv420p10le>:<w>:<h>:<chroma location> (base layer only).
// MB_MODE=hdr10 (default): the picture for output as HDR10 (dvbridge_render_hdr10()), MB_OUTW x MB_OUTH (default
//   3840x2160, the whole picture on the whole target), mapped as MB_MAP says: 0 off (default), 1 to an HDR10 display
//   of MB_PEAK cd/m2 (600), MB_CURVE (1 BT.2390, 0 spline), MB_GAMUT (1 P3, 0 BT.2020, 2 BT.709); 2 to SDR
//   (dvbridge_sdr_mapping()). MB_FORK=1: the idea of the fork's "Expert at source resolution", written here: drawn
//   (and mapped) at the source's own size, then scaled to the target by libplacebo's main scaler with the same
//   default parameters as the bridge's render, the result clamped to [0, 1] and to the peak (HDR10 target); with
//   MB_NOSIGMOID=1 that scaling without sigmoidization. Writes <prefix>.f32: RGB floats, rows from the top.
// MB_MODE=transport: the composed way (dvbridge_render_rgb()) and the direct way at the picture's own size on the
//   raster (MB_SOURCE: 1 the source transport, 0 converted): <prefix>-picture.f32 (the RGBA32F texture as RGB
//   floats, rows as held), -composed.rgba, -direct.rgba (RGBA8 as read back), -ccm_{y,cb,cr}.u16 (source),
//   -info.txt (the colour metadata of the RPU).
// Usage: mb <out prefix> <picture>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/pixfmt.h>
#include <libplacebo/opengl.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvbridge_gl_frame.h"
#include "dvbridge_gl_pack.h"
#include "dvbridge_placebo.h"
#include "dvbridge_render.h"
#include "layers.h"
#include "upload.h"

#include "pack_sources.h"

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

static int WriteFile(const char* prefix, const char* what, const void* data, size_t size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s%s", prefix, what);
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
  f = fopen(parts[0], "rb");
  if (!ok || !f)
    return 0;
  fseek(f, 0, SEEK_END);
  p->meta_size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  void* m = aligned_alloc(64, (p->meta_size + 63) / 64 * 64);
  ok = m && fread(m, 1, p->meta_size, f) == p->meta_size;
  fclose(f);
  p->meta = m;
  p->bl_pts = p->el_pts = 1000;
  return ok;
}

// --- MB_FORK: the clamp after the scaling, to [0, 1] and to the peak (in the target's signal)
struct after_clamp
{
  float peak;
  int called;
};

static struct pl_hook_res ClampHook(void* priv, const struct pl_hook_params* p)
{
  struct after_clamp* c = priv;
  struct pl_shader_var var = {.var = pl_var_float("mb_peak"), .data = &c->peak, .dynamic = true};
  const struct pl_custom_shader shader = {
      .input = PL_SHADER_SIG_COLOR, .output = PL_SHADER_SIG_COLOR,
      .body = "color = clamp(color, 0.0, 1.0);\ncolor.rgb = min(color.rgb, vec3(mb_peak));\n",
      .variables = &var, .num_variables = 1, .description = "mb: clamp after scaling"};
  c->called = p->stage == PL_HOOK_OUTPUT && pl_shader_custom(p->sh, &shader);
  return (struct pl_hook_res){.failed = !c->called, .output = PL_HOOK_SIG_NONE};
}

static void Info(const char* prefix, const AVDOVIMetadata* meta, int loc, const char* extra)
{
  char text[4096];
  int n = 0;
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
  n += snprintf(text + n, sizeof text - n, "\nsignal_color_space %d\nchroma_location %d\nsource_pq %u %u\n%s",
                c->signal_color_space, loc, c->source_min_pq, c->source_max_pq, extra ? extra : "");
  WriteFile(prefix, "-info.txt", text, strlen(text));
  printf("%s", text);
}

int main(int argc, char** argv)
{
  if (argc != 3)
    return 2;
  const char* prefix = argv[1];
  const char* spec = argv[2];
  const int transport = getenv("MB_MODE") && !strcmp(getenv("MB_MODE"), "transport");

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

  struct fid_picture p;
  const int fid = !strncmp(spec, "fid:", 4);
  if (fid ? !fid_load(spec + 4, &p) : !LoadRaw(spec + 4, &p))
  {
    printf("FAIL: the picture %s not loaded\n", spec);
    return 1;
  }
  struct fid_gl_layer bl = {0}, el = {0};
  const int noel = !fid || envi("MB_NOEL", 0);
  if (!fid_upload(&bl, p.bl, p.bl_loc, 1) || (!noel && !fid_upload(&el, p.el, p.el_loc, 1)))
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
  const AVDOVIMetadata* meta = p.meta;
  int failures = 0;
  // the metadata changed as asked: MB_MINPQ (source_min_PQ), MB_L6MIN / MB_L6MAX (the level 6 block's mastering
  // display, 0.0001 and 1 cd/m2)
  {
    AVDOVIColorMetadata* c = (AVDOVIColorMetadata*)av_dovi_get_color(meta);
    if (getenv("MB_MINPQ"))
      c->source_min_pq = (uint16_t)envi("MB_MINPQ", 0);
    AVDOVIDmData* l6 = av_dovi_find_level(meta, 6);
    if (l6 && getenv("MB_L6MIN"))
      l6->l6.min_luminance = (uint16_t)envi("MB_L6MIN", 0);
    if (l6 && getenv("MB_L6MAX"))
      l6->l6.max_luminance = (uint16_t)envi("MB_L6MAX", 0);
    AVDOVIDmData* l1 = av_dovi_find_level(meta, 1);
    if ((getenv("MB_L1MIN") || getenv("MB_L1MAX") || getenv("MB_L1AVG")) && !l1)
    {
      printf("FAIL: no level 1 block to change\n");
      return 1;
    }
    if (l1 && getenv("MB_L1MIN"))
      l1->l1.min_pq = (uint16_t)envi("MB_L1MIN", 0);
    if (l1 && getenv("MB_L1MAX"))
      l1->l1.max_pq = (uint16_t)envi("MB_L1MAX", 0);
    if (l1 && getenv("MB_L1AVG"))
      l1->l1.avg_pq = (uint16_t)envi("MB_L1AVG", 0);
    if (l1)
      printf("L1 used: %d %d %d\n", l1->l1.min_pq, l1->l1.max_pq, l1->l1.avg_pq);
    if (getenv("MB_L3"))
    {
      unsigned v[3];
      if (sscanf(getenv("MB_L3"), "%u,%u,%u", &v[0], &v[1], &v[2]) != 3)
      {
        printf("FAIL: MB_L3\n");
        return 1;
      }
      AVDOVIMetadata* mm = (AVDOVIMetadata*)meta;
      AVDOVIDmData* l3 = av_dovi_find_level(mm, 3);
      if (!l3)
      {
        if (mm->num_ext_blocks >= AV_DOVI_MAX_EXT_BLOCKS ||
            mm->ext_block_offset + (size_t)(mm->num_ext_blocks + 1) * mm->ext_block_size > p.meta_size)
        {
          printf("FAIL: no room for a level 3 block\n");
          return 1;
        }
        l3 = av_dovi_get_ext(mm, mm->num_ext_blocks++);
        memset(l3, 0, mm->ext_block_size);
        l3->level = 3;
      }
      l3->l3.min_pq_offset = (uint16_t)v[0];
      l3->l3.max_pq_offset = (uint16_t)v[1];
      l3->l3.avg_pq_offset = (uint16_t)v[2];
      printf("L3 set: %u %u %u\n", v[0], v[1], v[2]);
    }
    if ((getenv("MB_L6MIN") || getenv("MB_L6MAX")) && !l6)
    {
      printf("FAIL: no level 6 block to change\n");
      return 1;
    }
  }

  if (transport)
  {
    struct dvbridge_renderer* r = dvbridge_renderer_create(gl->gpu, log);
    struct dvbridge_gl_packer* packer = dvbridge_gl_packer_create(PACK_VERTEX, PACK_FRAGMENT);
    if (!r || !packer)
      return 1;
    dvbridge_render_use_ccm_composer(r, envi("MB_CCM", 1) != 0);
    dvbridge_render_use_source_transport(r, envi("MB_SOURCE", 1) != 0);
    // MB_EARLY=0: every row composed (no rows of the bars left out), for references that read far past them
    dvbridge_render_allow_early_mask(r, envi("MB_EARLY", 1) != 0);
    GLuint color = 0, fbo = 0;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pl_tex scanout = pl_opengl_wrap(gl->gpu, pl_opengl_wrap_params(.texture = color, .target = GL_TEXTURE_2D,
                                                                   .iformat = GL_RGBA8, .width = W, .height = H));
    uint8_t* bytes = malloc((size_t)W * H * 4);
    float* floats = malloc((size_t)W * H * 16);
    struct dvbridge_geometry geometry = {bl.w, bl.h, 0, 0, bl.w, bl.h};
    Fill(fbo, 0.5f);
    const int rgb = dvbridge_render_rgb(r, &b.frame, meta, p.meta_size, pts, elPts, geometry);
    const char* why = NULL;
    const int source = rgb && dvbridge_render_source(r, &why);
    unsigned margins[4] = {0}, count = 0;
    if (rgb)
    {
      const struct dvbridge_candidate* cand = dvbridge_render_candidate(r);
      dvbridge_packets(cand, &count);
      dvbridge_active_area(cand, margins);
      pl_tex tex = dvbridge_render_texture(r);
      const GLuint id = tex ? pl_opengl_unwrap(gl->gpu, tex, NULL, NULL, NULL) : 0;
      if (id && ReadFloatTexture(id, W, H, floats))
      {
        float* f3 = malloc((size_t)W * H * 12);
        for (size_t i = 0; i < (size_t)W * H; ++i)
          for (int k = 0; k < 3; ++k)
            f3[i * 3 + k] = floats[i * 4 + k];
        WriteFile(prefix, "-picture.f32", f3, (size_t)W * H * 12);
        free(f3);
      }
      else
        ++failures;
      unsigned planes[2] = {0, 0};
      if (source && dvbridge_render_source_planes(r, planes))
      {
        const int cw = (bl.w + 1) / 2, ch = (bl.h + 1) / 2;
        uint16_t* out = malloc((size_t)bl.w * bl.h * 2);
        if (ReadFloatTexture(planes[0], bl.w, bl.h, floats))
        {
          for (size_t i = 0; i < (size_t)bl.w * bl.h; ++i)
            out[i] = (uint16_t)lround(floats[i * 4] * 4096.0);
          WriteFile(prefix, "-ccm_y.u16", out, (size_t)bl.w * bl.h * 2);
        }
        if (ReadFloatTexture(planes[1], cw, ch, floats))
          for (int k = 0; k < 2; ++k)
          {
            for (size_t i = 0; i < (size_t)cw * ch; ++i)
              out[i] = (uint16_t)lround(floats[i * 4 + k] * 4096.0);
            WriteFile(prefix, k ? "-ccm_cr.u16" : "-ccm_cb.u16", out, (size_t)cw * ch * 2);
          }
        free(out);
      }
      Fill(fbo, 0.25f);
      if (dvbridge_render_pack(r, packer, fbo, W, H, true, DVBRIDGE_COVER_NONE) && ReadRgba8(fbo, bytes))
        WriteFile(prefix, "-composed.rgba", bytes, (size_t)W * H * 4);
      else
        ++failures;
    }
    else
      ++failures;
    Fill(fbo, 0.75f);
    const int direct = dvbridge_render_packed(r, &b.frame, meta, p.meta_size, pts, elPts, geometry, scanout, true) &&
                       ReadRgba8(fbo, bytes);
    if (direct)
      WriteFile(prefix, "-direct.rgba", bytes, (size_t)W * H * 4);
    char extra[512];
    snprintf(extra, sizeof extra, "transport: source %d (%s), direct %d, packets %u, margins %u %u %u %u\n", source,
             why ? why : "-", direct, count, margins[0], margins[1], margins[2], margins[3]);
    Info(prefix, meta, bl.loc, extra);
    printf("%s %s\n", failures ? "MB-FAILED" : "MB-OK", prefix);
    fflush(stdout);
    _Exit(failures ? 1 : 0);
  }

  // --- the picture for output as HDR10
  struct dvbridge_renderer* r = dvbridge_renderer_create_hdr10(gl->gpu, log);
  if (!r)
    return 1;
  dvbridge_render_use_ccm_composer(r, envi("MB_CCM", 1) != 0);
  // MB_GAMUTFINE=1: the finer gamut mapping table (videoscreen.hadesdvgamutfine)
  if (envi("MB_GAMUTFINE", 0))
    dvbridge_render_set_gamut_table(r, DVBRIDGE_GAMUT_TABLE_FINE);
  const int map = envi("MB_MAP", 0);
  struct dvbridge_hdr10_mapping m = {0};
  if (map == 1)
  {
    m.enabled = true;
    m.peak = (float)envi("MB_PEAK", 600);
    m.gamut = (enum dvbridge_hdr10_gamut)envi("MB_GAMUT", 1);
    m.curve = (enum dvbridge_hdr10_curve)envi("MB_CURVE", 1);
    m.target = DVBRIDGE_HDR10_TARGET_HDR10;
  }
  else if (map == 2)
    m = dvbridge_sdr_mapping(NULL);
  // MB_SRCP3=1: the gamut of the mastering display known, P3 (D65), as the HDR10 metadata of a stream gives it
  if (map && envi("MB_SRCP3", 0))
  {
    static const float p3[8] = {0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f};
    m.source_known = true;
    memcpy(m.source_xy, p3, sizeof p3);
  }
  if (!dvbridge_render_hdr10_mapping(r, map ? &m : NULL))
  {
    printf("FAIL: the mapping\n");
    return 1;
  }
  const int ow = envi("MB_OUTW", W), oh = envi("MB_OUTH", H);
  const int fork = envi("MB_FORK", 0);
  const int dw = fork ? bl.w : ow, dh = fork ? bl.h : oh;
  const struct dvbridge_view view = {0, 0, (float)bl.w, (float)bl.h, 0, 0, dw, dh};
  const int ok = dvbridge_render_hdr10(r, &b.frame, meta, p.meta_size, pts, elPts, view, dw, dh);
#ifdef DVBRIDGE_LEVEL1_EFFECTIVE
  {
    struct dvbridge_render_notes notes;
    if (dvbridge_render_take_notes(r, &notes))
    {
      if (notes.level3_added)
        printf("NOTE level3 added: offsets %+d %+d %+d, L1 %u %u %u taken as %.2f %.2f %.2f\n", notes.level3.offset[0],
               notes.level3.offset[1], notes.level3.offset[2], notes.level3.l1[0], notes.level3.l1[1],
               notes.level3.l1[2], notes.level3.min * 4095.0, notes.level3.max * 4095.0, notes.level3.avg * 4095.0);
      if (notes.level3_refused)
        printf("NOTE level3 refused: offsets %+d %+d %+d, L1 %u %u %u\n", notes.level3_not_used.offset[0],
               notes.level3_not_used.offset[1], notes.level3_not_used.offset[2], notes.level3_not_used.l1[0],
               notes.level3_not_used.l1[1], notes.level3_not_used.l1[2]);
    }
  }
#endif
  pl_tex tex = ok ? dvbridge_render_hdr10_texture(r) : NULL;
  if (!tex)
  {
    printf("FAIL: the HDR10 picture (render %d)\n", ok);
    return 1;
  }
  pl_tex out = tex;
  if (fork && (ow != dw || oh != dh))
  {
    pl_renderer rr = pl_renderer_create(log, gl->gpu);
    pl_fmt fmt = pl_find_fmt(gl->gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_SAMPLEABLE);
    out = pl_tex_create(gl->gpu, pl_tex_params(.w = ow, .h = oh, .format = fmt, .sampleable = true, .renderable = true));
    struct pl_color_space csp = map == 2 ? (struct pl_color_space){.primaries = PL_COLOR_PRIM_BT_709,
                                                                    .transfer = PL_COLOR_TRC_BT_1886}
                                         : (struct pl_color_space){.primaries = PL_COLOR_PRIM_BT_2020,
                                                                    .transfer = PL_COLOR_TRC_PQ};
    struct pl_frame image = {
        .num_planes = 1,
        .planes = {{.texture = tex, .components = 3, .component_mapping = {0, 1, 2}}},
        .repr = pl_color_repr_rgb,
        .color = csp,
    };
    struct pl_frame target = {
        .num_planes = 1,
        .planes = {{.texture = out, .components = 3, .component_mapping = {0, 1, 2}}},
        .repr = pl_color_repr_rgb,
        .color = csp,
    };
    struct pl_render_params params = pl_render_default_params;
    params.min_fbo_precision = 32;
    params.peak_detect_params = NULL;
    params.dither_params = NULL;
    params.frame_mixer = NULL;
    params.skip_caching_single_frame = true;
    params.border = PL_CLEAR_SKIP;
    if (envi("MB_NOSIGMOID", 0))
      params.sigmoid_params = NULL;
    struct after_clamp clamp = {.peak = map == 1 ? pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, m.peak) : 1.0f};
    const struct pl_hook hook = {.stages = PL_HOOK_OUTPUT, .input = PL_HOOK_SIG_COLOR, .priv = &clamp,
                                 .hook = ClampHook, .signature = 0x4d42434c414d5031ULL};
    const struct pl_hook* hooks[] = {&hook};
    params.hooks = hooks;
    params.num_hooks = 1;
    if (!out || !pl_render_image(rr, &image, &target, &params) || !clamp.called)
    {
      printf("FAIL: the scaling after the mapping\n");
      return 1;
    }
  }
  float* floats = malloc((size_t)ow * oh * 16);
  const GLuint id = pl_opengl_unwrap(gl->gpu, out, NULL, NULL, NULL);
  if (!id || !ReadFloatTexture(id, ow, oh, floats))
  {
    printf("FAIL: not read\n");
    return 1;
  }
  float* f3 = malloc((size_t)ow * oh * 12);
  for (size_t i = 0; i < (size_t)ow * oh; ++i)
    for (int k = 0; k < 3; ++k)
      f3[i * 3 + k] = floats[i * 4 + k];
  if (!WriteFile(prefix, ".f32", f3, (size_t)ow * oh * 12))
    ++failures;
  struct dvbridge_hdr10_static st = {0};
  dvbridge_hdr10_static(&st, meta, p.meta_size);
  char extra[512];
  snprintf(extra, sizeof extra,
           "hdr10: %dx%d from %dx%d, map %d peak %.0f curve %d gamut %d fork %d nosigmoid %d; composed %d; master %u..%u "
           "(1e-4 cd/m2 min), L6 %d\n",
           ow, oh, bl.w, bl.h, map, m.peak, m.curve, m.gamut, fork, envi("MB_NOSIGMOID", 0),
           dvbridge_render_composed(r), st.min_luminance, st.max_luminance, st.level6);
  Info(prefix, meta, bl.loc, extra);
  printf("%s %s\n", failures ? "MB-FAILED" : "MB-OK", prefix);
  fflush(stdout);
  _Exit(failures ? 1 : 0);
}
