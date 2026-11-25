# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: the frame packing card of "Check this setup" (LibreELEC-settings-9074): hades_check.py (whether an EDID says
# that the display takes HDMI 3D, which 3D settings of Kodi the card sets and their journal, what became of the 3D
# output, why frame packing was not used as Kodi's log and the driver's kernel lines say it, the lines and the facts)
# and the flow of hades_check_ui.py with stand-ins for Kodi (xbmc, xbmcgui, xbmcaddon, oe, log): skipped where no
# display takes 3D or the 3D output of Hades is off, the clip played with the settings it needs and every one put back,
# frame packing taken or refused with its reason, and "Everything" with the card only where a display takes 3D.
# usage: check_fp_test.py <resources/lib of the add-on>
import importlib
import json
import os
import shutil
import sys
import tempfile
import types

LIB = sys.argv[1]
fails = 0


def check(ok, text):
    global fails
    fails += 0 if ok else 1
    print(('ok:   ' if ok else 'FAIL: ') + text)


# ---- stand-ins for Kodi
SETTINGS = {}


class Monitor:
    def abortRequested(self):
        return False

    def waitForAbort(self, t=0):
        return False


class Player:
    played = []

    def __init__(self):
        self._on = False

    def play(self, path, windowed=False):
        Player.played.append((path, dict(SETTINGS)))
        self._on = True
        self._left = 3

    def isPlaying(self):
        if self._on:
            self._left -= 1
            if self._left < 0:
                self._on = False
                return False
        return self._on

    def stop(self):
        self._on = False

    def getTime(self):
        return 1.0


class Window:
    props = {}

    def __init__(self, n):
        pass

    def setProperty(self, k, v):
        Window.props[k] = v

    def clearProperty(self, k):
        Window.props.pop(k, None)

    def getProperty(self, k):
        return Window.props.get(k, '')


class Dialog:
    script = []
    shown = []

    def _next(self, kind, *a):
        Dialog.shown.append((kind,) + a)
        if not Dialog.script:
            raise AssertionError('no answer scripted for %s %r' % (kind, a[:1]))
        want, answer = Dialog.script.pop(0)
        if want != kind:
            raise AssertionError('asked %s, the script has %s' % (kind, want))
        return answer

    def textviewer(self, h, t):
        Dialog.shown.append(('textviewer', h, t))

    def ok(self, h, t):
        Dialog.shown.append(('ok', h, t))

    def select(self, h, items, **kw):
        return self._next('select', h, items)

    def yesno(self, h, t, **kw):
        return self._next('yesno', h, t)


# a setting that cannot be set while its test says so ({setting: () -> True while it cannot})
STUCK = {}


def rpc(method, **params):
    if method == 'Settings.GetSettingValue':
        k = params['setting']
        return ({'value': SETTINGS[k]}, None) if k in SETTINGS else (None, {'code': -32602})
    if params['setting'] in STUCK and STUCK[params['setting']]():
        return None, {'code': -32602}
    SETTINGS[params['setting']] = params['value']
    return True, None


xbmc = types.SimpleNamespace(Player=Player, getLocalizedString=lambda n: 'k%d' % n,
                             getInfoLabel=lambda k: '', getCondVisibility=lambda c: True,
                             executeJSONRPC=None)
xbmcgui = types.SimpleNamespace(Dialog=Dialog, Window=Window, DialogProgress=None, DLG_YESNO_YES_BTN=11,
                                DLG_YESNO_NO_BTN=10)
xbmcaddon = types.SimpleNamespace(Addon=lambda n: None)
LOG = []
log = types.SimpleNamespace(INFO=1, WARNING=2, ERROR=3, log=lambda m, l=1: LOG.append((l, m)))
oe = types.SimpleNamespace(_=lambda n: ('s%d %%s' % n) if n in (33853, 33854, 33855) else
                           ('s%d %%s %%s' % n) if n == 33866 else 's%d' % n, xbmcm=Monitor())
