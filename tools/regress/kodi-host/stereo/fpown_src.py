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
#  - the driver's dumb buffers (where GBM's could not be made or were refused), 1.2.3: no part of GBM in them (NVIDIA's
#    GBM refused to import one, Khazar321's RTX 5090): made, their framebuffer made of their handle with
#    DRM_FORMAT_MOD_LINEAR said, a small buffer of GBM standing for each whose user data is that framebuffer
#    (MakeDumb()); mapped; imported into EGL by their dma-buf at the driver's pitch, linear, where EGL takes them, and
#    then what the GPU draws read back through the mapping (DumbLayoutProbe()) or else copied into by the processor,
#    with that copy checked first; a buffer of GBM that EGL cannot draw into refused as before; when they go, the
#    mapping, then the small buffer (with the framebuffer), then the dumb buffer
#  - CDRMUtils: what a framebuffer of a dumb buffer is made of is asked of it, never of the small buffer of GBM
#    (FramePackedBufferUsable(), DescribeFramePackedBuffer(), DrmOpaqueFbGetFromBo())
#  - CDRMAtomic::DrmAtomicCommit(): the request before a refused one goes out with the frame only where its plane can
#    show the frame's buffer (FALLBACK_FRAME::GoesOut()); where it cannot, it is not committed, said as it is and not
#    as "atomic commit failed", and Kodi goes on as after a refused commit; a refused change to a frame-packed mode a
#    warning, any other one an error as before
#  - the card "3D: frame packing" (FramePackingCheck.h): the builtin HadesFramePackingCheck writes the answer of
#    CWinSystemGbmEGLContext::FramePackingCheck() to the Home window; that answer is made of test commits only
#    (TestFramePackedMode()), never while a video plays, of the kind of the window's buffer and, on nvidia-drm, of the
#    dumb buffers as a video makes them
#  - 1.2.3 (the owner's RTX 4080 SUPER: 40.45 ms a frame copied): the block-linear buffers that the GPU draws into
#    (COwnScanout::Memory::GBM_EYES) come first, with the plane's modifiers and the mode, and only where they are not
#    taken are the linear and dumb ones made; they are made with the block-linear modifiers alone, each probed with
#    EyesProbe(), and each frame is put into them by EyesBlit(); CDRMUtils takes their right eye from the block row
#    (BlockLinearRightEyeRow()); the copy of the processor goes through pixel buffers, made before its check, and
#    falls back to reading at once; the card asks for the block-linear buffers first, and says what the copy costs
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
     'FP_OWN::Asked ownAsked = FP_OWN::Asked::NOT_ASKED;' in start and dumb_call in start and
     before(start, '{DRM_FORMAT_MOD_LINEAR}, true, true, failed);', 'FP_OWN::TryDumb(own != nullptr, ownAsked)') and
     before(start, 'FP_OWN::TryDumb(own != nullptr, ownAsked)', '    own.reset();\n') and
     before(start, '    own.reset();\n', 'COwnScanout::Memory::DUMB'),
     "FramePackedOwnStart(): the driver's dumb buffers only after GBM's, where TryDumb() says so, GBM's given back "
     'first, each one asked as GBM\'s are')
need('{DRM_FORMAT_MOD_LINEAR}, true, true, failed,\n' in start,
     "FramePackedOwnStart(): the dumb buffers flipped by the GPU or copied, linear")

OWN = read('xbmc/windowing/gbm/OwnScanout.cpp')
make = body(OWN, 'bool MakeDumb(')
need(make != '' and 'gbm_bo_import(' not in OWN, 'MakeDumb() is there, and nothing of OwnScanout.cpp imports into GBM')
need(before(make, 'DRM_IOCTL_MODE_CREATE_DUMB', 'drmModeAddFB2WithModifiers(') and
     before(make, 'drmModeAddFB2WithModifiers(', 'gbm_bo_create(') and
     before(make, 'gbm_bo_create(', 'gbm_bo_set_user_data(standIn, fb, DumbFramebufferGone);'),
     'MakeDumb(): made, its framebuffer made of its handle, then the small buffer of GBM that holds that framebuffer')
need('uint64_t modifiers[4] = {DRM_FORMAT_MOD_LINEAR, 0, 0, 0};' in make and 'DRM_MODE_FB_MODIFIERS) != 0)' in make and
     'uint32_t handles[4] = {handle, 0, 0, 0};' in make and 'uint32_t pitches[4] = {pitch, 0, 0, 0};' in make and
     'pitch = create.pitch;' in make and 'create.bpp = 32;' in make,
     "MakeDumb(): its framebuffer of its handle, at the driver's pitch, DRM_FORMAT_MOD_LINEAR said, 32 bits a pixel")
