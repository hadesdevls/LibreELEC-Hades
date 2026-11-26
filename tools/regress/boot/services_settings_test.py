# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: Services of the LibreELEC settings add-on, where the safe defaults are met (LibreELEC-settings-9095 and 9096):
# Samba's "How to connect" and the question before sharing without a password, the button of the SSH password that
# says why SSH takes only keys, and Network access (Recommended or As in LibreELEC: what holds, what a choice changes,
# that As in LibreELEC asks first, the log of Kodi, the page of the wizard). modules/services.py and modules/netaccess.py
# of the add-on are imported with stand-ins for Kodi (xbmc with the JSON-RPC of AirPlay, xbmcgui, oe with the service
# files of LibreELEC in a folder of the test, log); systemctl and sshd-default-password are stand-ins too.
# usage: services_settings_test.py <the add-on: its folder with resources/>
import os
import re
import shutil
import sys
import tempfile
import types

ADDON = sys.argv[1]
LIB = os.path.join(ADDON, 'resources', 'lib')
fails = 0


def check(ok, text):
    global fails
    fails += 0 if ok else 1
    print(('ok:   ' if ok else 'FAIL: ') + text)


# ---- the strings
IDS = list(range(34121, 34131)) + [n for n in range(34140, 34170) if n not in (34147, 34161)]


def po_entries(path):
    text = open(path, encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'^msgctxt "#(\d+)"\nmsgid "((?:[^"\\]|\\.)*)"\nmsgstr "((?:[^"\\]|\\.)*)"$', text, re.M):
        out.setdefault(int(m.group(1)), []).append((m.group(2), m.group(3)))
    return out


english = {}
for lang in ('en_gb', 'es_es', 'es_ar', 'es_mx'):
    p = os.path.join(ADDON, 'resources', 'language', f'resource.language.{lang}', 'strings.po')
    entries = po_entries(p) if os.path.exists(p) else {}
    ok = all(len(entries.get(n, [])) == 1 for n in IDS)
    check(ok, f'{lang}: the strings of 9095 and 9096, once each')
    if not ok:
        continue
    if lang == 'en_gb':
        english = {n: entries[n][0][0] for n in IDS}
        check(all(entries[n][0][1] == '' for n in IDS), 'en_gb: English in msgid, msgstr empty')
        continue
    check(all(entries[n][0][0] == english.get(n) for n in IDS), f'{lang}: the msgids are the English ones')
    check(all(entries[n][0][1] for n in IDS), f'{lang}: every one translated')
    check(all(sorted(re.findall(r'%[sd]|\{[a-z]+\}', entries[n][0][1])) ==
              sorted(re.findall(r'%[sd]|\{[a-z]+\}', entries[n][0][0])) for n in IDS),
          f'{lang}: the same placeholders as the English')
    vos = re.compile(r'\b(podés|activá|poné|entrá|elegí|escribí|marcá|sacalo|probá|usá|activás|conectate|cambialo)\b')
    hits = [n for n in IDS if vos.search(entries[n][0][1])]
    check(hits == [] if lang != 'es_ar' else True, f'{lang}: voseo only in es_ar ({hits})')
check(english and 'No more authentication methods available' in english.get(34123, ''),
      'en_gb: the help of the SSH password names what an SFTP program says')
check(english and 'Windows 11' in english.get(34130, '') and 'root' in english.get(34130, ''),
      'en_gb: the question before sharing without a password says what it opens, and Windows 11')
check(english and all(w in english.get(34141, '') for w in ('Recommended', 'As in LibreELEC', 'firewall', 'AirPlay',
                                                             'Updates never change it', 'internal disks')),
      'en_gb: the help of Network access says both choices, that updates keep it, and the internal disks')


