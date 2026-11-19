// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image).
//
// One picture directory (meta.txt and the planes, as src/ref_dump.c or p82_mkdata.py write them)
// drawn by libplacebo on the OpenGL ES at hand (zink on lavapipe here), the way the bridge of Kodi draws: the
// RPU in an AVDOVIMetadata mapped by pl_map_avdovi_metadata(), the chroma location of the frame given to
// libplacebo, the target a 3840x2160 RGBA32F texture in the colour space of the picture, 32-bit FBOs, no
// dithering, no peak detection. Writes the picture as 12-bit codes of PQ RGB (three uint16 a pixel, black
// outside the level 5 active area, the layout of the bridge dumps).
//
// Usage: ccm_gpu_test <picture dir> <out .rgb16> [layout]
//   layout: planar (yuv420p10: codes in the low bits, the default) or p010 (two planes, codes in the high bits)
//   DV_CCM=0 draws with libplacebo's own reshaping (pl_render_params.dovi_ccm off); DV_CCM_DUMP=<prefix> (the
//   test-only hook of lp-dump-hook.py) writes the composed planes. Built with P82_BL_TRC defined:
//   DV_BL_TRC=bt1886|hlg|pq, the transfer of the base layer (pl_render_params.dovi_ccm_bl_transfer).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/libav.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 3840
#define H 2160

static char g_dir[1024];

static int key_line(const char* key, char* out, size_t n)
{
  char path[1200];
  snprintf(path, sizeof path, "%s/meta.txt", g_dir);
  FILE* f = fopen(path, "r");
  if (!f)
    return 0;
  char line[4096];
  size_t k = strlen(key);
  int found = 0;
  while (fgets(line, sizeof line, f))
    if (!strncmp(line, key, k) && line[k] == ' ')
    {
      snprintf(out, n, "%s", line + k + 1);
      found = 1;
      break;
    }
  fclose(f);
  return found;
}

static long long ki(const char* key)
{
  char v[4096];
  if (!key_line(key, v, sizeof v))
  {
    fprintf(stderr, "missing %s\n", key);
    exit(2);
  }
  return strtoll(v, NULL, 10);
}

static int kis(const char* key, long long* out, int max)
{
  char v[4096];
  if (!key_line(key, v, sizeof v))
    return -1;
  int n = 0;
  char* p = v;
  while (n < max)
  {
    char* e;
    long long x = strtoll(p, &e, 10);
    if (e == p)
      break;
    out[n++] = x;
    p = e;
  }
  return n;
}

static int krat(const char* key, AVRational* out, int max)
{
  char v[4096];
  if (!key_line(key, v, sizeof v))
    return -1;
  int n = 0;
  char* p = v;
  while (n < max)
  {
    int num, den, used;
    if (sscanf(p, " %d/%d%n", &num, &den, &used) != 2)
      break;
    out[n++] = (AVRational){num, den};
    p += used;
  }
  return n;
}

static uint16_t* plane(const char* name, int w, int h)
{
  char path[1200];
  snprintf(path, sizeof path, "%s/%s.u16", g_dir, name);
  FILE* f = fopen(path, "rb");
  if (!f)
  {
    perror(path);
    exit(2);
  }
  uint16_t* p = malloc((size_t)w * h * 2);
  if (fread(p, 2, (size_t)w * h, f) != (size_t)w * h)
    exit(2);
  fclose(f);
  return p;
}

