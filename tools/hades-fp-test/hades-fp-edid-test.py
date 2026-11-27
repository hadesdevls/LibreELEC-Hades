#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# hades-fp-edid-test.py: a test of HDMI 3D frame packing on a display that has no 3D (developers only; nothing of it is
# part of the image, and nothing of it lasts past the next start). It gives the kernel, for one connector, the EDID of
# the display connected there with 3D_present added to its HDMI vendor block (HDMI 1.4b, 8.3.2): the display then
# declares the 3D formats that HDMI makes mandatory, frame packing at 1080p24 among them (where its EDID lists VIC 32).
# The kernel's own way is used: an EDID given to drm.edid_firmware is taken in place of the display's by every driver
# that asks the kernel for an override when it probes, nvidia-drm among them (nvidia-drm-connector.c,
# drm_edid_override_connector_update(): the EDID is then NVKMS's too, overrideEdid). The file is written to the
# firmware folder of the running system (/usr/lib/firmware is /run/kernel-overlays/firmware, a tmpfs), the parameter of
# the running kernel is set, and the connector is probed again: a start of the system clears all three.
#   make [connector]   the EDID of the display, with 3D_present, into edid/hades-fp-test.bin of the firmware folder
#   on [connector]     drm.edid_firmware=<connector>:edid/hades-fp-test.bin, and the connector probed again
#   off                drm.edid_firmware names no connector, and every connector probed again: the display's own EDID
#   show               what the EDID of each connector says of 3D now, and the parameter
# The connector is that of /sys/class/drm/card*-<connector> (HDMI-A-1...); without one, the only one that is connected.
# Each connector is shown with the name that its display gives itself in its EDID (HDMI-A-3 (LG TV SSCR2)), so that
# the TV can be told from a monitor; a device between the PC and the display (a sound bar, a receiver) passes the
# display's name on.
# Restart Kodi after "on" and after "off" (systemctl restart kodi): it reads the modes of the display as it starts.
# Run as root (the parameter and the probe are root's). Nothing is written anywhere but those three places.
import glob
import os
import sys

FIRMWARE = '/usr/lib/firmware'
NAME = 'edid/hades-fp-test.bin'
PARAM = '/sys/module/drm/parameters/edid_firmware'
OFF = 'hades-fp-test-off:none'  # (a connector that no driver has: no EDID is loaded, and nothing is said)
HDMI_OUI = b'\x03\x0c\x00'
MANDATORY = {32: '1080p24', 4: '720p60', 19: '720p50'}


class EdidError(Exception):
    pass


def checksum(block):
    return (-sum(block[:127])) & 0xff