for name, mod in (('xbmc', xbmc), ('xbmcgui', xbmcgui), ('xbmcaddon', xbmcaddon), ('log', log), ('oe', oe)):
    sys.modules[name] = mod
sys.path.insert(0, LIB)
hc = importlib.import_module('hades_check')
ui = importlib.import_module('hades_check_ui')
ui.CheckSetup.rpc = staticmethod(rpc)


def edid(vsdb=None, ext=True):
    '''an EDID of a base block and, where ext, a CTA-861 extension with the HDMI vendor block vsdb (its payload after
    the OUI: the address, the flags, the TMDS clock, the latency and video flags and what follows them)'''
    base = bytearray(128)
    base[0:8] = b'\x00\xff\xff\xff\xff\xff\xff\x00'
    m = ((ord('G') - 64) << 10) | ((ord('S') - 64) << 5) | (ord('M') - 64)
    base[8], base[9] = m >> 8, m & 0xFF
    base[10], base[11] = 0x9E, 0x5B
    base[54:72] = b'\x00\x00\x00\xfc\x00' + b'LG TV SSCR2\n '  # (13 bytes: a descriptor is 18)
    assert len(base) == 128
    if not ext:
        return bytes(base)
    base[126] = 1
    cta = bytearray(128)
    cta[0], cta[1] = 0x02, 0x03
    blocks = bytes([0x40 | 2, 0x10, 0x04])  # (a video block first: the vendor block is found after it)
    if vsdb is not None:
        payload = b'\x03\x0c\x00' + bytes(vsdb)
        blocks += bytes([0x60 | len(payload)]) + payload
    cta[4:4 + len(blocks)] = blocks
    cta[2] = 4 + len(blocks)
    return bytes(base) + bytes(cta)


