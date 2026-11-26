#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""src_check.py <kodi tree>: the device change events that ActiveAE acts on while it is configured, checked in the source
(the state machine of the engine runs in no host test). A change of the devices of a driver (DEVICECOUNTCHANGE) or of
the default device (DEFAULTDEVICECHANGE) takes the events of both kinds already queued behind it and lists the outputs
once for all of them (Kodi pull request 29331): one per event made a burst of them hold a new stream past the 10 s
that MakeStream() waits. What the events taken carried is kept: a change of the default device among them is acted on
as one, and a driver other than the event's own lists the outputs of every driver. One line per check, "ok: <check>"
or "FAIL: <check>: <why>", then DEVCHANGE-SRC PASSED or DEVCHANGE-SRC FAILED. Each mutant of mutants.py that names
this check takes one rule out, and the check of that rule must fail."""
import re
import sys

K = sys.argv[1]
fails = 0


def check(name, ok, why):
    global fails
    if ok:
        print('ok: %s' % name)
    else:
        fails += 1
        print('FAIL: %s: %s' % (name, why))


def block(text, start):
    """from the { at or after start to the } that closes it"""
    i = text.index('{', start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


ae = open('%s/xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAE.cpp' % K, encoding='utf-8').read()
heads = list(re.finditer(r'case CActiveAEControlProtocol::DEVICECOUNTCHANGE:\s*'
                         r'case CActiveAEControlProtocol::DEFAULTDEVICECHANGE:\s*\{', ae))
b = block(ae, heads[0].end() - 1) if len(heads) == 1 else None
act = b.find('HandleDeviceCountChange(driver, defaultDeviceChanged);') if b else -1
check('one-handler-for-both-kinds', b is not None and act >= 0 and
      'HandleDeviceCountChange(reinterpret_cast<const char*>(msg->data), false);' not in ae and
      'HandleDeviceCountChange("", true);' not in ae,
      'the two kinds are not acted on by one handler that lists the outputs once (%d handlers found)' % len(heads))
check('own-driver-from-the-event', b is not None and re.search(
      r'if \(signal == CActiveAEControlProtocol::DEVICECOUNTCHANGE\)\s+'
      r'driver = reinterpret_cast<const char\*>\(msg->data\);', b) is not None,
      'the driver is not read from the event, and only from a DEVICECOUNTCHANGE')
m = re.search(r'const bool defaultDeviceChanged =\s+m_controlPort\.PurgeOut\(CActiveAEControlProtocol::'
              r'DEFAULTDEVICECHANGE\) > 0 \|\|\s+signal == CActiveAEControlProtocol::DEFAULTDEVICECHANGE;', b or '')
check('queued-default-changes-taken-and-kept', m is not None and m.start() < act,
      'the changes of the default device queued behind the event are not taken, or a change among them is lost')
count = b.find('m_controlPort.PurgeOut(CActiveAEControlProtocol::DEVICECOUNTCHANGE,') if b else -1
check('queued-count-changes-taken', 0 <= count < act,
      'the changes of the devices of a driver queued behind the event are not taken before the outputs are listed')
widen = re.search(r'bool allDrivers = defaultDeviceChanged;\s+m_controlPort\.PurgeOut\(CActiveAEControlProtocol::'
                  r'DEVICECOUNTCHANGE,\s+\[&driver, &allDrivers\]\(const Message& purged\)\s+\{\s+'
                  r'if \(driver != reinterpret_cast<const char\*>\(purged\.data\)\)\s+allDrivers = true;\s+\}\);\s+'
                  r'if \(allDrivers\)\s+driver\.clear\(\);\s+HandleDeviceCountChange\(driver, defaultDeviceChanged\);',
                  b or '')
check('another-driver-lists-every-driver', widen is not None,
      'an event of another driver taken with it, or a change of the default device, does not list every driver')

print('DEVCHANGE-SRC PASSED' if fails == 0 else 'DEVCHANGE-SRC FAILED')
sys.exit(1 if fails else 0)