def blocks(edid):
    '''The CTA-861 extension blocks of an EDID: [(index, block)]'''
    if len(edid) < 128 or edid[:8] != b'\x00\xff\xff\xff\xff\xff\xff\x00':
        raise EdidError('not an EDID (its header is not there)')
    found = []
    for k in range(1, len(edid) // 128):
        b = edid[128 * k:128 * (k + 1)]
        if b[0] == 0x02 and b[1] >= 3 and 4 <= b[2] <= 127:
            found.append((k, b))
    return found


def data_blocks(b):
    '''The data blocks of a CTA-861 block: [(offset of its header byte, tag, payload)]'''
    out, i, end = [], 4, b[2]
    while i < end:
        tag, n = b[i] >> 5, b[i] & 31
        if i + 1 + n > end:
            raise EdidError('a data block of the CTA-861 extension runs past its end')
        out.append((i, tag, bytes(b[i + 1:i + 1 + n])))
        i += 1 + n
    return out


def svds(b):
    vics = []
    for _, tag, p in data_blocks(b):
        if tag == 2:
            for x in p:
                vic = x & 0x7f if 1 <= (x & 0x7f) <= 64 else x
                vics.append(vic)
    return vics


def with_3d_present(payload):
    '''The HDMI vendor block's payload (after its header byte, the OUI first) with HDMI_Video_present and 3D_present
    set; the rest as it was. Returns (payload, what changed).'''
    p = bytearray(payload)
    if len(p) < 5 or bytes(p[:3]) != HDMI_OUI:
        raise EdidError('not an HDMI vendor block')
    while len(p) < 8:  # (the flags, the TMDS clock and the byte of the latency and video flags: none means 0)
        p.append(0)
    flags = p[7]
    at = 8 + (2 if flags & 0x80 else 0) + (2 if flags & 0x40 else 0)
    if len(p) < at:
        raise EdidError('the HDMI vendor block ends inside its latency fields')
    if flags & 0x20:
        if len(p) <= at:
            raise EdidError('HDMI_Video_present is set, but the block ends before its byte')
        if p[at] & 0x80:
            return bytes(p), 'nothing: 3D_present is set already'
        p[at] |= 0x80
        return bytes(p), '3D_present set in the byte of HDMI_Video_present'
    p[7] = flags | 0x20
    # (3D_present, no 3D_Multi_present, no image size; no HDMI_VIC, no 3D structure: the mandatory formats only)
    p[at:at] = b'\x80\x00'
    return bytes(p), 'HDMI_Video_present and 3D_present added (2 bytes)'


def add_3d_present(edid):
    '''The EDID with 3D_present in its first HDMI vendor block (the one that drivers read), each checksum made again.
    Returns (edid, what changed, the mandatory 3D formats that it then declares).'''
    edid = bytearray(edid)
    for k, b in blocks(edid):
        for at, tag, p in data_blocks(b):
            if tag != 3 or p[:3] != HDMI_OUI:
                continue
            new, said = with_3d_present(p)
            if len(new) > 31:
                raise EdidError('the HDMI vendor block would be longer than 31 bytes')
            grow = len(new) - len(p)
            nb = bytearray(b)
            d = nb[2]
            # (the detailed timings after the data blocks, then padding to the checksum: grow takes padding)
            dtds = d
            while dtds + 18 <= 127 and (nb[dtds] or nb[dtds + 1]):
                dtds += 18
            if grow and any(nb[127 - grow:127]) or 127 - dtds < grow:
                raise EdidError('the CTA-861 extension has no room for %d more bytes' % grow)
            body = nb[4:at] + bytes([(3 << 5) | len(new)]) + new + nb[at + 1 + len(p):127 - grow]
            nb[4:127] = body
            nb[2] = d + grow
            nb[127] = checksum(nb)
            edid[128 * k:128 * (k + 1)] = nb
            vics = svds(nb)
            return bytes(edid), said, [MANDATORY[v] for v in (32, 4, 19) if v in vics]
    raise EdidError('the EDID has no HDMI vendor block: it is no HDMI display')


def declares_3d(edid):
    '''3D_present of the first HDMI vendor block, as drivers read it'''
    for _, b in blocks(edid):
        for _, tag, p in data_blocks(b):
            if tag == 3 and p[:3] == HDMI_OUI:
                if len(p) < 8 or not p[7] & 0x20:
                    return False
                at = 8 + (2 if p[7] & 0x80 else 0) + (2 if p[7] & 0x40 else 0)
                return len(p) > at and bool(p[at] & 0x80)
    return False


def monitor_name(edid):
    '''The name that the display gives itself: the display product name descriptor (tag 0xFC) of the base block
    (EDID 1.4, 3.10.3.4: up to 13 characters, ended by a line feed and padded with spaces); "" where there is none'''
    if len(edid) < 128 or edid[:8] != b'\x00\xff\xff\xff\xff\xff\xff\x00':
        return ''
    for at in (54, 72, 90, 108):
        d = edid[at:at + 18]
        if d[0] == 0 and d[1] == 0 and d[3] == 0xfc:
            text = d[5:18].split(b'\n', 1)[0].decode('cp437', 'replace')
            return ''.join(c for c in text if c.isprintable()).strip()
    return ''


def label(folder):
    '''A connector as it is named here, with the name of its display where its EDID gives one: HDMI-A-3 (LG TV SSCR2)'''
    connector = os.path.basename(folder).split('-', 1)[1]
    try:
        name = monitor_name(open(os.path.join(folder, 'edid'), 'rb').read())
    except OSError:
        name = ''
    return '%s (%s)' % (connector, name) if name else connector


def connector_folder(name=None):
    folders = sorted(glob.glob('/sys/class/drm/card*-*'))
    if name:
        found = [f for f in folders if os.path.basename(f).split('-', 1)[1] == name]
        if not found:
            raise EdidError('no connector %s (there are: %s)' % (name, ', '.join(
                label(f) for f in folders) or 'none'))
        return found[0]
    connected = [f for f in folders if open(os.path.join(f, 'status')).read().strip() == 'connected']
    if len(connected) != 1:
        raise EdidError('%d connectors are connected: name one (%s)' % (
            len(connected), ', '.join(label(f) for f in connected) or 'none'))
    return connected[0]


def probe(folder):
    with open(os.path.join(folder, 'status'), 'w') as f:
        f.write('detect')


def main(argv):
    if not argv or argv[0] not in ('make', 'on', 'off', 'show'):
        print(__doc__ if __doc__ else 'usage: hades-fp-edid-test.py make|on|off|show [connector]')
        return 2
    what, name = argv[0], (argv[1] if len(argv) > 1 else None)
    try:
        if what == 'show':
            param = open(PARAM).read().strip()
            print('drm.edid_firmware: %s' % (param or '(none)'))
            for f in sorted(glob.glob('/sys/class/drm/card*-*')):
                e = open(os.path.join(f, 'edid'), 'rb').read()
                if e:
                    name = monitor_name(e)
                    print('%s%s: EDID of %d bytes, 3D_present %s' % (
                        os.path.basename(f), ' (%s)' % name if name else '', len(e), 'yes' if declares_3d(e) else 'no'))
            return 0
        if what == 'off':
            with open(PARAM, 'w') as f:
                f.write(OFF)
            for f in sorted(glob.glob('/sys/class/drm/card*-*')):
                if open(os.path.join(f, 'status')).read().strip() == 'connected':
                    probe(f)
            print('off: the displays\' own EDIDs again (restart Kodi: systemctl restart kodi)')
            return 0
        folder = connector_folder(name)
        connector = os.path.basename(folder).split('-', 1)[1]
        if what == 'make':
            if open(PARAM).read().strip().startswith(connector + ':'):
                raise EdidError('the test EDID is on for %s: "off" first, so that the display\'s own is read' % connector)
            own = open(os.path.join(folder, 'edid'), 'rb').read()
            new, said, formats = add_3d_present(own)
            if not declares_3d(new):
                raise EdidError('3D_present is not read back from the EDID made')
            os.makedirs(os.path.join(FIRMWARE, 'edid'), exist_ok=True)
            with open(os.path.join(FIRMWARE, NAME), 'wb') as f:
                f.write(new)
            print('%s: %s; it declares frame packing at %s; written to %s (%d bytes)' % (
                connector, said, ', '.join(formats) or 'NOTHING (no 1080p24, 720p60 or 720p50 in its list)',
                os.path.join(FIRMWARE, NAME), len(new)))
            return 0 if formats else 1
        if what == 'on':
            if not os.path.isfile(os.path.join(FIRMWARE, NAME)):
                raise EdidError('no %s: "make" first' % os.path.join(FIRMWARE, NAME))
            with open(PARAM, 'w') as f:
                f.write('%s:%s' % (connector, NAME))
            probe(folder)
            now = open(os.path.join(folder, 'edid'), 'rb').read()
            print('%s: the test EDID is on (3D_present %s in the EDID of the connector now); restart Kodi: '
                  'systemctl restart kodi' % (connector, 'yes' if declares_3d(now) else 'NO'))
            return 0 if declares_3d(now) else 1
    except (EdidError, OSError) as e:
        print('hades-fp-edid-test: %s' % e)
        return 1
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
