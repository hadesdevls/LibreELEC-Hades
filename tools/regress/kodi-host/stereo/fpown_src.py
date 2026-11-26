# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# fpown_src.py <Kodi tree> (issue #33): in the source, what fpown_gl_test.cpp cannot run on the build host (no DRM
# device, no NVIDIA driver): where Kodi's own buffers of a frame-packed mode of nvidia-drm are made, asked and used.
#  - CWinSystemGbmEGLContext::FramePackedOwnStart() does nothing, not even a test, unless the mode is frame packed and
#    the driver is nvidia-drm (CDRMUtils::FramePackedByOffsets()), asks the window's buffer before anything is made,
#    makes its buffers of DRM_FORMAT_MOD_LINEAR, flipped by the GPU, linear without modifiers too, never reflects the
#    plane, checks and asks each buffer, and puts them in place only where Decide() says so
#  - CreateNewWindow() takes them away with the window and asks again for a new frame-packed mode only when no other
#    buffers of Kodi's own are in use; SetVideoOutput() keeps them; CheckFloatOutput() never holds a refusal with them
#    against the buffers of more than 8 bits; OwnScanoutStop() counts no failure of those for them; the window system
#    forgets them when it goes
#  - the buffers of more than 8 bits are made as before (not linear)
#  - CDRMAtomic::TestFramePacked() only tests (DRM_MODE_ATOMIC_TEST_ONLY), with the mode, and only a frame-packed mode
#    of nvidia-drm on an output that is meant to be on
# Prints FAIL lines and FPOWN-SRC PASSED or FAILED (n) last.
import os
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


def before(text, first, second):
    a, b = text.find(first), text.find(second)
    return a >= 0 and b >= 0 and a < b


EGL = read('xbmc/windowing/gbm/WinSystemGbmEGLContext.cpp')
ATOMIC = read('xbmc/windowing/gbm/drm/DRMAtomic.cpp')

start = body(EGL, 'void CWinSystemGbmEGLContext::FramePackedOwnStart(')
need(start != '', 'FramePackedOwnStart() is there')
need(before(start, 'if (!m_DRM || !m_DRM->FramePackedByOffsets())\n    return;', 'TestFramePacked('),
     'FramePackedOwnStart(): nothing, not even a test, unless the mode is frame packed on nvidia-drm')
need(before(start, 'm_DRM->TestFramePacked(window, why)', 'COwnScanout::Create('),
     "FramePackedOwnStart(): the window's buffer is asked before anything is made")
need(before(start, '  if (!FP_OWN::MakeOwn(m_DRM->FramePackedByOffsets(), m_renderableType != EGL_OPENGL_BIT, asked))\n  {',
            'COwnScanout::Create('),
     'FramePackedOwnStart(): MakeOwn() decides before anything is made')
need('{DRM_FORMAT_MOD_LINEAR}, true, true, failed)' in start,
     'FramePackedOwnStart(): buffers of DRM_FORMAT_MOD_LINEAR, flipped by the GPU, linear without modifiers too')
need('SetReflectY(true)' not in start and 'SetReflectY(false)' in start,
     'FramePackedOwnStart(): the plane is never asked to reflect them (a frame-packed plane takes no rotation)')
need('for (unsigned int i = 0; i < COwnScanout::BUFFERS && ownAsked == FP_OWN::Asked::TAKEN; ++i)' in start and
     'm_DRM->FramePackedBufferUsable(own->BufferAt(i), wrong)' in start and
     'ownAsked = FP_OWN::AskedOf(m_DRM->TestFramePacked(own->BufferAt(i), ownWhy));' in start,
     'FramePackedOwnStart(): each buffer checked for what nvidia-drm asks of a frame-packed one, and asked')
need(before(start, 'if (verdict.use != FP_OWN::Use::OWN)', 'SetOwnBuffers(own)') and
     'FP_OWN::Decide(asked, own != nullptr, ownAsked)' in start,
     'FramePackedOwnStart(): the buffers are put in place only where Decide() says so')
need('m_ownFramePacked = true;' in start and 'm_ownBits' not in start and 'm_ownDepth' not in start,
     'FramePackedOwnStart(): they are of a frame-packed mode, not buffers of more than 8 bits')

create = body(EGL, 'bool CWinSystemGbmEGLContext::CreateNewWindow(')
need('else if (OwnScanoutOn() && m_ownFramePacked)\n    OwnScanoutStop("the window is made again, for another mode", false);' in create,
     'CreateNewWindow(): they go with the window')
need(before(create, 'OwnScanoutFollow(ownDepth);', 'if (!OwnScanoutOn())\n    FramePackedOwnStart(bo);'),
     'CreateNewWindow(): a frame-packed mode asks for them after the buffers of more than 8 bits, where none are in use')

video = body(EGL, 'bool CWinSystemGbmEGLContext::SetVideoOutput(')
need(before(video, 'if (OwnScanoutOn() && m_ownFramePacked)\n    return CWinSystemGbm::SetVideoOutput(videoPicture);',
            'OwnScanoutWanted(bitDepth)'),
     'SetVideoOutput(): a frame-packed mode keeps its buffers of 8 bits')

check = body(EGL, 'void CWinSystemGbmEGLContext::CheckFloatOutput(')
fp = body(check, 'if (m_ownFramePacked)')
need(fp != '' and fp.rstrip('}').rstrip().endswith('return;') and before(check, 'if (m_ownFramePacked)', 'RefusalAboutBuffer()'),
     'CheckFloatOutput(): a refusal with them is not held against the buffers of more than 8 bits')

stop = body(EGL, 'void CWinSystemGbmEGLContext::OwnScanoutStop(')
need(before(stop, 'if (std::exchange(m_ownFramePacked, false))', 'm_ownBlocked = true;'),
     'OwnScanoutStop(): no failure of the buffers of more than 8 bits counted for them')

gone = body(EGL, 'bool CWinSystemGbmEGLContext::DestroyWindowSystem(')
need('m_ownFramePacked = false;' in gone, 'DestroyWindowSystem(): forgotten with the window system')

hdr = body(EGL, 'bool CWinSystemGbmEGLContext::OwnScanoutStart(')
need('format->drm, format->modifiers, flipOnGpu, false,' in hdr,
     'OwnScanoutStart(): the buffers of more than 8 bits are made as before (not linear)')

test = body(ATOMIC, 'std::optional<bool> CDRMAtomic::TestFramePacked(')
need(test != '', 'CDRMAtomic::TestFramePacked() is there')
need('if (!FramePackedByOffsets() || !bo || !plane || !m_crtc || !m_connector || !m_mode || !m_active)\n    return std::nullopt;' in test,
     'TestFramePacked(): only a frame-packed mode of nvidia-drm, on an output that is meant to be on')
need('DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr) == 0)' in test and test.count('drmModeAtomicCommit(m_fd') == 1,
     'TestFramePacked(): only a test commit (nothing changes on the output)')
need('add(m_crtc, "MODE_ID", mode.Get());' in test and 'add(m_connector, "CRTC_ID", m_crtc->GetCrtcId());' in test,
     'TestFramePacked(): the commit asked is the one of the mode')
need('add(plane, "CRTC_H", ScanoutHeight());' in test and 'add(plane, "SRC_H", static_cast<uint64_t>(m_height) << 16);' in test,
     'TestFramePacked(): the buffer covers the frame of both eyes, as the commit sets it')

print('FPOWN-SRC PASSED' if not fails else 'FPOWN-SRC FAILED (%d)' % len(fails))
sys.exit(1 if fails else 0)
