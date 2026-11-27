// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// fakeva.c: a VA-API driver that decodes nothing, for the stage slkeep of run-ffmpeg.sh. libva loads it as
// hadesfake_drv_video.so from LIBVA_DRIVERS_PATH for the display that slkeep.c makes. It takes what FFmpeg's VA-API
// hwaccel (libavcodec/vaapi_decode.c, vaapi_hevc.c) gives a decoder and writes to $HADES_FAKEVA_LOG, one line each:
//   CONFIG <id> profile <VAProfile>                      vaCreateConfig()
//   CONTEXT <id> config <id> <w>x<h> targets <n>         vaCreateContext(): the hardware decoder is set up
//   DESTROY-CONTEXT <id>                                 vaDestroyContext()
//   PIC <n> context <id> pp <hash> iq <hash|-> sp <hash> sd <hash>
//                                                        vaEndPicture(): an FNV-1a hash of each kind of buffer the
//                                                        picture was given: its picture parameters (with the surface
//                                                        ids of the picture and its references made 1, or left
//                                                        VA_INVALID_SURFACE, as a surface id depends on the pool), the
//                                                        IQ matrix (VAIQMatrixBufferHEVC: the scaling lists; - when it
//                                                        was given none), the slice parameters and the slice data
//   IQ <n> <hex of the 4x4 luma intra list and the 16x16 DC values>
//                                                        the same IQ matrix, readable
// Surfaces are ids only. HEVC Main and Main 10 (NV12, P010), VLD.
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <va/va.h>
#include <va/va_backend.h>
#include <va/va_dec_hevc.h>

#define MAX_BUF 4096
#define MAX_CFG 64

typedef struct Buf { int used; VABufferType type; unsigned size; uint8_t *data; } Buf;

static Buf bufs[MAX_BUF];
static VAProfile cfg_profile[MAX_CFG];
static unsigned next_cfg = 1, next_ctx = 1, next_surface = 0x100, pic_count;
static FILE *logf;
static uint64_t h_pp, h_iq, h_sp, h_sd;
static int have_iq, in_picture;
static VAContextID cur_ctx;
static char iq_text[512];

static uint64_t fnv(uint64_t h, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}
#define FNV0 0xcbf29ce484222325ULL

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    if (!logf)
        return;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fflush(logf);
}

static Buf *buf_of(VABufferID id)
{
    return id < MAX_BUF && bufs[id].used ? &bufs[id] : NULL;
}

