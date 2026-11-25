# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# chroma_src_check.py <Kodi tree>: in the source, what the host test of ChromaStepDown.h cannot run: how Kodi's renderer
# measures the pictures of its chroma on a GPU without a usable timer, and what kodi.log says of it.
#  - the timer queries of the renderer and of the window system are usable only where the GPU's timestamps have bits
#    (GL_QUERY_COUNTER_BITS_EXT of GL_TIMESTAMP_EXT);
#  - a video is measured by the GPU time where the timer is usable, else by the missed refreshes where the window system
#    reads the flips of the commits (ChromaMeasure());
#  - the renderer tells the window system of each picture drawn at a level whose refreshes are counted, and takes back
#    what the flips said (ChromaFlipsCollect() -> CStepDown::Presented());
#  - PresentRender() reads the flips for those pictures, tells CFlipChain of each one and of each commit;
#  - the log says which measure, and "no step-down" only where neither can be had.
# Prints FAIL lines and CHROMA-SRC PASSED or FAILED (n) last.
import os
import re
import sys

K = sys.argv[1]
fails = []


def fail(text):
    fails.append(text)
    print('FAIL: ' + text)


def read(rel):
    return open(os.path.join(K, rel), encoding='utf-8').read()


def body(text, signature):
    at = text.find(signature)
    if at < 0:
        return ''
    start = text.index('{', at)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return ''


def need(cond, text):
    if not cond:
        fail(text)


rd = read('xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.cpp')
ws = read('xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp')
# the timer of the renderer: usable only with bits
q = rd[rd.find('struct ChromaTimerQueries'):rd.find('const ChromaTimerQueries& ChromaQueries()')]
need(re.search(r'bool Usable\(\) const \{[^}]*bits > 0', q), 'the renderer\'s timer is usable without bits '
     '(ChromaTimerQueries::Usable())')
cq = body(rd, 'const ChromaTimerQueries& ChromaQueries()')
need('GL_QUERY_COUNTER_BITS_EXT' in cq and 'GL_TIMESTAMP_EXT' in cq, 'the renderer does not ask the bits of the '
     'GPU\'s timestamps (GL_QUERY_COUNTER_BITS_EXT)')
# the timers of the window system: none without bits
gtb = body(ws, 'int CWinSystemGbmGLESContext::GpuTimestampBits(')
need('GL_QUERY_COUNTER_BITS_EXT' in gtb, 'the window system does not ask the bits of the GPU\'s timestamps')
for fn in ('void CWinSystemGbmGLESContext::Hdr10AdaptTimerMark(', 'void CWinSystemGbmGLESContext::ConvTimerMark(',
           'void CWinSystemGbmGLESContext::DVGpuTimerMark('):
    need('GpuTimestampBits() > 0' in body(ws, fn), f'{fn.split("::")[1][:-1]} makes timers where the timestamps '
         'may have no bits')
# the measure of a video
cm = body(rd, 'KODI::CHROMA::CStepDown::Measure CLinuxRendererGLES::ChromaMeasure(')
need(re.search(r'if \(ChromaQueries\(\)\.Usable\(\)\)\s*return Measure::GPU_TIME;', cm),
     'a GPU with a usable timer is not measured by its GPU time')
need(re.search(r'KodiFlipsKnown\(\)\)\s*return Measure::MISSED_REFRESHES;', cm),
     'a GPU without a timer is not measured by its missed refreshes where the flips are read')
need(re.search(r'm_chroma\.Start\([^;]*ChromaMeasure\(\)\);', rd), 'a new video does not take ChromaMeasure()')
# the pictures told, and what came back taken
need(re.search(r'm_chroma\.CountingRefreshes\(\)[^;]*KodiPictureDrawn\(static_cast<int>\(m_chroma\.Current\(\)\)\)',
               rd), 'the renderer does not tell the window system of a picture whose refresh is counted')
fc = body(rd, 'void CLinuxRendererGLES::ChromaFlipsCollect(')
need('KodiFlipsTake()' in fc and 'm_chroma.Presented(' in fc, 'what the flips said is not given to the step-down')
need('outcome.tag != static_cast<int>(m_chroma.Current())' in fc, 'a flip of a picture of the level before is taken '
     'for this level')
tc = body(rd, 'void CLinuxRendererGLES::ChromaTimerCollect(')
need(re.search(r'MISSED_REFRESHES\)\s*\{\s*ChromaFlipsCollect\(\);', tc), 'ChromaTimerCollect() does not take the '
     'flips on a GPU without a timer')
# the window system: the flips of those commits
pr = body(ws, 'void CWinSystemGbmGLESContext::PresentRender(')
need('const int kodiDrawn = std::exchange(m_kodiFlipDrawn, -1);' in pr[:700], 'PresentRender() does not take the '
     'picture of the pass at its start')
need(re.search(r'm_kodiFlips\.WantsFlips\(kodiDrawn >= 0\)\)\s*lagFd = dup\(fd\);', pr),
     'the flips are not read for the pictures of Kodi\'s renderer')
need('KodiFlipTold(flipRead ? flippedNs : 0, kodiDrawn >= 0);' in pr, 'the flip read is not told to CFlipChain')
need(re.search(r'if \(!kodiFlipTold && m_kodiFlips\.WantsFlips\(kodiDrawn >= 0\)\)\s*KodiFlipTold\(0,', pr),
     'a flip not read does not break the chain')
need('m_kodiFlips.Committed(kodiDrawn);' in pr, 'the commit of a picture is not told to CFlipChain')
need('m_eglFence' in body(ws, 'bool CWinSystemGbmGLESContext::KodiFlipsKnown('), 'KodiFlipsKnown() does not ask for '
     'the out-fence of the commits')
# the log
said = body(rd, 'std::string CLinuxRendererGLES::ChromaStepSaid(')
need('case Measure::MISSED_REFRESHES:' in said and 'miss refreshes' in said, 'the log does not say that the pictures '
     'are measured by their missed refreshes')
need('no step-down' in said and said.rfind('no step-down') > said.find('case Measure::NONE:') > 0,
     'the log says "no step-down" elsewhere than where neither measure can be had')
need('GL_EXT_disjoint_timer_query): no step-down' not in rd, 'the log says no step-down for every GPU without a timer')
print(f'CHROMA-SRC FAILED ({len(fails)})' if fails else 'CHROMA-SRC PASSED')
sys.exit(1 if fails else 0)
