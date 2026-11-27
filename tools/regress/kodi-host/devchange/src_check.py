#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""src_check.py <kodi tree>: the device change events that ActiveAE acts on while it is configured, checked in the source
(the state machine of the engine runs in no host test). A change of the devices of a driver (DEVICECOUNTCHANGE) or of
the default device (DEFAULTDEVICECHANGE) takes the events of both kinds already queued behind it and lists the outputs
once for all of them (Kodi pull request 29331): one per event made a burst of them hold a new stream past the 10 s
that MakeStream() waits. What the events taken carried is kept: a change of the default device among them is acted on
as one, and a driver other than the event's own lists the outputs of every driver. Hades 1.2.3 (av3): an ELD event of
ALSA says whether every ELD is as the last listing of the outputs read it (ALSAHControlMonitor.cpp: its bytes read when
it is listed and at each event), and the engine, suspended for a lost display, lists the outputs again when the display
is back only where another device changed or an ELD is not as listed (ActiveAESuspendedChanges.h); in every other state
an ELD event is a device change event as before. One line per check, "ok: <check>"
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

# ---- Hades 1.2.3 (av3): the ELD events and the reset of the display
mon = open('%s/xbmc/cores/AudioEngine/Sinks/alsa/ALSAHControlMonitor.cpp' % K, encoding='utf-8').read()
add = block(mon, mon.index('bool CALSAHControlMonitor::Add('))
check('eld-read-when-listed', add is not None and 'if (name == "ELD")' in add and
      'snd_hctl_elem_set_callback_private(elem, this);' in add and
      'eld.readable = ReadEld(elem, eld.listed);' in add and 'eld.now = eld.listed;' in add,
      'an ELD is not read as the listing of the outputs reads it')
cb = block(mon, mon.index('int CALSAHControlMonitor::HCTLCallback('))
check('eld-event-says-whether-as-listed', cb is not None and
      re.search(r'const bool read = ReadEld\(elem, bytes\);\s+const bool asListed = monitor->EldNow\(EldKey\(elem\), '
                r'read, bytes\);', cb) is not None and
      'CServiceBroker::GetActiveAE()->EldChange(asListed);' in cb,
      'an ELD event does not say whether every ELD is as it was listed')
check('other-controls-a-device-change', cb is not None and 'CServiceBroker::GetActiveAE()->DeviceChange();' in cb,
      'a control that is not an ELD is no longer a device change event')
now = block(mon, mon.index('bool CALSAHControlMonitor::EldNow('))
check('every-eld-compared', now is not None and 'return false; // (an ELD that the listing did not read' in now and
      re.search(r'for \(const auto& \[eldKey, eld\] : m_elds\)\s+\{\s+if \(!eld\.readable \|\| eld\.now != eld\.listed\)'
                r'\s+return false;', now) is not None,
      'not every ELD is compared with what the listing read')
clear = block(mon, mon.index('void CALSAHControlMonitor::Clear()'))
check('elds-forgotten-with-the-listing', clear is not None and 'm_elds.clear();' in clear,
      'the ELDs of a listing before are kept for the next')
susp = ae.index('    case AE_TOP_CONFIGURED_SUSPEND:')
reset = ae[susp:ae.index('    case AE_TOP_CONFIGURED_PLAY:', susp) if '    case AE_TOP_CONFIGURED_PLAY:' in ae[susp:] else len(ae)]
check('suspended-eld-kept-as-said', re.search(
      r'if \(signal == CActiveAEControlProtocol::DEVICECHANGE && msg->data &&\s+\(msg->data\[0\] == '
      r'DEVICE_CHANGE_ELD_AS_LISTED \|\|\s+msg->data\[0\] == DEVICE_CHANGE_ELD_CHANGED\)\)\s+'
      r'm_extSuspendedChanges\.Eld\(msg->data\[0\] == DEVICE_CHANGE_ELD_AS_LISTED\);\s+else\s+'
      r'm_extSuspendedChanges\.Device\(\);', reset) is not None,
      'while suspended, an ELD event is not kept with whether it is as listed, or another change not as a device change')
check('reset-lists-again-only-where-changed', re.search(
      r'if \(!displayReset \|\| m_extSuspendedChanges\.ListAgain\(\)\)\s+\{\s+m_controlPort\.PurgeOut\(CActiveAEControl'
      r'Protocol::DEVICECHANGE\);', reset) is not None and 'm_extSuspendedChanges.Reset();' in reset,
      'the display back does not list the outputs exactly where something changed')
check('eld-event-elsewhere-a-device-change', 'void CActiveAE::EldChange(bool asListed)' in ae and re.search(
      r'm_controlPort\.SendOutMessage\(CActiveAEControlProtocol::DEVICECHANGE, &mark, sizeof\(mark\)\);', ae) is not None,
      'an ELD event is not sent as a device change event (every other state acts on it as one)')

print('DEVCHANGE-SRC PASSED' if fails == 0 else 'DEVCHANGE-SRC FAILED')
sys.exit(1 if fails else 0)
