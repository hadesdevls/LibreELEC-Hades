#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""projector_src_check.py: what no host test of the projector control runs, read in its source
(xbmc/network/ProjectorControl.cpp): every notice that the control gives (Notify()) is made of strings that are
translated, a string of the control (Text(), the core strings 62700-62769 with their Estuary twins) or a word of
the player information (Word(), an Estuary string, whose English is only its fallback), and of names (the brand, a
mode, an address): no English text of its own, which a Spanish interface would show as it is (1.2: "picture mode"
and "lens memory" in the notice of a command the projector refused). Hades 1.2.3: the anamorphic mode of a JVC sent
after its lens memory, the kind of signal refined by whether a video plays and by the gamut the renderer feeds, the band
of a scope picture told to the renderer and to the skin, the slides of Estuary for each step of it in the windows at
the top and the bottom of the screen, the subtitles kept in it, and the defaults of the new settings.

usage: projector_src_check.py <Kodi tree>
Prints "ok: ..." or "FAIL: ..." for each check and "PROJECTOR-SRC PASSED" or "PROJECTOR-SRC FAILED (n)".
"""
import pathlib
import re
import sys

src = (pathlib.Path(sys.argv[1]) / 'xbmc/network/ProjectorControl.cpp').read_text(encoding='utf-8')
fails = 0
checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok: ' if ok else 'FAIL: ') + what)


def argument(text, at):
    """the text between the parenthesis at `at` and the one that closes it (strings taken whole)"""
    depth, i, out = 0, at, []
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return ''.join(out)[1:]
        out.append(c)
        i += 1
    return None


calls = [m.start() for m in re.finditer(r'(?<![:\w])Notify\(', src)]
check(len(calls) >= 5, f'the notices of the control are found in its source ({len(calls)} calls of Notify())')
for at in calls:
    line = src.count('\n', 0, at) + 1
    arg = argument(src, at + len('Notify'))
    if arg is None:
        check(False, f'line {line}: the call of Notify() is read to its end')
        continue
    # (the English of a word of the player information is its fallback only)
    rest = re.sub(r'Word\(\d+,\s*"(?:[^"\\]|\\.)*"\)', 'Word()', arg)
    texts = re.findall(r'"(?:[^"\\]|\\.)*"', rest)
    check(not texts, f'line {line}: the notice has no English text of its own'
                     f'{": " + ", ".join(texts) if texts else ""}')
    ids = [int(x) for x in re.findall(r'Text\((\d+)\)', arg)]
    check(all(62700 <= i <= 62769 for i in ids), f'line {line}: its strings are strings of the control ({ids})')


# Hades: the thread of the control sleeps while it is off, and the test of the connection can be cancelled and is
# waited for a bounded time
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


process = body(src, 'void CProjectorControl::Process(')
check(re.search(r'ControlWaitMs\(!m_configRead \|\| m_config\.Active\(\)\); ms < 0\)\s*m_wake\.Wait\(\);', process),
      'while the control is off its thread waits until it is woken (ControlWaitMs(), m_wake.Wait())')
# (Hades 1.2.3: ... and it runs, with no projector, while the player controls are always kept in a band)
hdr = (pathlib.Path(sys.argv[1]) / 'xbmc/network/ProjectorControl.h').read_text(encoding='utf-8')
check(re.search(r'return brand != Brand::NONE \|\| osd == static_cast<int>\(OsdSafe::ALWAYS\);', hdr) and
      'CSettings::SETTING_VIDEOSCREEN_HADESPROJECTOROSD});' in src,
      'the thread runs with no projector where the player controls are always kept in a band, and that setting wakes it')
changed = body(src, 'void CProjectorControl::OnSettingChanged(')
check(changed.rstrip('}').rstrip().endswith('m_wake.Set();') and 'SETTING_VIDEOSCREEN_HADESPROJECTOR,' in src,
      'turning the control on wakes the thread (OnSettingChanged() of the protocol sets m_wake)')
action = body(src, 'void CProjectorControl::OnSettingAction(')
check('WaitOnEvent' not in action, 'the test is not waited for under the busy dialog, which cannot be cancelled')
check('GetWindow<CGUIDialogProgress>' in action and 'progress->SetCanCancel(true);' in action and
      'progress->IsCanceled()' in action, 'the test is waited for with a progress that can be cancelled')
check(re.search(r'const int limitMs = TestWaitMs\(Timeouts\(\)\);', action) and
      re.search(r'if \(elapsedMs >= limitMs\)\s*break;', action), 'the test is waited for TestWaitMs() at most')
check('m_testAsked.compare_exchange_strong(asked, 0)' in action and re.search(r'm_testStop = true;', action),
      'a test given up is taken back, or stopped')
check('if (m_testDoneNumber == number)' in action, 'the answer of a test given up is never shown for the next one')
check('MakeSession(config, &m_testStop)' in body(src, 'std::string CProjectorControl::RunTest('),
      'the talk of the test stops when it is given up')
check(re.search(r'm_bStop = true;\s*m_testStop = true;', body(src, 'CProjectorControl::~CProjectorControl(')),
      'a test that runs while Kodi stops is stopped')
# every string of the control, in the core and in Estuary, translated
tree = pathlib.Path(sys.argv[1])
core = (tree / 'addons/resource.language.en_gb/resources/strings.po').read_text(encoding='utf-8')
langs = {l: (tree / f'addons/skin.estuary/language/resource.language.{l}/strings.po').read_text(encoding='utf-8')
         for l in ('en_gb', 'es_es', 'es_ar', 'es_mx')}


def entry(t, i):
    m = re.search(r'msgctxt "#%d"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"' % i, t)
    return m.groups() if m else None


ids = sorted({int(x) for x in re.findall(r'\bText\((\d+)\)', src)})
bad = []
for i in ids:
    c = entry(core, i)
    for l, t in langs.items():
        e = entry(t, 54700 + i - 62700)
        if not c or not e or e[0] != c[0] or (l != 'en_gb' and not e[1]):
            bad.append(f'{i}/{l}')
check(ids and not bad, f'the {len(ids)} strings of the control are in the core and in Estuary, translated'
                       f'{": not " + ", ".join(bad) if bad else ""}')
# ... and the words of the player information that it uses (Word(): Estuary's alone), translated
words = sorted({int(x) for x in re.findall(r'\bWord\((\d+),', src)})
badw = [f'{i}/{l}' for i in words for l, t in langs.items() if not entry(t, i) or (l != 'en_gb' and not entry(t, i)[1])]
check(words and not badw, f'the {len(words)} words of the player information that it uses are in Estuary, translated'
                          f'{": not " + ", ".join(badw) if badw else ""}')

# ---- Hades 1.2.3 (requests of a JVC NZ800 owner): what no host test runs of the three changes
tick = body(src, 'void CProjectorControl::Tick(')
send = body(src, 'void CProjectorControl::Send(')
# the anamorphic mode of a JVC: after its lens memory (an installation mode recalls one of its own), never before it
memory_at = send.find('result = session->SetLensMemory(memory);')
anam_at = send.find('result = session->SetAnamorphic(anamorphic);')
check(0 <= memory_at < anam_at, 'the anamorphic mode is sent after the lens memory in a talk')
check(re.search(r'SetLensMemory\(memory\);.*?m_sentAnamorphic = ANAMORPHIC_UNKNOWN;.*?SetAnamorphic\(anamorphic\)', send,
                re.S), 'a lens memory recalled marks the anamorphic mode as unknown, so the one of its class is sent again')
check(re.search(r'const bool anamorphicDue = config\.lens && lens\.anamorphic != NO_ANAMORPHIC &&\s*'
                r'lens\.anamorphic != m_sentAnamorphic && \(!lensWaits \|\| lensDue\);', tick),
      'the anamorphic mode waits for a lens memory that waits (the quiet time of a lens that moved)')
check('AnamorphicModesOf(config.brand) ? value : NO_ANAMORPHIC' in src,
      'an anamorphic mode is read only for a protocol that has it (JVC)')
# the menus and SDR in a wide gamut: the kind of signal refined with whether a video plays and the gamut fed
check(re.search(r'KindOf\(fed\.signalAge < 2\.0 \? fed\.signal : Signal::NONE, playing,\s*'
                r'fed\.gamutAge < 2\.0 \? fed\.gamut : 0,', tick) and 'm_signal.Update(now, kind)' in tick,
      'the kind followed is the signal refined by whether a video plays (IsPlayingVideo: paused is playing) and its gamut')
check(re.search(r'const bool playing = player && player->IsPlayingVideo\(\);', tick),
      'whether a video plays is IsPlayingVideo() (a video paused with its controls shown plays)')
rm = (tree / 'xbmc/cores/VideoPlayer/VideoRenderers/RenderManager.cpp').read_text(encoding='utf-8')
check(re.search(r'if \(PROJECTOR::GamutWanted\(\)\)\s*PROJECTOR::FeedGamut\(m_pRenderer->SdrGamutOut\(\)\);', rm) and
      'SetGamutWanted(config.brand != Brand::NONE);' in tick,
      'the renderer feeds the gamut of its SDR picture while the control is on')
gles = (tree / 'xbmc/cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.h').read_text(encoding='utf-8')
check('return m_passthroughHDR || !m_sdrWide ? 0 : m_sdrWideMapped ? m_sdrContainer : 1;' in gles,
      "Kodi's GLES renderer says the gamut it drew the SDR picture into (BT.2020 or the container of a mapping)")
# the player controls and the subtitles inside a scope picture: told to the skin and the renderer at both ends of Tick()
check(tick.count('FollowOsd(config, playing,') == 2 and
      'FollowOsd(config, playing, config.lens && ShowsScope(m_lens.Now()));' in tick,
      'the band is followed both where no projector is (always) and where one is (its scope picture)')
follow = body(src, 'void CProjectorControl::FollowOsd(')
check('SetSafeBand(band);' in follow and '"SetProperty(hades.osd.insetsteps,{},home)"' in follow and
      '"ClearProperty(hades.osd.insetsteps,home)"' in follow and 'TMSG_EXECUTE_BUILT_IN' in follow,
      'the band goes to the renderer and, on the thread of the interface, to the skin (hades.osd.insetsteps)')
check(re.search(r'now - m_osdToldAt < OSD_RETELL_SECONDS', follow),
      'the skin is told again while there is a band (a skin loaded again has lost it)')
check(len(re.findall(r'const bool band = SubtitleBand\(view\);\s*m_overlays\.SetVideoRect\(src, dst, view\);\s*'
                     r'(?://[^\n]*\n\s*)?m_overlays\.SetBitmapsKeptInView\(m_pRenderer->ActiveAreaZoomed\(\) \|\| band\);',
                     rm)) == 2,
      'both places that place the subtitles narrow their view to the band, and keep bitmap subtitles inside it')
sb = body(rm, 'bool SubtitleBand(')
check('gfx.IsFullScreenVideo()' in sb and 'RenderStereoMode::MONO' in sb and 'PROJECTOR::InsetOf(band, frame)' in sb,
      'the subtitles are narrowed only in a fullscreen video in 2D, by the inset of the band')
# the skin: each step of InsetSteps() has its slide, both ways, and the windows at the top and the bottom take them
skin = tree / 'addons/skin.estuary/xml'
anim = (skin / 'Includes_Animations.xml').read_text(encoding='utf-8')


def include(name):
    m = re.search(r'<include name="%s">(.*?)</include>' % name, anim, re.S)
    return m.group(1) if m else ''


steps = [4, 8, 16, 32, 64, 128]
for name, sign in (('HadesOsdInsetDown', ''), ('HadesOsdInsetUp', '-')):
    inc = include(name)
    got = [(int(a), int(b)) for a, b in re.findall(
        r'<animation effect="slide" end="0,(-?\d+)" time="0" condition="String\.Contains\(Window\(Home\)\.Property\('
        r'hades\.osd\.insetsteps\),_(\d+)_\)">Conditional</animation>', inc)]
    check(got == [(int(sign + str(s)), s) for s in steps], f'{name}: a slide of each step 4 to 128, {"up" if sign else "down"} ({got})')
where = {'VideoOSD.xml': ('HadesOsdInsetUp', 1), 'DialogSeekBar.xml': ('HadesOsdInsetUp', 3),
         'Custom_1109_TopBarOverlay.xml': ('HadesOsdInsetDown', 2), 'DialogNotification.xml': ('HadesOsdInsetDown', 1),
         'DialogVolumeBar.xml': ('HadesOsdInsetDown', 1)}
for f, (name, n) in where.items():
    t = (skin / f).read_text(encoding='utf-8')
    other = 'HadesOsdInsetDown' if name == 'HadesOsdInsetUp' else 'HadesOsdInsetUp'
    check(t.count('<include>%s</include>' % name) == n and other not in t, f'{f}: {n} x {name}, none the other way')
inc = (skin / 'Includes.xml').read_text(encoding='utf-8')
touch = re.search(r'<include name="TouchBackOSDButton">(.*?)</control>\s*</include>', inc, re.S)
check(touch and '<include>HadesOsdInsetDown</include>' in touch.group(1), 'the back button of the touch mode slides down')
# the settings: each new one at the default that changes nothing, and the anamorphic modes only for JVC
xml = (tree / 'system/settings/settings.xml').read_text(encoding='utf-8')


def setting(sid):
    m = re.search(r'<setting id="%s"[^>]*>(.*?)</setting>' % re.escape(sid), xml, re.S)
    return m.group(1) if m else ''


for sid, default in (('videoscreen.hadesprojectormodemenus', '-2'), ('videoscreen.hadesprojectormodesdr2020', '-2'),
                     ('videoscreen.hadesprojectormodesdrp3', '-2'), ('videoscreen.hadesprojectoranamorphicnarrow', '-1'),
                     ('videoscreen.hadesprojectoranamorphicwide', '-1'), ('videoscreen.hadesprojectoranamorphicwidest', '-1'),
                     ('videoscreen.hadesprojectorosd', '1')):
    check(f'<default>{default}</default>' in setting(sid), f'{sid}: default {default}')
check(all('<condition setting="videoscreen.hadesprojector" operator="is">1</condition>' in setting(s)
          for s in ('videoscreen.hadesprojectoranamorphicnarrow', 'videoscreen.hadesprojectoranamorphicwide',
                    'videoscreen.hadesprojectoranamorphicwidest')), 'the anamorphic modes are shown for JVC only')
check('<dependency type' not in setting('videoscreen.hadesprojectorosd'),
      'the player controls inside a scope picture is shown with no projector chosen too (always, by hand)')
print(f'{checks} checks')
print('PROJECTOR-SRC PASSED' if not fails else f'PROJECTOR-SRC FAILED ({fails})')
sys.exit(1 if fails else 0)
