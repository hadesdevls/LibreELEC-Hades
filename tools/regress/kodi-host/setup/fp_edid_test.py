# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# fp_edid_test.py (1.2.3, issue #33): tools/hades-fp-test/hades-fp-edid-test.py, the test EDID with 3D_present for a
# display without 3D (the owner's test of frame packing on an RTX 4080 with an LG C5): on real EDIDs (an LG C5 direct
# and through a sound bar, a receiver, an older LG) and made ones:
#  - 3D_present read back by a reader of its own (hades_check.edid_3d() of LibreELEC-settings, which Check this setup
#    uses) and by the tool's; every checksum right; nothing else of the EDID changed but the HDMI vendor block, which is
#    one or two bytes longer where HDMI_Video_present was not set, and what follows it moved by that much
#  - which mandatory formats it then declares (1080p24 where VIC 32 is listed)
#  - an EDID with 3D already, one without an HDMI vendor block, one with no room left: said, never a wrong EDID
#  - the parameter of "off" names no connector (nothing loaded, nothing said by the kernel); "on" names the connector
# usage: fp_edid_test.py <tool> <resources/lib of LibreELEC-settings> [<name>=]<edid>...
import importlib.util
import sys

TOOL, LIB, EDIDS = sys.argv[1], sys.argv[2], sys.argv[3:]
spec = importlib.util.spec_from_file_location('fptool', TOOL)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
sys.path.insert(0, LIB)
import hades_check as hc  # noqa: E402

fails = 0


def check(ok, text):
    global fails
    fails += 0 if ok else 1
    print(('ok:   ' if ok else 'FAIL: ') + text)


