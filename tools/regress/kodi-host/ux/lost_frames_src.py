# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# lost_frames_src.py <Kodi tree>: in the source, what the host test of CLostFrames (verdict_test.cpp) cannot run: the
# window system samples Kodi's own counters of the frames that its video player dropped and its renderer skipped, with
# whether the player buffers, and starts the counting again (Restart()) at every new output of a video, at every
# emptying of the renderer's queue (a seek, a chapter, a change of stream: DVBridgeDiscarded()), and whenever the
# player is paused, at another speed or seeking; the sentence takes what CLostFrames says, and how busy the graphics
# are. Prints FAIL lines and LOST-FRAMES-SRC PASSED or FAILED (n) last.
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


cpp = read('xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp')
h = read('xbmc/windowing/gbm/WinSystemGbmGLESContext.h')
pub = body(cpp, 'void CWinSystemGbmGLESContext::PublishPlayerInfo(')
m = re.search(r'if \(([^{;]*?)\)\s*\{[^{}]*?m_lostFrames\.Restart\(', pub, re.S)
cond = m.group(1) if m else ''
need(m, 'PublishPlayerInfo() never starts the counting of lost frames again (m_lostFrames.Restart())')
for part, what in ((r'outputs != m_lostFramesOutputs', 'a new output of a video'),
                   (r'seeks != m_lostFramesSeeks', 'an emptying of the renderer\'s queue (a seek, a chapter, a stream)'),
                   (r'player->IsPaused\(\)', 'a pause'),
                   (r'player->GetPlaySpeed\(\) != 1\.0f', 'another speed'),
                   (r'cache\.IsSeeking\(\)', 'a seek in progress')):
    need(re.search(part, cond), f'the counting of lost frames does not start again at {what}')
need(re.search(r'const unsigned int outputs = m_videoOutputs\.load\(', pub) and
     re.search(r'const unsigned int seeks = m_dvStepSeeks\.load\(', pub),
     'the outputs and the seeks that the counting compares are not those of the window system')
need(re.search(r'm_lostFramesOutputs = outputs;', pub) and re.search(r'm_lostFramesSeeks = seeks;', pub),
     'the outputs and the seeks it restarted with are not kept')
need(re.search(r'm_lostFrames\.Sample\(t, cache\.GetVideoDropped\(\), cache\.GetVideoSkipped\(\), player->IsCaching\(\)\)',
               pub), 'the counting does not sample Kodi\'s dropped and skipped frames with whether the player buffers')
need(pub.find('m_lostFrames.Restart(') < pub.find('m_lostFrames.Sample(') < pub.find('m_lostFrames.Window('),
     'the counting is not restarted, sampled and read in that order')
for field, src in (('input.dropped', 'lost.dropped'), ('input.skipped', 'lost.skipped'),
                   ('input.lostSustained', 'lost.sustained'), ('input.bufferingLost', 'lost.bufferingSustained')):
    need(f'{field} = {src};' in pub, f'the sentence does not take {src}')
need(re.search(r'input\.gpuBusy = std::atoi\(gpu\.c_str\(\)\)', pub) and 'info["hades.sys.gpu"]' in pub,
     'the sentence does not take how busy the graphics are')
svo = body(cpp, 'bool CWinSystemGbmGLESContext::SetVideoOutput(')
need(re.search(r'if \(picture\)\s*m_videoOutputs\.fetch_add\(1', svo), 'SetVideoOutput() does not count a new output of a video')
disc = body(h, 'void DVBridgeDiscarded()')
need('m_dvStepSeeks.fetch_add(1' in disc, 'DVBridgeDiscarded() does not count an emptying of the renderer\'s queue')
print(f'LOST-FRAMES-SRC FAILED ({len(fails)})' if fails else 'LOST-FRAMES-SRC PASSED')
sys.exit(1 if fails else 0)
