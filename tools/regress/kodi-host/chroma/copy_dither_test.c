/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades: the dither of the bridge's copy into an 8-bit window (tools/dvbridge/dvbridge_gl_pack.c, the copier
 * that CDVBridgeGLES::CopyConversion() writes the pictures it maps or upscales with), on the software GPU:
 *  1. the thresholds (dvbridge_gl_blue_noise()): each of the 4096 ranks once, the same at each call, and blue:
 *     the mean of every 8 x 8 block near 0.5 (white noise strays four times as far);
 *  2. into 255 levels: every value goes to the level below it or the one above, never further; a flat value
 *     between two levels comes out as their mix in the right proportion (its mean within 0.01 of a level over
 *     64 x 64 pixels); a value on a level, black among them, stays on it; a gradient is dithered (not the
 *     rounding of before); in limited range the same within 16 to 235;
 *  3. into 1023 levels (10 bits): the rounding of before, exactly; into a floating-point window, the values.
 * usage: copy_dither_test     (EGL surfaceless, OpenGL ES 3.1; each failure prints FAIL)
 */
#include "dvbridge_gl_pack.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 256, H = 128, S = DVBRIDGE_GL_NOISE };

static int checks, failures;
static void check(int ok, const char *fmt, ...)
{
    ++checks;
    if (ok)
        return;
    ++failures;
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static int cmp_float(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static void test_noise(void)
{
    static float a[S * S], b[S * S], sorted[S * S];
    dvbridge_gl_blue_noise(a);
    dvbridge_gl_blue_noise(b);
    check(!memcmp(a, b, sizeof(a)), "the thresholds are the same at each call");
    memcpy(sorted, a, sizeof(a));
    qsort(sorted, S * S, sizeof(float), cmp_float);
    int ranks = 1;
    for (int i = 0; i < S * S; ++i)
        ranks &= fabsf(sorted[i] - ((float)i + 0.5f) / (S * S)) < 1e-7f;
    check(ranks, "each rank of 4096 once, as (rank + 0.5) / 4096");
    double worst = 0;
    for (int by = 0; by < S; by += 8)
        for (int bx = 0; bx < S; bx += 8) {
            double m = 0;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    m += a[(by + y) * S + bx + x];
            m /= 64;
            worst = fabs(m - 0.5) > worst ? fabs(m - 0.5) : worst;
        }
    check(worst < 0.05, "blue: every 8 x 8 block has a mean near 0.5 (%.4f from it at most)", worst);
}

/* copies a float picture into a window of that format; out: the codes (or values) of R */
static int copy_to(struct dvbridge_gl_copier *c, GLuint picture, GLenum format, int limited, double *out)
{
    GLuint fbo, rb;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &rb);
    glBindTexture(GL_TEXTURE_2D, rb);
    glTexStorage2D(GL_TEXTURE_2D, 1, format, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rb, 0);
    const int levels = dvbridge_gl_copy_levels(fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    int ok = dvbridge_gl_copy(c, picture, fbo, W, H, 0, 0, W, H, false, limited);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (format == GL_RGBA8) {
        static unsigned char px[W * H * 4];
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        for (int i = 0; i < W * H; ++i)
            out[i] = px[i * 4];
    } else if (format == GL_RGB10_A2) {
        static unsigned px[W * H];
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, px);
        for (int i = 0; i < W * H; ++i)
            out[i] = px[i] & 1023;
    } else {
        static float px[W * H * 4];
        glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px);
        for (int i = 0; i < W * H; ++i)
            out[i] = px[i * 4];
    }
    ok = ok && glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &rb);
    return ok ? levels : -2;
}

static GLuint picture_of(const float *v)
{
    static float px[W * H * 4];
    for (int i = 0; i < W * H; ++i)
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v[i], px[i * 4 + 3] = 1;
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, W, H);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_FLOAT, px);
    return t;
}

