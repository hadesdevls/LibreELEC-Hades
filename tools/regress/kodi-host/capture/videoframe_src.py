#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# videoframe_src.py <Kodi tree>: in the source, what no host test runs of the frame of the video at its own size (issue
# #30): which picture Kodi's renderer gives (only one it drew whole with its own shaders in this frame, drawn again with
# its top at the first row, without the display's 3D LUT), where the capture is served (before anything is drawn over
# the video, the renderer's way never with the picture of native Dolby Vision, the tags of the output), how it is asked
# for (Shift+Print Screen next to Print Screen, TakeScreenshot(video), JSON-RPC, the button of the video settings,
# submitted before the folder is looked for) and its strings in English and in the three Spanish. Prints a line per
# failed check and PASSED or FAILED (n) last.
import os
import re
import sys
import xml.etree.ElementTree as ET

K = sys.argv[1]
failed = 0


def read(path):
    with open(os.path.join(K, path), encoding='utf-8') as f:
        return f.read()


def expect(ok, what):
    global failed
    if not ok:
        print('FAIL: ' + what)
        failed += 1


def body(text, head):
    """the body of the function whose definition starts with head, up to its closing brace at column 0"""
    i = text.find(head)
    if i < 0:
        return ''
    j = text.find('\n}\n', i)
    return text[i:j + 2] if j > 0 else ''


R = 'xbmc/cores/VideoPlayer/VideoRenderers/'
lr = read(R + 'LinuxRendererGLES.cpp')
render = body(lr, 'bool CLinuxRendererGLES::Render(unsigned int flags, int index)')
expect(bool(render), 'CLinuxRendererGLES::Render() found')
# the slot of the picture drawn is forgotten first thing in every frame, and set only after Kodi's own shaders drew it
# whole: a picture of the bridge, or one not drawn, is never taken for it
first = render.split('{', 1)[1].lstrip() if render else ''
expect(first.startswith('// Hades: drawn by Kodi\'s own shaders, whole, or not (CaptureVideoOwnSize()): set where it is\n'
                        '  m_ownDrawn = -1;'),
       'Render(): m_ownDrawn forgotten before anything else')
expect(render.count('m_ownDrawn = index;') == 1, 'Render(): m_ownDrawn set in one place')
setp = render.find('m_ownDrawn = index;')
glsl = render.find('else if (m_renderMethod & RENDER_GLSL)')
dvreq = render.find('if (m_dvRequested)')
expect(glsl > 0 and setp > glsl and setp > dvreq, 'Render(): m_ownDrawn set in the branch of Kodi\'s own shaders')
guard = render[max(0, setp - 260):setp]
expect('if (m_currentField == FIELD_FULL &&' in guard, 'Render(): m_ownDrawn set only for a whole picture (not a field)')

can = body(lr, 'bool CLinuxRendererGLES::CanCaptureVideoOwnSize() const')
expect('m_ownDrawn != m_iYUVRenderBuffer' in can and 'm_renderOrientation != 0' in can,
       'CanCaptureVideoOwnSize(): only the picture presented now, not rotated')
cap = body(lr, 'bool CLinuxRendererGLES::CaptureVideoOwnSize(')
expect('ReadTextureCorner(m_fbo.fbo.Texture()' in cap, 'CaptureVideoOwnSize(): two passes: the picture before the scaler')
# drawn again: the top-left corner of the picture at (0, 0) with Ortho2D(0, w, 0, h) (its top row the first one read)
corners = ['m_rotatedDestCoords[0] = CPoint(0.0f, 0.0f);',
           'm_rotatedDestCoords[1] = CPoint(static_cast<float>(width), 0.0f);',
           'm_rotatedDestCoords[2] = CPoint(static_cast<float>(width), static_cast<float>(height));',
           'm_rotatedDestCoords[3] = CPoint(0.0f, static_cast<float>(height));']
expect(all(c in cap for c in corners), 'CaptureVideoOwnSize(): the corners of the picture drawn again')
expect('glMatrixProject->Ortho2D(0, static_cast<float>(width), 0, static_cast<float>(height));' in cap,
       'CaptureVideoOwnSize(): its top at y = 0 (Ortho2D(0, w, 0, h))')
off = cap.find('m_lut3d.SetSignal(!m_fullRange, false);')
draw = cap.find('RenderSinglePass(index, FIELD_FULL);')
back = cap.find('m_lut3d.SetSignal(!m_fullRange, m_lut3dShaders && m_lut3d.Valid());')
expect(0 < off < draw < back, 'CaptureVideoOwnSize(): drawn again without the display\'s 3D LUT, which is put back after')
expect('saveRotatedCoords();' in cap and cap.find('restoreRotatedCoords();') > draw,
       'CaptureVideoOwnSize(): the corners of the window put back')

rm = read(R + 'RenderManager.cpp')
svc = body(rm, 'void CRenderManager::ServiceVideoCaptures()')
own = svc.find('if (requests.front()->spec.videoOwnSize)')
byp = svc.find('if (m_pRenderer->VideoBypassesFramebuffer())')
expect(0 < own < byp, 'ServiceVideoCaptures(): the frame of the video served first, by ServiceVideoFrame()')
expect('ServiceVideoFrame(result);' in svc[own:byp] and 'captureService->Complete(request, std::move(result));' in svc[own:byp],
       'ServiceVideoCaptures(): the frame of the video completed, also with no pixels (why)')
call = rm.find('ServiceVideoCaptures();', rm.find('void CRenderManager::Render(bool clear'))
over = rm.find('m_overlays.Render', call)
expect(0 < call < over, 'Render(): the video tap before the subtitles and overlays are drawn')
vf = body(rm, 'void CRenderManager::ServiceVideoFrame(')
expect('!separate && m_pRenderer->CanCaptureVideoOwnSize()' in vf,
       'ServiceVideoFrame(): the renderer\'s way never where the output has a picture of its own (native Dolby Vision)')
