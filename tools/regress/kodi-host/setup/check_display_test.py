# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades: "Your display" (the page of the first-start wizard and its button in System > Check this setup) and "Share my
# results" of the LibreELEC settings add-on: hades_display.py (what an EDID says, where the TV's remote reaches the PC
# over HDMI-CEC, what a projector or a choice of HDR sets and how another answer puts it back, the line to share and
# that it holds no serial number, address or name of a network or of the PC) and the flow of hades_display_ui.py and
# modules/display.py with stand-ins for Kodi (xbmc, xbmcgui, xbmcaddon, oe, log), on the EDIDs of an LG C5 read
# direct and through a sound bar.
# usage: check_display_test.py <resources/lib of the add-on> <c5 direct EDID> <c5 through a sound bar EDID>
import importlib
import json
import os
import shutil
import sys
import tempfile
import types

LIB, DIRECT, BAR = sys.argv[1], sys.argv[2], sys.argv[3]
fails = 0


def check(ok, text):
    global fails
    fails += 0 if ok else 1
    print(('ok:   ' if ok else 'FAIL: ') + text)


# ---- stand-ins for Kodi
SETTINGS = {}
HIDDEN = set()


def execute_jsonrpc(request):
    q = json.loads(request)
    p = q['params']
    if q['method'] == 'Settings.GetSettingValue':
        k = p['setting']
        if k in SETTINGS and k not in HIDDEN:
            return json.dumps({'id': 1, 'result': {'value': SETTINGS[k]}})
        return json.dumps({'id': 1, 'error': {'code': -32602}})
    if p['setting'] in HIDDEN or p['setting'] not in SETTINGS:
        return json.dumps({'id': 1, 'error': {'code': -32602}})
    SETTINGS[p['setting']] = p['value']
    return json.dumps({'id': 1, 'result': True})


class Dialog:
    script = []
    shown = []

    def _next(self, kind, *a):
        Dialog.shown.append((kind,) + a)
        if not Dialog.script:
            raise AssertionError('no answer scripted for %s %r' % (kind, a[:1]))
        want, answer = Dialog.script.pop(0)
        if want != kind:
            raise AssertionError('asked %s, the script has %s' % (kind, want))
        return answer

    def textviewer(self, h, t):
        Dialog.shown.append(('textviewer', h, t))

    def ok(self, h, t):
        Dialog.shown.append(('ok', h, t))

    def select(self, h, items, **kw):
        return self._next('select', h, items)

    def yesno(self, h, t, **kw):
        return self._next('yesno', h, t)


xbmc = types.SimpleNamespace(getLocalizedString=lambda n: 'k%d' % n, executeJSONRPC=execute_jsonrpc)
xbmcgui = types.SimpleNamespace(Dialog=Dialog, Window=None, DialogProgress=None, DLG_YESNO_YES_BTN=11,
                                DLG_YESNO_NO_BTN=10)
xbmcaddon = types.SimpleNamespace(Addon=lambda n: None)
LOG = []
log = types.SimpleNamespace(INFO=1, WARNING=2, ERROR=3, DEBUG=0, log=lambda m, l=1: LOG.append((l, m)),
                            log_function=lambda *a, **k: (lambda f: f))
WIZARD = []


class Wizard:
    def __getattr__(self, name):
        return lambda *a: WIZARD.append((name,) + a)


oe = types.SimpleNamespace(_=lambda n: f's{n}' + (' %s' if n in (33882, 33885, 33886, 33893, 33897, 33898, 33907, 33909,
                                                                   33914, 33919) else '') +
                           (' %s' if n == 33910 else ''), xbmcm=None, winOeMain=Wizard())
for name, mod in (('xbmc', xbmc), ('xbmcgui', xbmcgui), ('xbmcaddon', xbmcaddon), ('log', log), ('oe', oe)):
    sys.modules[name] = mod