static AVDOVIMetadata* build_rpu(void)
{
  size_t size;
  AVDOVIMetadata* m = av_dovi_metadata_alloc(&size);
  AVDOVIRpuDataHeader* h = av_dovi_get_header(m);
  AVDOVIDataMapping* map = av_dovi_get_mapping(m);
  AVDOVIColorMetadata* c = av_dovi_get_color(m);
  h->coef_data_type = (uint8_t)ki("hdr.coef_data_type");
  h->coef_log2_denom = (uint8_t)ki("hdr.coef_log2_denom");
  h->bl_bit_depth = (uint8_t)ki("hdr.bl_bit_depth");
  h->el_bit_depth = (uint8_t)ki("hdr.el_bit_depth");
  h->vdr_bit_depth = (uint8_t)ki("hdr.vdr_bit_depth");
  h->disable_residual_flag = (uint8_t)ki("hdr.disable_residual_flag");
  h->bl_video_full_range_flag = (uint8_t)ki("hdr.bl_video_full_range_flag");
  h->rpu_type = 2;
  h->rpu_format = 18;
  map->nlq_method_idc = (enum AVDOVINLQMethod)ki("map.nlq_method_idc");
  for (int k = 0; k < 3; ++k)
  {
    AVDOVIReshapingCurve* cv = &map->curves[k];
    char key[80];
    long long b[32];
    snprintf(key, sizeof key, "curve%d.num_pivots", k);
    cv->num_pivots = (uint8_t)ki(key);
    snprintf(key, sizeof key, "curve%d.pivots", k);
    kis(key, b, 9);
    for (int i = 0; i < cv->num_pivots; ++i)
      cv->pivots[i] = (uint16_t)b[i];
    for (int i = 0; i + 1 < cv->num_pivots; ++i)
    {
      snprintf(key, sizeof key, "curve%d.piece%d.mapping_idc", k, i);
      cv->mapping_idc[i] = (enum AVDOVIMappingMethod)ki(key);
      if (cv->mapping_idc[i] == AV_DOVI_MAPPING_POLYNOMIAL)
      {
        snprintf(key, sizeof key, "curve%d.piece%d.poly", k, i);
        kis(key, b, 4);
        cv->poly_order[i] = (uint8_t)b[0];
        for (int j = 0; j < 3; ++j)
          cv->poly_coef[i][j] = b[1 + j];
      }
      else
      {
        snprintf(key, sizeof key, "curve%d.piece%d.mmr_order", k, i);
        cv->mmr_order[i] = (uint8_t)ki(key);
        snprintf(key, sizeof key, "curve%d.piece%d.mmr_constant", k, i);
        cv->mmr_constant[i] = ki(key);
        for (int o = 0; o < cv->mmr_order[i]; ++o)
        {
          snprintf(key, sizeof key, "curve%d.piece%d.mmr%d", k, i, o + 1);
          kis(key, b, 7);
          for (int j = 0; j < 7; ++j)
            cv->mmr_coef[i][o][j] = b[j];
        }
      }
    }
    snprintf(key, sizeof key, "nlq%d", k);
    kis(key, b, 4);
    map->nlq[k].nlq_offset = (uint16_t)b[0];
    map->nlq[k].vdr_in_max = (uint64_t)b[1];
    map->nlq[k].linear_deadzone_slope = (uint64_t)b[2];
    map->nlq[k].linear_deadzone_threshold = (uint64_t)b[3];
  }
  krat("color.ycc_to_rgb_matrix", c->ycc_to_rgb_matrix, 9);
  krat("color.ycc_to_rgb_offset", c->ycc_to_rgb_offset, 3);
  krat("color.rgb_to_lms_matrix", c->rgb_to_lms_matrix, 9);
  c->source_min_pq = (uint16_t)ki("color.source_min_pq");
  c->source_max_pq = (uint16_t)ki("color.source_max_pq");
  return m;
}

static pl_tex upload(pl_gpu gpu, const uint16_t* data, int w, int h, int comps, int shift)
{
  pl_fmt fmt = pl_find_fmt(gpu, PL_FMT_UNORM, comps, 16, 16, PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR);
  if (!fmt)
    return NULL;
  uint16_t* buf = malloc((size_t)w * h * comps * 2);
  for (size_t i = 0; i < (size_t)w * h * comps; ++i)
    buf[i] = (uint16_t)(data[i] << shift);
  pl_tex t = pl_tex_create(gpu, pl_tex_params(.w = w, .h = h, .format = fmt, .sampleable = true,
                                               .initial_data = buf));
  free(buf);
  return t;
}

