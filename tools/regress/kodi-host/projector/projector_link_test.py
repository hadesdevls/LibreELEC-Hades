#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""projector_link_test.py: the protocols of the projector control of Hades (xbmc/network/ProjectorLink.cpp) against a
simulator of each projector: JVC D-ILA, Sony ADCP, Sony SDCP and Epson ESC/VP.net. Each simulator is a small TCP
server on 127.0.0.1 that does the handshake of its protocol and answers the commands that Hades sends, as their public
descriptions give them (the sources are named in ProjectorLink.h); what it receives is kept, and each scenario checks
both what the client says and the bytes that reached the "projector". Nothing leaves the host.

usage: projector_link_test.py <projector_cli>       (built from ProjectorLink.cpp and projector_cli.cpp)
Prints "ok: ..." or "FAIL: ..." for each check and "PROJECTOR-LINK PASSED" or "PROJECTOR-LINK FAILED (n)".
"""
import hashlib
import socket
import struct
import subprocess
import sys
import threading
import time

CLI = sys.argv[1]
FAILS = 0
CHECKS = 0
JVC_UNIT = b'\x89\x01'


def check(cond, what):
    global FAILS, CHECKS
    CHECKS += 1
    if cond:
        print('ok: ' + what)
    else:
        FAILS += 1
        print('FAIL: ' + what)


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


class Sim(threading.Thread):
    """A projector: a server that handles each connection with handle(conn, index)"""

    def __init__(self, **opts):
        super().__init__(daemon=True)
        self.opts = opts
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(('127.0.0.1', 0))
        self.server.listen(4)
        self.server.settimeout(0.2)
        self.port = self.server.getsockname()[1]
        self.received = []  # every byte received, connection after connection
        self.commands = []  # the commands understood
        self.connections = 0
        self.running = True
        self.start()

    def run(self):
        while self.running:
            try:
                conn, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            self.connections += 1
            conn.settimeout(3)
            try:
                self.handle(conn, self.connections)
            except (OSError, ValueError):
                pass
            finally:
                conn.close()
        self.server.close()

    def recv(self, conn, n):
        data = b''
        while len(data) < n:
            chunk = conn.recv(n - len(data))
            if not chunk:
                raise ValueError('closed')
            data += chunk
        self.received.append(data)
        return data

    def recv_until(self, conn, end):
        data = b''
        while not data.endswith(end):
            chunk = conn.recv(1)
            if not chunk:
                raise ValueError('closed')
            data += chunk
        self.received.append(data)
        return data

    def recv_quiet(self, conn, wait=0.3):
        """what comes within a moment (a field whose length the protocol does not give)"""
        conn.settimeout(wait)
        data = b''
        try:
            while True:
                chunk = conn.recv(256)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        conn.settimeout(3)
        self.received.append(data)
        return data

    def stop(self):
        self.running = False
        self.join(2)

    def got(self):
        return b''.join(self.received)


class Jvc(Sim):
    # opts: auth = none | plain | hash | both (what the projector takes), password, busy, power ('1' on), lens_delay,
    # mute (no acknowledgement of operations), model
    def handle(self, conn, index):
        o = self.opts
        if o.get('busy'):
            conn.sendall(b'PJ_NG')
            return
        conn.sendall(b'PJ_OK')
        tries = 0
        while True:
            req = self.recv(conn, 5)
            if req != b'PJREQ':
                return
            rest = self.recv_quiet(conn)
            tries += 1
            auth = o.get('auth', 'none')
            pw = o.get('password', '')
            plain = b'_' + pw.encode().ljust(16, b'\0')
            hashed = b'_' + sha(pw + 'JVCKWPJ').encode()
            ok = (auth == 'none' or (auth in ('plain', 'both') and rest == plain) or
                  (auth in ('hash', 'both') and rest == hashed))
            self.commands.append(('auth', rest[:1] + (b'(plain)' if rest == plain else b'(hash)' if rest == hashed
                                                       else b'(none)' if not rest else b'(other)')))
            if ok:
                conn.sendall(b'PJACK')
                break
            conn.sendall(b'PJNAK')
            if tries >= 2:
                return
        while True:
            line = self.recv_until(conn, b'\n')
            head, unit, body = line[:1], line[1:3], line[3:-1]
            if unit != JVC_UNIT:
                return
            code = body[:2].decode()
            if head == b'!':
                self.commands.append(('op', body.decode()))
                if o.get('mute'):
                    continue
                if body.startswith(b'IN') and o.get('lens_delay'):
                    time.sleep(o['lens_delay'])
                conn.sendall(b'\x06' + JVC_UNIT + body[:2] + b'\n')
            elif head == b'?':
                self.commands.append(('ref', body.decode()))
                conn.sendall(b'\x06' + JVC_UNIT + body[:2] + b'\n')
                value = {'PW': o.get('power', '1'), 'MD': 'ILAFPJ -- ' + o.get('model', 'B8A2')}.get(code, '0')
                conn.sendall(b'@' + JVC_UNIT + body[:2] + value.encode() + b'\n')


class Adcp(Sim):
    # opts: password (the projector's: '' NOKEY), power ('on'), refuse (picture modes refused), model
    POSITIONS = ['1.85_1', '2.35_1', 'custom1', 'custom2', 'custom3', 'custom4', 'custom5']
    MODES = ['cinema_film1', 'cinema_film2', 'reference', 'tv', 'photo', 'game', 'brt_cinema', 'brt_tv', 'user',
             'user1', 'user2', 'user3']

    def handle(self, conn, index):
        o = self.opts
        pw = o.get('password', '')
        if not pw:
            conn.sendall(b'NOKEY\r\n')
        else:
            challenge = 'r4Nd0m%02d' % index
            conn.sendall(challenge.encode() + b'\r\n')
            answer = self.recv_until(conn, b'\r\n')[:-2].decode()
            if answer != sha(challenge + pw):
                self.commands.append(('auth', 'wrong'))
                conn.sendall(b'err_auth\r\n')
                return
            self.commands.append(('auth', 'ok'))
            conn.sendall(b'ok\r\n')
        line = self.recv_until(conn, b'\r\n')[:-2].decode()
        self.commands.append(('cmd', line))
        if line == 'power_status ?':
            reply = '"%s"' % o.get('power', 'on')
        elif line == 'modelname ?':
            reply = '"%s"' % o.get('model', 'VPL-XW5000')
        elif line.startswith('pic_pos_sel "'):
            reply = 'ok' if line[13:-1] in self.POSITIONS else 'err_val'
        elif line.startswith('picture_mode "'):
            mode = line[14:-1]
            reply = 'ok' if mode in self.MODES and mode not in o.get('refuse', ()) else 'err_val'
        else:
            reply = 'err_cmd'
        conn.sendall(reply.encode() + b'\r\n')


class Sdcp(Sim):
    # opts: community ('SONY'), power (3: on), refuse (presets refused), other_item (the answer names another
    # item), cut (only the first n bytes of the answer, then the connection closed), hold (... and kept open)
    def handle(self, conn, index):
        o = self.opts
        head = self.recv(conn, 10)
        data = self.recv(conn, head[9]) if head[9] else b''
        community = head[2:6].decode('latin-1')
        item = struct.unpack('>H', head[7:9])[0]
        action = head[6]
        self.commands.append(('req', action, item, data, community))

        def reply(ok, value):
            named = struct.pack('>H', item + 1) if o.get('other_item') else head[7:9]
            answer = b'\x02\x0a' + head[2:6] + bytes([1 if ok else 0]) + named + b'\x02' + struct.pack('>H', value)
            if o.get('cut'):
                conn.sendall(answer[:o['cut']])
                if o.get('hold'):
                    time.sleep(o['hold'])
                return
            conn.sendall(answer)
        if head[0] != 2 or head[1] != 0x0A:
            return
        if community != o.get('community', 'SONY'):
            reply(False, 0x0201)  # (the error the simulator gives for a wrong community: an assumption)
            return
        if action == 1 and item == 0x0102:
            reply(True, o.get('power', 3))
        elif action == 0 and item == 0x0066:
            value = struct.unpack('>H', data)[0]
            reply(value <= 4, 0 if value <= 4 else 0x0104)
        elif action == 0 and item == 0x0002:
            value = struct.unpack('>H', data)[0]
            ok = value <= 8 and value not in o.get('refuse', ())
            reply(ok, 0 if ok else 0x0103)
        else:
            reply(False, 0x0101)


class Epson(Sim):
    # opts: password (the projector's), power ('01'), refuse (CMODE codes refused), lens_delay
    def handle(self, conn, index):
        o = self.opts
        head = self.recv(conn, 16)
        if head[:10] != b'ESC/VP.net' or head[10] != 0x10 or head[11] != 3:
            return
        extra = self.recv(conn, 18 * head[15]) if head[15] else b''
        given = None
        if head[15] and extra[0] == 1:
            given = extra[2:18].rstrip(b'\0').decode()
        pw = o.get('password', '')
        status = 0x20
        if pw and given is None:
            status = 0x41
        elif pw and given != pw:
            status = 0x43
        self.commands.append(('connect', status, given))
        conn.sendall(b'ESC/VP.net\x10\x03\x00\x00' + bytes([status, 0]))
        if status != 0x20:
            return
        while True:
            line = self.recv_until(conn, b'\r')[:-1].decode()
            self.commands.append(('cmd', line))
            if line == 'PWR?':
                conn.sendall(('PWR=%s\r:' % o.get('power', '01')).encode())
            elif line.startswith('POPLP '):
                if o.get('lens_delay'):
                    time.sleep(o['lens_delay'])
                conn.sendall(b':' if 1 <= int(line[6:], 16) <= 10 else b'ERR\r:')
            elif line.startswith('CMODE '):
                conn.sendall(b'ERR\r:' if line[6:] in o.get('refuse', ()) else b':')
            else:
                conn.sendall(b'ERR\r:')


def cli(kind, port, password, *ops):
    p = subprocess.run([CLI, kind, str(port), password or '-'] + list(ops), capture_output=True, text=True, timeout=120)
    out = p.stdout
    results = [l[7:] for l in out.splitlines() if l.startswith('RESULT ')]
    values = dict(l[6:].split(' ', 1) for l in out.splitlines() if l.startswith('VALUE '))
    logs = [l[4:] for l in out.splitlines() if l.startswith('LOG ')]
    if p.returncode != 0:
        check(False, '%s exits 0 (exit %d): %s' % (CLI, p.returncode, p.stderr[-300:]))
    return results, values, logs, out


def failure(result):
    """'lens done' -> 'done'; 'open the password was refused | PJNAK' -> 'the password was refused'"""
    return result.split(' ', 1)[1].split(' | ')[0]


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


# ---------------------------------------------------------------------------------------------------- JVC
s = Jvc()
r, v, logs, out = cli('jvc', s.port, '', 'open', 'power', 'model', 'lens=2', 'mode=12')
s.stop()
check([failure(x) for x in r] == ['done'] * 5, 'JVC without a password: open, power, model, lens, mode done (%s)' % r)
check(v.get('power') == 'on' and v.get('model') == 'B8A2', 'JVC: power on and model B8A2 read (%s)' % v)
check(s.commands[0] == ('auth', b'(none)'), 'JVC: PJREQ without a password (%s)' % (s.commands[:1],))
check(('op', 'INML1') in s.commands, 'JVC: lens memory 2 is INML1 (%s)' % s.commands)
check(('op', 'PMPM0C') in s.commands, 'JVC: picture mode User 1 is PMPM0C')
check(b'!\x89\x01INML1\n' in s.got() and b'?\x89\x01PW\n' in s.got(), 'JVC: the bytes of an operation and a reference')
check(s.connections == 1, 'JVC: one connection for the session (%d)' % s.connections)
check('SHA256-SELFTEST ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' in out,
      'the test\'s SHA-256 gives the FIPS 180-4 value of "abc"')

s = Jvc(auth='plain', password='secret12')
r, v, logs, out = cli('jvc', s.port, 'secret12', 'open', 'power')
s.stop()
check([failure(x) for x in r] == ['done', 'done'], 'JVC, password in plain: let in (%s)' % r)
check(s.commands[0] == ('auth', b'_(plain)'), 'JVC: "PJREQ_" and the password padded to 16 bytes')
check('HASHED no' in out, 'JVC: the plain form was the one let in')
check(not any('secret12' in l for l in logs), 'JVC: the password is in no line of the log')

s = Jvc(auth='hash', password='secret12')
r, v, logs, out = cli('jvc', s.port, 'secret12', 'open', 'power')
s.stop()
check([failure(x) for x in r] == ['done', 'done'], 'JVC of 2024 on: the hashed password after a PJNAK (%s)' % r)
check([c[1] for c in s.commands if c[0] == 'auth'] == [b'_(plain)', b'_(hash)'],
      'JVC: the plain form, then sha256(password + "JVCKWPJ") in hexadecimal on the same connection')
check(s.connections == 1, 'JVC: on the same connection (%d)' % s.connections)
check('HASHED yes' in out, 'JVC: the hashed form is known for the next session')
hashed = sha('secret12JVCKWPJ')
check(not any('secret12' in l or hashed in l for l in logs), 'JVC: neither the password nor its hash in the log')

s = Jvc(auth='hash', password='secret12')
r, v, logs, out = cli('jvc', s.port, 'secret12', 'hash', 'open')
s.stop()
check([c[1] for c in s.commands if c[0] == 'auth'] == [b'_(hash)'], 'JVC: the form let in before is tried first')

s = Jvc(auth='both', password='secret12')
r, v, logs, out = cli('jvc', s.port, 'wrong', 'open', 'power')
s.stop()
check(failure(r[0]) == 'the password was refused', 'JVC: a wrong password is refused (%s)' % r[0])
check(failure(r[1]) == 'no connection', 'JVC: nothing more is sent after it (%s)' % r[1])

s = Jvc(auth='plain', password='secret12')
r, v, logs, out = cli('jvc', s.port, '', 'open')
s.stop()
check(failure(r[0]) == 'a password is needed', 'JVC: a projector that wants a password, none set (%s)' % r[0])

s = Jvc(busy=True)
r, v, logs, out = cli('jvc', s.port, '', 'open')
s.stop()
check(failure(r[0]) == 'busy with another connection', 'JVC: PJ_NG is busy (%s)' % r[0])

s = Jvc(power='0')
r, v, logs, out = cli('jvc', s.port, '', 'open', 'power')
s.stop()
check(v.get('power') == 'standby', 'JVC: power 0 is standby (%s)' % v)

s = Jvc(lens_delay=1.5)
t0 = time.monotonic()
r, v, logs, out = cli('jvc', s.port, '', 'open', 'lens=1')
check(failure(r[1]) == 'done' and time.monotonic() - t0 >= 1.4,
      'JVC: a lens memory is answered once the lens has moved, and waited for (%s)' % r[1])
r, v, logs, out = cli('jvc', s.port, '', 'lens-ms=500', 'open', 'lens=3')
s.stop()
check(failure(r[1]) == 'no answer in time', 'JVC: a lens that does not answer within its timeout (%s)' % r[1])

s = Jvc(mute=True)
t0 = time.monotonic()
r, v, logs, out = cli('jvc', s.port, '', 'mode-ms=700', 'open', 'mode=0')
s.stop()
check(failure(r[1]) == 'no answer in time' and time.monotonic() - t0 < 5,
      'JVC: a mode without an acknowledgement ends at its timeout (%s)' % r[1])

r, v, logs, out = cli('jvc', free_port(), '', 'open')
check(failure(r[0]) == 'no connection', 'JVC: a port where nothing listens is no connection (%s)' % r[0])

s = Jvc()
r, v, logs, out = cli('jvc', s.port, '', 'open', 'lens=11', 'mode=200')
s.stop()
check([failure(x) for x in r[1:]] == ['not available with this protocol'] * 2 and
      not any(c[0] == 'op' for c in s.commands), 'JVC: a memory or a mode it has not is not sent (%s)' % r)

# Hades 1.2.3: the anamorphic mode, INVS of JVC's External Command Communication Specification (NZ800/NZ900 Ver. 2.0,
# table 4-58): "0" Off, "1" A, "2" B, "3" C, "4" D; after a lens memory in the same talk, as the control sends it
s = Jvc()
r, v, logs, out = cli('jvc', s.port, '', 'open', 'lens=2', 'anamorphic=1', 'anamorphic=0', 'anamorphic=4')
s.stop()
check([failure(x) for x in r] == ['done'] * 5, 'JVC: open, a lens memory, then anamorphic A, Off and D, done (%s)' % r)
check([c[1] for c in s.commands if c[0] == 'op'] == ['INML1', 'INVS1', 'INVS0', 'INVS4'],
      'JVC: INML1, then INVS1, INVS0 and INVS4 (%s)' % s.commands)
check(b'!\x89\x01INVS1\n' in s.got() and s.connections == 1, 'JVC: the bytes of INVS1, on the one connection')
s = Jvc()
r, v, logs, out = cli('jvc', s.port, '', 'open', 'anamorphic=5', 'anamorphic=-1')
s.stop()
check([failure(x) for x in r[1:]] == ['not available with this protocol'] * 2 and
      not any(c[0] == 'op' for c in s.commands), 'JVC: a value past D, or none, is not sent (%s)' % r)
for kind, sim in (('adcp', Adcp), ('sdcp', Sdcp), ('epson', Epson)):
    s = sim()
    r, v, logs, out = cli(kind, s.port, '', 'open', 'anamorphic=1')
    s.stop()
    sent = [c for c in s.commands if c[0] in ('cmd', 'req') and 'power' not in str(c) and c[2:3] != (0x0102,)]
    check(failure(r[-1]) == 'not available with this protocol' and not sent,
          '%s: no anamorphic command, nothing sent for it (%s)' % (kind, r[-1]))

# ---------------------------------------------------------------------------------------------- Sony ADCP
s = Adcp()
r, v, logs, out = cli('adcp', s.port, '', 'open', 'power', 'model', 'lens=2', 'mode=1')
s.stop()
check([failure(x) for x in r] == ['done'] * 5, 'ADCP without a password (NOKEY): all done (%s)' % r)
check(v.get('model') == 'VPL-XW5000' and v.get('power') == 'on', 'ADCP: model and power read (%s)' % v)
check(('cmd', 'pic_pos_sel "2.35_1"') in s.commands, 'ADCP: memory 2 is the picture position 2.35:1')
check(('cmd', 'picture_mode "cinema_film1"') in s.commands, 'ADCP: mode 1 is cinema_film1')
check(b'pic_pos_sel "2.35_1"\r\n' in s.got(), 'ADCP: a command ends in \\r\\n')

s = Adcp(password='Projector')
r, v, logs, out = cli('adcp', s.port, 'Projector', 'open', 'lens=1')
s.stop()
check([failure(x) for x in r] == ['done', 'done'] and ('auth', 'ok') in s.commands,
      'ADCP: sha256(challenge + password) in hexadecimal lets it in (%s)' % r)
check(not any('Projector' in l for l in logs), 'ADCP: the password is in no line of the log')

s = Adcp(password='Projector')
r, v, logs, out = cli('adcp', s.port, 'wrong', 'open')
s.stop()
check(failure(r[0]) == 'the password was refused', 'ADCP: err_auth is a refused password (%s)' % r[0])

s = Adcp(password='Projector')
r, v, logs, out = cli('adcp', s.port, '', 'open')
s.stop()
check(failure(r[0]) == 'a password is needed' and len(s.received) == 0,
      'ADCP: a challenge with no password set: nothing is sent (%s)' % r[0])

s = Adcp(refuse=('user3',))
r, v, logs, out = cli('adcp', s.port, '', 'open', 'mode=12')
s.stop()
check(failure(r[1]) == 'the command was refused' and 'err_val' in r[1], 'ADCP: err_val is a refusal (%s)' % r[1])

s = Adcp(power='standby')
r, v, logs, out = cli('adcp', s.port, '', 'open', 'power')
s.stop()
check(v.get('power') == 'standby', 'ADCP: "standby" (%s)' % v)

# ---------------------------------------------------------------------------------------------- Sony SDCP
s = Sdcp()
r, v, logs, out = cli('sdcp', s.port, '', 'open', 'power', 'model', 'lens=2', 'mode=0')
s.stop()
check([failure(x) for x in r] == ['done', 'done', 'not available with this protocol', 'done', 'done'],
      'SDCP: open, power, (no model), lens, mode (%s)' % r)
reqs = [c for c in s.commands if c[0] == 'req']
check((1, 0x0102, b'', 'SONY') in [c[1:] for c in reqs], 'SDCP: power read with the community SONY')
check((0, 0x0066, b'\x00\x01', 'SONY') in [c[1:] for c in reqs], 'SDCP: memory 2 is picture position 1 (2.35:1)')
check((0, 0x0002, b'\x00\x00', 'SONY') in [c[1:] for c in reqs], 'SDCP: mode 0 is the calibration preset Cinema Film 1')
check(b'\x02\x0aSONY\x00\x00\x66\x02\x00\x01' in s.got(), 'SDCP: the bytes of a request that sets')

s = Sdcp(community='ABCD')
r, v, logs, out = cli('sdcp', s.port, 'ABCD', 'power')
s.stop()
check(failure(r[0]) == 'done' and not any('ABCD' in l for l in logs), 'SDCP: a community of its own, not in the log')

s = Sdcp(community='ABCD')
r, v, logs, out = cli('sdcp', s.port, '', 'power')
s.stop()
check(failure(r[0]) == 'the password was refused', 'SDCP: a community not its own (%s)' % r[0])

# the projector says the community back in the header of its answer: an answer that is not of the protocol, or
# that comes only in part, is said in the detail of its failure (kodi.log, the notice of the test) without it
s = Sdcp(community='ABCD', other_item=True)
r, v, logs, out = cli('sdcp', s.port, 'ABCD', 'power')
s.stop()
check(failure(r[0]) == 'an answer that is not of its protocol' and '(community)' in r[0] and
      not any('ABCD' in x for x in r + logs), 'SDCP: an answer of another item, the community not in its detail (%s)' % r)
s = Sdcp(community='ABCD', cut=7)
r, v, logs, out = cli('sdcp', s.port, 'ABCD', 'power')
s.stop()
check(failure(r[0]) == 'no answer in time' and 'closed the connection' in r[0] and '(community)' in r[0] and
      not any('ABCD' in x for x in r + logs), 'SDCP: an answer closed in its header, the community not said (%s)' % r)
s = Sdcp(community='ABCD', cut=8, hold=1.5)
r, v, logs, out = cli('sdcp', s.port, 'ABCD', 'answer-ms=500', 'power')
s.stop()
check(failure(r[0]) == 'no answer in time' and 'cut short' in r[0] and '(community)' in r[0] and
      not any('ABCD' in x for x in r + logs), 'SDCP: an answer cut short in its header, the community not said (%s)' % r)

s = Sdcp(refuse=(7,))
r, v, logs, out = cli('sdcp', s.port, '', 'mode=7')
s.stop()
check(failure(r[0]) == 'the command was refused' and '0x0103' in r[0], 'SDCP: a preset refused, with its code (%s)' % r[0])

s = Sdcp(power=0)
r, v, logs, out = cli('sdcp', s.port, '', 'power')
s.stop()
check(v.get('power') == 'standby', 'SDCP: power 0 is standby (%s)' % v)

# ---------------------------------------------------------------------------------------------------- Epson
s = Epson()
r, v, logs, out = cli('epson', s.port, '', 'open', 'power', 'lens=3', 'mode=21')
s.stop()
check([failure(x) for x in r] == ['done'] * 4, 'Epson without a password: all done (%s)' % r)
check(s.commands[0] == ('connect', 0x20, None), 'Epson: ESC/VP.net connect, no password header')
check(('cmd', 'POPLP 03') in s.commands and ('cmd', 'CMODE 15') in s.commands,
      'Epson: lens memory 3 is POPLP 03, mode 0x15 (Cinema) is CMODE 15 (%s)' % s.commands)
check(s.got().startswith(b'ESC/VP.net\x10\x03\x00\x00\x00\x00'), 'Epson: the 16 bytes of the connect request')

s = Epson(password='pw1234')
r, v, logs, out = cli('epson', s.port, 'pw1234', 'open', 'power')
s.stop()
check([failure(x) for x in r] == ['done', 'done'] and s.commands[0] == ('connect', 0x20, 'pw1234'),
      'Epson: the password header (0x01, 0x01, 16 bytes) lets it in (%s)' % r)
check(not any('pw1234' in l for l in logs), 'Epson: the password is in no line of the log')

s = Epson(password='pw1234')
r, v, logs, out = cli('epson', s.port, '', 'open')
s.stop()
check(failure(r[0]) == 'a password is needed', 'Epson: status 0x41 (%s)' % r[0])

s = Epson(password='pw1234')
r, v, logs, out = cli('epson', s.port, 'nope', 'open')
s.stop()
check(failure(r[0]) == 'the password was refused', 'Epson: status 0x43 (%s)' % r[0])

s = Epson(refuse=('22',))
r, v, logs, out = cli('epson', s.port, '', 'open', 'mode=34')
s.stop()
check(failure(r[1]) == 'the command was refused', 'Epson: ERR is a refusal (%s)' % r[1])

s = Epson(power='04')
r, v, logs, out = cli('epson', s.port, '', 'open', 'power')
s.stop()
check(v.get('power') == 'standby', 'Epson: PWR=04 is standby (%s)' % v)

print('%d checks' % CHECKS)
print('PROJECTOR-LINK PASSED' if FAILS == 0 else 'PROJECTOR-LINK FAILED (%d)' % FAILS)
sys.exit(1 if FAILS else 0)
