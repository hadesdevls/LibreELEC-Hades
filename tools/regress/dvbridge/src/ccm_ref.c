// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (nothing of this goes into an image).
//
// The composer of ETSI GS CCM 001 V1.1.1, clause 5.4, transcribed literally from its pseudo-code into C with
// 64-bit integers, one sample at a time, written apart from (and without reading) dvref.py, so that the two
// can check each other. The enhancement layer is first scaled to the size of each plane of the base layer by
// the filters of annex B.3 (informative), vertical pass first, then horizontal, as the annex says.
//
// Two places where the text needs a reading, both written down here and in the report:
//  - 5.4.2.2: the pseudo-code starts pivot_idx at num_pivots_minus2 + 1, which is one past the last piece
//    (pieces are 0 .. num_pivots_minus2); a sample at or above the last pivot (code 1023 when the last
//    pivot is 1023) would have no mapping. Such a sample takes the last piece (its value is clamped to the
//    last pivot by 5.4.2.3.2 / 5.4.2.3.3 in any case). Option -p1 keeps the literal index and reports it.
//  - 5.4.2.3.3: s*_{i,j} = (((s_{2i-1,2j} + 2 s_{2i,2j} + s_{2i+1,2j} + 2) >> 2) +
//    ((s_{2i-1,2j+1} + 2 s_{2i,2j+1} + s_{2i+1,2j+1} + 2) >> 2) + 1) >> 1, with s_{i,j} of a
//    (PicWidth)x(PicHeight) block, "MxN (M-column by N-row)" (clause 3.1): i is the column, j the row. The
//    [1 2 1] filter is horizontal and the two rows are averaged: chroma co-sited horizontally and centred
//    vertically (type 0). Option -v takes the other reading (i the row) for comparison.
//
// Input: a picture directory as written by ref_dump (src/ref_dump.c: meta.txt and the planes as uint16).
// Output: <prefix>_y.u16 (PicWidth x PicHeight), <prefix>_cb.u16, <prefix>_cr.u16 (PicWidthC x PicHeightC):
// the samples h of clause 5.4.3.3, and <prefix>_v*.u16 (the mapped base layer v, 16-bit) with -m.
// Usage: ccm_ref [-v] [-p1] [-m] [-n] <picture dir> <output prefix>     (-n: no enhancement layer)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long i64;

struct comp
{
  int num_pivots;
  int pivots[17];
  int idc[16], poly_order[16], mmr_order[16];
  i64 poly[16][3], mmr_const[16], mmr[16][3][7];
};

struct meta
{
  int denom, bld, eld, hdrd, disable_residual, nlq_method;
  struct comp c[3];
  int nlq_offset[3];
  i64 nlq_in_max[3], nlq_slope[3], nlq_thr[3];
  int blw, blh, elw, elh;
};

static int g_vertical_reading, g_literal_pivot, g_literal_hits;

static int key_line(FILE* f, const char* key, char* out, size_t n)
{
  rewind(f);
  char line[4096];
  size_t k = strlen(key);
  while (fgets(line, sizeof line, f))
    if (!strncmp(line, key, k) && line[k] == ' ')
    {
      snprintf(out, n, "%s", line + k + 1);
      return 1;
    }
  return 0;
}

static i64 key_int(FILE* f, const char* key)
{
  char v[4096];
  if (!key_line(f, key, v, sizeof v))
  {
    fprintf(stderr, "missing %s\n", key);
    exit(2);
  }
  return strtoll(v, NULL, 10);
}

static int key_ints(FILE* f, const char* key, i64* out, int max)
{
  char v[4096];
  if (!key_line(f, key, v, sizeof v))
  {
    fprintf(stderr, "missing %s\n", key);
    exit(2);
  }
  int n = 0;
  char* p = v;
  while (n < max)
  {
    char* e;
    i64 x = strtoll(p, &e, 10);
    if (e == p)
      break;
    out[n++] = x;
    p = e;
  }
  return n;
}

