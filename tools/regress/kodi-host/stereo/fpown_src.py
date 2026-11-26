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
#  - the driver's dumb buffers (where GBM's could not be made or were refused): made, given a dma-buf, their handle
#    closed and imported into GBM (MakeDumb()), at the driver's pitch, linear; copied into by the processor only where
#    they are dumb buffers that EGL cannot draw into, with that copy checked first; unmapped before they go
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
need('for (unsigned int i = 0; i < COwnScanout::BUFFERS && taken == FP_OWN::Asked::TAKEN; ++i)' in start and
     'm_DRM->FramePackedBufferUsable(buffers->BufferAt(i), wrong)' in start and
     'taken = FP_OWN::AskedOf(m_DRM->TestFramePacked(buffers->BufferAt(i), refusal));' in start,
     'FramePackedOwnStart(): each buffer checked for what nvidia-drm asks of a frame-packed one, and asked')
# (the driver's dumb buffers: only after GBM's, only where TryDumb() says so, GBM's given back first, and asked the
# same way)
dumb_call = 'COwnScanout::Memory::DUMB, m_DRM->GetFileDescriptor());\n    ownAsked = ask(own, failed, ownWhy);'
need(start.count('ownAsked = ask(own, failed, ownWhy);') == 2 and
     'FP_OWN::Asked ownAsked = ask(own, failed, ownWhy);' in start and dumb_call in start and
     before(start, '{DRM_FORMAT_MOD_LINEAR}, true, true, failed);', 'FP_OWN::TryDumb(own != nullptr, ownAsked)') and
     before(start, 'FP_OWN::TryDumb(own != nullptr, ownAsked)', '    own.reset();\n') and
     before(start, '    own.reset();\n', 'COwnScanout::Memory::DUMB'),
     "FramePackedOwnStart(): the driver's dumb buffers only after GBM's, where TryDumb() says so, GBM's given back "
     'first, each one asked as GBM\'s are')
need('{DRM_FORMAT_MOD_LINEAR}, true, true, failed,\n' in start,
     "FramePackedOwnStart(): the dumb buffers flipped by the GPU or copied, linear")

OWN = read('xbmc/windowing/gbm/OwnScanout.cpp')
make = body(OWN, 'gbm_bo* MakeDumb(')
need(before(make, 'DRM_IOCTL_MODE_CREATE_DUMB', 'drmPrimeHandleToFD(') and
     before(make, 'drmPrimeHandleToFD(', 'DRM_IOCTL_MODE_DESTROY_DUMB') and
     before(make, 'DRM_IOCTL_MODE_DESTROY_DUMB', 'gbm_bo_import(') and
     before(make, 'gbm_bo_import(', 'close(fd);'),
     'MakeDumb(): made, given a dma-buf, its handle closed, imported into GBM by the dma-buf (one handle, GBM\'s)')
need('data.strides[0] = static_cast<int>(create.pitch);' in make and 'data.modifier = DRM_FORMAT_MOD_LINEAR;' in make and
     'pitch = create.pitch;' in make and 'create.bpp = 32;' in make,
     "MakeDumb(): imported at the driver's pitch, linear, 32 bits a pixel")
create_own = body(OWN, 'std::shared_ptr<COwnScanout> COwnScanout::Create(')
need('      if (memory != Memory::DUMB)\n      {\n        failed = drawFailed;\n        return nullptr;\n      }' in create_own,
     'Create(): only dumb buffers are copied into by the processor; a buffer of GBM that EGL cannot draw into is refused')
need('    if (own->m_copy\n            ? !own->CopyProbe(failed)' in create_own,
     'Create(): the copy is checked before use, as the flip of the GPU is')
swap = body(OWN, 'bool COwnScanout::Swap(')
need(before(swap, 'if (m_copy && m_ring.BackIndex() >= 0)', 'CopyOut(static_cast<unsigned int>(m_ring.BackIndex()), failed)'),
     'Swap(): a frame copied into the buffer chosen for it where the frames are copied')
gone_own = body(OWN, 'COwnScanout::~COwnScanout(')
need(before(gone_own, 'munmap(buffer.map, buffer.mapSize);', 'gbm_bo_destroy(buffer.bo);'),
     'the mapping of a dumb buffer goes before the buffer')
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