need('fb->own = drm_fb::Own{handle, static_cast<uint32_t>(width), static_cast<uint32_t>(height), pitch,' in make,
     'MakeDumb(): the framebuffer says what it is made of (drm_fb::own)')
need(before(make, 'standIn = gbm_bo_create(', '    drmModeRmFB(drmFd, fbId);\n    return false;'),
     'MakeDumb(): a framebuffer made for a small buffer that GBM could not make is removed')
gone_fb = body(OWN, 'void DumbFramebufferGone(')
need('drmModeRmFB(fd, fb->fb_id);' in gone_fb and 'drmModeRmFB(fd, fb->opaque_fb_id);' in gone_fb and
     'delete fb;' in gone_fb, 'the framebuffer of a dumb buffer goes with its small buffer of GBM')
create_own = body(OWN, 'std::shared_ptr<COwnScanout> COwnScanout::Create(')
need('    if (!drawn)\n    {\n      failed = drawFailed;\n      return nullptr;\n    }' in create_own,
     'Create(): a buffer of GBM that EGL cannot draw into is refused')
dumb_branch = create_own[create_own.find('    if (memory == Memory::DUMB)\n    {\n      if (!MakeDumb('):]
dumb_branch = dumb_branch[:dumb_branch.find('      continue;\n    }\n') + 1]
need(before(dumb_branch, 'MakeDumb(', 'MapDumb(drmFd, buffer.dumbHandle') and
     before(dumb_branch, 'MapDumb(drmFd, buffer.dumbHandle', 'drmPrimeHandleToFD(drmFd, buffer.dumbHandle') and
     'dmabuf.pitch[0] = buffer.pitch;' in dumb_branch and 'dmabuf.modifier = DRM_FORMAT_MOD_LINEAR;' in dumb_branch and
     'dmabuf.hasModifier = true;' in dumb_branch and
     before(dumb_branch, 'Probe(buffer.framebuffer, bits, drawFailed)', 'own->m_copy = true;'),
     'Create(): each dumb buffer made, mapped, imported into EGL at its pitch, linear, and checked; copied into where '
     'EGL cannot draw into it')
need(before(create_own, 'if (!own->m_copy && memory == Memory::DUMB)', '!own->DumbLayoutProbe(layout)') and
     before(create_own, '!own->DumbLayoutProbe(layout)', 'own->m_copy = true;\n        own->m_copyReason = "EGL imported them, but " + layout;'),
     'Create(): what the GPU draws into a dumb buffer is read back where the display engine reads it; where it is not '
     'there, the processor copies')
need('    if (own->m_copy\n            ? !own->CopyProbe(failed)' in create_own,
     'Create(): the copy is checked before use, as the flip of the GPU is')
probe = body(OWN, 'bool COwnScanout::CopyProbe(')
need('RowHolds(buffer.map + static_cast<size_t>(m_height - 1) * buffer.pitch, m_format,' in probe and
     'RowHolds(buffer.map, m_format, false, "first", wrong)' in probe,
     'CopyProbe(): the probe pixels of the bottom row of the frame in the last row of the buffer, the first one black')
layout = body(OWN, 'bool COwnScanout::DumbLayoutProbe(')
need(before(layout, 'FlipBlit(m_frameFramebuffer, buffer.framebuffer, m_width, m_height);', 'glFinish();') and
     before(layout, 'glFinish();', 'RowHolds(buffer.map + static_cast<size_t>(m_height - 1) * buffer.pitch') and
     'for (unsigned int i = 0; same && i < m_buffers.size(); ++i)' in layout,
     'DumbLayoutProbe(): each buffer: the GPU\'s flip finished, then read through its mapping at its pitch')
swap = body(OWN, 'bool COwnScanout::Swap(')
need(before(swap, 'if (m_copy && m_ring.BackIndex() >= 0)', 'CopyOut(static_cast<unsigned int>(m_ring.BackIndex()), failed,'),
     'Swap(): a frame copied into the buffer chosen for it where the frames are copied')
# (1.2.3: the block-linear buffers that the GPU draws into, first)
eyes_call = 'PlaneModifiersOf(format), true, false,\n                                    eyesFailed, COwnScanout::Memory::GBM_EYES, -1, &mode);'
need(eyes_call in start and 'const FP_OWN::Asked eyesAsked = ask(eyes, eyesFailed, eyesWhy);' in start and
     before(start, eyes_call, '{DRM_FORMAT_MOD_LINEAR}, true, true, failed);') and
     before(start, 'if (FP_OWN::EyesTaken(eyes != nullptr, eyesAsked))', '  if (!own)\n  {'),
     'FramePackedOwnStart(): the block-linear buffers first, with the plane\'s modifiers and the mode, asked as the '
     'others; the linear and dumb ones only where they are not taken')
