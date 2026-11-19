// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (profile 8.4; nothing of this goes into an image).
//
// One picture directory (meta.txt and the planes; p84_mkdata.py) drawn by libplacebo on the OpenGL ES at hand (zink on
// lavapipe), the RPU in an AVDOVIMetadata mapped by pl_map_avdovi_metadata() as the bridge maps it. Adapted from
// ccm_gpu_test.c (its RPU reader and uploads), reading also the signal that the RPU
// declares (color.signal_eotf: four values; color.signal_bit_depth).
//   DV_BL_TRC=hlg|bt1886|pq: pl_render_params.dovi_ccm_bl_transfer (unset: unknown, as before libplacebo-9955)
//   DV_CCM=0: libplacebo's own reshaping
//   DV_ONLY=<prefix>: compose only (dovi_ccm_only, the planes into textures of ours, as the source transport of the
//     bridge has it), and write them as codes h of 5.4.3.3: <prefix>_y.u16, _cb.u16, _cr.u16
//   otherwise: a whole render into a 3840x2160 RGBA32F target (the converted transport's), its result printed
// Prints: "render ok|FAILED", "composed 0|1", and the render errors.
// Usage: p84_gpu <picture dir>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/mem.h>
#include <libplacebo/opengl.h>
#include <libplacebo/renderer.h>
#include <libplacebo/utils/libav.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_dir[1024];

static int key_line(const char *key, char *out, size_t n)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/meta.txt", g_dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[4096];
    size_t k = strlen(key);
    int found = 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, k) && line[k] == ' ') {
            snprintf(out, n, "%s", line + k + 1);
            found = 1;
            break;
        }
    fclose(f);
    return found;
}

static long long ki(const char *key)
{
    char v[4096];
    if (!key_line(key, v, sizeof v)) {
        fprintf(stderr, "missing %s\n", key);
        exit(2);
    }
    return strtoll(v, NULL, 10);
}

static int kis(const char *key, long long *out, int max)
{
    char v[4096];
    if (!key_line(key, v, sizeof v))
        return -1;
    int n = 0;
    char *p = v;
    while (n < max) {
        char *e;
        long long x = strtoll(p, &e, 10);
        if (e == p)
            break;
        out[n++] = x;
        p = e;
    }
    return n;
}

static int krat(const char *key, AVRational *out, int max)
{
    char v[4096];
    if (!key_line(key, v, sizeof v))
        return -1;
    int n = 0;
    char *p = v;
    while (n < max) {
        int num, den, used;
        if (sscanf(p, " %d/%d%n", &num, &den, &used) != 2)
            break;
        out[n++] = (AVRational){num, den};
        p += used;
    }
    return n;
}

static uint16_t *plane(const char *name, int w, int h)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.u16", g_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    uint16_t *p = malloc((size_t)w * h * 2);
    if (fread(p, 2, (size_t)w * h, f) != (size_t)w * h)
        exit(2);
    fclose(f);
    return p;
}