// the layer's planes: planar (three R16, low bits) or p010 (R16 and RG16, high bits)
static void layer(pl_gpu gpu, struct pl_frame* f, const char* prefix, int w, int h, int depth, int p010)
{
  char n[3][16];
  snprintf(n[0], 16, "%s_y", prefix);
  snprintf(n[1], 16, "%s_u", prefix);
  snprintf(n[2], 16, "%s_v", prefix);
  const int cw = (w + 1) / 2, ch = (h + 1) / 2;
  uint16_t* y = plane(n[0], w, h);
  uint16_t* u = plane(n[1], cw, ch);
  uint16_t* v = plane(n[2], cw, ch);
  memset(f, 0, sizeof *f);
  f->crop = (pl_rect2df){0, 0, w, h};
  if (p010)
  {
    uint16_t* uv = malloc((size_t)cw * ch * 4);
    for (size_t i = 0; i < (size_t)cw * ch; ++i)
    {
      uv[2 * i] = u[i];
      uv[2 * i + 1] = v[i];
    }
    f->num_planes = 2;
    f->planes[0] = (struct pl_plane){.texture = upload(gpu, y, w, h, 1, 16 - depth), .components = 1,
                                     .component_mapping = {0}};
    f->planes[1] = (struct pl_plane){.texture = upload(gpu, uv, cw, ch, 2, 16 - depth), .components = 2,
                                     .component_mapping = {1, 2}};
    f->repr.bits = (struct pl_bit_encoding){.sample_depth = 16, .color_depth = depth, .bit_shift = 16 - depth};
    free(uv);
  }
  else
  {
    f->num_planes = 3;
    uint16_t* d[3] = {y, u, v};
    for (int i = 0; i < 3; ++i)
      f->planes[i] = (struct pl_plane){.texture = upload(gpu, d[i], i ? cw : w, i ? ch : h, 1, 0), .components = 1,
                                       .component_mapping = {i}};
    f->repr.bits = (struct pl_bit_encoding){.sample_depth = 16, .color_depth = depth, .bit_shift = 0};
  }
  free(y);
  free(u);
  free(v);
}

// DV_PASSES: the passes of the render, one line each (what the info callback says of them)
static void info_cb(void* priv, const struct pl_render_info* info)
{
  (void)priv;
  printf("pass %d (stage %d): %s; %d steps:", info->index, (int)info->stage,
         info->pass->shader->description ? info->pass->shader->description : "?", info->pass->shader->num_steps);
  for (int i = 0; i < info->pass->shader->num_steps; ++i)
    printf(" [%s]", info->pass->shader->steps[i]);
  printf("; GPU time %.3f ms (%d samples)\n", info->pass->last / 1e6, info->pass->num_samples);
}

