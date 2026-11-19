# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# LibreELEC Hades, TEST ONLY: gamut_table_test.c made from hdrsdr_test.c (beside this file): a plain HDR10 picture
# mapped to SDR with the standard and the finer gamut table (dvbridge_render_set_gamut_table()), the report of the
# table (dvbridge_render_gamut_table()), and, where the finer one cannot be made (GT_FAIL=1: the bridge built with a
# finer table beyond the GPU's largest 3D texture), the standard one in its place, frame after frame.
# usage: python3 -I gamut_table_mktest.py <hdrsdr_test.c> <gamut_table_test.c>
# (each text it changes must be there exactly once: if hdrsdr_test.c changes, it stops and says which)
import sys
src, out = sys.argv[1], sys.argv[2]
t = open(src).read()


def rep(old, new):
    global t
    assert t.count(old) == 1, (t.count(old), old[:80])
    t = t.replace(old, new)


rep('''static int g_failures;
''', '''static int g_failures;
static int g_lutErrors; // libplacebo's errors of a gamut table that could not be made
''')
rep('''  if (level <= PL_LOG_WARN)''', '''  if (strstr(msg, "exceeds texture dimensions") || strstr(msg, "Failed generating gamut-mapping LUT") ||
      strstr(msg, "Failed creating LUT texture"))
    ++g_lutErrors;
  if (level <= PL_LOG_WARN)''')