static void read_meta(const char* dir, struct meta* m)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/meta.txt", dir);
  FILE* f = fopen(path, "r");
  if (!f)
  {
    perror(path);
    exit(2);
  }
  memset(m, 0, sizeof *m);
  m->denom = (int)key_int(f, "hdr.coef_log2_denom");
  m->bld = (int)key_int(f, "hdr.bl_bit_depth");
  m->eld = (int)key_int(f, "hdr.el_bit_depth");
  m->hdrd = (int)key_int(f, "hdr.vdr_bit_depth");
  m->disable_residual = (int)key_int(f, "hdr.disable_residual_flag");
  m->nlq_method = (int)key_int(f, "map.nlq_method_idc");
  m->blw = (int)key_int(f, "bl.width");
  m->blh = (int)key_int(f, "bl.height");
  m->elw = (int)key_int(f, "el.width");
  m->elh = (int)key_int(f, "el.height");
  if (key_int(f, "hdr.coef_data_type") != 0)
  {
    fprintf(stderr, "coef_data_type is not fixed point\n");
    exit(2);
  }
  for (int c = 0; c < 3; ++c)
  {
    struct comp* k = &m->c[c];
    char key[64];
    i64 buf[32];
    snprintf(key, sizeof key, "curve%d.num_pivots", c);
    k->num_pivots = (int)key_int(f, key);
    snprintf(key, sizeof key, "curve%d.pivots", c);
    if (key_ints(f, key, buf, 17) != k->num_pivots)
      exit(2);
    for (int i = 0; i < k->num_pivots; ++i)
      k->pivots[i] = (int)buf[i];
    for (int i = 0; i + 1 < k->num_pivots; ++i)
    {
      snprintf(key, sizeof key, "curve%d.piece%d.mapping_idc", c, i);
      k->idc[i] = (int)key_int(f, key);
      if (k->idc[i] == 0)
      {
        snprintf(key, sizeof key, "curve%d.piece%d.poly", c, i);
        int n = key_ints(f, key, buf, 4);
        if (n != 4)
          exit(2);
        k->poly_order[i] = (int)buf[0];
        for (int j = 0; j < 3; ++j)
          k->poly[i][j] = buf[1 + j];
      }
      else
      {
        snprintf(key, sizeof key, "curve%d.piece%d.mmr_order", c, i);
        k->mmr_order[i] = (int)key_int(f, key);
        snprintf(key, sizeof key, "curve%d.piece%d.mmr_constant", c, i);
        k->mmr_const[i] = key_int(f, key);
        for (int o = 0; o < k->mmr_order[i]; ++o)
        {
          snprintf(key, sizeof key, "curve%d.piece%d.mmr%d", c, i, o + 1);
          if (key_ints(f, key, buf, 7) != 7)
            exit(2);
          for (int j = 0; j < 7; ++j)
            k->mmr[i][o][j] = buf[j];
        }
      }
    }
    snprintf(key, sizeof key, "nlq%d", c);
    if (key_ints(f, key, buf, 4) != 4)
      exit(2);
    m->nlq_offset[c] = (int)buf[0];
    m->nlq_in_max[c] = buf[1];
    m->nlq_slope[c] = buf[2];
    m->nlq_thr[c] = buf[3];
  }
  fclose(f);
}

static uint16_t* read_plane(const char* dir, const char* name, int w, int h)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.u16", dir, name);
  FILE* f = fopen(path, "rb");
  if (!f)
  {
    perror(path);
    exit(2);
  }
  uint16_t* p = malloc((size_t)w * h * 2);
  if (fread(p, 2, (size_t)w * h, f) != (size_t)w * h)
  {
    fprintf(stderr, "short plane %s\n", path);
    exit(2);
  }
  fclose(f);
  return p;
}

static void write_plane(const char* prefix, const char* name, const int* v, int w, int h)
{
  char path[1024];
  snprintf(path, sizeof path, "%s_%s.u16", prefix, name);
  FILE* f = fopen(path, "wb");
  for (int i = 0; i < w * h; ++i)
  {
    uint16_t x = (uint16_t)v[i];
    fwrite(&x, 2, 1, f);
  }
  fclose(f);
}

static int clampi(int x, int lo, int hi)
{
  return x < lo ? lo : x > hi ? hi : x;
}

// ---------------------------------------------------------------------------------------- 5.4.2.2
static int pivot_index(const struct comp* k, int s)
{
  int num_pivots_minus2 = k->num_pivots - 2;
  int pivot_idx = num_pivots_minus2 + 1;
  for (int idx = 0; idx < num_pivots_minus2 + 1; idx++)
  {
    if (s < k->pivots[idx + 1])
    {
      pivot_idx = idx;
      break;
    }
  }
  if (pivot_idx > num_pivots_minus2)
  {
    ++g_literal_hits;
    if (!g_literal_pivot)
      pivot_idx = num_pivots_minus2;
  }
  return pivot_idx;
}

