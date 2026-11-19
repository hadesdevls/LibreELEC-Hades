# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: ccm_ref_1886.c, the 64-bit reference of the fidelity bench
# (src/ccm_ref.c, a literal transcription of ETSI GS CCM 001 V1.1.1 clause 5.4) with
# what the clause says for a base layer in Recommendation ITU-R BT.1886: out_bit_depth 14 in 5.4.3.3, and the
# conversion of clause 5.5 by the method of its annex C, transcribed here equation by equation in double
# precision (Clip3 and Round of the annex; Round(x) = Sign(x) Floor(Abs(x) + 0.5), as ITU texts define it).
# Written from the text of the clause and of the annex (the plain-text extraction of the PDF that tools/regress/oracle/README.md names, l. 906-947,
# 1644-1895, and the PDF next to it), apart from the GPU code of libplacebo-9955.
# usage: python3 -I p82_mkref.py <ccm_ref.c> <out ccm_ref_1886.c>
import sys

src, out = sys.argv[1], sys.argv[2]
t = open(src).read()


def rep(old, new):
    global t
    assert t.count(old) == 1, old[:60]
    t = t.replace(old, new)


rep('''static int g_vertical_reading, g_literal_pivot, g_literal_hits;''',
    '''static int g_vertical_reading, g_literal_pivot, g_literal_hits;
// the base layer is in Recommendation ITU-R BT.1886 (option -s)
static int g_bt1886;''')

rep('''  int out_bit_depth = m->hdrd; // the base layer is SMPTE ST 2084 here''',
    '''  // 5.4.3.3: "If the transfer characteristics of the BL input picture are equal to the transfer
  // characteristics as defined in Recommendation ITU-R BT.1886-0, out_bit_depth shall be equal to 14.
  // Otherwise (... SMPTE ST 2084), out_bit_depth shall be equal to hdr_bit_depth."
  int out_bit_depth = g_bt1886 ? 14 : m->hdrd;''')