# ---- stand-ins for Kodi
class Dialog:
    script = []
    shown = []

    def _answer(self, kind):
        if not Dialog.script:
            raise AssertionError(f'no answer scripted for {kind}')
        return Dialog.script.pop(0)

    def select(self, h, items, preselect=-1, **kw):
        Dialog.shown.append(('select', h, list(items), preselect))
        return self._answer('select')

    def yesno(self, h, t, defaultbutton=None, **kw):
        Dialog.shown.append(('yesno', h, t, defaultbutton))
        return self._answer('yesno')

    def ok(self, h, t):
        Dialog.shown.append(('ok', h, t))

    def textviewer(self, h, t):
        Dialog.shown.append(('textviewer', h, t))

    def input(self, h, *a, **k):
        return self._answer('input')


KODI = {'services.airplay': False, 'refuse': False}


def jsonrpc(text):
    import json
    q = json.loads(text)
    s = q['params'].get('setting')
    if q['method'] == 'Settings.GetSettingValue':
        return json.dumps({'result': {'value': KODI.get(s)}})
    if KODI['refuse']:
        return json.dumps({'error': {'code': -32602, 'message': 'Invalid params.'}})
    KODI[s] = q['params']['value']
    return json.dumps({'result': True})


LOG = []
log = types.SimpleNamespace(INFO=1, WARNING=2, ERROR=3, DEBUG=0, log=lambda m, l=1: LOG.append((l, m)),
                            log_function=lambda *a, **k: (lambda f: f))
xbmc = types.SimpleNamespace(getLocalizedString=lambda n: f'k{n}', executebuiltin=lambda s: None,
                             executeJSONRPC=jsonrpc, Monitor=object)
xbmcgui = types.SimpleNamespace(Dialog=Dialog, DLG_YESNO_YES_BTN=11, DLG_YESNO_NO_BTN=10, Window=object)
work = tempfile.mkdtemp(prefix='services-settings-')
CC = os.path.join(work, 'cache')
os.makedirs(f'{CC}/services')


def get_service_option(service, option, default=None):
    name = ''
    if os.path.exists(f'{CC}/services/{service}.conf'):
        name = f'{CC}/services/{service}.conf'
    if os.path.exists(f'{CC}/services/{service}.disabled'):
        name = f'{CC}/services/{service}.disabled'
    if name:
        for line in open(name):
            if option in line and '=' in line:
                default = line.strip().split('=')[-1]
    return default


def get_service_state(service):
    b = f'{CC}/services/{service}'
    return '1' if os.path.exists(f'{b}.conf') and not os.path.exists(f'{b}.disabled') else '0'


def set_service(service, options, state):
    cfo, cfn = f'{CC}/services/{service}.disabled', f'{CC}/services/{service}.conf'
    if state == 1:
        if os.path.exists(cfo):
            os.remove(cfo)
        with open(cfn, 'w') as f:
            for o in options:
                f.write(f'{o}={options[o]}\n')
    elif os.path.exists(cfn):
        os.rename(cfn, cfo)
    else:
        open(cfo, 'w').close()


class Aborted:
    def abortRequested(self):
        return False


class Win:
    radio = {}

    def set_wizard_title(self, t): pass
    def set_wizard_text(self, t): Win.text = t
    def set_wizard_button_title(self, t): pass
    def set_wizard_radiobutton_1(self, label, modul, action, selected=False): Win.radio[1] = (label, action, selected)
    def set_wizard_radiobutton_2(self, label, modul, action, selected=False): Win.radio[2] = (label, action, selected)
    def build_menu(self, struct): Win.built = True


oe = types.SimpleNamespace(_=lambda n: f's{n}' + (' %s' if n in (34126, 34163) else '') +
                           (' {host} {windows}' if n == 34128 else '') +
                           (' {windows} {host} {user} {password}' if n == 33176 else ''),
                           get_service_option=get_service_option, get_service_state=get_service_state,
                           set_service=set_service, CONFIG_CACHE=CC, xbmcm=Aborted(), dictModules={},
                           read_setting=lambda *a: None, write_setting=lambda *a: None, notify=lambda *a, **k: None,
                           winOeMain=Win())
for name, mod in (('xbmc', xbmc), ('xbmcgui', xbmcgui), ('log', log), ('oe', oe)):
    sys.modules[name] = mod
