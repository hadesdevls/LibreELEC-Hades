#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of what the start asks hades-picture to draw: boot-picture (packages/sysutils/hades-picture/scripts, the
# picture of the start once the graphics driver has its frame buffer), kodi-display-notice and kodi-failed-notice
# (packages/mediacenter/kodi/scripts, the notices of Kodi's start). A hades-picture of the test answers as it is told
# and writes down how it was called; the paths of the scripts (/sys, /run, /dev/tty1, /dev/fb0, /flash, /proc/uptime,
# /etc/os-release) are moved into a folder, the wait for a frame buffer is 0.3 s instead of 30 s, and the busybox of a
# system runs them.
#
# Usage: picture_scripts_test.sh <busybox> <boot-picture> <kodi-display-notice> <kodi-failed-notice>
# With scripts that do nothing, 17 of its 23 checks fail. Exit 0 when every check passes, 1 when one fails, 2 when the test
# could not run.
set -u
BBI=${1:-}; BP=${2:-}; DN=${3:-}; FN=${4:-}
[ -x "$BBI" ] && [ -f "$BP" ] && [ -f "$DN" ] && [ -f "$FN" ] ||
  { echo "usage: $0 <busybox> <boot-picture> <kodi-display-notice> <kodi-failed-notice>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb" "$T/flash" "$T/etc"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
printf 'PRETTY_NAME="LibreELEC (Hades 1.1): devel-test"\n' > "$T/etc/os-release"
# the hades-picture of the test: "show" and "fb" answer what $T/answer-<show|fb> says, with the status of the first word
# (answer-show: one line a call, the last one for every call after it)
cat > "$T/bb/hades-picture" <<EOF
#!$T/bb/sh
echo "\$*" >> $T/calls
case "\$1" in
  show) a=\$(head -n 1 $T/answer-show 2>/dev/null); [ -n "\$a" ] || a="ok kms"
        [ -f $T/answer-show ] && [ "\$(wc -l < $T/answer-show)" -gt 1 ] && sed -i 1d $T/answer-show ;;
  fb) a=\$(cat $T/answer-fb 2>/dev/null || echo "ok fb0") ;;
  *) a="ok nothing is held" ;;
esac
echo "\$a"
case "\$a" in ok*) exit 0 ;; none*) exit 3 ;; *) exit 1 ;; esac
EOF
chmod +x "$T/bb/hades-picture"
place() {
  sed -e "s#/sys/#$T/sys/#g" -e "s#/run/#$T/run/#g" -e "s#/dev/tty1#$T/tty1#g" -e "s#cat /dev/zero >/dev/fb0#echo zeroed >$T/fb0#" \
      -e "s#/flash/#$T/flash/#g" -e "s#ls /flash #ls $T/flash #" -e "s#/proc/uptime#$T/uptime#" \
      -e "s#/etc/os-release#$T/etc/os-release#" -e "s#-ge 300 \]#-ge 3 ]#" "$1" > "$2"
}
place "$BP" "$T/bp.sh"; place "$DN" "$T/dn.sh"; place "$FN" "$T/fn.sh"
ok=0; bad=0
check() { if [ "$2" = "$3" ]; then ok=$((ok + 1)); echo "ok   $1"; else bad=$((bad + 1)); echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fi; }
# a system: <connected displays> [<name of fb0>]
system() {
  rm -rf "$T/sys" "$T/run" "$T/calls" "$T/tty1" "$T/fb0" "$T/answer-"* "$T/flash/"*
  mkdir -p "$T/sys/class/graphics/fb0" "$T/sys/class/vtconsole/vtcon0" "$T/run" "$T/sys/bus/pci/devices"
  echo "${2:-virtio_gpudrmfb}" > "$T/sys/class/graphics/fb0/name"
  echo "(S) dummy device" > "$T/sys/class/vtconsole/vtcon0/name"; echo 1 > "$T/sys/class/vtconsole/vtcon0/bind"
  local i
  for i in 1 2 3; do
    mkdir -p "$T/sys/class/drm/card0-HDMI-A-$i"
    if [ "$i" -le "$1" ]; then echo connected; else echo disconnected; fi > "$T/sys/class/drm/card0-HDMI-A-$i/status"
  done
}
run() { local s=$1; shift; env -i PATH="$T/bb" "$@" "$T/bb/sh" "$T/$s.sh" "${ARG:-}" >> "$T/out" 2>&1; }
calls() { cat "$T/calls" 2>/dev/null | tr '\n' '|'; }
at() { echo "$1.42 9.00" > "$T/uptime"; }
fstop() { at "$1"; ARG=stop run fn SERVICE_RESULT="$2" EXIT_CODE=exited EXIT_STATUS=1; }
fstart() { at "$1"; ARG=start run fn; }