static struct pl_hook_res rows_hook_fn(void* priv, const struct pl_hook_params* p)
{
  float* area = priv;
  struct pl_shader_var var = {.var = pl_var_vec2("test_rows"), .data = area, .dynamic = true};
  struct pl_custom_shader shader = {
      .prelude = "#define PL_ACTIVE_ROWS_TARGET 1\n",
      .input = PL_SHADER_SIG_COLOR,
      .output = PL_SHADER_SIG_COLOR,
      .body = "if (gl_FragCoord.y < test_rows.x || gl_FragCoord.y >= test_rows.y) return vec4(0.0, 0.0, 0.0, 1.0);\n",
      .variables = &var,
      .num_variables = 1,
  };
  bool ok = !pl_shader_is_compute(p->sh) && pl_shader_custom(p->sh, &shader);
  return (struct pl_hook_res){.failed = !ok, .output = PL_HOOK_SIG_NONE};
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return 2;
  setvbuf(stdout, NULL, _IONBF, 0);
  snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
  const int p010 = argc > 3 && !strcmp(argv[3], "p010");
  PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = get(0x31DD, EGL_DEFAULT_DISPLAY, NULL);
  EGLint major, minor, n = 0;
  if (!eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API))
    return 1;
  EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig cfg;
  if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n)
    return 1;
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
  EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
  if (!eglMakeCurrent(dpy, pb, pb, ctx))
    return 1;
  pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = pl_log_color,
                                                       .log_level = getenv("DV_TRACE") ? PL_LOG_TRACE : PL_LOG_INFO));
  pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t(*)(const char*))eglGetProcAddress,
                                                         .egl_display = dpy, .egl_context = ctx, .allow_software = true));
  if (!gl)
    return 1;
  pl_gpu gpu = gl->gpu;

  const int bw = (int)ki("bl.width"), bh = (int)ki("bl.height"), ew = (int)ki("el.width"), eh = (int)ki("el.height");
  const int bld = (int)ki("hdr.bl_bit_depth"), eld = (int)ki("hdr.el_bit_depth");
  struct pl_frame image, el;
  layer(gpu, &image, "bl", bw, bh, bld, p010);
  layer(gpu, &el, "el", ew, eh, eld, p010);
  AVDOVIMetadata* rpu = build_rpu();
  struct pl_dovi_metadata dovi;
  memset(&dovi, 0, sizeof dovi);
  struct pl_color_repr repr = {0};
  struct pl_color_space color = {0};
  pl_map_avdovi_metadata(&color, &repr, &dovi, rpu);
  repr.bits = image.repr.bits;
  image.repr = repr;
  image.color = color;
  const int loc = (int)ki("bl.chroma_location");
  if (!getenv("DV_CENTRED"))
  {
    pl_frame_set_chroma_location(&image, pl_chroma_from_av((enum AVChromaLocation)loc));
    pl_frame_set_chroma_location(&el, pl_chroma_from_av((enum AVChromaLocation)ki("el.chroma_location")));
  }
  if (!getenv("DV_NOEL"))
    image.enhancement_layer = &el;
  printf("ccm_gpu_test %s: %s layout, nlq_active %d, ccm.valid %d, chroma location %d\n", g_dir,
         p010 ? "p010" : "planar", dovi.nlq_active, dovi.ccm.valid, loc);

  pl_fmt tfmt = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_SAMPLEABLE);
  pl_tex target_tex = pl_tex_create(gpu, pl_tex_params(.w = W, .h = H, .format = tfmt, .renderable = true,
                                                       .sampleable = true, .host_readable = true));
  struct pl_frame target = {
      .num_planes = 1,
      .planes = {{.texture = target_tex, .components = 4, .component_mapping = {0, 1, 2, 3}}},
      .repr = pl_color_repr_rgb,
      .color = color,
  };
  struct pl_render_params params = pl_render_default_params;
  params.min_fbo_precision = 32;
  params.peak_detect_params = NULL;
  params.dither_params = NULL;
  params.frame_mixer = NULL;
  params.skip_caching_single_frame = true;
  params.border = PL_CLEAR_SKIP;
  params.dovi_ccm = !(getenv("DV_CCM") && !strcmp(getenv("DV_CCM"), "0"));
#ifdef P82_BL_TRC
  // DV_BL_TRC=bt1886|hlg|pq: the transfer of the base layer (libplacebo-9955)
  if (getenv("DV_BL_TRC"))
    params.dovi_ccm_bl_transfer = !strcmp(getenv("DV_BL_TRC"), "bt1886") ? PL_COLOR_TRC_BT_1886
                                  : !strcmp(getenv("DV_BL_TRC"), "hlg")  ? PL_COLOR_TRC_HLG
                                                                         : PL_COLOR_TRC_PQ;
  printf("dovi_ccm_bl_transfer %d\n", (int)params.dovi_ccm_bl_transfer);
#endif
  if (getenv("DV_PASSES"))
    params.info_callback = info_cb;
  // DV_LEVEL=1..3: the step-down levels of the bridge (step_down() of tools/dvbridge): the chroma
  // of the base layer bilinear, then also the chroma, then also the luma of the enhancement layer
  const int level = getenv("DV_LEVEL") ? atoi(getenv("DV_LEVEL")) : 0;
  if (level >= 1)
  {
    const struct pl_filter_config* faithful = params.plane_upscaler ? params.plane_upscaler : params.upscaler;
    params.plane_upscaler = &pl_filter_bilinear;
    params.el_chroma_upscaler = level >= 2 ? &pl_filter_bilinear : faithful;
    params.el_luma_upscaler = level >= 3 ? &pl_filter_bilinear : faithful;
  }
