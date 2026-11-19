# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades (1.0 RC4): the logic of the Bluetooth module of the LibreELEC settings add-on (LibreELEC-settings-9019):
# device_address() and device_disconnected() of its class, taken out of bluetooth.py and run with stand-ins for BlueZ,
# the log and the notifications: a disconnection says why in the log, without the name the user gave the device, and
# a rejected key tells the user to pair it again.
# usage: addon_logic_test.py <bluetooth.py>
import ast, sys, types
src = open(sys.argv[1], encoding='utf-8').read()
tree = ast.parse(src)
cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'bluetooth')
want = {'device_address', 'device_disconnected'}
funcs = [n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name in want]
assert {f.name for f in funcs} == want, [f.name for f in funcs]
mod = ast.Module(body=[ast.ClassDef(name='B', bases=[], keywords=[], body=funcs, decorator_list=[], type_params=[])] if sys.version_info >= (3, 12) else [ast.ClassDef(name='B', bases=[], keywords=[], body=funcs, decorator_list=[])], type_ignores=[])
ast.fix_missing_locations(mod)
logs, notes = [], []
props = {}
class FakeBluez:
    @staticmethod
    def device_get_property(path, name):
        if name not in props: raise RuntimeError('no such property')
        return props[name]
class FakeLog:
    INFO = 1
    @staticmethod
    def log(m, level=0): logs.append((level, m))
class FakeDialog:
    def notification(self, heading, msg, icon, ms): notes.append((heading, msg, icon, ms))
fake_gui = types.SimpleNamespace(Dialog=FakeDialog, NOTIFICATION_WARNING='warning')
fake_oe = types.SimpleNamespace(_=lambda code: {33120: '%s did not accept the key saved for it. Pair it again.'}[code])
ns = {'dbus_bluez': FakeBluez, 'log': FakeLog, 'xbmcgui': fake_gui, 'oe': fake_oe, 'staticmethod': staticmethod}
exec(compile(mod, 'bt', 'exec'), ns)
b = ns['B']()
fails = 0
def check(ok, text):
    global fails
    fails += 0 if ok else 1
    print(('ok:   ' if ok else 'FAIL: ') + text)
P = '/org/bluez/hci0/dev_E3_12_34_56_78_9A'
check(b.device_address(P) == 'E3:12:34:56:78:9A', 'the address of an object path, with colons')
props.update(Icon='input-keyboard', Modalias='usb:v0955p7214d0100', Paired=1, Name='SHIELD Remote', Alias='Remote of the lounge')
b.device_disconnected(P, 'org.bluez.Reason.Remote', 'Connection terminated by remote user')
check(len(logs) == 1 and 'E3:12:34:56:78:9A (input-keyboard, usb:v0955p7214d0100) disconnected: org.bluez.Reason.Remote' in logs[0][1] and logs[0][0] == 1, 'a remote disconnection: one line at INFO, with the reason')
check(not notes, 'a remote disconnection: nothing on the screen')
b.device_disconnected(P, 'org.bluez.Reason.Authentication', 'Connection terminated due to authentication failure')
check(len(logs) == 3 and 'did not accept the key' in logs[2][1], 'a rejected key: the reason and what it means, in the log')
check(len(notes) == 1 and notes[0][1] == 'Remote of the lounge did not accept the key saved for it. Pair it again.' and notes[0][2] == 'warning', 'a rejected key: one warning on the screen, with the alias')
check(all('lounge' not in m and 'SHIELD' not in m for _, m in logs), 'no name in the log')
b.device_disconnected(P, 'org.bluez.Reason.Authentication', 'x')
check(len(notes) == 1, 'a second rejection in the session: nothing more on the screen')
props.clear(); logs.clear()
b.device_disconnected('/org/bluez/hci0/dev_11_22_33_44_55_66', 'org.bluez.Reason.Authentication', 'x')
check(len(logs) == 1 and 'no icon, no modalias' in logs[0][1] and len(notes) == 1, 'a device that is gone (no properties): one line, nothing on the screen, no exception')
props.update(Paired=0, Icon='phone')
b.device_disconnected('/org/bluez/hci0/dev_AA_22_33_44_55_66', 'org.bluez.Reason.Authentication', 'x')
check(len(notes) == 1, 'a device that is not paired: nothing on the screen')
props.update(Paired=1); props.pop('Alias', None); props.pop('Name', None)
b.device_disconnected('/org/bluez/hci0/dev_AA_22_33_44_55_77', 'org.bluez.Reason.Authentication', 'x')
check(len(notes) == 2 and notes[1][1].startswith('AA:22:33:44:55:77 '), 'no name: the address on the screen')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
