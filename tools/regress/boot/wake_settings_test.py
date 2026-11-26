# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: the setting "Wake from sleep" of the LibreELEC settings add-on (System > Sleep, LibreELEC-settings-9093):
# its strings in English and the three Spanish languages, its place in defaults.py (the file of the choice and the
# path of hades-wake, which packages/sysutils/hades-wake installs), its group in System, when it is shown, what the list
# writes (no file for the default, one word otherwise, read back), that hades-wake is run at once with a PATH of its
# own and what it says goes to the log, and every answer: the same choice, a cancel, a file that cannot be written, a
# hades-wake that fails. modules/system.py of the add-on is imported with stand-ins for Kodi (xbmc, xbmcgui, oe, log,
# hostname, timezone, os_tools); hades-wake is a stand-in script.
# usage: wake_settings_test.py <the add-on: its folder with resources/>
import inspect
import os
import re
import shutil
import stat
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


# ---- the strings (34100 to 34108 of LibreELEC-settings-9093, 34109 to 34120 of 9094: the list of devices)
IDS = [n for n in range(34100, 34121) if n != 34111]


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
    check(ok, f'{lang}: the strings 34100 to 34120, once each')
    if not ok:
        continue
    if lang == 'en_gb':
        english = {n: entries[n][0][0] for n in IDS}
        check(all(entries[n][0][1] == '' for n in IDS), 'en_gb: English in msgid, msgstr empty')
        continue
    check(all(entries[n][0][0] == english.get(n) for n in IDS), f'{lang}: the msgids are the English ones')
    check(all(entries[n][0][1] for n in IDS), f'{lang}: every one translated')
    check(all(re.findall(r'%[sd]', entries[n][0][1]) == re.findall(r'%[sd]', entries[n][0][0]) for n in IDS),
          f'{lang}: the same placeholders as the English')
    check(len(entries[34106][0][1] % 99) <= 64, f'{lang}: the notification fits the 64 characters that oe.notify shows')
check(english and len(english[34106] % 99) <= 64, 'en_gb: the notification fits the 64 characters that oe.notify shows')
check(english and 'flirc' in english.get(34102, '') and 'Devices that may wake it' in english.get(34102, '') and
      'Bluetooth devices and Wake-on-LAN are not changed' not in english.get(34102, ''),
      'en_gb: the help names the kinds, and the list of devices; Bluetooth is no longer said to be left alone')
check(english and all(w in english.get(34110, '') for w in ('adapter', 'firmware', 'ErP', 'serial number', 'address')),
      'en_gb: the help of the list says what Bluetooth depends on and how a device is remembered')


# ---- stand-ins for Kodi
class Dialog:
    script = []
    shown = []

    def select(self, h, items, preselect=-1, **kw):
        Dialog.shown.append(('select', h, list(items), preselect))
        if not Dialog.script:
            raise AssertionError('no answer scripted for select')
        return Dialog.script.pop(0)

    def multiselect(self, h, items, preselect=None, **kw):
        Dialog.shown.append(('multiselect', h, list(items), list(preselect or [])))
        if not Dialog.script:
            raise AssertionError('no answer scripted for multiselect')
        return Dialog.script.pop(0)

    def ok(self, h, t):
        Dialog.shown.append(('ok', h, t))

    def yesno(self, h, t, **kw):
        raise AssertionError('no question is asked')

    def textviewer(self, h, t):
        Dialog.shown.append(('textviewer', h, t))


LOG = []
NOTIFY = []
log = types.SimpleNamespace(INFO=1, WARNING=2, ERROR=3, DEBUG=0, log=lambda m, l=1: LOG.append((l, m)),
                            log_function=lambda *a, **k: (lambda f: f))
xbmc = types.SimpleNamespace(getLocalizedString=lambda n: f'k{n}', executebuiltin=lambda s: None, Monitor=object)
xbmcgui = types.SimpleNamespace(Dialog=Dialog, Window=object, DialogProgress=object, DialogProgressBG=object,
                                DLG_YESNO_YES_BTN=11, DLG_YESNO_NO_BTN=10, ListItem=object)
oe = types.SimpleNamespace(_=lambda n: f's{n}' + (' %d' if n == 34106 else ''), notify=lambda t, m, icon='icon': NOTIFY.append((t, m)),
                           read_setting=lambda *a: None, write_setting=lambda *a: None, xbmcm=None, winOeMain=None)