# --- boot-picture
system 1; : > "$T/run/hades-picture.kodi"; ARG= run bp
check "boot: Kodi was started: nothing is asked for" "$(calls)" ""
system 1; ARG= run bp
check "boot: the picture of the start, by its role and the splash screens of the system" "$(calls)" "show fit boot /usr/share/splash/splash|"
system 1; echo "none no display is on" > "$T/answer-show"; ARG= run bp
check "boot: no display on, one connected: into the frame buffer" "$(calls)" "show fit boot /usr/share/splash/splash|fb fit /usr/share/splash/splash|"
system 2; printf 'none no display is on
ok kms
' > "$T/answer-show"; ARG= run bp
check "boot: no display on, two connected: lit black, then each its own" "$(calls)" "show fit boot /usr/share/splash/splash|fb black|show fit boot /usr/share/splash/splash|"
system 2; echo "no a driver refused" > "$T/answer-show"; ARG= run bp
check "boot: not shown on each display: into the frame buffer, as the init draws it" "$(calls)" "show fit boot /usr/share/splash/splash|fb fit /usr/share/splash/splash|"
system 1; mkdir -p "$T/sys/class/vtconsole/vtcon1"; echo "(M) frame buffer device" > "$T/sys/class/vtconsole/vtcon1/name"
echo 1 > "$T/sys/class/vtconsole/vtcon1/bind"; ARG= run bp
check "boot: the console has the screen: left as it is" "$(calls)" ""
system 1; : > "$T/flash/oemsplash.png"; ARG= run bp
check "boot: a picture of the user is stretched, as the init does" "$(calls)" "show stretch boot $T/flash/oemsplash|"
system 1 "EFI VGA"; ARG= run bp
check "boot: no frame buffer of a graphics driver: nothing drawn, said" "$(calls)/$(grep -c 'is not the frame buffer of a graphics driver' "$T/out")" "/1"

# --- kodi-display-notice
system 1; ARG= run dn
check "notice: one display: nothing drawn" "$(calls)" ""
system 2; ARG= run dn
check "notice: two displays: each its own picture, the card over the splash screen" "$(calls)" "show fit kodi /usr/share/splash/splash /usr/lib/kodi/notice/display|"
check "notice: held on the displays: nothing to clear from the frame buffer, and no text" "$([ -f "$T/run/kodi-display-notice.drawn" ] && echo drawn)/$([ -s "$T/tty1" ] && echo text)" "/"
system 2; echo "ok fb 2 display(s) of 1920x1080" > "$T/answer-show"; ARG= run dn
check "notice: drawn into the frame buffer: cleared from it later" "$([ -f "$T/run/kodi-display-notice.drawn" ] && echo drawn)" "drawn"
ARG=clear run dn
check "notice: clear lets go of a picture held and makes the frame buffer black" "$(calls)/$(cat "$T/fb0" 2>/dev/null)" "show fit kodi /usr/share/splash/splash /usr/lib/kodi/notice/display|stop|/zeroed"
system 3; echo "no a driver refused" > "$T/answer-show"; ARG= run dn
check "notice: not on each display: into the frame buffer" "$(calls)/$([ -f "$T/run/kodi-display-notice.drawn" ] && echo drawn)" "show fit kodi /usr/share/splash/splash /usr/lib/kodi/notice/display|fb fit /usr/share/splash/splash /usr/lib/kodi/notice/display|/drawn"
system 2; echo "no a driver refused" > "$T/answer-show"; echo "no no fb0" > "$T/answer-fb"; ARG= run dn
check "notice: no picture at all: the text, in English and Spanish" "$(grep -c 'shows on one display' "$T/tty1" 2>/dev/null)/$(grep -c 'una sola pantalla' "$T/tty1" 2>/dev/null)" "1/1"
system 2; echo kms > "$T/run/kodi-failed-notice.shown"; ARG= run dn; ARG=clear run dn
check "notice: the notice that Kodi could not start stays, also at clear" "$(calls)" ""

# --- kodi-failed-notice
system 2; : > "$T/out"
fstart 10; fstop 14 exit-code; fstart 16; fstop 20 exit-code
check "failed: two quick failures: nothing drawn" "$(calls)" ""
fstart 22; fstop 26 exit-code
check "failed: the third: the picture with the diagnostics off and the version, held on the displays" "$(calls)/$(cat "$T/run/kodi-failed-notice.shown" 2>/dev/null)/$([ -s "$T/tty1" ] && echo text)" "show fit kodi /usr/share/splash/splash /usr/lib/kodi/notice/failed-off text=LibreELEC (Hades 1.1): devel-test|/kms/"
check "failed: the journal has the count and the version" "$(grep -c '3 time(s) in a row; LibreELEC (Hades 1.1): devel-test' "$T/out")" "1"
: > "$T/flash/DIAG-ON.TXT"; rm -f "$T/calls"; echo "no a driver refused" > "$T/answer-show"; echo "ok fb0" > "$T/answer-fb"
fstart 28; fstop 31 signal
check "failed: with the diagnostics on, into the frame buffer" "$(calls)/$(cat "$T/run/kodi-failed-notice.shown" 2>/dev/null)" "show fit kodi /usr/share/splash/splash /usr/lib/kodi/notice/failed-on text=LibreELEC (Hades 1.1): devel-test|fb fit /usr/share/splash/splash /usr/lib/kodi/notice/failed-on text=LibreELEC (Hades 1.1): devel-test|/fb"
fstart 33; fstop 600 success
check "failed: Kodi ran: the notice is taken from the frame buffer" "$([ -f "$T/run/kodi-failed-notice.shown" ] && echo shown)/$(cat "$T/fb0" 2>/dev/null)" "/zeroed"
system 2; echo "no a driver refused" > "$T/answer-show"; echo "no no fb0" > "$T/answer-fb"
fstart 40; fstop 42 exit-code; fstart 44; fstop 46 exit-code; fstart 48; fstop 50 exit-code
check "failed: no picture at all: the text, with the count" "$(grep -c 'It stopped 3 times in a row' "$T/tty1" 2>/dev/null)/$(cat "$T/run/kodi-failed-notice.shown" 2>/dev/null)" "1/text"
ARG=clear run dn
check "failed: clear leaves the text of the notice" "$(grep -c 'It stopped 3 times' "$T/tty1" 2>/dev/null)" "1"
echo "picture-scripts: $ok checks passed, $bad failed"
[ $bad = 0 ]