// ---------------------------------------------------------------------------------------- 5.4.2.3.2
static int map_poly(const struct meta* m, const struct comp* k, int pivot_idx, int s)
{
  int last = k->num_pivots - 1;
  if (s < k->pivots[0])
    s = k->pivots[0];
  if (s > k->pivots[last])
    s = k->pivots[last];
  i64 ss = 1;
  int shift = 20; // 2*(maximum BL_bit_depth)
  i64 vv = 0;
  for (int i = 0; i <= k->poly_order[pivot_idx]; i++) // poly_order_minus1 + 1 = poly_order
  {
    vv += k->poly[pivot_idx][i] * (ss << shift);
    ss *= s;
    shift -= m->bld;
  }
  vv = (vv < 0) ? 0 : vv;
  i64 v = vv >> (4 + m->denom);
  v = (v > 0xffff) ? 0xffff : v;
  return (int)v;
}

// ---------------------------------------------------------------------------------------- 5.4.2.3.3
static int map_mmr(const struct meta* m, const struct comp* k, int pivot_idx, int s0, int s1, int s2)
{
  const struct comp* p0 = &m->c[0];
  const struct comp* p1 = &m->c[1];
  const struct comp* p2 = &m->c[2];
  if (s0 < p0->pivots[0])
    s0 = p0->pivots[0];
  if (s0 > p0->pivots[p0->num_pivots - 1])
    s0 = p0->pivots[p0->num_pivots - 1];
  if (s1 < p1->pivots[0])
    s1 = p1->pivots[0];
  if (s1 > p1->pivots[p1->num_pivots - 1])
    s1 = p1->pivots[p1->num_pivots - 1];
  if (s2 < p2->pivots[0])
    s2 = p2->pivots[0];
  if (s2 > p2->pivots[p2->num_pivots - 1])
    s2 = p2->pivots[p2->num_pivots - 1];
  const int B = m->bld;
  i64 tt[22] = {0};
  tt[0] = 1 << 20;
  int MMR_order = k->mmr_order[pivot_idx];
  if (MMR_order >= 1)
  {
    tt[1] = (i64)s0 << (20 - B);
    tt[2] = (i64)s1 << (20 - B);
    tt[3] = (i64)s2 << (20 - B);
    tt[4] = ((i64)s0 * s1) << (20 - 2 * B);
    tt[5] = ((i64)s0 * s2) << (20 - 2 * B);
    tt[6] = ((i64)s1 * s2) << (20 - 2 * B);
    tt[7] = (tt[4] * tt[3]) >> 20;
  }
  if (MMR_order >= 2)
  {
    tt[8] = ((i64)s0 * s0) << (20 - 2 * B);
    tt[9] = ((i64)s1 * s1) << (20 - 2 * B);
    tt[10] = ((i64)s2 * s2) << (20 - 2 * B);
    tt[11] = (tt[4] * tt[4]) >> 20;
    tt[12] = (tt[5] * tt[5]) >> 20;
    tt[13] = (tt[6] * tt[6]) >> 20;
    tt[14] = (tt[7] * tt[7]) >> 20;
  }
  if (MMR_order >= 3)
  {
    tt[15] = (tt[1] * tt[8]) >> 20;
    tt[16] = (tt[2] * tt[9]) >> 20;
    tt[17] = (tt[3] * tt[10]) >> 20;
    tt[18] = (tt[4] * tt[11]) >> 20;
    tt[19] = (tt[5] * tt[12]) >> 20;
    tt[20] = (tt[6] * tt[13]) >> 20;
    tt[21] = (tt[7] * tt[14]) >> 20;
  }
  i64 rr = k->mmr_const[pivot_idx] * tt[0];
  int cnt = 1;
  for (int i = 1; i <= MMR_order; i++)
    for (int j = 0; j < 7; j++)
    {
      rr += k->mmr[pivot_idx][i - 1][j] * tt[cnt];
      cnt++;
    }
  rr = rr < 0 ? 0 : rr;
  i64 v = rr >> (4 + m->denom);
  v = v > 0xffff ? 0xffff : v;
  return (int)v;
}

// the down-sampled luma s*_{i,j} of 5.4.2.3.3, edges by pixel replication
static int luma_down(const uint16_t* y, int w, int h, int ci, int cj)
{
#define S(a, b)                                                                                    \
  (g_vertical_reading ? (int)y[(size_t)clampi((a), 0, h - 1) * w + clampi((b), 0, w - 1)]          \
                      : (int)y[(size_t)clampi((b), 0, h - 1) * w + clampi((a), 0, w - 1)])
  // reading 1 (default): s_{a,b} = column a, row b; reading 2 (-v): row a, column b
  int i = g_vertical_reading ? cj : ci, j = g_vertical_reading ? ci : cj;
  int x = (((S(2 * i - 1, 2 * j) + 2 * S(2 * i, 2 * j) + S(2 * i + 1, 2 * j) + 2) >> 2) +
           ((S(2 * i - 1, 2 * j + 1) + 2 * S(2 * i, 2 * j + 1) + S(2 * i + 1, 2 * j + 1) + 2) >> 2) + 1) >>
          1;
#undef S
  return x;
}

