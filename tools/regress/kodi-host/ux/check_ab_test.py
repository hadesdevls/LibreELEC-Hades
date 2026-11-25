# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: the A/B comparison of native Dolby Vision of "Check this setup" (LibreELEC-settings-9073):
# hades_check.py (which values each variant sets, the order by chance, the offer, the journal of the settings of
# the form of Dolby Vision and how it is put back into guisettings.xml, the name of the TV from its EDID, the file
# of the answers) and the flow of hades_check_ui.py with stand-ins for Kodi (xbmc, xbmcgui, xbmcaddon, oe, log):
# A and B played with their settings, everything put back, nothing kept without a yes, the answer kept with the
# name of the TV, and nothing offered where the picture did not go out as native Dolby Vision.
# usage: check_ab_test.py <resources/lib of the add-on>
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
        self._left = 2

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
oe = types.SimpleNamespace(_=lambda n: ('s%d %%s' % n) if n in (33806, 33807, 33808, 33816, 33817, 33818, 33819,
                                                                  33820) else 's%d' % n, xbmcm=Monitor())
for name, mod in (('xbmc', xbmc), ('xbmcgui', xbmcgui), ('xbmcaddon', xbmcaddon), ('log', log), ('oe', oe)):
    sys.modules[name] = mod
sys.path.insert(0, LIB)
hc = importlib.import_module('hades_check')
ui = importlib.import_module('hades_check_ui')
ui.CheckSetup.rpc = staticmethod(rpc)