sys.path.insert(0, LIB)
sys.path.insert(0, os.path.join(LIB, 'modules'))
hd = importlib.import_module('hades_display')
ui = importlib.import_module('hades_display_ui')
wizard_module = importlib.import_module('display')
hc = importlib.import_module('hades_check')

direct = open(DIRECT, 'rb').read()
bar = open(BAR, 'rb').read()
work = tempfile.mkdtemp(prefix='check-display-')
try:
    # ---- what an EDID says
    fd = hd.edid_facts(direct)
    fb = hd.edid_facts(bar)
    print(f'direct: {fd}\nbar:    {fb}')
    check(fd['dv'] == 2 and fb['dv'] == 1, 'the C5 declares Dolby Vision v2 direct and v1 through the bar')
    check(fd['address'] == '4.0.0.0' and fd['between'] == 0, 'direct: on input 4 of the TV')
    check(fb['address'] == '2.2.0.0' and fb['between'] == 1, 'through the bar: behind one device')
    check('DTS' in fb['audio'] and 'DTS' not in fd['audio'], 'DTS through the bar only')
    check('HDR10' in fd['hdr'] and 'HLG' in fd['hdr'], 'the C5 takes HDR10 and HLG')
    check(fd['name'] == hc.edid_name(direct) and fd['name'].startswith('GSM '), 'the name is the EDID name: ' + fd['name'])
    # an EDID made here: serial number in the base block and in a descriptor, no HDMI VSDB, no HDR
    made = bytearray(256)
    made[0:8] = b'\x00\xff\xff\xff\xff\xff\xff\x00'
    made[8:10] = bytes([0x10, 0xac])     # DEL
    made[12:16] = (0x12345678).to_bytes(4, 'little')
    made[54:72] = b'\x00\x00\x00\xff\x00' + b'SN9Z8Y7X6W5V\n'
    made[72:90] = b'\x00\x00\x00\xfc\x00' + b'MONITOR X\n   '
    made[126] = 1
    made[128:132] = bytes([2, 3, 4, 0])
    fm = hd.edid_facts(bytes(made))
    check(fm['dv'] == -1 and fm['hdr'] == [] and fm['between'] == -1 and fm['address'] == '',
          'an EDID without HDR, Dolby Vision or HDMI VSDB: nothing of them')
    for address, between in ((0x1000, 0), (0x2100, 1), (0x2140, 2), (0x0000, -1), (0xffff, -1), (0x2010, -1)):
        e = bytearray(made)
        e[132:140] = bytes([0x65, 0x03, 0x0c, 0x00, address >> 8, address & 255, 0x00, 0x00])
        e[130] = 140
        f = hd.edid_facts(bytes(e))
        check(f['between'] == between, f'address {address:04x}: between {f["between"]}, {between} expected')

    # ---- HDMI-CEC
    def fake_ioctl(name):
        def ioctl(fd_, req, buf, mutate):
            if req != hd.CEC_ADAP_G_CAPS or len(buf) != hd.CEC_CAPS.size:
                raise OSError('bad request')
            buf[:] = hd.CEC_CAPS.pack(b'i915', name.encode(), 1, 0x7e, 0x60000)
        return ioctl
    check(hd.CEC_ADAP_G_CAPS == 0xc04c6100, 'CEC_ADAP_G_CAPS is _IOWR(a, 0, 76): 0xc04c6100')
    dev = os.path.join(work, 'dev')
    usb = os.path.join(work, 'usb')
    os.makedirs(dev)
    os.makedirs(os.path.join(usb, '1-4'))
    open(os.path.join(dev, 'cec0'), 'w').close()
    check(hd.cec_adapters(dev, usb, fake_ioctl('DP-2')) == [('output', 'DP-2')], 'a CEC adapter of DP-2')
    with open(os.path.join(usb, '1-4', 'idVendor'), 'w') as f:
        f.write('2548\n')
    with open(os.path.join(usb, '1-4', 'idProduct'), 'w') as f:
        f.write('1002\n')
    both = hd.cec_adapters(dev, usb, fake_ioctl('DP-2'))
    check(both == [('output', 'DP-2'), ('usb', 'Pulse-Eight')], f'and a Pulse-Eight: {both}')
    check(hd.cec_verdict([('output', 'DP-2')], ['DP-2']) == ('same', 'DP-2'), 'CEC on the output of the picture')
    check(hd.cec_verdict([('output', 'HDMI-A-3')], ['DP-1']) == ('other', 'HDMI-A-3'),
          'CEC on another output (issue 24: the board HDMI, the picture on the graphics card)')
    check(hd.cec_verdict(both, ['DP-1']) == ('usb', ''), 'with a USB adapter, the USB one')
    check(hd.cec_verdict([], ['DP-1']) == ('none', ''), 'no adapter')
    check(hd.cec_name('/nonexistent/cec9') == '', 'a device that cannot be opened: no name')

    # ---- what the choices set
    c, note = hd.projector_changes(150, {'hdr': ['HDR10'], 'dv': -1})
    check(note == 'hdr10' and c == {hd.SDR_TARGET: 2, hd.DV_HDR10_MAP: 1, hd.DV_HDR10_PEAK: 150, hd.HDR10_ADAPT: 2},
          f'an HDR10 projector of 150 nits: {c}')
    c, note = hd.projector_changes(75, {'hdr': ['HDR10'], 'dv': 2})
    check(note == 'dv' and c == {hd.SDR_TARGET: 2}, 'a Dolby Vision projector: nothing of Dolby Vision')
    c, note = hd.projector_changes(100, {'hdr': [], 'dv': -1})
    check(note == 'sdr' and c == {hd.SDR_TARGET: 2}, 'an SDR projector: the SDR target only')
    check(hd.projector_changes(33, {'hdr': ['HDR10'], 'dv': -1})[0][hd.DV_HDR10_PEAK] == 40, 'a peak under 40: 40')
    check(hd.hdr_changes('hdr') == {} and hd.hdr_changes('hlg_sdr') == {hd.HDR_DISPLAY: True, hd.HDR_AS_SDR: 1} and
          hd.hdr_changes('all_sdr') == {hd.HDR_DISPLAY: False, hd.DV_NATIVE: False}, 'the HDR choices')
    cur = {hd.SDR_TARGET: 0, hd.DV_HDR10_MAP: 0, hd.DV_HDR10_PEAK: 1000, hd.HDR10_ADAPT: 0, hd.HDR_AS_SDR: 0,
           hd.HDR_DISPLAY: True, hd.DV_NATIVE: True}
    to_set, after = hd.plan(cur, {}, {hd.SDR_TARGET: 2}, {})
    check(to_set == {hd.SDR_TARGET: 2} and after == {hd.SDR_TARGET: 0}, 'the first change keeps what it was')
    to_set, after = hd.plan(dict(cur, **{hd.SDR_TARGET: 2}), {hd.SDR_TARGET: 0}, {}, {})
    check(to_set == {hd.SDR_TARGET: 0} and after == {}, 'nothing chosen: what the step changed goes back')
    to_set, after = hd.plan(dict(cur, **{hd.SDR_TARGET: 3}), {hd.SDR_TARGET: 0}, {hd.SDR_TARGET: 2}, {})
    check(after == {hd.SDR_TARGET: 0}, 'changed again: the value before the step is kept, not the one between')
    to_set, after = hd.plan(dict(cur, **{hd.HDR_AS_SDR: None}), {}, {}, {hd.HDR_AS_SDR: 1})
    check(to_set == {} and after == {}, 'a setting that JSON-RPC does not answer is not touched')
    jp = os.path.join(work, 'state', hd.JOURNAL_NAME)
    hd.write_journal(jp, {hd.SDR_TARGET: 0, 'videoscreen.other': 5, hd.HDR_DISPLAY: 1})
    check(hd.read_journal(jp) == {hd.SDR_TARGET: 0}, 'the journal keeps only the settings of the step, of their type')

    # ---- the line to share
    rel = os.path.join(work, 'os-release')
    with open(rel, 'w') as f:
        f.write('NAME="LibreELEC"\nVERSION="devel-20261125-abcdef0"\nBUILDER_NAME="Hades"\nBUILDER_VERSION="1.2"\n'
                'HOSTNAME="livingroom-pc"\n')
    cpuinfo = os.path.join(work, 'cpuinfo')
    with open(cpuinfo, 'w') as f:
        f.write('processor\t: 0\nmodel name\t: AMD Ryzen 9 7950X3D 16-Core Processor          \nserial\t: 1234\n')
    klog = os.path.join(work, 'kodi.log')
    with open(klog, 'w') as f:
        f.write('info <general>: Running on host livingroom-pc 192.168.1.20 aa:bb:cc:dd:ee:ff\n'
                'info <general>: GL_RENDERER = AMD Radeon Graphics (radeonsi, raphael_mendocino, LLVM 21.1.8, DRM 3.64, '
                '7.3.5)\ninfo <general>: GL_VERSION = OpenGL ES 3.2 Mesa 26.0.1\n')
    gpu, driver = hd.gpu_of_log(klog)
    check(gpu == 'AMD Radeon Graphics (radeonsi, raphael_mendocino)' and driver == 'Mesa 26.0.1',
          f'the GPU and driver from kodi.log: {gpu!r} {driver!r}')
    with open(klog, 'w') as f:
        f.write('GL_RENDERER = NVIDIA GeForce RTX 4080 SUPER/PCIe/SSE2\nGL_VERSION = OpenGL ES 3.2 NVIDIA 620.73.04\n')
    check(hd.gpu_of_log(klog) == ('NVIDIA GeForce RTX 4080 SUPER/PCIe/SSE2', 'NVIDIA 620.73.04'), 'NVIDIA')
    results = [
        {'facts': 'picture sdr: sum.head=sdr out.mode=3840x2160@60 answer=right dark_squares=4 light_squares=4'},
        {'facts': 'picture dv81: sum.head=dv out.mode=3840x2160@23.976 dv.state=native dv.transport=standard '
                  'dv.display=LG answer=right clip_stop=end clip_shown=1.2s clip_played=15.0s'},
        {'facts': 'picture hlg: sum.head=hdr_as_sdr answer=pale'},
        {'facts': 'frame packing: displays with 3D HDMI-A-1: GSM 0x83CD LG TV SSCR2; outcome=refused; stereo=2d; '
                  'mode=none; why=kernel: refused; settings_changed=none; answer=halves'},
        {'facts': 'dolby vision a/b: display HDMI-A-1: GSM 0x83CD LG TV SSCR2; A=hades B=licensed; answer=a '
                  'chosen=hades; tv_player_compared=yes; native=yes/yes; settings=l5form; offered=nothing; applied=no'},
        {'facts': 'speakers 5.1: audio.path=pcm audio.channels=6 answer=right'},
        {'facts': 'passthrough: ac3=heard eac3=heard truehd=heard dts=not-sent before[passthrough=on] after[passthrough=on]'},
    ]
    disp = [('HDMI-A-1', fb, True)]
    line = hd.share_line(disp, results, '2026-11-25 10:00 UTC', hd.os_release(rel), hd.cpu_model(cpuinfo),
                         *hd.gpu_of_log(klog))
    print('line: ' + line)
    check('\n' not in line and line.isascii(), 'one line of ASCII')
    for part in ('LibreELEC Hades 1.2', 'AMD Ryzen 9 7950X3D 16-Core Processor', 'NVIDIA GeForce RTX 4080 SUPER',
                 'NVIDIA 620.73.04', 'GSM ', 'on HDMI-A-1', 'DV v1', 'via sound bar/receiver 2.2.0.0',
                 'SDR SDR ok', 'DV 8.1 native ok', 'HLG as SDR pale', 'frame packing refused, halves', 'DV A/B hades',
                 'speakers 5.1 ok', 'passthrough AC-3 heard, DD+ heard, TrueHD heard, DTS not sent',
                 'check 2026-11-25 10:00 UTC'):
        check(part in line, f'the line says {part!r}')
    for secret in ('livingroom', '192.168', 'aa:bb', '1234', 'SN9Z8Y7X6W5V', '12345678', 'devel-20261125'):
        check(secret not in line, f'the line has no {secret!r}')
    made_line = hd.share_line([('DP-1', hd.edid_facts(bytes(made)), True)], [], '', {}, 'x', '', '')
    check('SN9Z8Y7X6W5V' not in made_line and '305419896' not in made_line and 'DEL ' in made_line,
          'an EDID with a serial number: the serial is not in the line: ' + made_line)
    check('no check made' in made_line, 'without a check, the line says so')
    check(hd.clean('host 10.0.0.7 mac 01:23:45:67:89:ab hdmi 2.2.0.0') == 'host [ip] mac [mac] hdmi 2.2.0.0',
          'the last guard: an address of the network and a MAC go, an HDMI address stays')

    # ---- the flow of "Your display" with stand-ins
    SETTINGS.update(cur)
    ui.hd.connected_displays = lambda *a: [('HDMI-A-1', dict(fd, dv=-1), True)]  # an HDR10 projector
    ui.hd.cec_adapters = lambda *a: [('output', 'HDMI-A-3')]
    state = os.path.join(work, 'display')
    step = ui.DisplayStep(state)
    text = '\n'.join(step.found_text())
    check('s33909' not in text and 's33910' in text and 'HDMI-A-3' in text and 'HDMI-A-1' in text,
          'the CEC adapter on another output is said, with both outputs')
    Dialog.script = [('select', 1), ('select', 3)]  # projector, 150 nits
    notes = step.choose_kind()
    check(SETTINGS[hd.DV_HDR10_MAP] == 1 and SETTINGS[hd.DV_HDR10_PEAK] == 150 and SETTINGS[hd.HDR10_ADAPT] == 2 and
          SETTINGS[hd.SDR_TARGET] == 2, 'a projector of 150 nits is set')
    check(notes and notes[0].startswith('s33914'), 'and said')
    Dialog.script = [('select', 2)]  # all as SDR
    step.choose_hdr()
    check(SETTINGS[hd.HDR_DISPLAY] is False and SETTINGS[hd.DV_NATIVE] is False, 'all as SDR: HDR mode and native DV off')
    # a new session reads the choices and the journal
    step = ui.DisplayStep(state)
    check(step.choices == {'kind': 'projector', 'peak': 150, 'hdr': 'all_sdr'}, f'the choices kept: {step.choices}')
    Dialog.script = [('select', 0)]  # TV
    step.choose_kind()
    Dialog.script = [('select', 0)]  # as HDR
    notes = step.choose_hdr()
    check(SETTINGS == cur, f'TV and as HDR: every setting as it was before the step: {SETTINGS}')
    check(notes == ['s33918'], 'and said')
    check(hd.read_journal(hd.journal_path(state)) == {}, 'the journal is empty again')
    # closed without an answer: nothing changes
    Dialog.script = [('select', -1)]
    check(step.choose_kind() == [] and SETTINGS == cur, 'a list closed without an answer changes nothing')
    # an SDR display is not asked about HDR, and its HDR settings are not touched
    ui.hd.connected_displays = lambda *a: [('DP-1', hd.edid_facts(bytes(made)), True)]
    step = ui.DisplayStep(os.path.join(work, 'display2'))
    check(not step.takes_hdr(), 'an SDR display: no question of HDR')
    # the button: what was found, then the list until closed
    Dialog.shown.clear()
    Dialog.script = [('select', 1), ('select', -1)]
    step.run()
    check(Dialog.shown[0][0] == 'textviewer' and any(k == 'textviewer' for k, *_ in Dialog.shown[1:]),
          'the button shows what was found, and again from its list')
    # the wizard page
    WIZARD.clear()
    ui.hd.connected_displays = lambda *a: [('HDMI-A-1', fd, True)]
    # (where no journal can be written, nothing is set: the folder of the state, /storage/.cache, is not here)
    SETTINGS.update(cur)
    Dialog.script = [('select', 1), ('select', 0)]
    ui.DisplayStep().choose_kind()
    check(SETTINGS == cur, 'without a journal on the drive nothing is changed')
    ui.DisplayStep.__init__.__defaults__ = (os.path.join(work, 'display3'),)
    w = wizard_module.display(None)
    w.do_wizard()
    names = [c[0] for c in WIZARD]
    check({'set_wizard_title', 'set_wizard_text', 'set_wizard_button_1', 'set_wizard_button_2'} <= set(names),
          f'the wizard page has its title, text and two buttons: {names}')
    check(wizard_module.display.ENABLED and wizard_module.display.menu == {}, 'enabled, with no menu entry of its own')
    SETTINGS.update(cur)
    Dialog.script = [('select', 1), ('select', 6)]  # projector, does not know
    w.wizard_kind()
    check(SETTINGS[hd.DV_HDR10_MAP] == 0 and SETTINGS[hd.SDR_TARGET] == 2,
          'the C5 as a projector: it takes Dolby Vision, nothing of Dolby Vision is set')

    # the journal of after the change that cannot be written (LibreELEC-settings-9086): no exception, what was set
    # stays set, the choices are kept, and the journal of before stays, with the value from before the step
    real_write = hd.write_journal
    writes = []

    def second_fails(path, before):
        writes.append(dict(before))
        if len(writes) == 2:
            raise OSError('no room on the storage')
        return real_write(path, before)

    hd.write_journal = second_fails
    SETTINGS.update(cur)
    state4 = os.path.join(work, 'display4')
    step = ui.DisplayStep(state4)
    Dialog.script = [('select', 1), ('select', 3)]  # projector, 150 nits
    try:
        step.choose_kind()
        raised = None
    except OSError as e:
        raised = e
    hd.write_journal = real_write
    check(raised is None and len(writes) == 2 and SETTINGS[hd.SDR_TARGET] == 2 and
          hd.read_journal(hd.journal_path(state4)).get(hd.SDR_TARGET) == 0 and
          os.path.exists(os.path.join(state4, ui.CHOICES)),
          f'the second journal not written: no error, set, the choices and the journal of before kept ({raised!r})')
    SETTINGS.update(cur)

    # ---- "Share my results"
    check_state = os.path.join(work, 'check')
    os.makedirs(check_state)
    Dialog.shown.clear()
    Dialog.script = []
    ui.share('/clips', check_state)
    check(Dialog.shown and Dialog.shown[0][0] == 'ok' and Dialog.shown[0][2] == 's33884', 'no check yet: said')
    with open(os.path.join(check_state, 'last.json'), 'w') as f:
        json.dump({'version': 1, 'when': '2026-11-25 10:00 UTC', 'stopped': False, 'results': results}, f)
    drive = os.path.join(work, 'media', 'USB')
    os.makedirs(drive)
    ui.hc.usb_drives = lambda *a: [drive]
    ui.hd.gpu_of_log = lambda *a: ('GPU X', 'Mesa 1.0')
    Dialog.shown.clear()
    Dialog.script = [('yesno', True)]
    ui.share('/clips', check_state)
    copied = open(os.path.join(drive, 'hades-results.txt')).read()
    check(copied.startswith('LibreELEC ') and copied.count('\n') == 1 and 'GPU X, Mesa 1.0' in copied,
          'the line is copied to the drive: ' + copied.strip()[:80])
    check(open(os.path.join(check_state, hd.SHARE_NAME)).read() == copied, 'and kept beside the results')
    check(Dialog.shown[0][0] == 'textviewer' and copied.strip() in Dialog.shown[0][2], 'and shown')
finally:
    shutil.rmtree(work)
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