// ---------------------------------------------------------------------------------------- 5.4.3.2
static int nlq(const struct meta* m, int cmp, int e)
{
  i64 T = m->nlq_thr[cmp];
  i64 S = m->nlq_slope[cmp];
  i64 R = m->nlq_in_max[cmp];
  i64 rr = e - m->nlq_offset[cmp];
  if (rr == 0)
    return 0;
  int sign = rr < 0 ? -1 : 1;
  rr <<= 1;
  rr -= sign;
  rr <<= (10 - m->eld);
  i64 dq = rr * S;
  i64 TT = (T << (10 - m->eld + 1)) * sign;
  dq += TT;
  i64 RR = (R << (10 - m->eld + 1));
  if (dq > RR)
    dq = RR;
  else if (dq < -RR)
    dq = -RR;
  return (int)(dq >> (m->denom - 5 - m->eld));
}

// ---------------------------------------------------------------------------------------- annex B.3
static int* el_resample(const uint16_t* y, int w, int h, int luma)
{
  int* v = malloc(sizeof(int) * (size_t)w * h * 2);
  for (int n = 0; n < h; ++n)
    for (int x = 0; x < w; ++x)
    {
#define Y(r) ((int)y[(size_t)clampi((r), 0, h - 1) * w + x])
      int a, b;
      if (luma)
      {
        a = (-3 * Y(n - 2) + 29 * Y(n - 1) + 111 * Y(n) - 9 * Y(n + 1) + 64) >> 7;
        b = (-9 * Y(n - 1) + 111 * Y(n) + 29 * Y(n + 1) - 3 * Y(n + 2) + 64) >> 7;
      }
      else
      {
        a = (64 * Y(n - 1) + 192 * Y(n) + 128) >> 8;
        b = (192 * Y(n) + 64 * Y(n + 1) + 128) >> 8;
      }
#undef Y
      v[(size_t)(2 * n) * w + x] = clampi(a, 0, 0xFFFF);
      v[(size_t)(2 * n + 1) * w + x] = clampi(b, 0, 0xFFFF);
    }
  int* r = malloc(sizeof(int) * (size_t)w * h * 4);
  for (int row = 0; row < 2 * h; ++row)
    for (int n = 0; n < w; ++n)
    {
#define V(c) (v[(size_t)row * w + clampi((c), 0, w - 1)])
      r[(size_t)row * 2 * w + 2 * n] = V(n);
      int o = (22 * V(n - 3) + 94 * V(n - 2) - 524 * V(n - 1) + 2456 * V(n) + 2456 * V(n + 1) - 524 * V(n + 2) +
               94 * V(n + 3) + 22 * V(n + 4) + 2048) >> 12;
#undef V
      r[(size_t)row * 2 * w + 2 * n + 1] = clampi(o, 0, 0xFFFF);
    }
  free(v);
  return r;
}

// ---------------------------------------------------------------------------------------- 5.4.3.3
static int reconstruct(const struct meta* m, int v, int r, int use_residual)
{
  int out_bit_depth = m->hdrd; // the base layer is SMPTE ST 2084 here
  int MAXOUT = (1 << out_bit_depth) - 1;
  int h = v;
  if (use_residual)
    h += r;
  h += (1 << (15 - out_bit_depth));
  h >>= (16 - out_bit_depth);
  h = h < 0 ? 0 : h;
  h = h > MAXOUT ? MAXOUT : h;
  return h;
}