for name, mod in (('xbmc', xbmc), ('xbmcgui', xbmcgui), ('log', log), ('oe', oe),
                  ('hostname', types.SimpleNamespace()), ('timezone', types.SimpleNamespace()),
                  ('os_tools', types.SimpleNamespace(execute=lambda *a, **k: None))):
    sys.modules[name] = mod
sys.path.insert(0, LIB)
sys.path.insert(0, os.path.join(LIB, 'modules'))
import defaults  # noqa: E402
import system  # noqa: E402

work = tempfile.mkdtemp(prefix='wake-settings-')
try:
    # ---- defaults: the file of the choice beside the others of the add-on, hades-wake where its package puts it
    d = getattr(defaults, 'system', {})
    check(d.get('WAKE_CHOICE') == f'{defaults.CONFIG_CACHE}/hades-wake', 'defaults: WAKE_CHOICE in CONFIG_CACHE')
    check(d.get('WAKE_HELPER') == '/usr/lib/hades-wake/wake.sh', 'defaults: WAKE_HELPER where hades-wake installs it')

    s = system.system(None)
    g = s.struct.get('sleep', {})
    st = g.get('settings', {}).get('wake_from_sleep', {})
    check(g.get('name') == 34100 and st.get('action') == 'set_wake' and st.get('type') == 'button' and
          st.get('InfoText') == 34102 and st.get('name') == 34101, 'System > Sleep > Wake from sleep: a button')
    orders = [v.get('order') for v in s.struct.values()]
    check(len(orders) == len(set(orders)), 'the order of the group is its own')
    check('self.load_wake()' in inspect.getsource(system.system.load_menu), 'shown each time the list is (load_menu)')

    helper = os.path.join(work, 'wake.sh')
    record = os.path.join(work, 'record')
    states = os.path.join(work, 'state')
    choice = os.path.join(work, 'cache', 'hades-wake')

    listing = os.path.join(work, 'listing')

    def fake_helper(status=0, last='armed 2', list_status=0):
        with open(helper, 'w') as f:
            f.write(f'echo "$*" >> {record}\necho "PATH=$PATH" >> {record}\n'
                    f'if [ "$1" = list ]; then cat {listing}; exit {list_status}; fi\n'
                    f'echo "wake from sleep with: whatever; USB input devices that may wake the PC: x"\n'
                    f'echo "{last}"\nexit {status}\n')
        if os.path.exists(record):
            os.remove(record)

    def recorded():
        return open(record).read().split('\n') if os.path.exists(record) else []

    s.WAKE_CHOICE, s.WAKE_HELPER, s.SLEEP_STATES = choice, helper, states
    open(states, 'w').write('freeze mem\n')
    fake_helper()

    # ---- shown or not, and what it shows
    os.remove(helper)
    s.load_wake()
    check(g.get('hidden') == 'true', 'hidden without hades-wake')
    fake_helper()
    open(states, 'w').write('disk\n')
    s.load_wake()
    check(g.get('hidden') == 'true', 'hidden on a computer that cannot sleep (no freeze, no mem)')
    open(states, 'w').write('freeze\n')
    s.load_wake()
    check('hidden' not in g and st['value'] == 'remotes' and st['name'] == 's34101 (s34103)',
          'shown where it can sleep (s2idle only too): no file, the default, Remotes')
    os.makedirs(os.path.dirname(choice))
    for word, label in (('keyboards', 's34104'), ('button', 's34105'), ('something', 's34103'), ('', 's34103')):
        open(choice, 'w').write(word + '\n')
        s.load_wake()
        check(st['name'] == f's34101 ({label})' and st['value'] == {'s34104': 'keyboards', 's34105': 'button'}.get(label, 'remotes'),
              f'a file that says {word!r}: {label}')
    os.remove(choice)

    # ---- a choice made: written, applied at once, said
    Dialog.shown.clear(); NOTIFY.clear(); LOG.clear()
    Dialog.script = [2]
    s.set_wake()
    sel = [x for x in Dialog.shown if x[0] == 'select']
    check(len(sel) == 1 and sel[0][2] == ['s34103', 's34104', 's34105'] and sel[0][3] == 0,
          'the list: three choices, the one in force preselected')
    check(open(choice).read() == 'button\n' and stat.S_IMODE(os.stat(choice).st_mode) == 0o644,
          'only the power button: the file says button')
    check([x for x in os.listdir(os.path.dirname(choice)) if x != 'hades-wake'] == [], 'nothing left beside it')
    r = recorded()
    check(r[:2] == ['apply', 'PATH=/usr/bin:/usr/sbin'], 'hades-wake run at once (apply), with a PATH of its own')
    check(NOTIFY == [('s34101: s34105', 's34106 2')], 'a notification with the choice and how many may wake it')
    check(any(m == 'hades-wake: armed 2' for l, m in LOG) and
          any(m.startswith('hades-wake: wake from sleep with: ') for l, m in LOG), 'what hades-wake said is in the log of Kodi')
    check(st['name'] == 's34101 (s34105)', 'the list says what holds now')

    Dialog.script = [1]; NOTIFY.clear(); fake_helper()
    s.set_wake()
    check(open(choice).read() == 'keyboards\n' and recorded()[:1] == ['apply'], 'remotes and keyboards: written, applied')

    Dialog.script = [0]; NOTIFY.clear(); fake_helper()
    s.set_wake()
    check(not os.path.exists(choice) and recorded()[:1] == ['apply'] and len(NOTIFY) == 1,
          'back to the default: no file, applied')

    # ---- nothing to do
    for answer, what in ((0, 'the same choice'), (-1, 'the list closed')):
        Dialog.script = [answer]; NOTIFY.clear(); fake_helper(); Dialog.shown.clear()
        s.set_wake()
        check(not os.path.exists(choice) and recorded() == [] and NOTIFY == [] and
              [x for x in Dialog.shown if x[0] == 'ok'] == [], f'{what}: nothing written, nothing run')

    # ---- hades-wake that fails: the choice is kept, and that is said
    for status, last in ((1, 'armed 2'), (0, 'nothing')):
        Dialog.script = [2]; NOTIFY.clear(); fake_helper(status, last); Dialog.shown.clear(); LOG.clear()
        s.set_wake()
        check(open(choice).read() == 'button\n' and NOTIFY == [] and ('ok', 's34101', 's34108') in Dialog.shown and
              any(l == 3 for l, m in LOG), f'hades-wake that fails (exit {status}, "{last}"): kept, said, logged')
        os.remove(choice)

    # ---- a file that cannot be written: nothing run, said
    s.WAKE_CHOICE = os.path.join(work, 'notafolder', 'hades-wake')
    open(os.path.join(work, 'notafolder'), 'w').write('a file where the folder would be\n')
    Dialog.script = [2]; NOTIFY.clear(); fake_helper(); Dialog.shown.clear()
    s.set_wake()
    check(recorded() == [] and NOTIFY == [] and ('ok', 's34101', 's34107') in Dialog.shown,
          'a file that cannot be written: hades-wake not run, said')
    s.WAKE_CHOICE = choice

    # ---- write_wake_choice reads back what it wrote
    system.write_wake_choice(choice, 'keyboards')
    check(system.wake_choice(choice) == 'keyboards', 'write_wake_choice then wake_choice')
    system.write_wake_choice(choice, 'remotes')
    check(not os.path.exists(choice) and system.wake_choice(choice) == 'remotes', 'the default is no file')

    # ---- the list of devices (LibreELEC-settings-9094)
    devfile = os.path.join(work, 'cache', 'hades-wake-devices')
    check(d.get('WAKE_DEVICES') == f'{defaults.CONFIG_CACHE}/hades-wake-devices', 'defaults: WAKE_DEVICES in CONFIG_CACHE')
    sd = g.get('settings', {}).get('wake_devices', {})
    check(sd.get('action') == 'set_wake_devices' and sd.get('type') == 'button' and sd.get('name') == 34109 and
          sd.get('InfoText') == 34110, 'System > Sleep > Devices that may wake it: a button')
    s.WAKE_DEVICES = devfile
    open(listing, 'w').write(
        'usb|20a0:0006|enabled|remote|USB 5-2.4.1 20a0:0006 flirc|an IR receiver known by its ID (flirc)|-\n'
        'usb|046d:c077 port=3-2|disabled|pointer|USB 3-2 046d:c077 Logitech USB Optical Mouse|a mouse or another pointer|-\n'
        'usb|1234:5678||remote|USB 3-6 1234:5678 Plain Remote|a remote\'s receiver|-\n'
        'usb|045e:02ea serial=3039363431|disabled|gamepad|USB 3-3 045e:02ea|a game controller|-\n'
        'bt|11:22:33:44:55:02|disabled|gamepad|Bluetooth 11:22:33:44:55:02 Xbox Wireless Controller|a Bluetooth game controller|-\n'
        'bt|11:22:33:44:55:04|enabled|other|Bluetooth 11:22:33:44:55:04|a Bluetooth input device|on\n'
        'garbage line\n')
    devs, cannot = system.parse_wake_list(open(listing).read())
    check([x['key'] for x in devs] == ['usb 20a0:0006', 'usb 046d:c077 port=3-2', 'usb 045e:02ea serial=3039363431',
                                       'bt 11:22:33:44:55:02', 'bt 11:22:33:44:55:04'] and cannot == 1,
          'the list: the devices that can wake a computer, one without power/wakeup counted apart, nothing else')
    check([system.wake_device_name(x) for x in devs] == ['flirc', 'Logitech USB Optical Mouse', '045e:02ea',
                                                          'Xbox Wireless Controller', '11:22:33:44:55:04'],
          'plain names: make and model, the IDs where there is none, the name or the address of a Bluetooth device')
    Dialog.shown.clear(); NOTIFY.clear(); LOG.clear(); fake_helper()
    # tick the mouse and the Xbox controller, untick the flirc
    Dialog.script = [[1, 3, 4]]
    s.set_wake_devices()
    ms = [x for x in Dialog.shown if x[0] == 'multiselect']
    check(len(ms) == 1 and ms[0][3] == [0, 4] and ms[0][2][1] == 'Logitech USB Optical Mouse (s34115, USB)' and
          ms[0][2][3] == 'Xbox Wireless Controller (s34116, Bluetooth)', 'the list: plain labels, what may wake it ticked')
    check(open(devfile).read() == 'off usb 20a0:0006\non usb 046d:c077 port=3-2\non bt 11:22:33:44:55:02\n' and
          stat.S_IMODE(os.stat(devfile).st_mode) == 0o644, 'only what was changed is written, on and off, by its key')
    check(recorded()[:1] == ['list'] and recorded()[2:3] == ['apply'] and NOTIFY == [('s34109', 's34106 2')],
          'applied at once, and said')
    check(any(m == f'{devfile}: on bt 11:22:33:44:55:02' for l, m in LOG), 'each change in the log of Kodi')
    # the flirc ticked again: its line changes and goes last; the others stay
    open(listing, 'w').write(
        'usb|20a0:0006|disabled|remote|USB 5-2.4.1 20a0:0006 flirc|an IR receiver known by its ID (flirc)|off\n'
        'usb|046d:c077 port=3-2|enabled|pointer|USB 3-2 046d:c077 Logitech USB Optical Mouse|a mouse or another pointer|on\n')
    Dialog.script = [[0, 1]]; fake_helper()
    s.set_wake_devices()
    check(open(devfile).read() == 'on usb 046d:c077 port=3-2\non bt 11:22:33:44:55:02\non usb 20a0:0006\n',
          'a line changed goes last, the others are kept')
    for answer, what in ((None, 'the list closed'), ([1], 'nothing changed')):
        Dialog.script = [answer]; NOTIFY.clear(); fake_helper(); before = open(devfile).read()
        s.set_wake_devices()
        check(open(devfile).read() == before and [x for x in recorded() if x] == ['list', 'PATH=/usr/bin:/usr/sbin'] and NOTIFY == [],
              f'{what}: nothing written, nothing applied')
    open(choice, 'w').write('button\n'); Dialog.shown.clear(); fake_helper()
    s.set_wake_devices()
    check(('ok', 's34109', 's34118') in Dialog.shown and recorded() == [], 'with only the power button: said, no list')
    os.remove(choice)
    Dialog.shown.clear(); fake_helper(list_status=1)
    s.set_wake_devices()
    check(('ok', 's34109', 's34119') in Dialog.shown, 'a list that cannot be read: said')
    open(listing, 'w').write('usb|1234:5678||remote|USB 3-6 1234:5678 Plain Remote|a remote\'s receiver|-\n')
    Dialog.shown.clear(); fake_helper()
    s.set_wake_devices()
    check(('ok', 's34109', 's34120') in Dialog.shown, 'no device that can wake a computer: said')
    system.write_wake_devices(devfile, {'usb 046d:c077 port=3-2': 'off'})
    check(system.wake_device_lines(devfile)[-1] == ('off', 'usb 046d:c077 port=3-2'), 'write_wake_devices reads back')
    open(devfile, 'a').write('on usb ZZZZ:1\nmaybe usb 1234:5678\non bt 11:22:33:44:55\n')
    check(len(system.wake_device_lines(devfile)) == 3, 'lines that hades-wake would not understand are not read')
finally:
    shutil.rmtree(work, ignore_errors=True)

print('ALL PASSED' if fails == 0 else f'{fails} FAILED')
sys.exit(0 if fails == 0 else 1)