start = t.index('  // --- 2. mapped to an SDR output')
end = t.index('  dvbridge_gl_frame_release(&b);')
body = r'''  // --- the gamut table of the mapping to SDR
  float xy[8];
  dvbridge_hdr10_gamut_xy(DVBRIDGE_HDR10_GAMUT_P3_D65, xy);
  const struct dvbridge_hdr10_mapping sdr = dvbridge_sdr_mapping(!strcmp(prim, "p3") ? xy : NULL);
  Check(dvbridge_render_hdr10_mapping(g.renderer, &sdr), "the renderer takes the SDR mapping");
  GLint max3d = 0;
  glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3d);
  printf("largest 3D texture of this GPU: %d\n", max3d);
#ifndef DVBRIDGE_RENDER_GAMUT_TABLE
  Check(0, "the renderer has a finer gamut table to ask for (dvbridge_render_set_gamut_table())");
#else
  const int fail = getenv("GT_FAIL") && atoi(getenv("GT_FAIL"));
  int fine[3] = {192, 128, 1024};
  if (getenv("GT_FINE"))
    sscanf(getenv("GT_FINE"), "%d,%d,%d", &fine[0], &fine[1], &fine[2]);
  struct dvbridge_gamut_table tab;
  Check(!dvbridge_render_gamut_table(g.renderer, &tab), "no table is reported before a picture is mapped");
  float* std = malloc((size_t)W * H * 16);
  float* fin = malloc((size_t)W * H * 16);
  // the standard table, as without the setting
  const int s1 = Draw(&g, &b.frame, &plain);
  memcpy(std, g.pic, (size_t)W * H * 16);
  Check(s1 && dvbridge_render_gamut_table(g.renderer, &tab) && tab.asked == DVBRIDGE_GAMUT_TABLE_STANDARD &&
            tab.used == DVBRIDGE_GAMUT_TABLE_STANDARD && !tab.fine_failed && tab.size[0] == 96 &&
            tab.size[1] == 64 && tab.size[2] == 512 && tab.format && !strcmp(tab.format, "rgba16") &&
            tab.bytes == 96ULL * 64 * 512 * 8,
        "the standard table: 96x64x512 in rgba16, 24 MiB, as reported");
  WriteRgb(prefix, "std", std);
  // the finer one
  const int e0 = g_lutErrors;
  dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_FINE);
  const int f1 = Draw(&g, &b.frame, &plain);
  memcpy(fin, g.pic, (size_t)W * H * 16);
  const int r1 = dvbridge_render_gamut_table(g.renderer, &tab);
  printf("finer table asked for: drawn %d; reported asked %d used %d failed %d, %dx%dx%d %s, %llu bytes; "
         "libplacebo said %d table errors\n", f1, tab.asked, tab.used, tab.fine_failed, tab.size[0],
         tab.size[1], tab.size[2], tab.format ? tab.format : "(none)", tab.bytes, g_lutErrors - e0);
  long differ = 0;
  for (size_t i = 0; i < (size_t)W * H * 4; ++i)
    differ += fin[i] != std[i];
  if (!fail)
  {
    Check(f1 && r1 && tab.asked == DVBRIDGE_GAMUT_TABLE_FINE && tab.used == DVBRIDGE_GAMUT_TABLE_FINE &&
              !tab.fine_failed && tab.size[0] == fine[0] && tab.size[1] == fine[1] && tab.size[2] == fine[2] &&
              tab.bytes == (unsigned long long)fine[0] * fine[1] * fine[2] * 8 && g_lutErrors == e0,
          "the finer table: drawn with it, as reported");
    Check(differ > 0, "... and a picture that is not the standard table's");
    printf("values that differ from the standard table's picture: %ld\n", differ);
    WriteRgb(prefix, "fine", fin);
  }
  else
  {
    Check(f1 && r1 && tab.asked == DVBRIDGE_GAMUT_TABLE_FINE && tab.used == DVBRIDGE_GAMUT_TABLE_STANDARD &&
              tab.fine_failed && tab.size[0] == 96 && tab.size[1] == 64 && tab.size[2] == 512 &&
              g_lutErrors > e0,
          "the finer table could not be made: the picture drawn with the standard one, as reported");
    Check(!differ, "... the standard table's picture, to the bit");
    // the next frames: the standard table without trying the finer one again
    const int e1 = g_lutErrors;
    int same = 1;
    for (int k = 0; k < 3; ++k)
      same = same && Draw(&g, &b.frame, &plain) && !memcmp(g.pic, std, (size_t)W * H * 16);
    Check(same && g_lutErrors == e1 && dvbridge_render_gamut_table(g.renderer, &tab) && tab.fine_failed,
          "the next frames: the standard table, the finer one not tried again");
    // asked again (the setting changed): tried again
    dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_STANDARD);
    dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_FINE);
    const int again = Draw(&g, &b.frame, &plain) && !memcmp(g.pic, std, (size_t)W * H * 16);
    Check(again && g_lutErrors > e1 && dvbridge_render_gamut_table(g.renderer, &tab) && tab.fine_failed,
          "asked for again: tried again, and the standard one in its place again");
  }
  // the standard table again: the same picture as at first, to the bit
  dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_STANDARD);
  const int s2 = Draw(&g, &b.frame, &plain) && !memcmp(g.pic, std, (size_t)W * H * 16);
  Check(s2 && dvbridge_render_gamut_table(g.renderer, &tab) && tab.used == DVBRIDGE_GAMUT_TABLE_STANDARD &&
            !tab.fine_failed,
        "the standard table again: the same picture as at first, to the bit");
  if (!fail)
  {
    dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_FINE);
    const int f2 = Draw(&g, &b.frame, &plain) && !memcmp(g.pic, fin, (size_t)W * H * 16);
    Check(f2, "the finer table again: the same picture as before, to the bit");
  }
  // the mapping off: no table, the picture as it is
  {
    const int off = dvbridge_render_hdr10_mapping(g.renderer, NULL) && Draw(&g, &b.frame, &plain) &&
                    !memcmp(g.pic, g.ref, (size_t)W * H * 16);
    Check(off, "the mapping off again draws the picture as it is, to the bit");
  }
  if (times > 0)
  {
    dvbridge_render_hdr10_mapping(g.renderer, &sdr);
    dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_STANDARD);
    Time(&g, &b.frame, &plain, &sdr, times, "mapped to SDR, standard table");
    dvbridge_render_set_gamut_table(g.renderer, DVBRIDGE_GAMUT_TABLE_FINE);
    Time(&g, &b.frame, &plain, &sdr, times, "mapped to SDR, finer table");
  }
  free(std);
  free(fin);
#endif
'''
t = t[:start] + body + t[end:]
t = t.replace('TEST ONLY (nothing of this goes into an image): an HDR10 or HLG picture',
              'TEST ONLY (nothing of this goes into an image; made by gamut_table_mktest.py): an HDR10 or HLG picture', 1)
open(out, 'w').write(t)
print('written', out)