def sums_right(e):
    return all((sum(e[128 * k:128 * (k + 1)]) & 0xff) == 0 for k in range(len(e) // 128))


def vsdb(e):
    for k, b in tool.blocks(e):
        for at, tag, p in tool.data_blocks(b):
            if tag == 3 and p[:3] == tool.HDMI_OUI:
                return k, at, p
    return None


def made(vsdb_payload, more=b'', dtds=1, pad=True):
    '''an EDID: a base block and a CTA-861 extension with a video block (VIC 32, 4, 16), the HDMI vendor block
    vsdb_payload (after its OUI) and more, then dtds detailed timings'''
    base = bytearray(128)
    base[0:8] = b'\x00\xff\xff\xff\xff\xff\xff\x00'
    base[126] = 1
    base[127] = tool.checksum(base)
    cta = bytearray(128)
    cta[0], cta[1] = 0x02, 0x03
    blocks = bytes([0x40 | 3, 32, 4, 16])
    payload = tool.HDMI_OUI + bytes(vsdb_payload)
    blocks += bytes([0x60 | len(payload)]) + payload + more
    cta[4:4 + len(blocks)] = blocks
    cta[2] = 4 + len(blocks)
    at = cta[2]
    for _ in range(dtds):
        cta[at:at + 18] = bytes([0x02, 0x3a]) + bytes(range(1, 17))
        at += 18
    if not pad:
        for i in range(at, 127):
            cta[i] = 0x11
    cta[127] = tool.checksum(cta)
    return bytes(base) + bytes(cta)


for arg in EDIDS:
    # (<name>=<file>, the name as inputs-kodi-host.tsv gives it; or a file, named by itself)
    name, path = arg.split('=', 1) if '=' in arg else (arg.split('/')[-1], arg)
    own = open(path, 'rb').read()
    try:
        new, said, formats = tool.add_3d_present(own)
    except tool.EdidError as e:
        check(False, '%s: %s' % (name, e))
        continue
    check(hc.edid_3d(new) and tool.declares_3d(new) and tool.declares_3d(own) == hc.edid_3d(own),
          '%s: 3D_present read back by Check this setup\'s reader and the tool\'s, which read the EDID before alike '
          '(%s)' % (name, said))
    check(not hc.edid_3d(own) or 'lg-c5' not in name, '%s: the LG C5 declares no 3D of its own' % name)
    check(sums_right(new) and len(new) == len(own), '%s: every checksum right, the same size' % name)
    k, at, p = vsdb(own)
    k2, at2, p2 = vsdb(new)
    grow = len(p2) - len(p)
    rest_same = (new[:128 * k] == own[:128 * k] and new[128 * (k + 1):] == own[128 * (k + 1):] and
                 new[128 * k + 3:128 * k + at] == own[128 * k + 3:128 * k + at] and
                 new[128 * k + at2 + 1 + len(p2):128 * (k + 1) - 1] ==
                 own[128 * k + at + 1 + len(p):128 * (k + 1) - 1 - grow] and
                 new[128 * k + 2] == own[128 * k + 2] + grow and k2 == k and at2 == at)
    check(rest_same and grow in (0, 2), '%s: only the HDMI vendor block changed (%d bytes more), the rest moved by that' %
          (name, grow))
    diff = [i for i in range(len(p)) if i < len(p2) and p[i] != p2[i]]
    check(grow != 0 or len(diff) == 1, '%s: where HDMI_Video_present was set, one byte changed: %r' % (name, diff))
    print('     %s: %s; declares %s' % (name, said, ', '.join(formats) or 'none'))
    if 'lg-c5' in name:
        check('1080p24' in formats, '%s: it declares frame packing at 1080p24' % name)

check(any('lg-c5' in a for a in EDIDS), 'an LG C5 among the EDIDs given')

# made ones
a = made([0x10, 0x00, 0x00, 0x78, 0x20, 0x00])  # HDMI_Video_present, no 3D
new, said, formats = tool.add_3d_present(a)
check(hc.edid_3d(new) and 'byte of HDMI_Video_present' in said and formats == ['1080p24', '720p60'],
      'HDMI_Video_present set: 3D_present set in its byte; it declares 1080p24 and 720p60: %s %r' % (said, formats))
b = made([0x10, 0x00, 0x00, 0x78, 0xc0, 1, 2, 3, 4])  # both latencies, no HDMI video
new, said, formats = tool.add_3d_present(b)
_, _, p = vsdb(new)
check(hc.edid_3d(new) and 'added (2 bytes)' in said and p[7] == 0xe0 and p[8:12] == bytes([1, 2, 3, 4]) and
      p[12:14] == b'\x80\x00' and sums_right(new),
      'no HDMI_Video_present: set, the latencies kept, 3D_present and an empty length byte after them')
c = made([0x10, 0x00])  # (the shortest block: the address only)
new, said, formats = tool.add_3d_present(c)
check(hc.edid_3d(new) and tool.declares_3d(new) and sums_right(new), 'the shortest HDMI vendor block: grown to hold it')
d = made([0x10, 0x00, 0x00, 0x78, 0x20, 0x80])
new, said, _ = tool.add_3d_present(d)
check(new == d and said.startswith('nothing'), '3D already: nothing changed')
for edid, why, text in ((made([0x10, 0x00, 0x00, 0x78, 0x00], dtds=6, pad=False), 'no room', 'no room left: refused'),
                        (bytes(made([0x10, 0x00])[:128]), 'no HDMI vendor block', 'no extension: refused')):
    try:
        tool.add_3d_present(edid)
        check(False, text)
    except tool.EdidError as e:
        check(why in str(e), '%s (%s)' % (text, e))
try:
    tool.add_3d_present(b'\x00' * 256)
    check(False, 'not an EDID: refused')
except tool.EdidError:
    check(True, 'not an EDID: refused')
check(tool.OFF.split(':')[0] not in ('HDMI-A-1', 'DP-1') and ':' in tool.OFF and tool.NAME == 'edid/hades-fp-test.bin' and
      tool.PARAM == '/sys/module/drm/parameters/edid_firmware' and tool.FIRMWARE == '/usr/lib/firmware',
      'off names no connector (drm_edid_load_firmware(): no EDID, nothing said); the file and the parameter of the '
      'kernel')
print('ALL PASSED' if not fails else '%d FAILED' % fails)
sys.exit(1 if fails else 0)
