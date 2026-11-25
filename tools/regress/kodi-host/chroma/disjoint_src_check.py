# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# disjoint_src_check.py <Kodi tree>: in the source, what the host test of GpuDisjoint.h cannot see: GL_GPU_DISJOINT_EXT
# (read and cleared by the GL) is read in one place of the window system, CWinSystemGbmGLESContext::GpuDisjointLost(),
# for every reader of GPU times, and by Kodi's renderer itself only where the window system is not GBM's; each reader
# asks through its own CGpuDisjoint::Reader; the frame is counted at the start of PresentRender(). Prints FAIL lines
# and DISJOINT-SRC PASSED or FAILED (n) last.
import os
import re
import sys

K = sys.argv[1]
fails = []


def fail(text):
    fails.append(text)
    print('FAIL: ' + text)


def span(text, signature):
    """(start, end) of the body of the function whose definition starts with signature; None where there is none"""
    at = text.find(signature)
    if at < 0:
        return None
    start = text.index('{', at)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return start, i + 1
    return None


WS = 'xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp'
RD = 'xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.cpp'
texts = {}
reads = []
for root, _, files in os.walk(os.path.join(K, 'xbmc')):
    for f in files:
        if f.endswith(('.cpp', '.h', '.c', '.cc')):
            p = os.path.join(root, f)
            rel = os.path.relpath(p, K)
            t = open(p, encoding='utf-8', errors='replace').read()
            texts[rel] = t
            for m in re.finditer(r'glGetIntegerv\s*\(\s*GL_GPU_DISJOINT_EXT', t):
                reads.append((rel, m.start()))
ws, rd = texts.get(WS, ''), texts.get(RD, '')
central = span(ws, 'bool CWinSystemGbmGLESContext::GpuDisjointLost(')
collect = span(rd, 'void CLinuxRendererGLES::ChromaTimerCollect(')
if central is None:
    fail('the window system has no GpuDisjointLost()')
allowed = 0
for rel, at in reads:
    if rel == WS and central and central[0] <= at < central[1]:
        allowed += 1
        continue
    if rel == RD and collect and collect[0] <= at < collect[1]:
        # only on the way where the window system is not GBM's: after the else of the call to it
        before = rd[collect[0]:at]
        if 'window->GpuDisjointLost(m_chromaDisjoint)' in before and re.search(r'\n\s*else\s*\n#endif\s*\n\s*\{\s*\n[^\n]*\n\s*$', before):
            allowed += 1
            continue
    fail(f'{rel}:{texts[rel][:at].count(chr(10)) + 1} reads GL_GPU_DISJOINT_EXT itself (it clears it for every other '
         'reader)')
print(f'reads of GL_GPU_DISJOINT_EXT: {len(reads)}, {allowed} of them in their places')
if allowed != 2:
    fail(f'{allowed} reads in their places, not 2 (the window system\'s, and the renderer\'s where it is not GBM\'s)')
# each reader asks through its own
readers = {}
for name, src, fn in (('HDR10 adapted', ws, 'void CWinSystemGbmGLESContext::Hdr10AdaptTimerCollect('),
                      ('the passes of the conversion bridge', ws, 'void CWinSystemGbmGLESContext::ConversionTimesDisjoint('),
                      ('Dolby Vision rebuilt', ws, 'void CWinSystemGbmGLESContext::ConvTimerCollect('),
                      ('native Dolby Vision', ws, 'void CWinSystemGbmGLESContext::DVGpuTimerCollect('),
                      ('Kodi\'s renderer', rd, 'void CLinuxRendererGLES::ChromaTimerCollect(')):
    s = span(src, fn)
    got = re.findall(r'GpuDisjointLost\((m_\w+)\)', src[s[0]:s[1]] if s else '')
    if not got:
        fail(f'{name} ({fn.split("::")[1][:-1]}) does not ask through GpuDisjointLost()')
    for r in got:
        if r in readers and readers[r] != name:
            fail(f'{name} and {readers[r]} share the reader {r}: one would take the other\'s notice')
        readers.setdefault(r, name)
print(f'readers: {len(readers)} ({", ".join(sorted(readers))})')
pr = span(ws, 'void CWinSystemGbmGLESContext::PresentRender(')
if not pr or 'm_gpuDisjoint.Frame();' not in ws[pr[0]:pr[0] + 400]:
    fail('PresentRender() does not count the frame at its start (m_gpuDisjoint.Frame())')
print(f'DISJOINT-SRC FAILED ({len(fails)})' if fails else 'DISJOINT-SRC PASSED')
sys.exit(1 if fails else 0)
