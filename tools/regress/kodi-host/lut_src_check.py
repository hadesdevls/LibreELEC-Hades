# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# lut_src_check.py <Kodi tree>: in the source, what the host tests of DisplayLut.h and DisplayLutLoader.h cannot run:
#  - the window system reads a LUT file only through the loader (CLutLoader, off the render thread): LutFor() neither
#    reads a file nor converts one, and says LOADING meanwhile, which it does not keep for half a second;
#  - the bridges let go of a table before the loader lets it go (a change of the file of a kind, the setting off);
#  - Kodi's renderer makes no copy of the table (no dvbridge_lut_atlas_make()) and puts it on the GPU a part at a time,
#    at most LUT_PART_BYTES a frame, the picture without it until all of it is there (CLut3dGLES::Ready()), and says it
#    is loading meanwhile (LutDrawn(..., pending)), which the window system tells as LOADING, not as a failure;
#  - the player information has a word for LOADING, in Estuary's strings and in the core's.
# Prints FAIL lines and LUT-SRC PASSED or FAILED (n) last.
import os
import re
import sys

K = sys.argv[1]
fails = []


def need(cond, text):
    if not cond:
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


ws = read('xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp')
rd = read('xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.cpp')
lf = body(ws, 'const dvbridge_lut* CWinSystemGbmGLESContext::LutFor(')
need(lf, 'no LutFor()')
for bad in ('LoadFile', 'dvbridge_lut_parse', 'ReadLutFile(', 'LoadLut('):
    need(bad not in lf, f'LutFor() reads or converts a file on the render thread ({bad})')
need('m_lutLoader->Want(' in lf, 'LutFor() does not ask the loader')
need(re.search(r'State::LOADING\)\s*d\.why = Reason::LOADING;', lf), 'LutFor() does not say LOADING while a file is read')
need('m_lutNow.why != Reason::LOADING' in lf, 'a LOADING decision is kept for half a second')
need(re.search(r'm_lutAsked\[kind\]\.path != d\.path \|\| m_lutAsked\[kind\]\.range != range\)\s*\{\s*if \(m_dvBridge\)\s*'
               r'm_dvBridge->SetLut\(nullptr\);\s*if \(m_dvConvertBridge\)\s*m_dvConvertBridge->SetLut\(nullptr\);', lf),
     'the bridges do not let go of a table before another file of its kind is asked for')
need(re.search(r'm_dvConvertBridge->SetLut\(nullptr\);\s*m_lutLoader->Clear\(\);', lf),
     'the bridges do not let go of the tables before the setting off clears them')
rf = body(ws, 'std::shared_ptr<dvbridge_lut> CWinSystemGbmGLESContext::ReadLutFile(')
need('dvbridge_lut_parse' in rf and 'std::vector<uint8_t>().swap(data);' in rf,
     'ReadLutFile() does not let go of the bytes of the file before it returns the table')
need('&CWinSystemGbmGLESContext::ReadLutFile' in lf, 'the loader does not read with ReadLutFile()')
ld = body(ws, 'void CWinSystemGbmGLESContext::LutDrawn(')
need(re.search(r'Reason::APPLIED && pending && !failed\)\s*\{\s*m_lutNow\.why = Reason::LOADING;', ld),
     'a table that Kodi\'s renderer still puts on the GPU is not said to be loading')
need('case Reason::LOADING:' in body(ws, 'std::string CWinSystemGbmGLESContext::LutWords('),
     'LOADING has no words in the player information')
# Kodi's renderer
take = body(rd, 'void CLinuxRendererGLES::Lut3dTake(')
need(take, 'no Lut3dTake()')
need('dvbridge_lut_atlas_make' not in rd, 'Kodi\'s renderer makes a copy of the table (dvbridge_lut_atlas_make())')
need('dvbridge_lut_atlas_layout(' in take and 'm_lut3d.Begin(' in take, 'the table is not laid out without a copy')
need(re.search(r'while \(m_lut3dPart < n && \(put == 0 \|\| put \+ slice <= LUT_PART_BYTES\)\)', take),
     'the table is not put on the GPU a part a frame (LUT_PART_BYTES)')
need(re.search(r'm_lut3d\.Put\(dvbridge_lut_atlas_slice_data\(lut, m_lut3dPart\), x0, y0, n, n, why\)', take),
     'the slices are not put straight from the LUT')
need(re.search(r'm_lut3dPart == n\)\s*\{\s*m_lut3d\.Ready\(\);', take), 'the table is not ready once all of it is there')
need(re.search(r'const bool want = lut != nullptr && m_lut3d\.Valid\(\);', take),
     'the shaders take a table that is not all there')
need('window->LutDrawn(true, applied, failed, m_lut3dPending);' in rd, 'Kodi\'s renderer does not say it is loading')
h = read('xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.h')
m = re.search(r'LUT_PART_BYTES = size_t\{(\d+)\} << 20;', h)
need(m and 1 <= int(m.group(1)) <= 64, 'LUT_PART_BYTES is not a few MB')
lg = read('xbmc/cores/VideoPlayer/VideoRenderers/VideoShaders/Lut3dGLES.h')
need('bool Valid() const { return m_texture != 0 && m_ready; }' in lg, 'CLut3dGLES is valid before all of it is there')
# the words
core = read('addons/resource.language.en_gb/resources/strings.po')
need(re.search(r'msgctxt "#62821"\nmsgid "loading"', core), 'no core string #62821 "loading"')
for lang, want in (('en_gb', ''), ('es_es', 'cargando'), ('es_ar', 'cargando'), ('es_mx', 'cargando')):
    po = read(f'addons/skin.estuary/language/resource.language.{lang}/strings.po')
    need(re.search(r'msgctxt "#54821"\nmsgid "loading"\nmsgstr "%s"' % want, po), f'Estuary {lang}: no #54821')
print(f'LUT-SRC FAILED ({len(fails)})' if fails else 'LUT-SRC PASSED')
sys.exit(1 if fails else 0)