static VAStatus f_terminate(VADriverContextP ctx)
{
    (void)ctx;
    for (int i = 0; i < MAX_BUF; i++)
        free(bufs[i].data);
    memset(bufs, 0, sizeof(bufs));
    if (logf)
        fclose(logf);
    logf = NULL;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_query_profiles(VADriverContextP ctx, VAProfile *list, int *n)
{
    (void)ctx;
    list[0] = VAProfileHEVCMain;
    list[1] = VAProfileHEVCMain10;
    *n = 2;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_query_entrypoints(VADriverContextP ctx, VAProfile p, VAEntrypoint *list, int *n)
{
    (void)ctx;
    if (p != VAProfileHEVCMain && p != VAProfileHEVCMain10)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    list[0] = VAEntrypointVLD;
    *n = 1;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_get_config_attributes(VADriverContextP ctx, VAProfile p, VAEntrypoint e, VAConfigAttrib *a, int n)
{
    (void)ctx; (void)p; (void)e;
    for (int i = 0; i < n; i++)
        a[i].value = a[i].type == VAConfigAttribRTFormat ? VA_RT_FORMAT_YUV420 | VA_RT_FORMAT_YUV420_10
                                                          : VA_ATTRIB_NOT_SUPPORTED;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_config(VADriverContextP ctx, VAProfile p, VAEntrypoint e, VAConfigAttrib *a, int n,
                                VAConfigID *id)
{
    (void)ctx; (void)a; (void)n;
    if ((p != VAProfileHEVCMain && p != VAProfileHEVCMain10) || e != VAEntrypointVLD)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    if (next_cfg >= MAX_CFG)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    *id = next_cfg++;
    cfg_profile[*id] = p;
    say("CONFIG %u profile %d\n", *id, (int)p);
    return VA_STATUS_SUCCESS;
}

static VAStatus f_destroy_config(VADriverContextP ctx, VAConfigID id)
{
    (void)ctx; (void)id;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_query_config_attributes(VADriverContextP ctx, VAConfigID id, VAProfile *p, VAEntrypoint *e,
                                          VAConfigAttrib *a, int *n)
{
    (void)ctx; (void)a;
    if (id == 0 || id >= next_cfg)
        return VA_STATUS_ERROR_INVALID_CONFIG;
    *p = cfg_profile[id];
    *e = VAEntrypointVLD;
    *n = 0;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_surfaces(VADriverContextP ctx, int w, int h, int format, int n, VASurfaceID *s)
{
    (void)ctx; (void)w; (void)h; (void)format;
    for (int i = 0; i < n; i++)
        s[i] = next_surface++;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_surfaces2(VADriverContextP ctx, unsigned format, unsigned w, unsigned h, VASurfaceID *s,
                                   unsigned n, VASurfaceAttrib *a, unsigned na)
{
    (void)a; (void)na;
    return f_create_surfaces(ctx, (int)w, (int)h, (int)format, (int)n, s);
}

static VAStatus f_destroy_surfaces(VADriverContextP ctx, VASurfaceID *s, int n)
{
    (void)ctx; (void)s; (void)n;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_query_surface_attributes(VADriverContextP ctx, VAConfigID id, VASurfaceAttrib *a, unsigned *n)
{
    VASurfaceAttrib l[6];
    unsigned k = 0;
    (void)ctx;
    if (id == 0 || id >= next_cfg)
        return VA_STATUS_ERROR_INVALID_CONFIG;
#define ATTR(t, v) do { l[k].type = (t); l[k].flags = VA_SURFACE_ATTRIB_GETTABLE; \
                        l[k].value.type = VAGenericValueTypeInteger; l[k].value.value.i = (v); k++; } while (0)
    ATTR(VASurfaceAttribPixelFormat, cfg_profile[id] == VAProfileHEVCMain10 ? VA_FOURCC_P010 : VA_FOURCC_NV12);
    ATTR(VASurfaceAttribMinWidth, 16);
    ATTR(VASurfaceAttribMinHeight, 16);
    ATTR(VASurfaceAttribMaxWidth, 8192);
    ATTR(VASurfaceAttribMaxHeight, 8192);
    if (!a) {
        *n = k;
        return VA_STATUS_SUCCESS;
    }
    if (*n < k)
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    memcpy(a, l, k * sizeof(*l));
    *n = k;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_context(VADriverContextP ctx, VAConfigID cfg, int w, int h, int flag, VASurfaceID *t, int n,
                                 VAContextID *id)
{
    (void)ctx; (void)flag; (void)t;
    *id = next_ctx++;
    say("CONTEXT %u config %u %dx%d targets %d\n", *id, cfg, w, h, n);
    return VA_STATUS_SUCCESS;
}

static VAStatus f_destroy_context(VADriverContextP ctx, VAContextID id)
{
    (void)ctx;
    say("DESTROY-CONTEXT %u\n", id);
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_buffer(VADriverContextP ctx, VAContextID c, VABufferType type, unsigned size, unsigned num,
                                void *data, VABufferID *id)
{
    (void)ctx; (void)c;
    for (unsigned i = 1; i < MAX_BUF; i++) {
        if (bufs[i].used)
            continue;
        bufs[i].data = calloc(1, (size_t)size * num + 1);
        if (!bufs[i].data)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        if (data)
            memcpy(bufs[i].data, data, (size_t)size * num);
        bufs[i].used = 1;
        bufs[i].type = type;
        bufs[i].size = size * num;
        *id = i;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_ALLOCATION_FAILED;
}

static VAStatus f_buffer_set_num_elements(VADriverContextP ctx, VABufferID id, unsigned n)
{
    (void)ctx; (void)n;
    return buf_of(id) ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_BUFFER;
}

static VAStatus f_map_buffer(VADriverContextP ctx, VABufferID id, void **p)
{
    Buf *b = buf_of(id);
    (void)ctx;
    if (!b)
        return VA_STATUS_ERROR_INVALID_BUFFER;
    *p = b->data;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_unmap_buffer(VADriverContextP ctx, VABufferID id)
{
    (void)ctx;
    return buf_of(id) ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_BUFFER;
}

static VAStatus f_destroy_buffer(VADriverContextP ctx, VABufferID id)
{
    Buf *b = buf_of(id);
    (void)ctx;
    if (!b)
        return VA_STATUS_ERROR_INVALID_BUFFER;
    free(b->data);
    memset(b, 0, sizeof(*b));
    return VA_STATUS_SUCCESS;
}

static VAStatus f_begin_picture(VADriverContextP ctx, VAContextID c, VASurfaceID t)
{
    (void)ctx; (void)t;
    if (in_picture)
        return VA_STATUS_ERROR_OPERATION_FAILED;
    in_picture = 1;
    cur_ctx = c;
    h_pp = h_iq = h_sp = h_sd = FNV0;
    have_iq = 0;
    iq_text[0] = 0;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_render_picture(VADriverContextP ctx, VAContextID c, VABufferID *ids, int n)
{
    (void)ctx;
    if (!in_picture || c != cur_ctx)
        return VA_STATUS_ERROR_OPERATION_FAILED;
    for (int i = 0; i < n; i++) {
        Buf *b = buf_of(ids[i]);
        if (!b)
            return VA_STATUS_ERROR_INVALID_BUFFER;
        switch (b->type) {
        case VAPictureParameterBufferType: {
            VAPictureParameterBufferHEVC pp;
            if (b->size < sizeof(pp))
                return VA_STATUS_ERROR_INVALID_PARAMETER;
            memcpy(&pp, b->data, sizeof(pp));
            if (pp.CurrPic.picture_id != VA_INVALID_SURFACE)
                pp.CurrPic.picture_id = 1;
            for (int j = 0; j < 15; j++)
                if (pp.ReferenceFrames[j].picture_id != VA_INVALID_SURFACE)
                    pp.ReferenceFrames[j].picture_id = 1;
            h_pp = fnv(h_pp, (const uint8_t *)&pp, sizeof(pp));
            h_pp = fnv(h_pp, b->data + sizeof(pp), b->size - sizeof(pp));
            break;
        }
        case VAIQMatrixBufferType: {
            const VAIQMatrixBufferHEVC *iq = (const VAIQMatrixBufferHEVC *)b->data;
            size_t o = 0;
            if (b->size < sizeof(*iq))
                return VA_STATUS_ERROR_INVALID_PARAMETER;
            // the lists only: va_reserved is not set by FFmpeg (a variable of the stack)
            h_iq = fnv(h_iq, b->data, offsetof(VAIQMatrixBufferHEVC, va_reserved));
            have_iq = 1;
            for (int j = 0; j < 16 && o < sizeof(iq_text) - 8; j++)
                o += snprintf(iq_text + o, sizeof(iq_text) - o, "%02x", iq->ScalingList4x4[0][j]);
            o += snprintf(iq_text + o, sizeof(iq_text) - o, " dc");
            for (int j = 0; j < 6 && o < sizeof(iq_text) - 8; j++)
                o += snprintf(iq_text + o, sizeof(iq_text) - o, " %02x", iq->ScalingListDC16x16[j]);
            break;
        }
        case VASliceParameterBufferType:
            h_sp = fnv(h_sp, b->data, b->size);
            break;
        case VASliceDataBufferType:
            h_sd = fnv(h_sd, b->data, b->size);
            break;
        default:
            say("BUFFER-TYPE %d\n", (int)b->type);
            break;
        }
    }
    return VA_STATUS_SUCCESS;
}

static VAStatus f_end_picture(VADriverContextP ctx, VAContextID c)
{
    (void)ctx;
    if (!in_picture || c != cur_ctx)
        return VA_STATUS_ERROR_OPERATION_FAILED;
    in_picture = 0;
    if (have_iq)
        say("PIC %u context %u pp %016llx iq %016llx sp %016llx sd %016llx\nIQ %u %s\n", pic_count, c,
            (unsigned long long)h_pp, (unsigned long long)h_iq, (unsigned long long)h_sp, (unsigned long long)h_sd,
            pic_count, iq_text);
    else
        say("PIC %u context %u pp %016llx iq - sp %016llx sd %016llx\n", pic_count, c,
            (unsigned long long)h_pp, (unsigned long long)h_sp, (unsigned long long)h_sd);
    pic_count++;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_sync_surface(VADriverContextP ctx, VASurfaceID s)
{
    (void)ctx; (void)s;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_query_surface_status(VADriverContextP ctx, VASurfaceID s, VASurfaceStatus *st)
{
    (void)ctx; (void)s;
    *st = VASurfaceReady;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_query_image_formats(VADriverContextP ctx, VAImageFormat *l, int *n)
{
    (void)ctx;
    memset(l, 0, 2 * sizeof(*l));
    l[0].fourcc = VA_FOURCC_NV12;
    l[0].byte_order = VA_LSB_FIRST;
    l[0].bits_per_pixel = 12;
    l[1].fourcc = VA_FOURCC_P010;
    l[1].byte_order = VA_LSB_FIRST;
    l[1].bits_per_pixel = 24;
    *n = 2;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_image(VADriverContextP ctx, VAImageFormat *f, int w, int h, VAImage *img)
{
    (void)ctx; (void)f; (void)w; (void)h; (void)img;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_derive_image(VADriverContextP ctx, VASurfaceID s, VAImage *img)
{
    (void)ctx; (void)s; (void)img;
    return VA_STATUS_ERROR_OPERATION_FAILED;
}

static VAStatus f_destroy_image(VADriverContextP ctx, VAImageID img)
{
    (void)ctx; (void)img;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_set_image_palette(VADriverContextP ctx, VAImageID img, unsigned char *p)
{
    (void)ctx; (void)img; (void)p;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_get_image(VADriverContextP ctx, VASurfaceID s, int x, int y, unsigned w, unsigned h, VAImageID img)
{
    (void)ctx; (void)s; (void)x; (void)y; (void)w; (void)h; (void)img;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_put_image(VADriverContextP ctx, VASurfaceID s, VAImageID img, int sx, int sy, unsigned sw,
                            unsigned sh, int dx, int dy, unsigned dw, unsigned dh)
{
    (void)ctx; (void)s; (void)img; (void)sx; (void)sy; (void)sw; (void)sh; (void)dx; (void)dy; (void)dw; (void)dh;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_query_subpicture_formats(VADriverContextP ctx, VAImageFormat *l, unsigned *flags, unsigned *n)
{
    (void)ctx; (void)l; (void)flags;
    *n = 0;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_create_subpicture(VADriverContextP ctx, VAImageID img, VASubpictureID *s)
{
    (void)ctx; (void)img; (void)s;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_destroy_subpicture(VADriverContextP ctx, VASubpictureID s)
{
    (void)ctx; (void)s;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_set_subpicture_image(VADriverContextP ctx, VASubpictureID s, VAImageID img)
{
    (void)ctx; (void)s; (void)img;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_set_subpicture_chromakey(VADriverContextP ctx, VASubpictureID s, unsigned a, unsigned b, unsigned c)
{
    (void)ctx; (void)s; (void)a; (void)b; (void)c;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_set_subpicture_global_alpha(VADriverContextP ctx, VASubpictureID s, float a)
{
    (void)ctx; (void)s; (void)a;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_associate_subpicture(VADriverContextP ctx, VASubpictureID s, VASurfaceID *t, int n, short sx,
                                       short sy, unsigned short sw, unsigned short sh, short dx, short dy,
                                       unsigned short dw, unsigned short dh, unsigned flags)
{
    (void)ctx; (void)s; (void)t; (void)n; (void)sx; (void)sy; (void)sw; (void)sh; (void)dx; (void)dy; (void)dw;
    (void)dh; (void)flags;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_deassociate_subpicture(VADriverContextP ctx, VASubpictureID s, VASurfaceID *t, int n)
{
    (void)ctx; (void)s; (void)t; (void)n;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_query_display_attributes(VADriverContextP ctx, VADisplayAttribute *l, int *n)
{
    (void)ctx; (void)l;
    *n = 0;
    return VA_STATUS_SUCCESS;
}

static VAStatus f_get_display_attributes(VADriverContextP ctx, VADisplayAttribute *l, int n)
{
    (void)ctx; (void)l; (void)n;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus f_set_display_attributes(VADriverContextP ctx, VADisplayAttribute *l, int n)
{
    (void)ctx; (void)l; (void)n;
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

#define INIT_NAME2(a, b) __vaDriverInit_##a##_##b
#define INIT_NAME(a, b) INIT_NAME2(a, b)

VAStatus INIT_NAME(VA_MAJOR_VERSION, VA_MINOR_VERSION)(VADriverContextP ctx);
VAStatus INIT_NAME(VA_MAJOR_VERSION, VA_MINOR_VERSION)(VADriverContextP ctx)
{
    struct VADriverVTable *v = ctx->vtable;
    const char *path = getenv("HADES_FAKEVA_LOG");

    if (path && !logf)
        logf = fopen(path, "a");
    ctx->version_major = VA_MAJOR_VERSION;
    ctx->version_minor = VA_MINOR_VERSION;
    ctx->max_profiles = 2;
    ctx->max_entrypoints = 1;
    ctx->max_attributes = 1;
    ctx->max_image_formats = 2;
    ctx->max_subpic_formats = 1;
    ctx->max_display_attributes = 1;
    ctx->str_vendor = "Hades fake VA-API driver (decodes nothing)";
    v->vaTerminate = f_terminate;
    v->vaQueryConfigProfiles = f_query_profiles;
    v->vaQueryConfigEntrypoints = f_query_entrypoints;
    v->vaGetConfigAttributes = f_get_config_attributes;
    v->vaCreateConfig = f_create_config;
    v->vaDestroyConfig = f_destroy_config;
    v->vaQueryConfigAttributes = f_query_config_attributes;
    v->vaCreateSurfaces = f_create_surfaces;
    v->vaCreateSurfaces2 = f_create_surfaces2;
    v->vaDestroySurfaces = f_destroy_surfaces;
    v->vaQuerySurfaceAttributes = f_query_surface_attributes;
    v->vaCreateContext = f_create_context;
    v->vaDestroyContext = f_destroy_context;
    v->vaCreateBuffer = f_create_buffer;
    v->vaBufferSetNumElements = f_buffer_set_num_elements;
    v->vaMapBuffer = f_map_buffer;
    v->vaUnmapBuffer = f_unmap_buffer;
    v->vaDestroyBuffer = f_destroy_buffer;
    v->vaBeginPicture = f_begin_picture;
    v->vaRenderPicture = f_render_picture;
    v->vaEndPicture = f_end_picture;
    v->vaSyncSurface = f_sync_surface;
    v->vaQuerySurfaceStatus = f_query_surface_status;
    v->vaQueryImageFormats = f_query_image_formats;
    v->vaCreateImage = f_create_image;
    v->vaDeriveImage = f_derive_image;
    v->vaDestroyImage = f_destroy_image;
    v->vaSetImagePalette = f_set_image_palette;
    v->vaGetImage = f_get_image;
    v->vaPutImage = f_put_image;
    v->vaQuerySubpictureFormats = f_query_subpicture_formats;
    v->vaCreateSubpicture = f_create_subpicture;
    v->vaDestroySubpicture = f_destroy_subpicture;
    v->vaSetSubpictureImage = f_set_subpicture_image;
    v->vaSetSubpictureChromakey = f_set_subpicture_chromakey;
    v->vaSetSubpictureGlobalAlpha = f_set_subpicture_global_alpha;
    v->vaAssociateSubpicture = f_associate_subpicture;
    v->vaDeassociateSubpicture = f_deassociate_subpicture;
    v->vaQueryDisplayAttributes = f_query_display_attributes;
    v->vaGetDisplayAttributes = f_get_display_attributes;
    v->vaSetDisplayAttributes = f_set_display_attributes;
    return VA_STATUS_SUCCESS;
}