work = tempfile.mkdtemp(prefix='check-fp-')
try:
    # ---- hades_check.py: the EDID
    addr = [0x10, 0x00]
    check(hc.edid_3d(edid(addr + [0x00, 0x78, 0x20, 0x80])), '3D_present right after the flags (no latency)')
    check(not hc.edid_3d(edid(addr + [0x00, 0x78, 0x20, 0x00])), 'HDMI video present, no 3D')
    check(not hc.edid_3d(edid(addr + [0x00, 0x78, 0x00, 0x80])), 'no HDMI video present: the byte after is no 3D byte')
    check(hc.edid_3d(edid(addr + [0x00, 0x78, 0xA0, 0x10, 0x10, 0x80])), 'after the two bytes of the latency')
    check(not hc.edid_3d(edid(addr + [0x00, 0x78, 0xA0, 0x80, 0x10, 0x00])), 'a latency byte is no 3D byte')
    check(hc.edid_3d(edid(addr + [0x00, 0x78, 0xE0, 0x10, 0x10, 0x20, 0x20, 0x80])),
          'after the latency and the interlaced latency')
    check(not hc.edid_3d(edid(addr + [0x00, 0x78, 0xE0, 0x10, 0x10, 0x80, 0x20, 0x00])),
          'the interlaced latency is skipped too')
    check(not hc.edid_3d(edid(addr + [0x00, 0x78, 0x20])), 'a block that ends before its 3D byte')
    check(not hc.edid_3d(edid(None)), 'no HDMI vendor block')
    check(not hc.edid_3d(edid(ext=False)) and not hc.edid_3d(b'\x00' * 256), 'no extension, no EDID')
    with3d = edid(addr + [0x00, 0x78, 0x20, 0x80])
    without = edid(addr + [0x00, 0x78, 0x20, 0x00])
    drm = os.path.join(work, 'drm')
    for c, s, data in (('card0-HDMI-A-1', 'connected', with3d), ('card0-HDMI-A-2', 'disconnected', with3d),
                       ('card1-HDMI-A-3', 'connected', without)):
        os.makedirs(os.path.join(drm, c))
        open(os.path.join(drm, c, 'status'), 'w').write(s + '\n')
        open(os.path.join(drm, c, 'edid'), 'wb').write(data)
    check(hc.read_3d_displays(drm) == [('HDMI-A-1', 'GSM 0x5B9E LG TV SSCR2')], 'the displays connected that take 3D')

    # ---- the settings and the journal
    P, F, R = hc.FP_PLAYBACK, hc.FP_PREFERRED, hc.FP_REFRESH
    check(hc.fp_changes({P: 1, F: 100, R: 2}) == {}, 'nothing to set where they are so')
    check(hc.fp_changes({P: 0, F: 5, R: 0}) == {P: 1, F: 100, R: 2}, 'Ask, a preferred mode of its own, no rate: set')
    check(hc.fp_changes({P: 1, F: 100, R: 1}) == {} and hc.fp_changes({P: 1, F: 100, R: 3}) == {},
          'a rate that follows the video in another way is kept')
    check(hc.fp_changes({P: None, F: None, R: None}) == {}, 'a setting that cannot be read is left alone')
    j = os.path.join(work, 'restore.json')
    hc.write_journal(j, {P: 0, F: 5, R: 0})
    check(hc.read_journal(j) == {P: 0, F: 5, R: 0}, 'the journal takes the 3D settings as integers')
    try:
        hc.write_journal(j, {P: True})
        check(False, 'a boolean for a 3D setting is refused')
    except ValueError:
        check(True, 'a boolean for a 3D setting is refused')
    g = os.path.join(work, 'guisettings.xml')
    open(g, 'w').write('<?xml version="1.0"?>\n<settings version="2"><setting id="%s">1</setting></settings>\n' % P)
    hc.write_journal(j, {P: 0, R: 0})
    hc.restore_guisettings(j, g)
    text = open(g).read()
    check('id="%s">0<' % P in text and 'id="%s">0<' % R in text and not os.path.exists(j),
          'put back into guisettings.xml')

    # ---- what became of it, and why
    check(hc.fp_outcome([]) == 'unknown' and hc.fp_outcome(['', '']) == 'none' and
          hc.fp_outcome(['', 'fp', 'fp']) == 'fp' and hc.fp_outcome(['fp', 'tab', 'sbs']) == 'fallback' and
          hc.fp_outcome(['tab', '']) == 'fallback', 'the outcome from what the player information said in turn')
    kodi = ['2026-11-24 10:00:00.000 T:1 info <general>: 3D output: frame packing is not used: the driver did not take '
            'its mode (the kernel log may say why). What comes next in the choice is taken; it is tried again when the '
            'eyes are next drawn split',
            '2026-11-24 10:00:01.000 T:1 info <general>: 3D output: frame packing is not used: the driver did not take '
            'its mode (the kernel log may say why). What comes next in the choice is taken; it is tried again when the '
            'eyes are next drawn split',
            '2026-11-24 10:00:02.000 T:1 info <general>: 3D output: none for 1920x1080 @ 23.98: the display has no 3D '
            'mode for it at this refresh rate. The two halves go out as they are, and the display is switched to 3D '
            'by hand']
    kernel = ['[nvidia-drm] [GPU ID 0x00000100] Hades: HDMI 3D frame packing refused: the framebuffer of plane 0 has '
              'no NVKMS surface: its memory is not video memory', 'something else']
    why = hc.fp_reasons(kodi, kernel)
    check(why == [('kodi', 'the driver did not take its mode (the kernel log may say why)'),
                  ('kodi', 'the display has no 3D mode for it at this refresh rate'),
                  ('driver', 'the framebuffer of plane 0 has no NVKMS surface: its memory is not video memory')],
          'the reasons of Kodi and of the driver, each once: %r' % why)
    lg = os.path.join(work, 'kodi.log')
    open(lg, 'w').write('old line\n')
    at = hc.log_size(lg)
    open(lg, 'a').write('new one\nand another\n')
    check(hc.read_log_from(at, lg) == ['new one', 'and another'], 'the lines of kodi.log since then')
    open(lg, 'w').write('a new log\n')
    check(hc.read_log_from(at + 100, lg) == ['a new log'] and hc.log_size(os.path.join(work, 'none')) == 0,
          'a log that started again, and one that is not there')
    asked = []

    def fake_run(args, **kw):
        asked.append(args)
        return types.SimpleNamespace(stdout=b'one\ntwo\n')
    check(hc.kernel_lines_since(1000.7, fake_run) == ['one', 'two'] and asked[0][-2:] == ['--since', '@1000'] and
          '-k' in asked[0], 'the kernel lines since a moment, from the journal')

    def broken(args, **kw):
        raise OSError('no journalctl')
    check(hc.kernel_lines_since(1, broken) == [], 'no journal: no lines')
    check(hc.fp_lines('fp', '1920x2205 23.976 Hz', 'fp', [], '3d') ==
          [[('f', 33853, [('t', '1920x2205 23.976 Hz')])], [('s', 33867)]], 'frame packing taken: said with its mode')
    check(hc.fp_lines('fallback', '', 'tab', why[:1], None) ==
          [[('f', 33854, [('s', 33861)])], [('f', 33855, [('t', why[0][1])])], [('s', hc.S['no_answer'])]],
          'refused: what it went out as, and why')
    check(hc.fp_lines('none', '', '', [], 'halves')[1] == [('s', 33865)], 'refused without a reason: said so')
    f = hc.fp_facts([('HDMI-A-1', 'GSM 0x5B9E LG TV SSCR2')], 'fallback', ['', 'fp', 'fp', 'tab'], '', why[2:], {P: 1},
                    'halves')
    check(f.startswith('frame packing: displays with 3D HDMI-A-1: GSM 0x5B9E LG TV SSCR2; outcome=fallback; '
                       'stereo=2d,fp,tab;') and 'why=driver: the framebuffer' in f and
          'settings_changed=videoplayer.stereoscopicplaybackmode=1' in f and f.endswith('answer=halves'),
          'the line of the summary: %s' % f)

    # ---- the flow, with stand-ins for Kodi
    clips = os.path.join(work, 'clips')
    os.makedirs(clips)
    open(os.path.join(clips, 'hades-check-3d-sbs.mkv'), 'wb').write(b'x' * 16)
    json.dump({'version': 1, 'clips': [], 'fp': {'file': 'hades-check-3d-sbs.mkv', 'duration': 15.015}},
              open(os.path.join(clips, 'clips.json'), 'w'))
    DISPLAYS = []
    hc.read_3d_displays = lambda root='/sys/class/drm': list(DISPLAYS)
    hc.read_display_names = lambda root='/sys/class/drm': [('HDMI-A-1', 'GSM 0x5B9E LG TV SSCR2')]
    KODI_NEW, KERNEL_NEW = [], []
    hc.log_size = lambda path=None: 0
    hc.read_log_from = lambda offset, path=None: list(KODI_NEW)
    hc.kernel_lines_since = lambda epoch, run=None: list(KERNEL_NEW)
    USER = {P: 0, F: 100, R: 0, hc.FP_OUTPUT: 1, 'other.setting': 7}

    def run(script, stereo='fp', user=None, displays=True, kodi=(), kernel=()):
        SETTINGS.clear()
        SETTINGS.update(user or USER)
        Player.played.clear()
        Dialog.script = list(script)
        Dialog.shown.clear()
        Window.props.clear()
        Window.props['hades.out.stereo'] = stereo
        Window.props['hades.out.mode'] = '1920x2205 23.976 Hz' if stereo == 'fp' else '1920x1080 23.976 Hz'
        Window.props['hades.sum.head'] = 'sdr'
        DISPLAYS[:] = [('HDMI-A-1', 'GSM 0x5B9E LG TV SSCR2')] if displays else []
        KODI_NEW[:], KERNEL_NEW[:] = list(kodi), list(kernel)
        c = ui.CheckSetup(clips, os.path.join(work, 'st%d' % len(LOG)))
        c.frame_packing('title')
        return c

    c = run([('select', 0)])
    check(len(Player.played) == 1 and {k: Player.played[0][1][k] for k in (P, F, R)} == {P: 1, F: 100, R: 2},
          'played in the preferred mode, the same as the video, the rate following it')
    check(SETTINGS == USER, 'every setting as it was after it, nothing else changed')
    check(not os.path.exists(c.journal), 'the journal gone')
    check(c.results[-1]['lines'][0] == [('f', 33853, [('t', '1920x2205 23.976 Hz')])] and
          c.results[-1]['lines'][-1] == [('s', 33867)] and 'outcome=fp' in c.results[-1]['facts'] and
          'answer=3d' in c.results[-1]['facts'], 'frame packing taken, seen in 3D: %s' % c.results[-1]['facts'])
    c = run([('select', 1)], stereo='tab', kodi=kodi[:1], kernel=kernel)
    lines = c.results[-1]['lines']
    check(lines[0] == [('f', 33854, [('s', 33861)])] and [('f', 33855, [('t', why[0][1])])] in lines and
          [('f', 33855, [('t', why[2][1])])] in lines and lines[-1] == [('s', 33868)] and SETTINGS == USER,
          'refused: top and bottom, why from Kodi and the driver, the two halves seen, the settings back')
    check('s33855 the framebuffer of plane 0' in Dialog.shown[-1][2], 'the result shown, with the reason')
    c = run([('select', -1)], stereo='')
    check(c.results[-1]['lines'][0] == [('s', 33856)] and c.results[-1]['lines'][-1] == [('s', hc.S['no_answer'])],
          'no 3D output, the question closed')
    # a 3D setting that cannot be put back after the card (LibreELEC-settings-9086): said as a setting that this check
    # changed for a while and that the next start puts back as it was (33840), not as a sound setting (33741); its
    # journal stays for that start
    STUCK[P] = lambda: len(Player.played) >= 1
    c = run([('select', 0)])
    STUCK.clear()
    lines = c.results[-1]['lines']
    check([('s', 33840)] in lines and [('s', 33741)] not in lines and hc.read_journal(c.journal).get(P) == 0,
          'a 3D setting not put back: said as one of this check, kept in the journal for the next start: %r' % lines[-1:])
    hc.remove_journal(c.journal)
    same = {P: 1, F: 100, R: 2, hc.FP_OUTPUT: 1}
    c = run([('select', 0)], user=same)
    check(SETTINGS == same and not any(m.startswith('check: the 3D settings before') for _, m in LOG[-5:]),
          'nothing set where the settings are so already')
    c = run([], displays=False)
    check(not Player.played and c.results[-1]['lines'] == [[('s', 33852)]] and SETTINGS == USER and
          'not checked: no display connected says that it takes 3D' in c.results[-1]['facts'],
          'no display that takes 3D: skipped, said so, nothing played')
    c = run([], user=dict(USER, **{hc.FP_OUTPUT: 0}))
    check(not Player.played and c.results[-1]['lines'] == [[('f', 33866, [('k', 59979), hc.DISPLAY_PATH])]],
          'the 3D output of Hades off: skipped, with the setting named')
    # "Everything": the card only where a display takes 3D
    c = ui.CheckSetup(clips, os.path.join(work, 'all'))
    check(33850 in [t for t, _ in c.entries()], 'the card is offered where the clip is there')
    DISPLAYS[:] = []
    Dialog.script = [('select', 0)]
    ran = []
    c.entries = lambda: [(33850, lambda: ran.append('fp')), (33652, lambda: ran.append('speakers'))]
    c.run()
    check(ran == ['speakers'], '"Everything" without a display that takes 3D leaves the card out')
    DISPLAYS[:] = [('HDMI-A-1', 'x')]
    Dialog.script = [('select', 0)]
    ran.clear()
    c.run()
    check(ran == ['fp', 'speakers'], '"Everything" with one runs it')
finally:
    shutil.rmtree(work, ignore_errors=True)
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