int main(int argc, char** argv)
{
  int a = 1, write_v = 0, no_el = 0;
  for (; a < argc && argv[a][0] == '-'; ++a)
  {
    if (!strcmp(argv[a], "-v"))
      g_vertical_reading = 1;
    else if (!strcmp(argv[a], "-p1"))
      g_literal_pivot = 1;
    else if (!strcmp(argv[a], "-m"))
      write_v = 1;
    else if (!strcmp(argv[a], "-n"))
      no_el = 1;
    else
      return 2;
  }
  if (argc - a != 2)
  {
    fprintf(stderr, "usage: ccm_ref [-v] [-p1] [-m] [-n] <picture dir> <output prefix>\n");
    return 2;
  }
  struct meta m;
  read_meta(argv[a], &m);
  const int W = m.blw, H = m.blh, WC = (W + 1) / 2, HC = (H + 1) / 2;
  uint16_t* bl[3] = {read_plane(argv[a], "bl_y", W, H), read_plane(argv[a], "bl_u", WC, HC),
                     read_plane(argv[a], "bl_v", WC, HC)};
  for (int c = 0; c < 3; ++c)
    for (int i = 0; i + 1 < m.c[c].num_pivots; ++i)
      if (c == 0 && m.c[c].idc[i] != 0)
      {
        fprintf(stderr, "MMR on luma: not defined by clause 5.4.2.3.3\n");
        return 3;
      }
  int use_residual = !m.disable_residual && !no_el && m.nlq_method == 0;
  int* el[3] = {NULL, NULL, NULL};
  if (use_residual)
  {
    const int EW = m.elw, EH = m.elh, EWC = (EW + 1) / 2, EHC = (EH + 1) / 2;
    uint16_t* e[3] = {read_plane(argv[a], "el_y", EW, EH), read_plane(argv[a], "el_u", EWC, EHC),
                      read_plane(argv[a], "el_v", EWC, EHC)};
    if (EW == W && EH == H)
    {
      for (int c = 0; c < 3; ++c)
      {
        int n = c ? EWC * EHC : EW * EH;
        el[c] = malloc(sizeof(int) * (size_t)n);
        for (int i = 0; i < n; ++i)
          el[c][i] = e[c][i];
      }
    }
    else if (2 * EW == W && 2 * EH == H)
    {
      el[0] = el_resample(e[0], EW, EH, 1);
      el[1] = el_resample(e[1], EWC, EHC, 0);
      el[2] = el_resample(e[2], EWC, EHC, 0);
    }
    else
    {
      fprintf(stderr, "enhancement layer %dx%d for a base layer %dx%d\n", EW, EH, W, H);
      return 3;
    }
    for (int c = 0; c < 3; ++c)
      free(e[c]);
  }
  int* out[3] = {malloc(sizeof(int) * (size_t)W * H), malloc(sizeof(int) * (size_t)WC * HC),
                 malloc(sizeof(int) * (size_t)WC * HC)};
  int* vmap[3] = {malloc(sizeof(int) * (size_t)W * H), malloc(sizeof(int) * (size_t)WC * HC),
                  malloc(sizeof(int) * (size_t)WC * HC)};
  // luma (cmp 0): polynomial only
  for (int j = 0; j < H; ++j)
    for (int i = 0; i < W; ++i)
    {
      int s = bl[0][(size_t)j * W + i];
      int idx = pivot_index(&m.c[0], s);
      int v = map_poly(&m, &m.c[0], idx, s);
      int r = use_residual ? nlq(&m, 0, el[0][(size_t)j * W + i]) : 0;
      vmap[0][(size_t)j * W + i] = v;
      out[0][(size_t)j * W + i] = reconstruct(&m, v, r, use_residual);
    }
  for (int j = 0; j < HC; ++j)
    for (int i = 0; i < WC; ++i)
    {
      int s0 = luma_down(bl[0], W, H, i, j);
      int s1 = bl[1][(size_t)j * WC + i], s2 = bl[2][(size_t)j * WC + i];
      for (int c = 1; c < 3; ++c)
      {
        int s = c == 1 ? s1 : s2;
        int idx = pivot_index(&m.c[c], s);
        int v = m.c[c].idc[idx] == 0 ? map_poly(&m, &m.c[c], idx, s) : map_mmr(&m, &m.c[c], idx, s0, s1, s2);
        int r = use_residual ? nlq(&m, c, el[c][(size_t)j * WC + i]) : 0;
        vmap[c][(size_t)j * WC + i] = v;
        out[c][(size_t)j * WC + i] = reconstruct(&m, v, r, use_residual);
      }
    }
  write_plane(argv[a + 1], "y", out[0], W, H);
  write_plane(argv[a + 1], "cb", out[1], WC, HC);
  write_plane(argv[a + 1], "cr", out[2], WC, HC);
  if (write_v)
  {
    write_plane(argv[a + 1], "vy", vmap[0], W, H);
    write_plane(argv[a + 1], "vcb", vmap[1], WC, HC);
    write_plane(argv[a + 1], "vcr", vmap[2], WC, HC);
  }
  printf("ccm_ref %s: %dx%d, residual %s, samples at or above the last pivot (pivot_idx past the last piece): %d%s\n",
         argv[a], W, H, use_residual ? "yes" : "no", g_literal_hits, g_literal_pivot ? " (kept literal)" : "");
  return 0;
}
