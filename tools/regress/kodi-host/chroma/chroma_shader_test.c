/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades: Kodi's own YUV shaders of a tree (system/shaders/GLES), put together as CShader::LoadSource()
 * and InsertSource() and BaseYUV2RGBGLSLShader put them together (the defines after #version; for a chroma
 * filter, KODI_CHROMA and KODI_CHROMA_LANCZOS3 or KODI_CHROMA_SPLINE16 and gles_yuv2rgb_chroma.frag before
 * main(), then gles_tonemap.frag and the dither files), run on the software GPU, against a model of each
 * way of taking the chroma computed here in double precision:
 *   KODI      the texture's bilinear filter at the place of the output pixel, every video taken as centred
 *             (Kodi's own sampling, which the shaders keep without the chroma defines);
 *   SITED     the same, the samples where the video places them (left, centre, top-left, top, bottom-left);
 *   SPLINE16  Spline16 over 4 x 4 samples there;
 *   LANCZOS3  Lanczos 3 over 6 x 6 samples there (normalized).
 * Each on random 4:2:0 chroma with a flat luma, at the picture's size and drawn 2.5 times larger, in the four
 * chroma layouts the renderer uploads (NV12_RRG of VAAPI: R16 + RG16; YV12_HI: three R16; NV12 of a software
 * frame: LUMINANCE + LUMINANCE_ALPHA; YV12: LUMINANCE + LUMINANCE + ALPHA), with the basic shader of GLES 2.0
 * and the filter shader of GLES 3.1 (NV12_RRG; its luma filter on a flat luma is flat). A check passes when
 * no output value is further from the model than the texture filter's own precision allows (bilinear: 1/256 of
 * the difference of neighbouring samples, 8-bit fixed-point weights; one or more samples read at the centres of
 * their texels: float rounding). A flat chroma comes out exactly flat with every filter.
 * usage: chroma_shader_test <Kodi tree>       (EGL surfaceless, OpenGL ES 3.1; each failure prints FAIL)
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_R16_EXT
#define GL_R16_EXT 0x822A
#define GL_RG16_EXT 0x822C
#endif

enum { W = 64, H = 48, CW = W / 2, CH = H / 2 };
enum filter { KODI, SITED, SPLINE16, LANCZOS3 };
static const char *FILTER_NAMES[] = {"KODI", "SITED", "SPLINE16", "LANCZOS3"};
enum layout { NV12_RRG, YV12_HI, NV12, YV12 };
static const char *LAYOUT_NAMES[] = {"NV12_RRG", "YV12_HI", "NV12", "YV12"};
static const char *LAYOUT_DEFINES[] = {"#define XBMC_NV12_RRG\n", "#define XBMC_YV12_HI\n", "#define XBMC_NV12\n",
                                       "#define XBMC_YV12\n"};
struct siting {
    const char *name;
    double x, y; /* m_chromaShift */
};
static const struct siting SITINGS[] = {{"left", 0.25, 0}, {"centre", 0, 0}, {"top-left", 0.25, 0.25},
                                        {"top", 0, 0.25}, {"bottom-left", 0.25, -0.25}};

static int checks, failures;
static void check(int ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
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

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b)
        b[n] = 0;
    return b;
}

/* insert text t at pos of the string *s */
static void insert_at(char **s, size_t pos, const char *t)
{
    size_t n = strlen(*s), tl = strlen(t);
    *s = realloc(*s, n + tl + 1);
    memmove(*s + pos + tl, *s + pos, n - pos + 1);
    memcpy(*s + pos, t, tl);
}

static char *load(const char *tree, const char *sub, const char *name, const char *defines)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/system/shaders/GLES/%s/%s", tree, sub, name);
    char *s = slurp(path);
    if (!s)
        return NULL;
    char *v = strstr(s, "#version");
    size_t pos = 0;
    if (v && strchr(v, '\n'))
        pos = (size_t)(strchr(v, '\n') - s) + 1;
    insert_at(&s, pos, defines);
    return s;
}

static int insert_file(char **s, const char *tree, const char *name, const char *loc)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/system/shaders/GLES/2.0/%s", tree, name);
    char *t = slurp(path);
    char *at = t ? strstr(*s, loc) : NULL;
    if (!at) {
        free(t);
        return 0;
    }
    insert_at(s, (size_t)(at - *s), t);
    free(t);
    return 1;
}