expect('m_captureBlit->Blit(visible, d.width, d.height, CaptureFormat::NATIVE)' in vf,
       'ServiceVideoFrame(): read where it is drawn at the size of the region read, at the depth of the output')
tags = vf.find('result.color = GetOutputColorMetadata(winSystem);')
expect(tags > 0 and vf.find('FillOutputHdrMetadata(winSystem, result);') > tags,
       'ServiceVideoFrame(): tagged as the output, with the HDR metadata of its picture')
expect('result.pixels.reset();' in vf, 'ServiceVideoFrame(): nothing that is not the picture is handed on')

ss = read('xbmc/utils/Screenshot.cpp')
take = body(ss, 'void CScreenShot::TakeVideoFrame(const std::string& target)')
expect('spec.videoOwnSize = true;' in take and 'spec.content = CaptureContent::VIDEO;' in take,
       'TakeVideoFrame(): the video alone, at its own size')
expect('ResolveScreenshotDir' not in take and 'IsRenderingVideo()' in take,
       'TakeVideoFrame(): submitted before the folder is looked for; nothing asked without a video')
wvf = body(ss, 'void WriteVideoFrame(')
expect('VIDEOFRAME::FILE_PATTERN' in wvf and 'WriteCapture(result, file)' in wvf,
       'WriteVideoFrame(): videoNNNNN.png, written as a screenshot is (with its SDR copy)')
bi = read('xbmc/interfaces/builtins/GUIBuiltins.cpp')
expect(re.search(r'  if \(video\)\n  \{\n    CScreenShot::TakeVideoFrame\(strSaveToPath\);\n    return 0;\n  \}', bi),
       'TakeScreenshot(video): the frame of the video')
expect('CScreenShot::TakeVideoFrame();' in read('xbmc/interfaces/json-rpc/GUIOperations.cpp'),
       'GUI.TakeScreenshot "video": the frame of the video')
vs = read('xbmc/video/dialogs/GUIDialogVideoSettings.cpp')
expect('AddButton(groupSaveAsDefault, SETTING_VIDEO_HADES_SAVE_FRAME, 62866, SettingLevel::Basic);' in vs and
       re.search(r'if \(settingId == SETTING_VIDEO_HADES_SAVE_FRAME\)\n\s+CScreenShot::TakeVideoFrame\(\);', vs),
       'the video settings: the button that saves the frame of the video')

# the keys: Print Screen as before, Shift+Print Screen the frame of the video, in the global keyboard map
km = ET.parse(os.path.join(K, 'system/keymaps/keyboard.xml')).getroot()
glob = km.find('global/keyboard')
keys = [(e.get('mod') or '', (e.text or '').strip()) for e in glob.findall('printscreen')] if glob is not None else []
expect(('', 'Screenshot') in keys, 'keyboard.xml: Print Screen takes the screenshot, as before')
expect(('shift', 'TakeScreenshot(video)') in keys, 'keyboard.xml: Shift+Print Screen saves the frame of the video')
ctrl = [(e.get('mod') or '', (e.text or '').strip()) for e in glob.findall('s')] if glob is not None else []
expect(('ctrl', 'Screenshot') in ctrl, 'keyboard.xml: Ctrl+S takes the screenshot, as before')

# the strings: English in the core and in Estuary, the same text; Spanish in the three
settings = read('system/settings/settings.xml')
expect('<setting id="debug.screenshotpath" type="path" label="20004" help="62867">' in settings,
       'settings.xml: the help of the screenshot folder says both')
base = read('xbmc/settings/dialogs/GUIDialogSettingsBase.cpp')
expect('if (labelId == 62866 || labelId == 62867)\n    return CGUIUtils::GetHadesString(labelId, 54866 + (labelId - 62866));'
       in base, 'the settings: 62866 and 62867 translated in Estuary')


def po(path):
    out = {}
    text = read(path)
    for m in re.finditer(r'msgctxt "#(\d+)"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"', text):
        out[int(m.group(1))] = (m.group(2), m.group(3))
    return out


core = po('addons/resource.language.en_gb/resources/strings.po')
est = {l: po('addons/skin.estuary/language/resource.language.%s/strings.po' % l)
       for l in ('en_gb', 'es_es', 'es_ar', 'es_mx')}
for i in range(8):
    c, e = 62860 + i, 54860 + i
    expect(c in core and core[c][1] == '', 'strings: core #%d in English' % c)
    expect(e in est['en_gb'] and c in core and est['en_gb'][e][0] == core[c][0],
           'strings: Estuary #%d the English of core #%d' % (e, c))
    for l in ('es_es', 'es_ar', 'es_mx'):
        ok = e in est[l] and c in core and est[l][e][0] == core[c][0] and est[l][e][1] != ''
        expect(ok, 'strings: Estuary #%d translated in %s' % (e, l))
        # (the placeholders of the English in the translation)
        if ok:
            expect(sorted(re.findall(r'\{\d\}', est[l][e][1])) == sorted(re.findall(r'\{\d\}', core[c][0])),
                   'strings: #%d in %s has the placeholders of the English' % (e, l))
for l in ('es_ar', 'es_mx'):
    expect(all('vídeo' not in est[l].get(54860 + i, ('', ''))[1] for i in range(8)),
           'strings: video without an accent in %s' % l)

print("VIDEOFRAME-SRC FAILED (%d)" % failed if failed else "VIDEOFRAME-SRC PASSED")
sys.exit(1 if failed else 0)