static AVDOVIMetadata *build_rpu(void)
{
    size_t size;
    AVDOVIMetadata *m = av_dovi_metadata_alloc(&size);
    AVDOVIRpuDataHeader *h = av_dovi_get_header(m);
    AVDOVIDataMapping *map = av_dovi_get_mapping(m);
    AVDOVIColorMetadata *c = av_dovi_get_color(m);
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
    for (int k = 0; k < 3; ++k) {
        AVDOVIReshapingCurve *cv = &map->curves[k];
        char key[80];
        long long b[32];
        snprintf(key, sizeof key, "curve%d.num_pivots", k);
        cv->num_pivots = (uint8_t)ki(key);
        snprintf(key, sizeof key, "curve%d.pivots", k);
        kis(key, b, 9);
        for (int i = 0; i < cv->num_pivots; ++i)
            cv->pivots[i] = (uint16_t)b[i];
        for (int i = 0; i + 1 < cv->num_pivots; ++i) {
            snprintf(key, sizeof key, "curve%d.piece%d.mapping_idc", k, i);
            cv->mapping_idc[i] = (enum AVDOVIMappingMethod)ki(key);
            if (cv->mapping_idc[i] == AV_DOVI_MAPPING_POLYNOMIAL) {
                snprintf(key, sizeof key, "curve%d.piece%d.poly", k, i);
                kis(key, b, 4);
                cv->poly_order[i] = (uint8_t)b[0];
                for (int j = 0; j < 3; ++j)
                    cv->poly_coef[i][j] = b[1 + j];
            } else {
                snprintf(key, sizeof key, "curve%d.piece%d.mmr_order", k, i);
                cv->mmr_order[i] = (uint8_t)ki(key);
                snprintf(key, sizeof key, "curve%d.piece%d.mmr_constant", k, i);
                cv->mmr_constant[i] = ki(key);
                for (int o = 0; o < cv->mmr_order[i]; ++o) {
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
    long long e[4];
    if (kis("color.signal_eotf", e, 4) == 4) {
        c->signal_eotf = (uint16_t)e[0];
        c->signal_eotf_param0 = (uint16_t)e[1];
        c->signal_eotf_param1 = (uint16_t)e[2];
        c->signal_eotf_param2 = (uint32_t)e[3];
    }
    c->signal_bit_depth = (uint8_t)ki("color.signal_bit_depth");
    return m;
}

static pl_tex upload(pl_gpu gpu, const uint16_t *data, int w, int h)
{
    pl_fmt fmt = pl_find_fmt(gpu, PL_FMT_UNORM, 1, 16, 16, PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR);
    return fmt ? pl_tex_create(gpu, pl_tex_params(.w = w, .h = h, .format = fmt, .sampleable = true,
                                                  .initial_data = data))
               : NULL;
}

static void write_codes(const char *prefix, const char *name, const float *v, int n, int depth)
{
    char path[1200];
    snprintf(path, sizeof path, "%s_%s.u16", prefix, name);
    uint16_t *out = malloc((size_t)n * 2);
    for (int i = 0; i < n; ++i)
        out[i] = (uint16_t)lrintf(v[i] * (float)(1 << depth));
    FILE *f = fopen(path, "wb");
    fwrite(out, 2, (size_t)n, f);
    fclose(f);
    free(out);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
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
    pl_log log = pl_log_create(PL_API_VER, pl_log_params(.log_cb = pl_log_simple, .log_level = PL_LOG_INFO));
    pl_opengl gl = pl_opengl_create(log, pl_opengl_params(.get_proc_addr = (pl_voidfunc_t (*)(const char *))eglGetProcAddress,
                                                          .egl_display = dpy, .egl_context = ctx,
                                                          .allow_software = true));
    if (!gl)
        return 1;
    pl_gpu gpu = gl->gpu;

    const int w = (int)ki("bl.width"), h = (int)ki("bl.height"), cw = (w + 1) / 2, ch = (h + 1) / 2;
    const int depth = (int)ki("hdr.bl_bit_depth"), hdr = (int)ki("hdr.vdr_bit_depth");
    struct pl_frame image = {0};
    image.num_planes = 3;
    image.crop = (pl_rect2df){0, 0, w, h};
    const char *names[3] = {"bl_y", "bl_u", "bl_v"};
    for (int i = 0; i < 3; ++i) {
        uint16_t *p = plane(names[i], i ? cw : w, i ? ch : h);
        image.planes[i] = (struct pl_plane){.texture = upload(gpu, p, i ? cw : w, i ? ch : h), .components = 1,
                                            .component_mapping = {i}};
        free(p);
    }
    image.repr.bits = (struct pl_bit_encoding){.sample_depth = 16, .color_depth = depth, .bit_shift = 0};
    AVDOVIMetadata *rpu = build_rpu();
    struct pl_dovi_metadata dovi;
    memset(&dovi, 0, sizeof dovi);
    struct pl_color_repr repr = {0};
    struct pl_color_space color = {0};
    pl_map_avdovi_metadata(&color, &repr, &dovi, rpu);
    repr.bits = image.repr.bits;
    image.repr = repr;
    image.color = color;
    pl_frame_set_chroma_location(&image, PL_CHROMA_TOP_LEFT);
    printf("p84_gpu %s: signal_eotf %u %u %u %u, signal_bit_depth %u, hdr_bit_depth %d\n", g_dir,
           av_dovi_get_color(rpu)->signal_eotf, av_dovi_get_color(rpu)->signal_eotf_param0,
           av_dovi_get_color(rpu)->signal_eotf_param1, av_dovi_get_color(rpu)->signal_eotf_param2,
           av_dovi_get_color(rpu)->signal_bit_depth, hdr);

    struct pl_render_params params = pl_render_default_params;
    params.min_fbo_precision = 32;
    params.peak_detect_params = NULL;
    params.dither_params = NULL;
    params.frame_mixer = NULL;
    params.skip_caching_single_frame = true;
    params.border = PL_CLEAR_SKIP;
    params.dovi_ccm = !(getenv("DV_CCM") && !strcmp(getenv("DV_CCM"), "0"));
    const char *trc = getenv("DV_BL_TRC");
    params.dovi_ccm_bl_transfer = !trc                      ? PL_COLOR_TRC_UNKNOWN
                                  : !strcmp(trc, "bt1886") ? PL_COLOR_TRC_BT_1886
                                  : !strcmp(trc, "hlg")    ? PL_COLOR_TRC_HLG
                                                           : PL_COLOR_TRC_PQ;
    bool composed = false;
    params.dovi_ccm_composed = &composed;
    const char *only = getenv("DV_ONLY");
    pl_tex planes[2] = {0};
    pl_tex target_tex;
    if (only) {
        pl_fmt f1 = pl_find_fmt(gpu, PL_FMT_FLOAT, 1, 32, 32, PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE);
        pl_fmt f2 = pl_find_fmt(gpu, PL_FMT_FLOAT, 2, 32, 32,
                                PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_LINEAR);
        planes[0] = pl_tex_create(gpu, pl_tex_params(.w = w, .h = h, .format = f1, .sampleable = true,
                                                     .renderable = true, .host_readable = true));
        planes[1] = pl_tex_create(gpu, pl_tex_params(.w = cw, .h = ch, .format = f2, .sampleable = true,
                                                     .renderable = true, .host_readable = true));
        params.dovi_ccm_only = true;
        params.dovi_ccm_planes[0] = planes[0];
        params.dovi_ccm_planes[1] = planes[1];
    }
    pl_fmt tfmt = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_SAMPLEABLE);
    target_tex = pl_tex_create(gpu, pl_tex_params(.w = 3840, .h = 2160, .format = tfmt, .renderable = true,
                                                  .sampleable = true, .host_readable = true));
    struct pl_frame target = {
        .num_planes = 1,
        .planes = {{.texture = target_tex, .components = 4, .component_mapping = {0, 1, 2, 3}}},
        .repr = pl_color_repr_rgb,
        .color = color,
    };
    pl_renderer rr = pl_renderer_create(log, gpu);
    const bool ok = pl_render_image(rr, &image, &target, &params);
    struct pl_render_errors errs = pl_renderer_get_errors(rr);
    printf("render %s, composed %d, render errors 0x%x, dovi_ccm %d, bl_transfer %s\n", ok ? "ok" : "FAILED",
           composed, (unsigned)errs.errors, params.dovi_ccm, trc ? trc : "unknown");
    if (only && ok && composed) {
        float *y = malloc((size_t)w * h * 4), *c = malloc((size_t)cw * ch * 8);
        if (!pl_tex_download(gpu, pl_tex_transfer_params(.tex = planes[0], .ptr = y)) ||
            !pl_tex_download(gpu, pl_tex_transfer_params(.tex = planes[1], .ptr = c)))
            return 1;
        float *cb = malloc((size_t)cw * ch * 4), *cr = malloc((size_t)cw * ch * 4);
        for (int i = 0; i < cw * ch; ++i) {
            cb[i] = c[2 * i];
            cr[i] = c[2 * i + 1];
        }
        write_codes(only, "y", y, w * h, hdr);
        write_codes(only, "cb", cb, cw * ch, hdr);
        write_codes(only, "cr", cr, cw * ch, hdr);
        printf("written %s_{y,cb,cr}.u16\n", only);
    }
    return 0;
}