#ifdef PL_DOVI_CCM_LADDER
  // DV_CCM_LEVEL=1..3: the ladder of the composer of CCM 001 (libplacebo-9940): 1 the EL luma between columns
  // from two taps, 2 also its rows and the EL chroma's columns, 3 also the composed planes scaled up bilinearly;
  // DV_CCM_EL=<mask> and DV_CCM_PLANE_LINEAR=1 set the two fields themselves
  const int ccm_level = getenv("DV_CCM_LEVEL") ? atoi(getenv("DV_CCM_LEVEL")) : 0;
  if (ccm_level >= 1)
    params.dovi_ccm_el_linear = PL_DOVI_CCM_EL_LUMA_COLUMNS;
  if (ccm_level >= 2)
    params.dovi_ccm_el_linear |= PL_DOVI_CCM_EL_LUMA_ROWS | PL_DOVI_CCM_EL_CHROMA_COLUMNS;
  if (ccm_level >= 3)
    params.dovi_ccm_plane_upscaler = &pl_filter_bilinear;
  if (getenv("DV_CCM_EL"))
    params.dovi_ccm_el_linear = atoi(getenv("DV_CCM_EL"));
  if (getenv("DV_CCM_PLANE_LINEAR"))
    params.dovi_ccm_plane_upscaler = &pl_filter_bilinear;
  printf("dovi_ccm_el_linear %d, dovi_ccm_plane_upscaler %s\n", params.dovi_ccm_el_linear,
         params.dovi_ccm_plane_upscaler ? "bilinear" : "(plane_upscaler)");