need("const std::vector<uint64_t>& asked = memory == Memory::GBM_EYES ? blockLinear : modifiers;" in create_own and
     'if (!buffer.bo && memory != Memory::GBM_EYES)' in create_own and
     'if (!EyesProbe(fb, buffer.framebuffer, width, height, own->m_eyes, failed))' in create_own and
     'STEREO::BlockLinearRightEyeRow(*framePacked, modifier, pitch,' in create_own,
     'Create(GBM_EYES): the block-linear modifiers alone, the right eye from what GBM made, each buffer probed')
need("else if (m_frameFramebuffer && m_ring.BackIndex() >= 0 && m_memory == Memory::GBM_EYES)\n    EyesBlit(" in swap,
     'Swap(): each eye put where a block-linear buffer holds it')
need(before(create_own, '      own->MakePackBuffers();', '            ? !own->CopyProbe(failed)'),
     'Create(): the pixel buffers of the copy made before the copy is checked')
copy_out = body(OWN, 'bool COwnScanout::CopyOut(')
need(before(copy_out, 'CopyOutInBands(', 'ReleasePackBuffers();') and
     before(copy_out, 'ReleasePackBuffers();', 'glReadPixels(0, 0, m_width, m_height,'),
     'CopyOut(): through the pixel buffers; where that fails, read back at once, as before')
gone_own = body(OWN, 'COwnScanout::~COwnScanout(')
need(before(gone_own, 'munmap(buffer.map, buffer.mapSize);', 'gbm_bo_destroy(buffer.bo);') and
     before(gone_own, 'gbm_bo_destroy(buffer.bo);', 'drmIoctl(m_drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);'),
     'the mapping of a dumb buffer goes first, then its small buffer of GBM with its framebuffer, then the dumb buffer')

UTILS = read('xbmc/windowing/gbm/drm/DRMUtils.cpp')
for name in ('bool CDRMUtils::FramePackedBufferUsable(', 'std::string CDRMUtils::DescribeFramePackedBuffer(',
             'uint32_t CDRMUtils::DrmOpaqueFbGetFromBo('):
    b = body(UTILS, name)
    need(before(b, 'OwnFramebufferOf(bo)', 'gbm_bo_get_'),
         name.split('::')[1] + ' asks a framebuffer of a dumb buffer what it is made of before anything of GBM')
usable = body(UTILS, 'bool CDRMUtils::FramePackedBufferUsable(')
describe = body(UTILS, 'std::string CDRMUtils::DescribeFramePackedBuffer(')
need('else if (STEREO::IsNvidiaBlockLinear(modifier))' in usable and 'blockRight + m_mode->vdisplay' in usable and
     'STEREO::BlockLinearRightEyeRow(' in describe,
     'CDRMUtils: a block-linear buffer judged and said with its right eye on the block row')
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
ask = body(ATOMIC, 'std::optional<bool> CDRMAtomic::AskFramePacked(')
mode_test = body(ATOMIC, 'std::optional<bool> CDRMAtomic::TestFramePackedMode(')
need(test != '' and ask != '' and mode_test != '', 'TestFramePacked(), TestFramePackedMode() and AskFramePacked() are there')
need('  if (!FramePackedByOffsets() || !m_mode)\n    return std::nullopt;\n  return AskFramePacked(*m_mode, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),' in test and
     'if (!bo || !plane || !m_crtc || !m_connector || !m_active)\n    return std::nullopt;' in ask,
     'TestFramePacked(): only a frame-packed mode of nvidia-drm, on an output that is meant to be on, with the frame of the window')
need('  if (STEREO::LayoutOf(mode.flags) != STEREO::Layout::FRAME_PACKING)\n    return std::nullopt;' in mode_test and
     'return AskFramePacked(mode, mode.hdisplay, static_cast<uint32_t>(mode.vdisplay + mode.vtotal), bo,' in mode_test,
     'TestFramePackedMode(): only a frame-packed mode, the plane over its whole frame')
need('DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr) == 0)' in ask and ask.count('drmModeAtomicCommit(m_fd') == 1,
     'AskFramePacked(): only a test commit (nothing changes on the output)')
need('CDRMPropertyBlob mode(m_fd, &asked, sizeof(asked));' in ask and 'add(m_crtc, "MODE_ID", mode.Get());' in ask and
     'add(m_connector, "CRTC_ID", m_crtc->GetCrtcId());' in ask,
     'AskFramePacked(): the commit asked is the one of the mode asked')
