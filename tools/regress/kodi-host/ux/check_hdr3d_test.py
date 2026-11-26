# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: the card "3D: HDR" of "Check this setup" (LibreELEC-settings-9097): hades_check.py (the settings it sets and
# their journal, what went out, what is kept and for which display, the file of each display's answer as Kodi reads
# it, the lines and the facts) and the flow of hades_check_ui.py with stand-ins for Kodi (xbmc, xbmcgui, xbmcaddon, oe,
# log): skipped where no display takes 3D or the 3D output of Hades is off, the clip played in HDR with the settings it
# needs and every one put back, the answer kept for the display that showed it and never for another, nothing kept
# where HDR did not go out in 3D or no answer was given, and "Everything" without the card. Against the Kodi tree: the
# file and its limit are the ones Kodi reads (Stereo3DVideo.h, DisplayModeMemory.h).
# usage: check_hdr3d_test.py <resources/lib of the add-on> <Kodi tree>
import importlib
import json
import os
import re
import shutil
import sys
import tempfile
import types

LIB, KODI = sys.argv[1], sys.argv[2]
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


def rpc(method, **params):
    if method == 'Settings.GetSettingValue':
        k = params['setting']
        return ({'value': SETTINGS[k]}, None) if k in SETTINGS else (None, {'code': -32602})
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
oe = types.SimpleNamespace(_=lambda n: ('s%d %%s' % n) if n in (33876, 33877, 33888) else
                           ('s%d %%s %%s' % n) if n == 33866 else 's%d' % n, xbmcm=Monitor())
for name, mod in (('xbmc', xbmc), ('xbmcgui', xbmcgui), ('xbmcaddon', xbmcaddon), ('log', log), ('oe', oe)):
    sys.modules[name] = mod
sys.path.insert(0, LIB)
hc = importlib.import_module('hades_check')
ui = importlib.import_module('hades_check_ui')
ui.CheckSetup.rpc = staticmethod(rpc)