work = tempfile.mkdtemp(prefix='check-ab-')
try:
    # ---- hades_check.py
    avail = list(hc.AB_KEYS)
    check(hc.ab_values('hades', avail) == {'videoscreen.hadesdvl5form': 0, 'videoscreen.hadesdvtransport': 2,
                                            'videoscreen.hadesdvlevelorder': False, 'videoscreen.hadesdvl254zero': False,
                                            'videoscreen.hadesdvl3neutral': False, 'videoscreen.hadesdvavibt2020': False},
          'A of Hades: the defaults of the form of native Dolby Vision')
    check(hc.ab_values('licensed', avail)['videoscreen.hadesdvl5form'] == 2 and
          hc.ab_values('licensed', avail)['videoscreen.hadesdvtransport'] == 0, 'licensed: no active area, IPT')
    check(hc.ab_values('licensed', ['videoscreen.hadesdvtransport']) == {'videoscreen.hadesdvtransport': 0},
          'a setting that this Kodi does not have is left out')
    check(hc.ab_order(False) == ('hades', 'licensed') and hc.ab_order(True) == ('licensed', 'hades'), 'the order')
    check(hc.ab_choice('a', ('licensed', 'hades')) == 'licensed' and hc.ab_choice('b', ('licensed', 'hades')) == 'hades'
          and hc.ab_choice(None, ('hades', 'licensed')) == 'none' and hc.ab_choice('same', ('a', 'b')) == 'same',
          'what A and B were')
    cur = hc.ab_values('hades', avail)
    check(hc.ab_offer('hades', cur, avail) == {} and hc.ab_offer('same', cur, avail) == {}, 'nothing to offer')
    off = hc.ab_offer('licensed', cur, avail)
    check(off == {'videoscreen.hadesdvl5form': 2, 'videoscreen.hadesdvtransport': 0, 'videoscreen.hadesdvlevelorder': True,
                  'videoscreen.hadesdvl254zero': True,
                  'videoscreen.hadesdvl3neutral': True, 'videoscreen.hadesdvavibt2020': True},
          'the offer: what differs, and only that')
    # the journal: the settings of the form of Dolby Vision with their types, and passthrough as before
    j = os.path.join(work, 'restore.json')
    hc.write_journal(j, dict(cur, **{'audiooutput.passthrough': True}))
    check(hc.read_journal(j) == dict(cur, **{'audiooutput.passthrough': True}), 'the journal: ints and booleans')
    for bad in ({'videoscreen.hadesdvl5form': True}, {'videoscreen.hadesdvl254zero': 1}, {'videoscreen.other': 1}):
        try:
            hc.write_journal(j, bad)
            check(False, 'a journal of %r is refused' % bad)
        except ValueError:
            check(True, 'a journal of %r is refused' % bad)
    g = os.path.join(work, 'guisettings.xml')
    open(g, 'w').write('<?xml version="1.0"?>\n<settings version="2"><setting id="videoscreen.hadesdvl5form">2'
                       '</setting><setting id="videoscreen.hadesdvavibt2020">true</setting></settings>\n')
    hc.write_journal(j, {'videoscreen.hadesdvl5form': 0, 'videoscreen.hadesdvavibt2020': False})
    hc.restore_guisettings(j, g)
    text = open(g).read()
    check('id="videoscreen.hadesdvl5form">0<' in text and 'id="videoscreen.hadesdvavibt2020">false<' in text
          and not os.path.exists(j), 'put back into guisettings.xml: an integer as a number, a boolean as false')
    check(hc.passthrough_facts({'audiooutput.passthrough': True, 'audiooutput.ac3passthrough': False},
                               {'audiooutput.passthrough': True, 'audiooutput.ac3passthrough': True}, {})
          .count('passthrough=on') >= 1, 'the facts of passthrough as before')
    # the name of the TV, from an EDID (the serial number is not read)
    e = bytearray(128)
    e[0:8] = b'\x00\xff\xff\xff\xff\xff\xff\x00'
    m = ((ord('G') - 64) << 10) | ((ord('S') - 64) << 5) | (ord('M') - 64)
    e[8], e[9] = m >> 8, m & 0xFF
    e[10], e[11] = 0x9E, 0x5B
    e[12:16] = b'\x01\x02\x03\x04'
    e[54:72] = b'\x00\x00\x00\xfc\x00' + b'LG TV SSCR2\n  '
    check(hc.edid_name(bytes(e)) == 'GSM 0x5B9E LG TV SSCR2', 'the name of a TV from its EDID')
    check(hc.edid_name(b'\x00' * 128) == '', 'no EDID, no name')
    drm = os.path.join(work, 'drm')
    for c, s, data in (('card0-HDMI-A-1', 'connected', bytes(e)), ('card0-HDMI-A-2', 'disconnected', bytes(e)),
                       ('card1-DP-1', 'connected', b'')):
        os.makedirs(os.path.join(drm, c))
        open(os.path.join(drm, c, 'status'), 'w').write(s + '\n')
        open(os.path.join(drm, c, 'edid'), 'wb').write(data)
    check(hc.read_display_names(drm) == [('HDMI-A-1', 'GSM 0x5B9E LG TV SSCR2')], 'the displays connected now')
    # the file of the answers: the newest last, at most 50
    r = os.path.join(work, 'state', hc.AB_RESULTS_NAME)
    for n in range(55):
        hc.ab_record(r, {'n': n})
    d = json.load(open(r))
    check(d['version'] == 1 and [a['n'] for a in d['answers']] == list(range(5, 55)), 'the answers: the last 50')
    check(hc.ab_lines('X', 'licensed', [True, False], {}, False) == [[('s', 33824)]], 'not native: said so')
    f = hc.ab_facts('HDMI-A-1: GSM 0x5B9E LG TV SSCR2', ('licensed', 'hades'), 'a', 'licensed', True, [True, True],
                    avail, off, True)
    check(f.startswith('dolby vision a/b: display HDMI-A-1: GSM 0x5B9E LG TV SSCR2; A=licensed B=hades; answer=a '
                       'chosen=licensed; tv_player_compared=yes; native=yes/yes;') and 'applied=yes' in f,
          'the line of the summary')

    # ---- the flow, with stand-ins for Kodi
    clips = os.path.join(work, 'clips')
    os.makedirs(clips)
    open(os.path.join(clips, 'hades-check-ab-dv81.mkv'), 'wb').write(b'x' * 16)
    json.dump({'version': 1, 'clips': [], 'ab': {'file': 'hades-check-ab-dv81.mkv', 'duration': 15.015}},
              open(os.path.join(clips, 'clips.json'), 'w'))
    hc.read_display_names = lambda root='/sys/class/drm': [('HDMI-A-1', 'GSM 0x5B9E LG TV SSCR2')]

    def run(script, native=True, user=None, flip=False):
        SETTINGS.clear()
        SETTINGS.update(user or hc.ab_values('hades', avail))
        SETTINGS['other.setting'] = 7
        Player.played.clear()
        Dialog.script = list(script)
        Dialog.shown.clear()
        Window.props.clear()
        Window.props['hades.dv.state'] = 'native' if native else 'hdr10'
        Window.props['hades.sum.head'] = 'dv'
        ui.random.SystemRandom = lambda: types.SimpleNamespace(random=lambda: 0.3 if flip else 0.7)
        state = os.path.join(work, 'st%d' % len(LOG))
        c = ui.CheckSetup(clips, state)
        c.ab('title')
        return c, state

    before = dict(hc.ab_values('hades', avail))
    # A = licensed (flip), the user picks A, then says yes to the offer
    c, state = run([('select', 0), ('select', 0), ('yesno', True), ('yesno', True)], flip=True)
    check([p[1]['videoscreen.hadesdvl5form'] for p in Player.played] == [2, 0], 'A played as licensed players send, B as Hades')
    check(SETTINGS['videoscreen.hadesdvl5form'] == 2 and SETTINGS['videoscreen.hadesdvtransport'] == 0,
          'yes to the offer: the licensed form set')
    check(SETTINGS['other.setting'] == 7, 'nothing else changed')
    check(not os.path.exists(c.journal), 'the journal gone')
    a = json.load(open(os.path.join(state, hc.AB_RESULTS_NAME)))['answers'][-1]
    check(a['display'] == 'HDMI-A-1: GSM 0x5B9E LG TV SSCR2' and a['chosen'] == 'licensed' and a['applied'] and
          a['order'] == ['licensed', 'hades'] and a['tv_player_compared'], 'the answer kept with the name of the TV')
    check(c.results and 'chosen=licensed' in c.results[-1]['facts'], 'the result of the check says it')
    # the user picks the licensed form and says no: everything as it was
    c, state = run([('select', 0), ('select', 1), ('yesno', False), ('yesno', False)], flip=False)
    check({k: SETTINGS[k] for k in before} == before, 'no to the offer: the settings as they were')
    # the same: nothing offered, nothing changed
    c, state = run([('select', 0), ('select', 2), ('yesno', True)])
    check({k: SETTINGS[k] for k in before} == before and
          not any(s[0] == 'yesno' and 's33816' in str(s) for s in Dialog.shown), 'the same: nothing offered')
    # Hades chosen, the user's settings were the licensed form: putting back the defaults is offered
    lic = hc.ab_values('licensed', avail)
    c, state = run([('select', 0), ('select', 0), ('yesno', True), ('yesno', True)], user=lic, flip=False)
    check({k: SETTINGS[k] for k in before} == before, 'Hades chosen, yes: the defaults back')
    # show both again: four plays
    c, state = run([('select', 0), ('select', 4), ('select', 3), ('yesno', False)])
    check(len(Player.played) == 4 and {k: SETTINGS[k] for k in before} == before, 'shown again, then no answer')
    # not native: no offer, said so, the settings as they were
    c, state = run([('select', 0), ('select', 0)], native=False)
    check(c.results[-1]['lines'] == [[('s', 33824)]] and {k: SETTINGS[k] for k in before} == before,
          'not native Dolby Vision: no offer, said so')
    # a setting that cannot be put back after the comparison (LibreELEC-settings-9086): said as a setting that this
    # check changed for a while and that the next start puts back as it was (33840), not as a sound setting set as the
    # check decided (33741, the passthrough check's); its journal stays for that start
    STUCK['videoscreen.hadesdvl254zero'] = lambda: len(Player.played) >= 2
    c, state = run([('select', 0), ('select', 0), ('yesno', False)])
    STUCK.clear()
    lines = c.results[-1]['lines']
    check([('s', 33840)] in lines and [('s', 33741)] not in lines and
          hc.read_journal(c.journal).get('videoscreen.hadesdvl254zero') is False,
          'a setting not put back: said as one of this check, kept in the journal for the next start: %r' % lines[-1:])
    hc.remove_journal(c.journal)
    # the list closed: nothing played, nothing changed
    c, state = run([('select', -1)])
    check(not Player.played and {k: SETTINGS[k] for k in before} == before, 'closed: nothing played')
    # a Kodi without the settings: said so
    SETTINGS.clear()
    Dialog.script = []
    c = ui.CheckSetup(clips, os.path.join(work, 'none'))
    c.ab('title')
    check(c.results[-1]['lines'] == [[('s', 33825)]], 'no settings of the form of Dolby Vision: said so')
    # the copy to a USB drive: the drive picked, the clip there whole
    usb = os.path.join(work, 'media', 'USB')
    os.makedirs(usb)
    hc.usb_drives = lambda root='/var/media': [usb]
    c, state = run([('select', 1), ('select', -1)])
    check(open(os.path.join(usb, 'hades-check-ab-dv81.mkv'), 'rb').read() == b'x' * 16 and
          not os.path.exists(os.path.join(usb, 'hades-check-ab-dv81.mkv.part')), 'the clip copied to the drive, whole')
    hc.usb_drives = lambda root='/var/media': []
    c, state = run([('select', 1), ('select', -1)])
    check(any(s[0] == 'ok' and s[2] == 's33804' for s in Dialog.shown), 'no drive: said so')
    # "Everything" does not run the comparison
    c = ui.CheckSetup(clips, os.path.join(work, 'all'))
    names = [t for t, _ in c.entries()]
    check(33800 in names, 'the comparison is offered where the clip is there')
finally:
    shutil.rmtree(work, ignore_errors=True)
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
