#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""hlg8_src_check.py <Kodi tree>: in the source, what no host test runs of an 8-bit HLG video sent as Dolby Vision:
the rule takes 8 bits (DVPlaybackPolicy.h), the probe gives it the depth of the pixel format (DVHdr10Plus.cpp), and
Kodi's VAAPI renderer gives the bridge the widened planes on each of its three ways (the native output, the
conversion, the measurement ahead), widens each picture once and anew for the next picture of a buffer, and frees
what it made. Prints ok:/FAIL: lines and "PASSED|FAILED: <n> failed"."""
import re
import sys

root = sys.argv[1]
failures = 0


def check(ok, what):
    global failures
    print(('ok:   ' if ok else 'FAIL: ') + what)
    failures += 0 if ok else 1


def read(path):
    with open(root + '/' + path, encoding='utf-8') as f:
        return f.read()


def body(text, name):
    m = re.search(r'\n[^\n]*CRendererVAAPIGLES::' + name + r'\([^)]*\)\s*\n\{', text)
    if not m:
        return ''
    depth, i = 0, m.end() - 1
    for j in range(i, len(text)):
        depth += {'{': 1, '}': -1}.get(text[j], 0)
        if depth == 0:
            return text[i:j + 1]
    return ''


pol = read('xbmc/cores/VideoPlayer/DVDCodecs/Video/DVPlaybackPolicy.h')
m = re.search(r'constexpr bool HlgDolbyVisionEligible\(.*?\n\}', pol, re.S)
check(m and '(bitDepth == 10 || bitDepth == 8)' in m.group(0), 'HlgDolbyVisionEligible() takes 10 and 8 bits')
probe = read('xbmc/cores/VideoPlayer/DVDDemuxers/DVHdr10Plus.cpp')
m = re.search(r'bool CDVHdr10Plus::HlgMeasurable\(.*?\n\}', probe, re.S)
check(m and 'desc->comp[0].depth' in m.group(0) and 'tenBits420' not in m.group(0),
      'HlgMeasurable() gives the rule the depth of the pixel format')
r = read('xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender/RendererVAAPIGLES.cpp')
for name in ('RenderDVBridge', 'RenderDVConversion', 'MeasureAhead'):
    check('WidenForBridge(idx, layer)' in body(r, name), name + '() gives the bridge the widened planes')
w = body(r, 'WidenForBridge')
check('dvbridge_gl_widen_nv12(' in w and 'm_dvWidenedNow[idx] = true' in w and 'DVBRIDGE_P010' in w,
      'WidenForBridge() widens a picture once, into the planes of a P010 picture')
check('m_dvWidenedNow[index] = false' in body(r, 'ReleaseBuffer'), 'the next picture of a buffer is widened anew')
check('dvbridge_gl_widened_free(&m_dvWidened[index])' in body(r, 'DeleteTexture') and
      'dvbridge_gl_widen_free(&m_dvWiden)' in r, 'what the widening made is freed')
print('%s: %d failed' % ('FAILED' if failures else 'PASSED', failures))
sys.exit(1 if failures else 0)