int main(void)
{
    EGLDisplay dpy = EGL_NO_DISPLAY;
    PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get)
        dpy = get(0x31DD, EGL_DEFAULT_DISPLAY, NULL);
    EGLint major, minor, n = 0;
    EGLConfig cfg;
    const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || !n) {
        printf("FAIL: no EGL display of OpenGL ES 3\n");
        return 1;
    }
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    if (ctx == EGL_NO_CONTEXT || pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx)) {
        printf("FAIL: no OpenGL ES 3.1 context\n");
        return 1;
    }
    test_noise();
    struct dvbridge_gl_copier *c = dvbridge_gl_copier_create();
    if (!c) {
        printf("FAIL: no copier\n");
        return 1;
    }
    static float ramp[W * H], flat[W * H], onlevel[W * H];
    static double out[W * H];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            ramp[y * W + x] = (float)(0.2 + 0.1 * x / (W - 1)); /* about 25 levels over 256 pixels */
            flat[y * W + x] = (float)((100 + 0.3) / 255.0);      /* 30 % of the way from 100 to 101 */
            onlevel[y * W + x] = (float)((x % 3 ? 37 + x % 7 : 0) / 255.0);
        }
    GLuint pr = picture_of(ramp), pf = picture_of(flat), po = picture_of(onlevel);

    /* 2. 8 bits */
    int levels = copy_to(c, pr, GL_RGBA8, 0, out);
    check(levels == 255, "an 8-bit window has 255 levels (%d)", levels);
    int beyond = 0, notround = 0;
    for (int i = 0; i < W * H; ++i) {
        const double v = ramp[i] * 255.0;
        beyond += out[i] != floor(v) && out[i] != floor(v) + 1;
        notround += out[i] != floor(v + 0.5);
    }
    check(beyond == 0, "8 bits: every value goes to the level below or above it (%d do not)", beyond);
    check(notround > W * H / 10, "8 bits: a gradient is dithered, not rounded (%d of %d pixels differ from rounding)",
          notround, W * H);
    copy_to(c, pf, GL_RGBA8, 0, out);
    double mean = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            mean += out[y * W + x];
    mean /= 64 * 64;
    check(fabs(mean - 100.3) < 0.01, "8 bits: a flat 100.3 comes out as 100.3 on average (%.4f)", mean);
    copy_to(c, po, GL_RGBA8, 0, out);
    int moved = 0;
    for (int i = 0; i < W * H; ++i)
        moved += out[i] != floor(onlevel[i] * 255.0 + 0.5);
    check(moved == 0, "8 bits: values on a level, black among them, stay there (%d moved)", moved);
    copy_to(c, pf, GL_RGBA8, 1, out);
    mean = 0;
    int outside = 0;
    for (int i = 0; i < W * H; ++i) {
        outside += out[i] < 16 || out[i] > 235;
        if (i % W < 64 && i / W < 64)
            mean += out[i];
    }
    mean /= 64 * 64;
    const double want = (100.3 / 255.0 * 219.0 + 16.0);
    check(outside == 0 && fabs(mean - want) < 0.01, "8 bits, limited range: %.4f on average, %.4f wanted, %d outside",
          mean, want, outside);

    /* 3. 10 bits and floating point: as before */
    levels = copy_to(c, pr, GL_RGB10_A2, 0, out);
    check(levels == 1023, "a 10-bit window has 1023 levels (%d)", levels);
    int differ = 0;
    for (int i = 0; i < W * H; ++i)
        differ += out[i] != floor(ramp[i] * 1023.0 + 0.5);
    check(differ == 0, "10 bits: the rounding of before, exactly (%d pixels differ)", differ);
    if (copy_to(c, pr, GL_RGBA16F, 0, out) == 0) {
        double worst = 0;
        for (int i = 0; i < W * H; ++i)
            worst = fabs(out[i] - ramp[i]) > worst ? fabs(out[i] - ramp[i]) : worst;
        check(worst < 1e-3, "floating point: the values as they are (%.5f from them at most)", worst);
    } else
        printf("(no floating-point window that renders here: not checked)\n");
    dvbridge_gl_copier_destroy(c);
    printf("%d checks, %d failed\n", checks, failures);
    printf("%s\n", failures ? "COPY-DITHER FAILED" : "COPY-DITHER PASSED");
    return failures ? 1 : 0;
}