static GLuint compile(GLenum type, const char *src, const char *what)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        printf("FAIL: %s does not compile: %.600s\n", what, log);
        ++failures;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* the program of a layout and filter: basic (GLES 2.0) or filter (GLES 3.1) shader */
static GLuint program(const char *tree, int filt310, enum layout l, enum filter f, const char *what)
{
    char defines[512];
    snprintf(defines, sizeof(defines), "%s%s%s", LAYOUT_DEFINES[l], f != KODI ? "#define KODI_CHROMA\n" : "",
             f == LANCZOS3 ? "#define KODI_CHROMA_LANCZOS3\n" : f == SPLINE16 ? "#define KODI_CHROMA_SPLINE16\n" : "");
    char *vs = load(tree, filt310 ? "3.1" : "2.0", filt310 ? "gles310_yuv2rgb.vert" : "gles_yuv2rgb.vert",
                    filt310 ? "" : LAYOUT_DEFINES[l]);
    char *fs = load(tree, filt310 ? "3.1" : "2.0", filt310 ? "gles310_yuv2rgb_filter.frag" : "gles_yuv2rgb_basic.frag",
                    defines);
    int ok = vs && fs;
    /* (as YUV2RGBProgressiveShader and YUV2RGBFilterShader: the chroma file first, where they take it) */
    if (ok && f != KODI)
        ok = insert_file(&fs, tree, "gles_yuv2rgb_chroma.frag", "void main()");
    ok = ok && insert_file(&fs, tree, "gles_tonemap.frag", "void main()") &&
         insert_file(&fs, tree, "gles_dither_uniforms.frag", "void main()") &&
         insert_file(&fs, tree, "gles_dither_body.frag", filt310 ? "fragColor = rgb" : "gl_FragColor");
    if (!ok) {
        printf("FAIL: %s: the shader files of %s could not be put together\n", what, tree);
        ++failures;
        free(vs);
        free(fs);
        return 0;
    }
    GLuint v = compile(GL_VERTEX_SHADER, vs, what), fr = compile(GL_FRAGMENT_SHADER, fs, what);
    free(vs);
    free(fs);
    if (!v || !fr)
        return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, fr);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(fr);
    GLint linked = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked) {
        printf("FAIL: %s does not link\n", what);
        ++failures;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ---------------------------------------------------------------------------------------------- the model */
static double sample(const double *plane, int x, int y)
{
    x = x < 0 ? 0 : x >= CW ? CW - 1 : x;
    y = y < 0 ? 0 : y >= CH ? CH - 1 : y;
    return plane[y * CW + x];
}

static double bilinear(const double *plane, double u, double v)
{
    double px = u - 0.5, py = v - 0.5;
    int x0 = (int)floor(px), y0 = (int)floor(py);
    double fx = px - x0, fy = py - y0;
    return (1 - fy) * ((1 - fx) * sample(plane, x0, y0) + fx * sample(plane, x0 + 1, y0)) +
           fy * ((1 - fx) * sample(plane, x0, y0 + 1) + fx * sample(plane, x0 + 1, y0 + 1));
}

static double spline16(double x)
{
    x = fabs(x);
    if (x < 1)
        return ((x - 9.0 / 5.0) * x - 1.0 / 5.0) * x + 1.0;
    if (x < 2) {
        x -= 1;
        return ((-1.0 / 3.0 * x + 4.0 / 5.0) * x - 7.0 / 15.0) * x;
    }
    return 0;
}

static double lanczos3(double x)
{
    if (fabs(x) < 1e-12)
        return 1;
    if (fabs(x) >= 3)
        return 0;
    return 3 * sin(M_PI * x) * sin(M_PI * x / 3) / (M_PI * M_PI * x * x);
}

/* the chroma of one plane at a texture coordinate (in [0, 1]) */
static double model(const double *plane, enum filter f, const struct siting *s, double tu, double tv)
{
    double u = tu * CW, v = tv * CH;
    if (f == KODI)
        return bilinear(plane, u, v);
    if (f == SITED)
        return bilinear(plane, u + s->x, v + s->y);
    const int taps = f == LANCZOS3 ? 6 : 4;
    const double px = u - 0.5 + s->x, py = v - 0.5 + s->y;
    const int bx = (int)floor(px), by = (int)floor(py);
    double sum = 0, wsum = 0;
    for (int j = 0; j < taps; ++j)
        for (int i = 0; i < taps; ++i) {
            const int x = bx - (taps / 2 - 1) + i, y = by - (taps / 2 - 1) + j;
            const double w = f == LANCZOS3 ? lanczos3(px - x) * lanczos3(py - y) : spline16(px - x) * spline16(py - y);
            sum += w * sample(plane, x, y);
            wsum += w;
        }
    return sum / wsum;
}

/* ---------------------------------------------------------------------------------------------- the test */
static double Y_FLAT = 0.5; /* the luma, as a value of the texture */
static double cb[CW * CH], cr[CW * CH];

static void make_chroma(unsigned seed, int flat)
{
    unsigned s = seed;
    for (int i = 0; i < CW * CH; ++i) {
        s = s * 1664525u + 1013904223u;
        cb[i] = flat ? 0.3 : 0.1 + 0.8 * ((s >> 8) & 0xffff) / 65535.0;
        s = s * 1664525u + 1013904223u;
        cr[i] = flat ? 0.65 : 0.1 + 0.8 * ((s >> 8) & 0xffff) / 65535.0;
    }
}

/* a value of [0, 1] as a texture of that layout stores it: 16 bits, or 8 */
static double stored(double v, enum layout l)
{
    return (l == NV12 || l == YV12) ? floor(v * 255 + 0.5) / 255 : floor(v * 65535 + 0.5) / 65535;
}

static void upload(enum layout l, GLuint tex[3], int point)
{
    static unsigned short y16[W * H], c16[CW * CH * 2];
    static unsigned char y8[W * H], c8[CW * CH * 2];
    const int deep = l == NV12_RRG || l == YV12_HI;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < W * H; ++i) {
        y16[i] = (unsigned short)floor(Y_FLAT * 65535 + 0.5);
        y8[i] = (unsigned char)floor(Y_FLAT * 255 + 0.5);
    }
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    if (deep)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16_EXT, W, H, 0, GL_RED, GL_UNSIGNED_SHORT, y16);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, W, H, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, y8);
    if (l == NV12_RRG || l == NV12) {
        for (int i = 0; i < CW * CH; ++i) {
            c16[2 * i] = (unsigned short)floor(cb[i] * 65535 + 0.5);
            c16[2 * i + 1] = (unsigned short)floor(cr[i] * 65535 + 0.5);
            c8[2 * i] = (unsigned char)floor(cb[i] * 255 + 0.5);
            c8[2 * i + 1] = (unsigned char)floor(cr[i] * 255 + 0.5);
        }
        glBindTexture(GL_TEXTURE_2D, tex[1]);
        if (l == NV12_RRG)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16_EXT, CW, CH, 0, GL_RG, GL_UNSIGNED_SHORT, c16);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, CW, CH, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, c8);
        tex[2] = tex[1];
    } else {
        for (int p = 0; p < 2; ++p) {
            const double *src = p ? cr : cb;
            for (int i = 0; i < CW * CH; ++i) {
                c16[i] = (unsigned short)floor(src[i] * 65535 + 0.5);
                c8[i] = (unsigned char)floor(src[i] * 255 + 0.5);
            }
            glBindTexture(GL_TEXTURE_2D, tex[1 + p]);
            if (l == YV12_HI)
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R16_EXT, CW, CH, 0, GL_RED, GL_UNSIGNED_SHORT, c16);
            else /* (Kodi: LUMINANCE for U, ALPHA for V) */
                glTexImage2D(GL_TEXTURE_2D, 0, p ? GL_ALPHA : GL_LUMINANCE, CW, CH, 0, p ? GL_ALPHA : GL_LUMINANCE,
                             GL_UNSIGNED_BYTE, c8);
        }
    }
    for (int p = 0; p < 3; ++p) {
        /* (CLinuxRendererGLES::SetChroma(): the chroma read GL_NEAREST by the filters of more than one sample) */
        const GLint filter = p > 0 && point ? GL_NEAREST : GL_LINEAR;
        glBindTexture(GL_TEXTURE_2D, tex[p]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

/* draws with the program into a float target of ow x oh; out: R G B A per pixel, rows from the top */
static int draw(GLuint prog, int filt310, GLuint tex[3], const struct siting *s, int ow, int oh, float *out)
{
    glUseProgram(prog);
    GLuint fbo, rt, vbo[2], ibo, vao, ktex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, ow, oh);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 0;
    if (filt310) {
        /* Kodi's Lanczos3Fast kernel for the luma (flat here: any normalized kernel gives it back) */
        float k[256 * 4];
        for (int i = 0; i < 256; ++i)
            k[i * 4] = k[i * 4 + 3] = 0, k[i * 4 + 1] = k[i * 4 + 2] = 0.5f;
        glGenTextures(1, &ktex);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, ktex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 256, 1, 0, GL_RGBA, GL_FLOAT, k);
        glUniform1i(glGetUniformLocation(prog, "m_kernelTex"), 3);
    }
    for (int p = 0; p < 3; ++p) {
        glActiveTexture(GL_TEXTURE0 + p);
        glBindTexture(GL_TEXTURE_2D, tex[p]);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(prog, "m_sampY"), 0);
    glUniform1i(glGetUniformLocation(prog, "m_sampU"), 1);
    glUniform1i(glGetUniformLocation(prog, "m_sampV"), 2);
    glUniform2f(glGetUniformLocation(prog, "m_step"), 1.0f / W, 1.0f / H);
    /* identity: the output is (Y, Cb, Cr) as sampled */
    const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float proj[16] = {2.0f / ow, 0, 0, 0, 0, 2.0f / oh, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(prog, "m_yuvmat"), 1, GL_FALSE, ident);
    glUniformMatrix4fv(glGetUniformLocation(prog, "m_proj"), 1, GL_FALSE, proj);
    glUniformMatrix4fv(glGetUniformLocation(prog, "m_model"), 1, GL_FALSE, ident);
    glUniform1f(glGetUniformLocation(prog, "m_alpha"), 1.0f);
    GLint ls = glGetUniformLocation(prog, "m_chromaSize"), lh = glGetUniformLocation(prog, "m_chromaShift");
    if (ls >= 0)
        glUniform4f(ls, CW, CH, 1.0f / CW, 1.0f / CH);
    if (lh >= 0)
        glUniform2f(lh, (float)s->x, (float)s->y);
    glViewport(0, 0, ow, oh);
    glDisable(GL_BLEND);
    const GLfloat vert[4][3] = {{0, 0, 0}, {(float)ow, 0, 0}, {(float)ow, (float)oh, 0}, {0, (float)oh, 0}};
    const GLfloat tc[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const GLubyte idx[4] = {0, 1, 3, 2};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(2, vbo);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vert), vert, GL_STATIC_DRAW);
    GLint lv = glGetAttribLocation(prog, "m_attrpos");
    glVertexAttribPointer(lv, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(lv);
    glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tc), tc, GL_STATIC_DRAW);
    const char *attr[3] = {"m_attrcordY", "m_attrcordU", "m_attrcordV"};
    for (int a = 0; a < 3; ++a) {
        GLint la = glGetAttribLocation(prog, attr[a]);
        if (la >= 0) {
            glVertexAttribPointer(la, 2, GL_FLOAT, GL_FALSE, 0, 0);
            glEnableVertexAttribArray(la);
        }
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);
    glReadPixels(0, 0, ow, oh, GL_RGBA, GL_FLOAT, out);
    const int ok = glGetError() == GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &rt);
    if (ktex)
        glDeleteTextures(1, &ktex);
    glDeleteBuffers(2, vbo);
    glDeleteBuffers(1, &ibo);
    glDeleteVertexArrays(1, &vao);
    return ok;
}