work = tempfile.mkdtemp(prefix='check-hdr3d-')
try:
    # ---- against Kodi: the file and its limit are the ones Kodi reads
    stereo = open(os.path.join(KODI, 'xbmc/utils/Stereo3DVideo.h'), encoding='utf-8').read()
    modes = open(os.path.join(KODI, 'xbmc/windowing/gbm/DisplayModeMemory.h'), encoding='utf-8').read()
    m = re.search(r'HDR_IN_3D_FILE = "special://profile/([^"]+)"', stereo)
    check(m is not None and hc.HDR3D_MEMORY == '/storage/.kodi/userdata/' + m.group(1),
          'the file of each display\'s answer is the one Kodi reads (special://profile is /storage/.kodi/userdata)')
    check('kept == "hdr" ? HdrIn3D::WORKS : kept == "sdr" ? HdrIn3D::REFUSED' in stereo,
          'the values hdr and sdr are the ones Kodi reads')
    m = re.search(r'constexpr size_t LIMIT = (\d+);', modes)
    check(m is not None and int(m.group(1)) == hc.HDR3D_LIMIT, 'the displays kept, as many as Kodi keeps')
    m = re.search(r'std::string key = make \+ " / " \+ model;', modes)
    check(m is not None, 'Kodi\'s key of a display: make / model (what hades.out.displaykey says)')

    # ---- the settings and the journal
    P, F, R, C = hc.FP_PLAYBACK, hc.FP_PREFERRED, hc.FP_REFRESH, hc.HDR3D_COLOUR
    check(hc.hdr3d_changes({P: 1, F: 100, R: 2, C: 1}) == {}, 'nothing to set where they are so')
    check(hc.hdr3d_changes({P: 0, F: 5, R: 0, C: 0}) == {P: 1, F: 100, R: 2, C: 1},
          '3D in the display\'s layout, and HDR for the 3D video whatever Automatic sends')
    check(hc.hdr3d_changes({P: 1, F: 100, R: 2, C: None}) == {}, 'a setting that cannot be read is left alone')
    j = os.path.join(work, 'restore.json')
    hc.write_journal(j, {C: 0, P: 0})
    check(hc.read_journal(j) == {C: 0, P: 0}, 'the journal takes "How 3D video goes out" as an integer')
    try:
        hc.write_journal(j, {C: True})
        check(False, 'a boolean for it is refused')
    except ValueError:
        check(True, 'a boolean for it is refused')
    hc.remove_journal(j)

    # ---- what went out, and what is kept
    out = lambda s, s3d='', key='GSM / LG TV': {'hades.out.stereo': s, 'hades.out.stereo3d': s3d,
                                                 'hades.out.displaykey': key}
    check(hc.hdr3d_outcome([]) == 'unknown' and hc.hdr3d_outcome([out('')]) == 'no3d' and
          hc.hdr3d_outcome([out(''), out('sbs', 'hdr10')]) == 'hdr' and
          hc.hdr3d_outcome([out('fp', 'hlg')]) == 'hdr' and hc.hdr3d_outcome([out('tab', 'sdr709')]) == 'sdr' and
          hc.hdr3d_outcome([out('sbs', 'hdr10'), out('sbs', 'sdr2020')]) == 'sdr' and
          hc.hdr3d_outcome([out('sbs', 'hdr10'), out('')]) == 'hdr',
          'the outcome from what the player information said last of the 3D output')
    check(hc.hdr3d_verdict('hdr', 'hdr') == 'hdr' and
          all(hc.hdr3d_verdict('hdr', a) == 'sdr' for a in ('nohdr', 'halves', 'nopicture')),
          'seen in 3D and in HDR: hdr; anything else seen: sdr')
    check(hc.hdr3d_verdict('hdr', None) is None and hc.hdr3d_verdict('sdr', 'hdr') is None and
          hc.hdr3d_verdict('no3d', 'nohdr') is None and hc.hdr3d_verdict('unknown', 'hdr') is None,
          'nothing learned without an answer, or where HDR did not go out in 3D')
    mem = os.path.join(work, 'hades-3d-hdr.txt')
    check(hc.read_hdr3d(mem) == [], 'no file: nothing kept')
    open(mem, 'w').write('GSM / LG TV\ttold\nSNY / VPL-VW290\tsdr\nbad line\n\tx\ny\t\na\tb\tc\n')
    check(hc.read_hdr3d(mem) == [('GSM / LG TV', 'told'), ('SNY / VPL-VW290', 'sdr')],
          'the lines Kodi keeps, as Kodi reads them (one tab, something on each side)')
    check(hc.remember_hdr3d('GSM / LG TV', 'hdr', mem), 'kept for the LG')
    check(hc.read_hdr3d(mem) == [('SNY / VPL-VW290', 'sdr'), ('GSM / LG TV', 'hdr')],
          'the LG\'s "told" replaced by its answer, the Sony\'s kept, the last used last')
    check(hc.remember_hdr3d('EPS / EH-TW9400', 'sdr', mem) and
          dict(hc.read_hdr3d(mem)) == {'SNY / VPL-VW290': 'sdr', 'GSM / LG TV': 'hdr', 'EPS / EH-TW9400': 'sdr'},
          'another display: its own line; the others\' never change')
    check(not hc.remember_hdr3d('', 'hdr', mem) and not hc.remember_hdr3d('A\tB', 'hdr', mem) and
          not hc.remember_hdr3d('A', 'h\ndr', mem) and len(hc.read_hdr3d(mem)) == 3,
          'no key, or a key or a value with a tab or a line break: nothing kept')
    for k in range(40):
        hc.remember_hdr3d('D%02d / M' % k, 'sdr', mem)
    kept = hc.read_hdr3d(mem)
    check(len(kept) == 32 and kept[0][0] == 'D08 / M' and kept[-1][0] == 'D39 / M',
          'the last 32 displays kept, as Kodi keeps them')
    text = open(mem, encoding='utf-8').read()
    check(text.endswith('\n') and all(l.count('\t') == 1 for l in text.splitlines()) and
          not os.path.exists(mem + '.new'), 'one line of key<TAB>value a display, written whole and put in place')
    check(hc.hdr3d_lines('hdr', 'sbs', 'hdr', 'hdr', True, 'GSM / LG TV') ==
          [[('f', 33888, [('s', 33862)])], [('s', 33872)], [('f', 33876, [('t', 'GSM / LG TV')])]],
          'HDR seen in 3D: what went out, what was seen, what is kept')
    check(hc.hdr3d_lines('hdr', 'fp', 'halves', 'sdr', True, 'K')[-1] == [('f', 33877, [('t', 'K')])] and
          hc.hdr3d_lines('hdr', 'fp', 'halves', 'sdr', True, 'K')[0] == [('f', 33888, [('s', 33889)])],
          'two halves seen: SDR kept')
    check(hc.hdr3d_lines('hdr', 'sbs', None, None, False, 'K') ==
          [[('f', 33888, [('s', 33862)])], [('s', 33878)]], 'no answer: nothing kept, said so')
    check(hc.hdr3d_lines('sdr', 'sbs', None, None, False, 'K') == [[('s', 33874)]] and
          hc.hdr3d_lines('no3d', '', None, None, False, 'K') == [[('s', 33875)]],
          'HDR not out in 3D, or no 3D: said, nothing kept')
    check(hc.hdr3d_lines('hdr', 'sbs', 'hdr', 'hdr', False, '')[-1] == [('s', 33887)] and
          hc.hdr3d_lines('hdr', 'sbs', 'hdr', 'hdr', False, 'K')[-1] == [('s', 33879)],
          'a display of no key, and a file that could not be written: said')
    f = hc.hdr3d_facts([('HDMI-A-1', 'GSM 0x5B9E LG TV')], 'hdr', [out(''), out('sbs', 'hdr10')], 'hdr', 'hdr', True,
                       'GSM / LG TV', {C: 1})
    check(f.startswith('HDR in 3D: displays with 3D HDMI-A-1: GSM 0x5B9E LG TV; outcome=hdr; out=2d/-,sbs/hdr10; '
                       'display=GSM / LG TV; answer=hdr; kept=hdr;') and 'videoscreen.hades3dcolour=1' in f,
          'the line of the summary: %s' % f)

    # ---- the flow, with stand-ins for Kodi
    clips = os.path.join(work, 'clips')
    os.makedirs(clips)
    open(os.path.join(clips, 'hades-check-3d-hdr10-sbs.mkv'), 'wb').write(b'x' * 16)
    json.dump({'version': 1, 'clips': [], 'fp': {'file': 'none.mkv'},
               'hdr3d': {'file': 'hades-check-3d-hdr10-sbs.mkv', 'duration': 15.015}},
              open(os.path.join(clips, 'clips.json'), 'w'))
    DISPLAYS = []
    hc.read_3d_displays = lambda root='/sys/class/drm': list(DISPLAYS)
    hc.read_display_names = lambda root='/sys/class/drm': [('HDMI-A-1', 'GSM 0x5B9E LG TV')]
    hc.HDR3D_MEMORY = os.path.join(work, 'flow-3d-hdr.txt')
    USER = {P: 0, F: 100, R: 0, C: 0, hc.FP_OUTPUT: 1, 'other.setting': 7}

    def run(script, stereo='sbs', s3d='hdr10', key='GSM / LG TV', user=None, displays=True):
        SETTINGS.clear()
        SETTINGS.update(user or USER)
        Player.played.clear()
        Dialog.script = list(script)
        Dialog.shown.clear()
        Window.props.clear()
        Window.props.update({'hades.out.stereo': stereo, 'hades.out.stereo3d': s3d, 'hades.out.displaykey': key,
                             'hades.out.mode': '1920x1080 23.976 Hz', 'hades.sum.head': 'hdr10'})
        DISPLAYS[:] = [('HDMI-A-1', 'GSM 0x5B9E LG TV')] if displays else []
        c = ui.CheckSetup(clips, os.path.join(work, 'st%d' % len(LOG)))
        c.hdr_3d('title')
        return c

    c = run([('select', 0)])
    check(len(Player.played) == 1 and {k: Player.played[0][1][k] for k in (P, F, R, C)} == {P: 1, F: 100, R: 2, C: 1},
          'played in the display\'s 3D layout, with HDR asked for')
    check(SETTINGS == USER and not os.path.exists(c.journal), 'every setting as it was after it, the journal gone')
    check(dict(hc.read_hdr3d(hc.HDR3D_MEMORY)) == {'GSM / LG TV': 'hdr'} and
          'kept=hdr' in c.results[-1]['facts'], 'seen in 3D and in HDR: kept as hdr for that display')
    select = [s for s in Dialog.shown if s[0] == 'select']
    check(select and select[0][2] == ['s33872', 's33873', 's33860', 's33864'], 'the four answers asked: %r' % select)
    c = run([('select', 2)], key='SNY / VPL-VW290')
    check(dict(hc.read_hdr3d(hc.HDR3D_MEMORY)) == {'GSM / LG TV': 'hdr', 'SNY / VPL-VW290': 'sdr'},
          'another display that showed two halves: sdr for it, the LG\'s answer untouched')
    c = run([('select', 1)])
    check(dict(hc.read_hdr3d(hc.HDR3D_MEMORY)) == {'GSM / LG TV': 'sdr', 'SNY / VPL-VW290': 'sdr'},
          'checked again, dull colours: the LG now sdr')
    before = hc.read_hdr3d(hc.HDR3D_MEMORY)
    c = run([('select', -1)])
    check(hc.read_hdr3d(hc.HDR3D_MEMORY) == before and c.results[-1]['lines'][-1] == [('s', 33878)],
          'the question closed: nothing kept, said so')
    c = run([], s3d='sdr709')
    check(hc.read_hdr3d(hc.HDR3D_MEMORY) == before and c.results[-1]['lines'] == [[('s', 33874)]] and
          not [s for s in Dialog.shown if s[0] == 'select'] and SETTINGS == USER,
          'HDR did not go out in 3D: no question, nothing kept, the settings back')
    c = run([], stereo='', s3d='')
    check(hc.read_hdr3d(hc.HDR3D_MEMORY) == before and c.results[-1]['lines'] == [[('s', 33875)]],
          'no 3D output: nothing kept')
    c = run([('select', 0)], key='')
    check(hc.read_hdr3d(hc.HDR3D_MEMORY) == before and c.results[-1]['lines'][-1] == [('s', 33887)],
          'a display that reports no make and model: nothing kept, said so')
    c = run([], displays=False)
    check(not Player.played and c.results[-1]['lines'] == [[('s', 33852)]] and SETTINGS == USER,
          'no display that takes 3D: skipped, nothing played')
    c = run([], user=dict(USER, **{hc.FP_OUTPUT: 0}))
    check(not Player.played and c.results[-1]['lines'] == [[('f', 33866, [('k', 59979), hc.DISPLAY_PATH])]],
          'the 3D output of Hades off: skipped, with the setting named')
    same = {P: 1, F: 100, R: 2, C: 1, hc.FP_OUTPUT: 1}
    c = run([('select', 0)], user=same)
    check(SETTINGS == same and not any('settings before the card' in m for _, m in LOG[-6:]),
          'nothing set where the settings are so already')
    # "Everything" leaves the card out: it keeps an answer for the display, which a quick check should not do unasked
    c = ui.CheckSetup(clips, os.path.join(work, 'all'))
    check(33870 in [t for t, _ in c.entries()], 'the card is offered where its clip is there')
    Dialog.script = [('select', 0)]
    ran = []
    c.entries = lambda: [(33870, lambda: ran.append('hdr3d')), (33652, lambda: ran.append('speakers'))]
    c.run()
    check(ran == ['speakers'], '"Everything" leaves the card out')
    Dialog.script = [('select', 1)]
    ran.clear()
    c.run()
    check(ran == ['hdr3d'], 'chosen by itself, it runs')
finally:
    shutil.rmtree(work, ignore_errors=True)
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
