# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
"""LibreELEC Hades, TEST ONLY: the bridge's render test (render_test.c, beside this file) for profile 8.4, with
DV_BASE_TRC=hlg|sdr (dvbridge_render_set_base_transfer(), a weak symbol), DV_SIGNAL_EOTF="<eotf> <p0> <p1> <p2>" (the
RPU of every picture declares that signal instead of its own: a copy of its metadata), and a line for each picture
whose render the bridge refused for its base layer (dvbridge_render_base_refused()).
usage: python3 -I p84_mkrt.py <render_test.c> <out.c> (each text it changes must be there exactly once)"""
import sys

src, out = sys.argv[1], sys.argv[2]
t = open(src).read()
a = '''static void CcmSwitch(struct dvbridge_renderer* r, const char* name)
{
  const char* e = getenv("DV_CCM");
  if (e && !strcmp(e, "0") && dvbridge_render_use_ccm_composer)
    dvbridge_render_use_ccm_composer(r, false);'''
assert t.count(a) == 1
t = t.replace(a, '''__attribute__((weak)) void dvbridge_render_set_base_transfer(struct dvbridge_renderer* renderer, int transfer);
__attribute__((weak)) int dvbridge_render_base_refused(const struct dvbridge_renderer* renderer);
''' + a + '''
  // DV_BASE_TRC=hlg|sdr: the transfer of the base layer
  const char* trc = getenv("DV_BASE_TRC");
  if (trc && dvbridge_render_set_base_transfer)
  {
    dvbridge_render_set_base_transfer(r, !strcmp(trc, "hlg") ? 2 : !strcmp(trc, "sdr") ? 1 : 0);
    printf("TEST: P84: the base layer of %s is %s\\n", name, trc);
  }''')
b = '''  long differing = -1;
  unsigned margins[4] = {0}, count = 0;'''
assert t.count(b) == 1
t = t.replace(b, '''  // DV_SIGNAL_EOTF: the RPU declares that signal instead of its own (a copy, never freed: a test)
  if (getenv("DV_SIGNAL_EOTF"))
  {
    AVDOVIMetadata* copy = av_malloc(bytes);
    memcpy(copy, meta, bytes);
    AVDOVIColorMetadata* c = av_dovi_get_color(copy);
    unsigned v[4] = {0, 0, 0, 0};
    sscanf(getenv("DV_SIGNAL_EOTF"), "%u %u %u %u", &v[0], &v[1], &v[2], &v[3]);
    c->signal_eotf = (uint16_t)v[0];
    c->signal_eotf_param0 = (uint16_t)v[1];
    c->signal_eotf_param1 = (uint16_t)v[2];
    c->signal_eotf_param2 = v[3];
    meta = copy;
  }
''' + b)
c = '''  // --- direct
  if (g->subgroups)'''
assert t.count(c) == 1
t = t.replace(c, '''  if (dvbridge_render_base_refused && dvbridge_render_base_refused(g->renderer))
    printf("%s: P84: refused: the base layer of transfer %d not composed\\n", label,
           dvbridge_render_base_refused(g->renderer));
  // the metadata that the transport refuses, and the picture for output as HDR10 (no transport)
  if (getenv("DV_SIGNAL_EOTF"))
  {
    const char* why = dvbridge_render_refused(g->renderer);
    if (why)
      printf("%s: P84: the transport refuses the metadata: %s\\n", label, why);
    const struct dvbridge_view v = {0, 0, (float)bl->w, (float)bl->h, 0, 0, W, H};
    const int hdr10 = dvbridge_render_hdr10(g->renderer, &b.frame, meta, bytes, pts, elPts, v, W, H);
    printf("%s: P84: for output as HDR10: %s, refused transfer %d, composed %d\\n", label,
           hdr10 ? "drawn" : "not drawn", dvbridge_render_base_refused ? dvbridge_render_base_refused(g->renderer) : -1,
           dvbridge_render_composed(g->renderer));
  }
''' + c)
d = '  if (level <= PL_LOG_WARN)\n    fprintf(stderr, "libplacebo[%d]: %s\\n", level, msg);'
assert t.count(d) == 1, t.count(d)
t = t.replace(d, '  if (level <= (getenv("DV_BASE_TRC") ? PL_LOG_INFO : PL_LOG_WARN))\n'
                 '    fprintf(stderr, "libplacebo[%d]: %s\\n", level, msg);')
open(out, 'w').write(t)
print('mkrt84:', out)