need('add(plane, "CRTC_H", static_cast<uint64_t>(asked.vdisplay) + asked.vtotal);' in ask and
     'add(plane, "SRC_H", static_cast<uint64_t>(srcHeight) << 16);' in ask,
     'AskFramePacked(): the buffer covers the frame of both eyes, as the commit sets it')

commit = body(ATOMIC, 'bool CDRMAtomic::DrmAtomicCommit(')
need(before(commit, 'm_req = oldRequest;', 'if (!FALLBACK_FRAME::GoesOut(source, size))') and
     before(commit, 'if (!FALLBACK_FRAME::GoesOut(source, size))', '    if (rendered && !notMade)\n    {\n      AddProperty(outputPlane, "FB_ID", fb_id);'),
     'DrmAtomicCommit(): the frame goes into the request before only where its plane can show it')
need('const bool fenced = rendered && !notMade && m_inFenceFd != -1;' in commit,
     'DrmAtomicCommit(): no fence of the frame in a request before that is not committed')
need('  if (notMade)\n  {\n    ret = -1;\n    errno = ENOSPC;\n  }\n  else\n    ret = drmModeAtomicCommit(m_fd, m_req->Get(), flags, nullptr);' in commit,
     'DrmAtomicCommit(): such a request is not committed; what follows is as for the answer of the kernel')
need('      if (!notMade)\n        CLog::LogF(LOGERROR, "atomic commit failed: {}", strerror(error));' in commit,
     'DrmAtomicCommit(): a commit not made is not said as "atomic commit failed"')
need(before(commit, 'const bool framePacking = FALLBACK_FRAME::FramePackingRefusal(', '    if (framePacking)\n      CLog::LogF(LOGWARNING,') and
     'CDRMAtomicRequest::LogAtomicDiff(m_req, oldRequest, framePacking ? LOGWARNING : LOGERROR);' in commit,
     'DrmAtomicCommit(): a refused change to a frame-packed mode is a warning, with its request')

check_fn = body(EGL, 'std::string CWinSystemGbmEGLContext::FramePackingCheck(')
need(check_fn != '', 'FramePackingCheck() is there')
need(before(check_fn, 'player->IsPlaying()', 'TestFramePackedMode('),
     'FramePackingCheck(): never asks while a video plays')
need('drmModeAtomicCommit' not in check_fn and check_fn.count('m_DRM->TestFramePackedMode(info, ') == 2,
     'FramePackingCheck(): only test commits, of the mode chosen, for each kind of buffer')
need(before(check_fn, 'COwnScanout::Memory::GBM_EYES, -1, &mode);', 'COwnScanout::Memory::DUMB') and
     'answer["draw"] = "gpu";' in check_fn and 'answer["copyms"] = std::round(ms * 100.0) / 100.0;' in check_fn and
     'buffers->MeasureCopy(10, ms)' in check_fn,
     'FramePackingCheck(): the block-linear buffers asked first; where the dumb ones are copied into, the cost said')
need("if (nvidia && m_renderableType != EGL_OPENGL_BIT)" in check_fn and
     'COwnScanout::Memory::DUMB, m_DRM->GetFileDescriptor());' in check_fn and
     'format, {DRM_FORMAT_MOD_LINEAR}, true, true, failed,' in check_fn,
     'FramePackingCheck(): the dumb buffers on nvidia-drm, made as FramePackedOwnStart() makes them')
need('answer["result"] = Overall(window, own);' in check_fn and 'ChooseMode(modes)' in check_fn,
     'FramePackingCheck(): the mode of FP_CHECK::ChooseMode(), and the answer of FP_CHECK::Overall()')
BUILTINS = read('xbmc/interfaces/builtins/GUIBuiltins.cpp')
bfn = body(BUILTINS, 'static int HadesFramePackingCheck(')
need('answer = gbm->FramePackingCheck();' in bfn and 'home->SetProperty(property, answer);' in bfn and
     '{"hadesframepackingcheck",' in BUILTINS and 'HadesFramePackingCheck}}' in BUILTINS,
     'the builtin HadesFramePackingCheck: the answer into the Home window')
for cm, name in (('xbmc/windowing/gbm/CMakeLists.txt', 'FramePackingCheck.h'),
                 ('xbmc/windowing/gbm/drm/CMakeLists.txt', 'DRMFramebuffer.h'),
                 ('xbmc/windowing/gbm/drm/CMakeLists.txt', 'FallbackFrame.h')):
    need(('            ' + name + '\n') in read(cm) or ('            ' + name + ')') in read(cm), name + ' in ' + cm)

print('FPOWN-SRC PASSED' if not fails else 'FPOWN-SRC FAILED (%d)' % len(fails))
sys.exit(1 if fails else 0)