# the conversion of clause 5.5 (annex C), before main()
rep('''int main(int argc, char** argv)
{''', r'''// ---------------------------------------------------------------------------------------- 5.5, annex C
#include <math.h>
static double clip3(double lo, double hi, double x) { return x < lo ? lo : x > hi ? hi : x; }
static double round_itu(double x) { return (x < 0 ? -1 : 1) * floor(fabs(x) + 0.5); }
static int at(const int* a, int w, int h, int x, int y)
{
  x = x < 0 ? 0 : x >= w ? w - 1 : x; // "set to the value of the pixel at the picture boundary"
  y = y < 0 ? 0 : y >= h ? h - 1 : y;
  return a[(size_t)y * w + x];
}
// ST 2084 EOTF of a 12-bit code (for L_W, L_B from source_max_PQ, source_min_PQ), cd/m2
static double pq_eotf_nits(double e)
{
  const double m1 = 2610.0 / 4096 / 4, m2 = 2523.0 / 4096 * 128;
  const double c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
  const double p = pow(e, 1 / m2);
  return 10000 * pow(fmax(p - c1, 0) / (c2 - c3 * p), 1 / m1);
}
// ST 2084 inverse EOTF of Y = L / 10000
static double pq_inverse(double y)
{
  const double m1 = 2610.0 / 4096 / 4, m2 = 2523.0 / 4096 * 128;
  const double c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
  const double p = pow(y, m1);
  return pow((c1 + c2 * p) / (1 + c3 * p), m2);
}
// h14: the 14-bit samples (Y W x H, Cb/Cr WC x HC); out: D' at hdr_bit_depth, Y W x H, Cb/Cr WC x HC
static void convert_1886(const struct meta* m, int* const h14[3], int* out[3], double lw, double lb)
{
  const int W = m->blw, H = m->blh, WC = (W + 1) / 2, HC = (H + 1) / 2, n = m->hdrd;
  // up-sampling, vertical first: f(2n) = 64 s(n); f(2n+1) = -4 s(n-1) + 36 s(n) + 36 s(n+1) - 4 s(n+2)
  int* f[2] = {malloc(sizeof(int) * (size_t)WC * H), malloc(sizeof(int) * (size_t)WC * H)};
  for (int c = 0; c < 2; ++c)
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < WC; ++x)
      {
        const int k = y / 2;
        const int* s = h14[1 + c];
        f[c][(size_t)y * WC + x] = y % 2 == 0 ? 64 * at(s, WC, HC, x, k)
                                              : -4 * at(s, WC, HC, x, k - 1) + 36 * at(s, WC, HC, x, k) +
                                                    36 * at(s, WC, HC, x, k + 1) - 4 * at(s, WC, HC, x, k + 2);
      }
  // ... then horizontal: g(2n) = (f(n) + 32) >> 6; g(2n+1) = (-4 f(n-1) + 36 f(n) + 36 f(n+1) - 4 f(n+2) + 2048) >> 12
  int* full[3] = {malloc(sizeof(int) * (size_t)W * H), malloc(sizeof(int) * (size_t)W * H),
                  malloc(sizeof(int) * (size_t)W * H)};
  const double gamma = 2.4;
  const double a = pow(pow(lw, 1 / gamma) - pow(lb, 1 / gamma), gamma);
  const double b = pow(lb, 1 / gamma) / (pow(lw, 1 / gamma) - pow(lb, 1 / gamma));
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
    {
      double D[3];
      D[0] = h14[0][(size_t)y * W + x];
      for (int c = 0; c < 2; ++c)
      {
        const int k = x / 2;
        long long g = x % 2 == 0 ? ((long long)at(f[c], WC, H, k, y) + 32) >> 6
                                 : ((long long)-4 * at(f[c], WC, H, k - 1, y) + 36LL * at(f[c], WC, H, k, y) +
                                    36LL * at(f[c], WC, H, k + 1, y) - 4LL * at(f[c], WC, H, k + 2, y) + 2048) >>
                                       12;
        D[1 + c] = (double)g;
      }
      // normalization of the 14-bit signal
      const double EY = clip3(0, 1, (D[0] / (1 << (14 - 8)) - 16) / 219);
      const double ECb = clip3(-0.5, 0.5, (D[1] / (1 << (14 - 8)) - 128) / 224);
      const double ECr = clip3(-0.5, 0.5, (D[2] / (1 << (14 - 8)) - 128) / 224);
      // YCbCr to R'G'B' (BT.2020)
      const double ER = clip3(0, 1, EY + 1.47460 * ECr);
      const double EG = clip3(0, 1, EY - 0.16455 * ECb - 0.57135 * ECr);
      const double EB = clip3(0, 1, EY + 1.88140 * ECb);
      // BT.1886 annex 1 to linear light, between L_B and L_W
      const double LR = clip3(lb, lw, a * pow(fmax(ER + b, 0), gamma));
      const double LG = clip3(lb, lw, a * pow(fmax(EG + b, 0), gamma));
      const double LB = clip3(lb, lw, a * pow(fmax(EB + b, 0), gamma));
      // ST 2084 inverse EOTF
      const double QR = clip3(0, 1, pq_inverse(LR / 10000));
      const double QG = clip3(0, 1, pq_inverse(LG / 10000));
      const double QB = clip3(0, 1, pq_inverse(LB / 10000));
      // R'G'B' to YCbCr (BT.2020)
      const double QY = 0.2627 * QR + 0.6780 * QG + 0.0593 * QB;
      const double QCb = (QB - QY) / 1.8814;
      const double QCr = (QR - QY) / 1.4746;
      // quantization at hdr_bit_depth
      const double maxv = (1 << n) - 1, scale = 1 << (n - 8);
      full[0][(size_t)y * W + x] = (int)clip3(0, maxv, round_itu(scale * (219 * QY + 16)));
      full[1][(size_t)y * W + x] = (int)clip3(0, maxv, round_itu(scale * (224 * QCb + 128)));
      full[2][(size_t)y * W + x] = (int)clip3(0, maxv, round_itu(scale * (224 * QCr + 128)));
    }
  memcpy(out[0], full[0], sizeof(int) * (size_t)W * H);
  // down-sampling, horizontal first: f(n) = s(2n-1) + 6 s(2n) + s(2n+1); vertical: (f(2n-1) + 6 f(2n) + f(2n+1) + 32) >> 6
  int* hd = malloc(sizeof(int) * (size_t)WC * H);
  for (int c = 0; c < 2; ++c)
  {
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < WC; ++x)
        hd[(size_t)y * WC + x] = at(full[1 + c], W, H, 2 * x - 1, y) + 6 * at(full[1 + c], W, H, 2 * x, y) +
                                 at(full[1 + c], W, H, 2 * x + 1, y);
    for (int y = 0; y < HC; ++y)
      for (int x = 0; x < WC; ++x)
        out[1 + c][(size_t)y * WC + x] =
            (at(hd, WC, H, x, 2 * y - 1) + 6 * at(hd, WC, H, x, 2 * y) + at(hd, WC, H, x, 2 * y + 1) + 32) >> 6;
  }
  free(hd);
  for (int c = 0; c < 3; ++c)
    free(full[c]);
  free(f[0]);
  free(f[1]);
}

int main(int argc, char** argv)
{''')

