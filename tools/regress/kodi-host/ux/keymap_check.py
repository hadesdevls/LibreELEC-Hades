# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# keymap_check.py <Kodi tree>: the keys that open the player information while a video plays, in Kodi's default keymaps
# (system/keymaps, sections FullscreenVideo and FullscreenInfo): a long press of Menu opens it (keyboard: a Fire TV remote over Bluetooth
# sends Menu as a key of a keyboard; remote: menu, and the "Contents menu" and "Root menu" that a TV remote sends over
# CEC), with the action of O, of the title key and of the red key (PlayerProcessInfo); a short press of each is the OSD,
# as before; no other long press of those keys there; the long press of Menu outside a video (the global section of the
# keyboard: Menu) is left as Kodi has it. Prints FAIL lines and KEYMAP-CHECK PASSED or FAILED (n) last.
import os
import sys
import xml.etree.ElementTree as ET

K = sys.argv[1]
fails = []


def need(cond, text):
    if not cond:
        fails.append(text)
        print('FAIL: ' + text)


def section(path, name, device):
    root = ET.parse(os.path.join(K, 'system/keymaps', path)).getroot()
    s = root.find(name)
    return s.find(device) if s is not None else None


def actions(node, key):
    """the action of a short press and of a long press of key in a keymap node"""
    short = [e.text for e in node.findall(key) if e.get('mod') is None] if node is not None else []
    long_ = [e.text for e in node.findall(key) if e.get('mod') == 'longpress'] if node is not None else []
    return short, long_


for name in ('FullscreenVideo', 'FullscreenInfo'):
    kb = section('keyboard.xml', name, 'keyboard')
    need(kb is not None, f'keyboard.xml has a {name} section')
    o, _ = actions(kb, 'o')
    need(o == ['PlayerProcessInfo'], f'{name}: O opens the player information ({o})')
    short, long_ = actions(kb, 'menu')
    need(short == ['OSD'], f'{name}, keyboard: a short press of Menu is the OSD, as before ({short})')
    need(long_ == ['PlayerProcessInfo'], f'{name}, keyboard: a long press of Menu opens the player information ({long_})')
    rc = section('remote.xml', name, 'remote')
    need(rc is not None, f'remote.xml has a {name} section')
    for key in ('title', 'red'):
        s, _ = actions(rc, key)
        need(s == ['PlayerProcessInfo'], f'{name}, remote: {key} opens the player information ({s})')
    for key in ('menu', 'contentsmenu', 'rootmenu'):
        short, long_ = actions(rc, key)
        need(short == ['OSD'], f'{name}, remote: a short press of {key} is the OSD, as before ({short})')
        need(long_ == ['PlayerProcessInfo'], f'{name}, remote: a long press of {key} opens the player information ({long_})')
glob = section('keyboard.xml', 'global', 'keyboard')
need(actions(glob, 'menu') == (['ContextMenu'], ['Menu']), 'keyboard, outside a video: Menu as Kodi has it '
     f'(short ContextMenu, long Menu: {actions(glob, "menu")})')
print(f'KEYMAP-CHECK FAILED ({len(fails)})' if fails else 'KEYMAP-CHECK PASSED')
sys.exit(1 if fails else 0)
