# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# power_src_check.py <Kodi tree>: in the source, kodi.log says why Kodi powered off, suspended, hibernated, rebooted or
# quit, and why it did not: what a Standby of CEC makes it do, with the setting that says so and its value, in every
# case of that setting (Ignore too), and where a Standby is left alone; every origin of a power action says itself at
# INFO before it posts the action (the builtins of the power menus and keys, JSON-RPC, the idle timer); the shutdown
# function says what it is set to; the message handler says the action carried out, and a refusal by PVR. Prints FAIL
# lines and POWER-SRC PASSED or FAILED (n) last.
import os
import re
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


def logs_before(b, post, words):
    '''an INFO line with these words comes before the first PostMsg(post) of b'''
    at = b.find(f'PostMsg({post}')
    m = re.search(r'CLog::Log\(LOGINFO,\s*"Power: ([^"]*)"', b[:at] if at >= 0 else '')
    return at >= 0 and m and all(w in m.group(1) for w in words)


cec = read('xbmc/peripherals/devices/PeripheralCecAdapter.cpp')
standby = body(cec, 'void CPeripheralCecAdapter::OnTvStandby(')
sw = standby.find('switch (iActionOnTvStandby)')
said = standby[:sw] if sw >= 0 else ''
need(re.search(r'CLog::Log\(LOGINFO,.*?standby_pc_on_tv_standby.*?iActionOnTvStandby', said, re.S),
     'a Standby of the TV does not say, before Kodi acts, the setting standby_pc_on_tv_standby and its value')
cases = set(re.findall(r'case (LOCALISED_ID_\w+):', standby[sw:]))
named = set(re.findall(r'iActionOnTvStandby == (LOCALISED_ID_\w+)', said))
need(cases and cases <= named, f'the line of a Standby does not name every case of the setting (not named: '
     f'{sorted(cases - named)})')
need('LOCALISED_ID_IGNORE' in named, 'the line of a Standby does not say Ignore')
case = cec[cec.find('case CEC_OPCODE_STANDBY:'):]
case = case[:case.find('break;', case.find('adapter->OnTvStandby();')) + 6]
need(re.search(r'else if \(command->initiator == CECDEVICE_TV\)\s*CLog::Log\(LOGINFO,[^;]*SCREENSAVER_TIMEOUT', case, re.S),
     'a Standby of the TV that Kodi leaves alone, because it put the TV in standby itself just before, is not said')
need(re.search(r'\belse\s*CLog::Log\(LOGINFO,[^;]*command->initiator', case, re.S),
     'a Standby of another device, which Kodi leaves alone, is not said')

blt = read('xbmc/interfaces/builtins/SystemBuiltins.cpp')
for name, post, word in (('Hibernate', 'TMSG_HIBERNATE', 'hibernate'), ('Powerdown', 'TMSG_POWERDOWN', 'power off'),
                         ('Quit', 'TMSG_QUIT', 'quit'), ('Reboot', 'TMSG_RESTART', 'reboot'),
                         ('RestartApp', 'TMSG_RESTARTAPP', 'restart'), ('Shutdown', 'TMSG_SHUTDOWN', 'shutdown'),
                         ('Suspend', 'TMSG_SUSPEND', 'suspend'), ('RebootToWindows', 'TMSG_RESTART', 'Windows')):
    need(logs_before(body(blt, f'static int {name}('), post, [word, f'builtin {name}()']),
         f'the builtin {name}() does not say itself before it posts {post}')
ops = read('xbmc/interfaces/json-rpc/SystemOperations.cpp')
for name, post in (('Shutdown', 'TMSG_POWERDOWN'), ('Suspend', 'TMSG_SUSPEND'), ('Hibernate', 'TMSG_HIBERNATE'),
                   ('Reboot', 'TMSG_RESTART')):
    need(logs_before(body(ops, f'JSONRPC_STATUS CSystemOperations::{name}('), post, ['JSON-RPC', f'System.{name}']),
         f'JSON-RPC System.{name} does not say itself before it posts {post}')
app = read('xbmc/interfaces/json-rpc/ApplicationOperations.cpp')
need(logs_before(body(app, 'JSONRPC_STATUS CApplicationOperations::Quit('), 'TMSG_QUIT', ['JSON-RPC', 'Application.Quit']),
     'JSON-RPC Application.Quit does not say itself before it posts TMSG_QUIT')
pwr = read('xbmc/application/ApplicationPowerHandling.cpp')
need(logs_before(body(pwr, 'void CApplicationPowerHandling::CheckShutdown('), 'TMSG_SHUTDOWN', ['idle timer']),
     'the idle timer does not say itself at INFO before it posts TMSG_SHUTDOWN')
hsm = body(pwr, 'void CApplicationPowerHandling::HandleShutdownMessage(')
need(re.search(r'CLog::Log\(LOGINFO,\s*"Power: the shutdown function[^;]*state', hsm[:hsm.find('switch (')], re.S),
     'the shutdown function does not say what it is set to before it acts')
msg = body(read('xbmc/application/ApplicationMessageHandling.cpp'), 'void CApplicationMessageHandling::OnApplicationMessage(')
pre = msg[:msg.find('switch (msg)')]
need(re.search(r'CanSystemPowerdown\(\)\)\s*msg = pMsg->param1;[^}]*else\s*\{\s*//[^\n]*\n\s*CLog::Log\(LOGINFO,[^;]*PVR[^;]*;\s*return;',
               pre, re.S), 'a power action of CEC that PVR refuses is not said')
carried = re.search(r'if \(([^;{]*)\)\s*CLog::Log\(LOGINFO, "Power: carried out: \{\}"', pre, re.S)
told = set(re.findall(r'msg == (TMSG_\w+)', carried.group(1))) if carried else set()
need(carried and pre.find('Power: carried out') > pre.find('msg = pMsg->param1'),
     'the message handler does not say the power action that it carries out, after the one of CEC is resolved')
want = {'TMSG_POWERDOWN', 'TMSG_SHUTDOWN', 'TMSG_HIBERNATE', 'TMSG_SUSPEND', 'TMSG_RESTART', 'TMSG_RESET', 'TMSG_QUIT',
        'TMSG_RESTARTAPP'}
need(told >= want, f'the message handler does not say every power action (not said: {sorted(want - told)})')
print(f'POWER-SRC FAILED ({len(fails)})' if fails else 'POWER-SRC PASSED')
sys.exit(1 if fails else 0)
