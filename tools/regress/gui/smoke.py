# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# Runs INSIDE Kodi (RunScript), started by gui_smoke.sh. Writes /storage/smoke.out: one line
#   SMOKE-ITEM <name>: PASSED|FAILED | <detail>
# per check, SMOKE-SHOT <name> <width>x<height> <width>x<height> for every screenshot taken (/storage/shots/<name>.png;
# the size of the screen before and after it), and SMOKE-DONE last.
# It never decides the verdict: gui_smoke_verdict.py counts the lines on the host (and a missing item is NOT-RUN).
import time
import xbmc

OUT = '/storage/smoke.out'
f = open(OUT, 'a')


def w(line):
    f.write(line + '\n')
    f.flush()
    xbmc.log('hades-smoke: ' + line, xbmc.LOGINFO)


def item(name, ok, detail=''):
    w('SMOKE-ITEM %s: %s | %s' % (name, 'PASSED' if ok else 'FAILED', detail))


def cond(expr):
    return xbmc.getCondVisibility(expr)


def wait(expr, secs):
    t = time.time()
    while time.time() - t < secs:
        if cond(expr):
            return True
        xbmc.sleep(250)
    return cond(expr)


def screen():
    return '%sx%s' % (xbmc.getInfoLabel('System.ScreenWidth'), xbmc.getInfoLabel('System.ScreenHeight'))


def shot(name):
    # the size of the screen just before and after the shot: the playback may run in another mode than the GUI, and
    # the verdict wants the shot at that size, the same both times
    before = screen()
    xbmc.executebuiltin('TakeScreenshot(/storage/shots/%s.png)' % name)
    xbmc.sleep(2500)
    w('SMOKE-SHOT %s %s %s' % (name, before, screen()))


def window(name, check):
    xbmc.executebuiltin('Dialog.Close(all,true)')
    xbmc.sleep(500)
    xbmc.executebuiltin('ActivateWindow(%s)' % name)
    ok = wait('Window.IsActive(%s)' % check, 10)
    xbmc.sleep(1500)
    item('window-' + name, ok, 'Window.IsActive(%s) = %s' % (check, ok))
    shot('window_' + name)


try:
    xbmc.sleep(2000)
    xbmc.executebuiltin('Dialog.Close(all,true)')
    xbmc.executebuiltin('ActivateWindow(home)')
    ok = wait('Window.IsActive(home)', 10)
    item('window-home', ok, 'Window.IsActive(home) = %s' % ok)
    shot('window_home')
    window('settings', 'settings')
    window('systemsettings', 'systemsettings')

    # a short software-decoded playback
    xbmc.executebuiltin('Dialog.Close(all,true)')
    xbmc.executebuiltin('PlayMedia(/storage/pay/clip.avi)')
    ok = wait('Player.HasVideo', 30)
    t0 = None
    if ok:
        xbmc.sleep(1500)
        t0 = xbmc.Player().getTime() if xbmc.Player().isPlayingVideo() else None
        xbmc.sleep(4000)
        t1 = xbmc.Player().getTime() if xbmc.Player().isPlayingVideo() else None
        adv = t0 is not None and t1 is not None and t1 > t0 + 2.0
        dec = xbmc.getInfoLabel('Player.Process(videodecoder)')
        item('playback', adv, 'time %s -> %s s, decoder "%s", %sx%s' % (t0, t1, dec, xbmc.getInfoLabel('Player.Process(videowidth)'), xbmc.getInfoLabel('Player.Process(videoheight)')))
    else:
        item('playback', False, 'Player.HasVideo did not become true in 30 s')
    shot('playback')

    # the player information page, during the playback
    xbmc.executebuiltin('Action(PlayerProcessInfo)')
    ok = wait('Window.IsActive(playerprocessinfo)', 10)
    xbmc.sleep(1500)
    item('info-page', ok, 'Window.IsActive(playerprocessinfo) = %s' % ok)
    shot('info_page')
    xbmc.executebuiltin('Dialog.Close(all,true)')
    xbmc.executebuiltin('PlayerControl(Stop)')
    xbmc.sleep(2000)
    item('playback-stop', not cond('Player.HasVideo'), 'Player.HasVideo = %s' % cond('Player.HasVideo'))

    # the power menu: every entry has a label (a $LOCALIZE that does not resolve is empty)
    xbmc.executebuiltin('ActivateWindow(home)')
    xbmc.sleep(1000)
    xbmc.executebuiltin('ActivateWindow(shutdownmenu)')
    ok = wait('Window.IsActive(shutdownmenu)', 10)
    xbmc.sleep(1500)
    n = int(xbmc.getInfoLabel('Container(9000).NumItems') or 0)
    labels = [xbmc.getInfoLabel('Container(9000).ListItemAbsolute(%d).Label' % i) for i in range(n)]
    blank = [i for i, l in enumerate(labels) if not l.strip() or '$' in l]
    item('power-menu', ok and n > 0 and not blank, 'open %s, %d entries %r, blank or unresolved at %r' % (ok, n, labels, blank))
    shot('power_menu')
    xbmc.executebuiltin('Dialog.Close(all,true)')
except Exception as e:
    item('script', False, 'exception %r' % (e,))
w('SMOKE-DONE')
f.close()