sys.path.insert(0, LIB)
sys.path.insert(0, os.path.join(LIB, 'modules'))
import defaults  # noqa: E402
import modules  # noqa: E402
import services  # noqa: E402

RUNS = []
DEFAULT_PW = {'on': True}


def fake_run(cmd, *a, **k):
    RUNS.append(cmd)
    rc = 0
    if cmd[:1] == ['/usr/lib/libreelec/sshd-default-password']:
        rc = 0 if DEFAULT_PW['on'] else 1
    return types.SimpleNamespace(returncode=rc, stdout='', stderr='')


services.subprocess = types.SimpleNamespace(run=fake_run, Popen=None, PIPE=-1)
services.services.phone_addresses = staticmethod(lambda: ['192.168.1.20'])


class Item:
    def __init__(self, **p): self.p = dict(p)
    def getProperty(self, k): return self.p.get(k, '')
    def setProperty(self, k, v): self.p[k] = v


try:
    d = getattr(defaults, 'services', {})
    check(d.get('NET_PROFILE') == f'{defaults.CONFIG_CACHE}/hades-network', 'defaults: NET_PROFILE in CONFIG_CACHE')
    order = defaults.wizard['order']
    check('netaccess' in order and order.index('netaccess') + 1 == order.index('services'),
          'the wizard: Network access right before Samba and SSH')
    s = services.services(None)
    for f in ('nmbd', 'smbd', 'sshd', 'avahi', 'cron', 'cmdline'):
        open(os.path.join(work, f), 'w').write('quiet\n')
    s.SAMBA_NMDB, s.SAMBA_SMDB, s.SSH_DAEMON = (os.path.join(work, f) for f in ('nmbd', 'smbd', 'sshd'))
    s.AVAHI_DAEMON, s.CRON_DAEMON, s.KERNEL_CMD = (os.path.join(work, f) for f in ('avahi', 'cron', 'cmdline'))
    s.NET_PROFILE = os.path.join(CC, 'hades-network')
    oe.dictModules['services'] = s
    st = s.struct

    # ---- 9095: Samba and SSH where they are met
    sl = st['samba']['settings'].get('samba_login', {})
    check(sl.get('action') == 'show_samba_login' and sl.get('type') == 'button' and sl.get('name') == 34124 and
          sl.get('parent') == {'entry': 'samba_autostart', 'value': ['1']}, 'Samba: How to connect, while Samba is on')
    check(st['ssh']['settings']['ssh_passwd'].get('InfoText') == 34123, 'SSH: the help of the password says why')
    set_service('samba', {'SAMBA_SECURE': 'true', 'SAMBA_USERNAME': 'libreelec', 'SAMBA_PASSWORD': 'Ab12Cd34'}, 1)
    set_service('iptables', {'RULES': 'home'}, 1)
    s.load_values()
    Dialog.shown.clear()
    s.show_samba_login(listItem=Item())
    tv = [x for x in Dialog.shown if x[0] == 'textviewer']
    check(len(tv) == 1 and tv[0][1] == 's33175' and 'libreelec' in tv[0][2] and 'Ab12Cd34' in tv[0][2] and
          '\\\\' in tv[0][2] and 's34126 [B]\\\\192.168.1.20[/B]' in tv[0][2] and 's34127' in tv[0][2],
          'How to connect: the user, the password, the name and the address, what Windows and macOS need')
    # turning the password off: asked, "no" preselected; no keeps it
    for answer, want in ((False, 'true'), (True, 'false')):
        set_service('samba', {'SAMBA_SECURE': 'true', 'SAMBA_USERNAME': 'libreelec', 'SAMBA_PASSWORD': 'Ab12Cd34'}, 1)
        s.load_values(); Dialog.shown.clear(); Dialog.script = [answer]
        item = Item(category='samba', entry='samba_secure', value='0')
        s.initialize_samba(listItem=item)
        yn = [x for x in Dialog.shown if x[0] == 'yesno']
        check(len(yn) == 1 and yn[0][1:] == ('s34129', 's34130', xbmcgui.DLG_YESNO_NO_BTN) and
              get_service_option('samba', 'SAMBA_SECURE') == want,
              f'without a password: asked with no preselected; answer {answer}: SAMBA_SECURE={want}')
    Dialog.shown.clear()
    s.show_samba_login()
    tv = [x for x in Dialog.shown if x[0] == 'textviewer']
    check(tv and tv[0][2].startswith('s34128') and 'Ab12Cd34' not in tv[0][2], 'How to connect without a password')
    Dialog.shown.clear()
    s.initialize_samba(listItem=Item(category='samba', entry='samba_workgroup', value='WORKGROUP'))
    check([x for x in Dialog.shown if x[0] == 'yesno'] == [], 'another setting of Samba asks nothing')
    DEFAULT_PW['on'] = True; s.load_values()
    check(st['ssh']['settings']['ssh_passwd']['name'] == 's32209 (s34121)', 'SSH Password: the default one, keys only')
    DEFAULT_PW['on'] = False; s.load_values()
    check(st['ssh']['settings']['ssh_passwd']['name'] == 's32209 (s34122)', 'SSH Password: your own')
    DEFAULT_PW['on'] = True

    # ---- 9096: Network access
    na = st.get('netaccess', {})
    b = na.get('settings', {}).get('net_access', {})
    check(na.get('order') == 0 and b.get('action') == 'set_net_access' and b.get('InfoText') == 34141,
          'Services > Network access: a button, first')
    # a new Hades: Samba off with a password, no file, firewall home, AirPlay off
    set_service('samba', {'SAMBA_SECURE': 'true', 'SAMBA_USERNAME': 'libreelec', 'SAMBA_PASSWORD': ''}, 0)
    s.load_values()
    check(s.net_state() == 'recommended' and b['name'] == 's34140 (s34144)', 'a new system: Recommended')
    LOG.clear(); s.start_service()
    check(any(m == 'Network access: Recommended (samba=off ssh=refuses firewall=home airplay=off)' for l, m in LOG),
          'the log of Kodi says it at each start')
    # As in LibreELEC, answered no: nothing changes
    Dialog.shown.clear(); Dialog.script = [1, False]
    s.set_net_access(listItem=Item())
    sel = [x for x in Dialog.shown if x[0] == 'select']
    tv = [x for x in Dialog.shown if x[0] == 'textviewer']
    yn = [x for x in Dialog.shown if x[0] == 'yesno']
    check(sel and sel[0][2] == ['s34144', 's34145'] and sel[0][3] == 0, 'the list: the one in force preselected')
    check(tv and all(f's{n}' in tv[0][2] for n in (34148, 34151, 34154, 34155, 34157, 34158, 34165, 34159, 34160)) and
          yn and yn[0][3] == xbmcgui.DLG_YESNO_NO_BTN, 'As in LibreELEC: what changes is shown, then asked with no preselected')
    check(s.net_state() == 'recommended' and not os.path.exists(s.NET_PROFILE) and KODI['services.airplay'] is False,
          'answered no: nothing changed')
    # answered yes
    Dialog.shown.clear(); Dialog.script = [1, True]; LOG.clear(); RUNS.clear()
    s.set_net_access(listItem=Item())
    check(get_service_state('samba') == '1' and get_service_option('samba', 'SAMBA_SECURE') == 'false' and
          open(s.NET_PROFILE).read() == 'libreelec\n' and get_service_state('iptables') == '0' and
          KODI['services.airplay'] is True, 'As in LibreELEC: Samba on without a password, SSH, no firewall, AirPlay on')
    check(['systemctl', 'try-reload-or-restart', 'sshd.service'] in RUNS, 'sshd told at once')
    check(s.net_state() == 'libreelec' and st['netaccess']['settings']['net_access']['name'] == 's34140 (s34145)',
          'it says As in LibreELEC')
    check(any(m.startswith('Network access: As in LibreELEC chosen; before: samba=off ssh=refuses firewall=home airplay=off; '
                           'now As in LibreELEC: samba=open ssh=accepts firewall=none airplay=on') for l, m in LOG),
          'the change in the log of Kodi')
    check(st['ssh']['settings']['ssh_passwd']['name'] == 's32209 (s34143)', 'SSH Password: accepted, as in LibreELEC')
    # a setting changed on its own: your own settings, none preselected, and that answer changes nothing
    KODI['services.airplay'] = False; s.load_values()
    check(s.net_state() == 'own' and st['netaccess']['settings']['net_access']['name'] == 's34140 (s34146)',
          'a setting changed afterwards: Your own settings')
    Dialog.shown.clear(); Dialog.script = [-1]
    s.set_net_access(listItem=Item())
    check([x[3] for x in Dialog.shown if x[0] == 'select'] == [-1] and s.net_state() == 'own', 'own: none preselected')
    # back to Recommended: no question, what changed is shown; LibreELEC's password is not kept
    set_service('samba', {'SAMBA_SECURE': 'false', 'SAMBA_USERNAME': 'libreelec', 'SAMBA_PASSWORD': 'libreelec'}, 1)
    KODI['services.airplay'] = True; s.load_values()
    Dialog.shown.clear(); Dialog.script = [0]
    s.set_net_access(listItem=Item())
    check([x for x in Dialog.shown if x[0] == 'yesno'] == [] and get_service_option('samba', 'SAMBA_SECURE') == 'true' and
          get_service_option('samba', 'SAMBA_PASSWORD') == '' and not os.path.exists(s.NET_PROFILE) and
          get_service_option('iptables', 'RULES') == 'home' and KODI['services.airplay'] is False and
          s.net_state() == 'recommended', 'Recommended: password, keys only, firewall, AirPlay off; no question')
    tv = [x for x in Dialog.shown if x[0] == 'textviewer']
    check(tv and tv[-1][2].startswith('s34163 s34144') and 's33177' in tv[-1][2], 'what changed is shown, and the login')
    # Recommended on a system with Samba off and without a password kept for later: with one when it is turned on
    set_service('samba', {'SAMBA_SECURE': 'false', 'SAMBA_PASSWORD': 'libreelec'}, 1)
    set_service('samba', {}, 0)
    s.NET_PROFILE and open(s.NET_PROFILE, 'w').write('libreelec\n')
    s.load_values(); Dialog.shown.clear()
    s.choose_net_access('recommended')
    check(get_service_option('samba', 'SAMBA_SECURE') == 'true' and get_service_state('samba') == '0',
          'Samba off: kept off, with a password for when it is turned on')
    # Kodi refuses AirPlay: said
    KODI['refuse'] = True; KODI['services.airplay'] = False; Dialog.shown.clear(); Dialog.script = [True]
    s.choose_net_access('libreelec')
    tv = [x for x in Dialog.shown if x[0] == 'textviewer']
    check(tv and 's34162' in tv[-1][2], 'Kodi refuses AirPlay: said')
    KODI['refuse'] = False
    # nothing to change
    KODI['services.airplay'] = True; s.load_values(); Dialog.shown.clear()
    s.choose_net_access('libreelec')
    check(('ok', 's34140: s34145', 's34150') in Dialog.shown, 'nothing to change: said, nothing asked')
    # the page of the wizard
    import netaccess
    w = netaccess.netaccess(None)
    w.do_wizard()
    check(Win.radio[1] == ('s34144', 'wizard_recommended', False) and Win.radio[2] == ('s34145', 'wizard_libreelec', True),
          'the wizard: the two choices, the one in force selected')
    Dialog.shown.clear(); Dialog.script = []
    w.wizard_recommended()
    check(s.net_state() == 'recommended' and Win.radio[1][2] is True and Win.radio[2][2] is False,
          'the wizard: Recommended chosen')
    Dialog.shown.clear(); Dialog.script = [False]
    w.wizard_libreelec()
    check(s.net_state() == 'recommended' and Win.radio[1][2] is True, 'the wizard: As in LibreELEC answered no')
finally:
    shutil.rmtree(work, ignore_errors=True)

print('ALL PASSED' if fails == 0 else f'{fails} FAILED')
sys.exit(0 if fails == 0 else 1)