rep('''    else if (!strcmp(argv[a], "-n"))
      no_el = 1;''', '''    else if (!strcmp(argv[a], "-n"))
      no_el = 1;
    else if (!strcmp(argv[a], "-s"))
      g_bt1886 = 1;''')

rep('''  write_plane(argv[a + 1], "y", out[0], W, H);
  write_plane(argv[a + 1], "cb", out[1], WC, HC);
  write_plane(argv[a + 1], "cr", out[2], WC, HC);''', '''  double lw = 0, lb = 0;
  if (g_bt1886)
  {
    // L_W, L_B: max_display_mastering_luminance (1 cd/m2) and min_display_mastering_luminance (0.0001 cd/m2) of
    // clause 5.3.2, taken from the source display of the display management metadata (source_max_PQ,
    // source_min_PQ, 12-bit ST 2084 codes) as the integers those syntax elements hold
    FILE* mf;
    char mp[1200];
    snprintf(mp, sizeof mp, "%s/meta.txt", argv[a]);
    mf = fopen(mp, "r");
    const int smax = (int)key_int(mf, "color.source_max_pq"), smin = (int)key_int(mf, "color.source_min_pq");
    fclose(mf);
    lw = round_itu(pq_eotf_nits(smax / 4095.0));
    lb = round_itu(pq_eotf_nits(smin / 4095.0) * 10000) / 10000;
    write_plane(argv[a + 1], "y14", out[0], W, H);
    write_plane(argv[a + 1], "cb14", out[1], WC, HC);
    write_plane(argv[a + 1], "cr14", out[2], WC, HC);
    int* conv[3] = {malloc(sizeof(int) * (size_t)W * H), malloc(sizeof(int) * (size_t)WC * HC),
                    malloc(sizeof(int) * (size_t)WC * HC)};
    convert_1886(&m, out, conv, lw, lb);
    for (int c = 0; c < 3; ++c)
      out[c] = conv[c];
    printf("ccm_ref BT.1886: out_bit_depth 14, clause 5.5 with L_W %.4f cd/m2, L_B %.4f cd/m2 (source PQ %d, %d)\\n",
           lw, lb, smax, smin);
  }
  write_plane(argv[a + 1], "y", out[0], W, H);
  write_plane(argv[a + 1], "cb", out[1], WC, HC);
  write_plane(argv[a + 1], "cr", out[2], WC, HC);''')
open(out, 'w').write(t)
print("mkref:", out)