#endif
  long long l5[4] = {0, 0, 0, 0};
  for (int i = 0; i < 9; ++i)
  {
    char key[32];
    snprintf(key, sizeof key, "ext%d.l5", i);
    if (kis(key, l5, 4) == 4)
      break;
  }
  // DV_ROWS: the rows outside the active area left out, as the bridge does with its bars written first
  // (pl_render_params.active_rows, and a hook that writes them and defines PL_ACTIVE_ROWS_TARGET)
  static float rows_area[4];
  const struct pl_hook* hooks[1];
  struct pl_hook rows_hook;
  if (getenv("DV_ROWS") && (l5[2] || l5[3]))
  {
    params.active_rows[0] = (int)l5[2];
    params.active_rows[1] = H - (int)l5[3];
    rows_area[0] = (float)l5[2];
    rows_area[1] = (float)(H - l5[3]);
    rows_hook = (struct pl_hook){.stages = PL_HOOK_NATIVE, .input = PL_HOOK_SIG_COLOR, .hook = rows_hook_fn,
                                 .priv = rows_area, .signature = 0x5445535452303031ULL};
    hooks[0] = &rows_hook;
    params.hooks = hooks;
    params.num_hooks = 1;
  }
  pl_renderer rr = pl_renderer_create(log, gpu);
  // DV_PRE=<picture dir>: the same renderer first draws that picture (its intermediate textures then hold
  // other contents when the picture of the test is drawn)
  if (getenv("DV_PRE"))
  {
    static struct pl_frame pimg, pel;
    static struct pl_dovi_metadata pdovi;
    char saved[1024];
    snprintf(saved, sizeof saved, "%s", g_dir);
    snprintf(g_dir, sizeof g_dir, "%s", getenv("DV_PRE"));
    layer(gpu, &pimg, "bl", bw, bh, bld, p010);
    layer(gpu, &pel, "el", ew, eh, eld, p010);
    AVDOVIMetadata* r = build_rpu();
    struct pl_color_repr rp = {0};
    struct pl_color_space cs = {0};
    pl_map_avdovi_metadata(&cs, &rp, &pdovi, r);
    rp.bits = pimg.repr.bits;
    pimg.repr = rp;
    pimg.color = cs;
    pl_frame_set_chroma_location(&pimg, pl_chroma_from_av((enum AVChromaLocation)ki("bl.chroma_location")));
    pl_frame_set_chroma_location(&pel, pl_chroma_from_av((enum AVChromaLocation)ki("el.chroma_location")));
    pimg.enhancement_layer = &pel;
    snprintf(g_dir, sizeof g_dir, "%s", saved);
    printf("DV_PRE: %s drawn first: %s\n", getenv("DV_PRE"), pl_render_image(rr, &pimg, &target, &params) ? "ok" : "FAILED");
  }
  if (!pl_render_image(rr, &image, &target, &params))
  {
    printf("FAIL: render\n");
    return 1;
  }
  struct pl_render_errors errs = pl_renderer_get_errors(rr);
  printf("render errors 0x%x, dovi_ccm %d, step-down level %d\n", (unsigned)errs.errors, params.dovi_ccm, level);
  float* px = malloc((size_t)W * H * 16);
  if (!pl_tex_download(gpu, pl_tex_transfer_params(.tex = target_tex, .ptr = px)))
    return 1;
  long nonfinite = 0, nonfinite_px = 0;
  for (int yy = (int)l5[2]; yy < H - (int)l5[3]; ++yy)
    for (int xx = (int)l5[0]; xx < W - (int)l5[1]; ++xx)
    {
      int bad = 0;
      for (int k = 0; k < 3; ++k)
        bad += !isfinite(px[((size_t)yy * W + xx) * 4 + k]);
      nonfinite += bad;
      nonfinite_px += bad > 0;
    }
  printf("non-finite values in the active area: %ld (%ld pixels)\n", nonfinite, nonfinite_px);
  uint16_t* out = malloc((size_t)W * H * 6);
  for (int yy = 0; yy < H; ++yy)
    for (int xx = 0; xx < W; ++xx)
    {
      const int in = xx >= l5[0] && xx < W - l5[1] && yy >= l5[2] && yy < H - l5[3];
      for (int k = 0; k < 3; ++k)
      {
        float v = in ? px[((size_t)yy * W + xx) * 4 + k] : 0.0f;
        v = isnan(v) ? 0 : v < 0 ? 0 : v > 1 ? 1 : v; // (a NaN written as 0, and counted above)
        out[((size_t)yy * W + xx) * 3 + k] = (uint16_t)(v * 4095.0f + 0.5f);
      }
    }
  FILE* f = fopen(argv[2], "wb");
  fwrite(out, 2, (size_t)W * H * 3, f);
  fclose(f);
  printf("written %s\n", argv[2]);

  // DV_ALTERNATE=<second picture dir>: the same renderer then draws the second picture, the first, the second,
  // the first, then (after pl_renderer_flush_cache(), what the bridge calls at each new stream) the second and
  // the first: what libplacebo logs about the composer each time (each reason once a stream)
  const char* alt = getenv("DV_ALTERNATE");
  if (alt)
  {
    static struct pl_frame images[2], els[2];
    static struct pl_dovi_metadata dovis[2];
    const char* dirs[2] = {argv[1], alt};
    for (int k = 0; k < 2; ++k)
    {
      snprintf(g_dir, sizeof g_dir, "%s", dirs[k]);
      layer(gpu, &images[k], "bl", bw, bh, bld, p010);
      layer(gpu, &els[k], "el", ew, eh, eld, p010);
      AVDOVIMetadata* r = build_rpu();
      struct pl_color_repr rp = {0};
      struct pl_color_space cs = {0};
      pl_map_avdovi_metadata(&cs, &rp, &dovis[k], r);
      rp.bits = images[k].repr.bits;
      images[k].repr = rp;
      images[k].color = cs;
      if (!getenv("DV_CENTRED"))
      {
        pl_frame_set_chroma_location(&images[k], pl_chroma_from_av((enum AVChromaLocation)ki("bl.chroma_location")));
        pl_frame_set_chroma_location(&els[k], pl_chroma_from_av((enum AVChromaLocation)ki("el.chroma_location")));
      }
      if (!getenv("DV_NOEL"))
        images[k].enhancement_layer = &els[k];
    }
    const int seq[6] = {1, 0, 1, 0, 1, 0};
    for (int i = 0; i < 6; ++i)
    {
      if (i == 4)
      {
        printf("ALTERNATE: pl_renderer_flush_cache()\n");
        pl_renderer_flush_cache(rr);
      }
      printf("ALTERNATE render %d: %s\n", i + 2, dirs[seq[i]]);
      fflush(stdout);
      if (!pl_render_image(rr, &images[seq[i]], &target, &params))
      {
        printf("FAIL: render\n");
        return 1;
      }
      fflush(stderr);
    }
    printf("ALTERNATE done, render errors 0x%x\n", (unsigned)pl_renderer_get_errors(rr).errors);
  }
  pl_renderer_destroy(&rr);
  return 0;
}