/* one case: how far the shader is from the model, at most, in the chroma (and the luma is the flat one) */
static void one(const char *tree, int filt310, enum layout l, enum filter f, const struct siting *s, double scale,
                int flat)
{
    char what[256];
    snprintf(what, sizeof(what), "%s %s %s, siting %s, %s, drawn %gx%s", filt310 ? "filter shader (3.1)" : "basic shader",
             LAYOUT_NAMES[l], FILTER_NAMES[f], s->name, flat ? "flat chroma" : "random chroma", scale,
             f == KODI ? " (Kodi's own: siting not taken)" : "");
    make_chroma(1234u + (unsigned)l * 17u, flat);
    double scb[CW * CH], scr[CW * CH];
    for (int i = 0; i < CW * CH; ++i)
        scb[i] = stored(cb[i], l), scr[i] = stored(cr[i], l);
    GLuint prog = program(tree, filt310, l, f, what);
    if (!prog)
        return;
    GLuint tex[3] = {0, 0, 0};
    glGenTextures(3, tex);
    GLuint keep2 = tex[2];
    upload(l, tex, f == SPLINE16 || f == LANCZOS3);
    const int ow = (int)(W * scale + 0.5), oh = (int)(H * scale + 0.5);
    float *out = malloc((size_t)ow * oh * 4 * sizeof(float));
    if (!draw(prog, filt310, tex, s, ow, oh, out)) {
        check(0, "%s: not drawn", what);
        free(out);
        return;
    }
    /* what the GPU may round: a bilinear filter has fixed-point weights of 8 bits (1/256 of the difference of
     * two samples, at most 0.8 here) and, from a texture of 8 bits, may return its result in 8 bits (half a
     * step); samples read GL_NEAREST, float rounding only; and the basic shader of GLES 2.0 is mediump, which
     * a GPU may compute in 16-bit floats, as Mesa's llvmpipe does: the YUV value and its product with the
     * matrix are rounded there, half a step of 2^-11 below 1 each (Kodi's own sampling as much) */
    const int deep = l == NV12_RRG || l == YV12_HI;
    double tol = 2e-5;
    if (f == KODI || f == SITED)
        tol += 0.8 / 256 + (deep ? 0 : 0.5 / 255);
    if (!filt310)
        tol += 2.0 / 4096;
    double worst = 0, worstY = 0;
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            const double tu = (x + 0.5) / ow, tv = (y + 0.5) / oh;
            const float *o = out + ((size_t)y * ow + x) * 4;
            const double db = fabs(o[1] - model(scb, f, s, tu, tv)), dr = fabs(o[2] - model(scr, f, s, tu, tv));
            worst = db > worst ? db : worst;
            worst = dr > worst ? dr : worst;
            const double dy = fabs(o[0] - stored(Y_FLAT, l));
            worstY = dy > worstY ? dy : worstY;
        }
    check(worst <= tol, "%s: the chroma is %.6f from the model at most (%.6f allowed)", what, worst, tol);
    check(worstY <= 2e-5 + (filt310 ? 0 : 2.0 / 4096), "%s: the flat luma changed by %.6f", what, worstY);
    if (flat)
        check(worst <= (filt310 ? 2e-5 : 2e-5 + 2.0 / 4096), "%s: a flat chroma does not come out flat (%.6f)", what,
              worst);
    free(out);
    tex[2] = keep2;
    glDeleteTextures(3, tex);
    glDeleteProgram(prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: chroma_shader_test <Kodi tree>\n");
        return 2;
    }
    const char *tree = argv[1];
    EGLDisplay dpy = EGL_NO_DISPLAY;
    PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get)
        dpy = get(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL);
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
    if (!strstr((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_texture_norm16")) {
        printf("FAIL: no GL_EXT_texture_norm16\n");
        return 1;
    }
    printf("GL_RENDERER %s\n", (const char *)glGetString(GL_RENDERER));
    const double scales[2] = {1.0, 2.5};
    for (int l = NV12_RRG; l <= YV12; ++l)
        for (int f = KODI; f <= LANCZOS3; ++f)
            for (size_t si = 0; si < sizeof(SITINGS) / sizeof(*SITINGS); ++si) {
                if (f == KODI && si > 0)
                    continue; /* (the siting is not taken) */
                for (int sc = 0; sc < 2; ++sc)
                    one(tree, 0, (enum layout)l, (enum filter)f, &SITINGS[si], scales[sc], 0);
            }
    for (int f = KODI; f <= LANCZOS3; ++f) {
        one(tree, 0, NV12_RRG, (enum filter)f, &SITINGS[2], 1.0, 1);
        one(tree, 0, YV12, (enum filter)f, &SITINGS[0], 2.5, 1);
        for (size_t si = 0; si < 3; ++si)
            one(tree, 1, NV12_RRG, (enum filter)f, &SITINGS[si], 1.0, 0);
    }
    printf("%d checks, %d failed\n", checks, failures);
    printf("%s\n", failures ? "CHROMA-SHADER FAILED" : "CHROMA-SHADER PASSED");
    return failures ? 1 : 0;
}
