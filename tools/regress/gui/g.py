#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Runs shell commands in the guest through its second serial port (the root shell that autostart.sh opens).
# The folder of the sockets is the first argument.
# Usage: g.py <folder with sh.sock> [-t seconds] 'command'      (output on stdout)
#        g.py <folder> [-t s] -get <guest file> <host file>     (base64 between markers, checked by the SHA-256 the
#                                                                guest computes of what it sends; prints "... OK")
#        g.py <folder> -put <host file> <guest file>
# One invocation = one connection (closing it ends the guest shell, which restarts).
import os, socket, sys, time, base64, hashlib, uuid, re
W = os.path.join(sys.argv[1], '') if len(sys.argv) > 1 else ''
ANSI = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
S = None


def run(cmd, timeout=60):
    global S
    if S is None:
        S = socket.socket(socket.AF_UNIX)
        S.connect(W + 'sh.sock')
        S.settimeout(2)
        time.sleep(0.5)
    hx = uuid.uuid4().hex[:8]
    tag = 'DONE' + hx
    # the shell echoes what it reads; the end marker is typed as DO""NE<hex> so only its output spells DONE<hex>
    S.sendall(('\n' + cmd + '\necho DO""NE' + hx + '\n').encode())
    buf = b''
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            d = S.recv(65536)
        except socket.timeout:
            continue
        if not d:
            break
        buf += d
        if tag.encode() in buf:
            break
    out = buf.decode('utf-8', 'replace')
    return ANSI.sub('', out.split(tag)[0] if tag in out else out + '\n[TIMEOUT]')


def parse_get(out, hx):
    """The bytes between the guest's own markers, checked against the SHA-256 the guest computed of the same bytes it
    encoded. Nothing outside the markers is taken: the shell echoes the command, and the terminal wraps that echo, so a
    piece of it (the end of a file name, "png") can stand alone on a line that looks like base64."""
    o = out.replace(chr(13), '')
    b, e = 'BEGIN' + hx + chr(10), chr(10) + 'END' + hx
    if b not in o or e not in o:
        return None, 'no markers'
    body = o.split(b, 1)[1].split(e, 1)[0].split(chr(10))
    want, lines = body[0].strip(), [l.strip() for l in body[1:] if l.strip()]
    if not re.fullmatch(r'[0-9a-f]{64}', want):
        return None, 'no hash from the guest'
    if not all(re.fullmatch(r'[A-Za-z0-9+/=]+', l) for l in lines):
        return None, 'a line that is not base64'
    try:
        data = base64.b64decode(''.join(lines), validate=True)
    except Exception as x:
        return None, 'base64: %r' % (x,)
    if hashlib.sha256(data).hexdigest() != want:
        return None, 'hash differs'
    return data, 'ok'


def get(guest, host, to=120, tries=6):
    """base64 over the serial line, between markers, checked against the SHA-256 that the guest computes of the bytes
    it encodes (one read of the file: a file still being written, kodi.log, is taken as one snapshot); up to <tries>
    attempts (the line can drop a character)"""
    for attempt in range(tries):
        hx = uuid.uuid4().hex[:8]
        # the markers are written as 'BE'+'GIN...' so that the echo of the command never spells them
        out = run('python3 -c "import base64,hashlib,sys;d=open(sys.argv[1],chr(114)+chr(98)).read();'
                  "sys.stdout.write(chr(10)+'BE'+'GIN%s'+chr(10)+hashlib.sha256(d).hexdigest()+chr(10)"
                  "+base64.encodebytes(d).decode()+chr(10)+'EN'+'D%s'+chr(10))\" " % (hx, hx) + guest, to)
        data, why = parse_get(out, hx)
        if data is not None:
            open(host, 'wb').write(data)
            return len(data), hashlib.sha256(data).hexdigest(), True
        sys.stderr.write('g.py: %s, attempt %d: %s\n' % (guest, attempt + 1, why))
    return 0, '', False


if __name__ == '__main__':
    a = sys.argv[2:]
    to = 60
    if a and a[0] == '-t':
        to = int(a[1])
        a = a[2:]
    if a and a[0] == '-get':
        n, h, ok = get(a[1], a[2], to)
        print('got', a[2], n, h, 'OK' if ok else 'MISMATCH')
    else:
        print(run(a[0], to))
