#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# LibreELEC Hades: GUI smoke tests of Kodi in QEMU/KVM (virgl on a private Xvfb, llvmpipe), against a given image.
# Usage: gui_smoke.sh <image.img.gz> [<kernel file>]      (the kernel file defaults to <image without .img.gz>.kernel)
# One VM at a time (it refuses to start while any other qemu-system-x86_64 runs). About 4 minutes.
# Result of SOFTWARE rendering (llvmpipe), not of hardware: it proves that Kodi starts and its GUI works, not the picture
# on a screen. What it checks (one GUISMOKE-ITEM line each, the verdict is counted from them):
#   boot         the guest comes up and Kodi writes GL_RENDERER (the log says virgl/llvmpipe)
#   home/settings/systemsettings  the window is active after ActivateWindow, and a screenshot of each is not blank
#   playback     a 30 s MJPEG clip (software decoded) plays: Player.HasVideo, the time advances, the decoder is named
#   info         Action(PlayerProcessInfo) opens the player information page during the playback
#   power menu   the shutdown menu opens and every entry has a non-empty label (a $LOCALIZE that does not resolve is blank)
#   no crash     kodi.log has no crash/abort line; the number of ERROR lines is reported (informative)
# The static partner of the blank-label check is kit/skin_localize_check.py (every $LOCALIZE of the skin against the strings).
# Files: the work folder GUI_W holds the harness that starts the machine: g.py, start-vm.sh, prep.sh.
set -uo pipefail
IMGGZ=${1:?usage: gui_smoke.sh <image.img.gz> [kernel]}
KERNEL=${2:-${IMGGZ%.img.gz}.kernel}
W=${GUI_W:-${HADES_REGRESS:-$HOME/hades-regress}/gui}
KIT=$(cd "$(dirname "$0")" && pwd)
export LC_ALL=C PATH=$PATH:/usr/sbin:/sbin
mkdir -p "$W/out" && cd "$W" || exit 2
L=$W/gui-smoke.log
: > "$L"
log() { echo "$*" | tee -a "$L"; }
fail_early() { log "GUISMOKE-ITEM $1: NOT-RUN | $2"; log "GUISMOKE-DONE 0 passed, 0 failed, 1 not run"; log "GUISMOKE-VERDICT FAILED"; exit 1; }
[ -f "$IMGGZ" ] || fail_early boot "no image $IMGGZ"
[ -f "$KERNEL" ] || fail_early boot "no kernel $KERNEL"
for t in qemu-system-x86_64 Xvfb sfdisk debugfs resize2fs e2fsck blkid python3; do command -v $t > /dev/null 2>&1 || [ -x /usr/sbin/$t ] || fail_early boot "missing program $t"; done
[ -c /dev/kvm ] && [ -w /dev/kvm ] || fail_early boot "no usable /dev/kvm"
pgrep -f '^qemu-system-x86_64' > /dev/null && fail_early boot "another qemu-system-x86_64 runs (one VM at a time): wait for it"
log "# image $(basename "$IMGGZ") sha256 $(sha256sum "$IMGGZ" | cut -c1-16)... kernel $(basename "$KERNEL") start $(date -u +%FT%TZ)"

# --- the guest disk: the image with a bigger storage partition, a root shell on the 2nd serial port, the payload
rm -f base.img vm.img p1.img p2.img qm.sock sh.sock serial.log qemu.log
gzip -dc "$IMGGZ" > vm.img || fail_early boot "cannot unpack the image"
truncate -s 3G vm.img
field() { sfdisk -d vm.img | awk -v d="vm.img$1" -v f="$2" '$1 == d { for (i = 1; i <= NF; i++) if ($i ~ "^" f "=") { v = $(i+1); if ($i != f "=") v = substr($i, length(f) + 2); gsub(/[ ,]/, "", v); print v } }'; }
s1=$(field 1 start); n1=$(field 1 size); s2=$(field 2 start); n2=$(field 2 size)
[ -n "$s1" ] && [ -n "$s2" ] || fail_early boot "cannot read the partition table of the image"
sfdisk -q --relocate gpt-bak-std vm.img
echo ",+" | sfdisk -q -N 2 --no-reread vm.img > /dev/null 2>&1
n2new=$(field 2 size)
dd if=vm.img of=p1.img bs=1M iflag=skip_bytes,count_bytes skip=$((s1 * 512)) count=$((n1 * 512)) status=none
dd if=vm.img of=p2.img bs=1M iflag=skip_bytes,count_bytes skip=$((s2 * 512)) count=$((n2 * 512)) status=none
truncate -s $((n2new * 512)) p2.img; /usr/sbin/e2fsck -fy p2.img > /dev/null 2>&1 || true; /usr/sbin/resize2fs -f p2.img > /dev/null 2>&1
BOOTUUID=$(blkid -p -o value -s UUID p1.img 2> /dev/null); DISKUUID=$(blkid -p -o value -s UUID p2.img 2> /dev/null)
[ -n "$BOOTUUID" ] && [ -n "$DISKUUID" ] || fail_early boot "cannot read the UUIDs of the boot and storage partitions"
debugfs -w -R "rm /.please_resize_me" p2.img > /dev/null 2>&1 || true
python3 -I "$KIT/gen_clip.py" clip.avi 30 > /dev/null || fail_early boot "cannot make the test clip"
rm -rf pay; mkdir pay; cp clip.avi pay/clip.avi; cp "$KIT/smoke.py" pay/smoke.py
# GUI_SKIN=<a skin.estuary folder>: that skin goes in as a newer add-on (version 4.1.99), to test a tree's own skin
if [ -n "${GUI_SKIN:-}" ]; then
  mkdir -p pay/addons; cp -r "$GUI_SKIN" pay/addons/skin.estuary; rm -rf pay/addons/skin.estuary/.git
  sed -i -E 's/<addon id="skin.estuary" version="[0-9.]+"/<addon id="skin.estuary" version="4.1.99"/' pay/addons/skin.estuary/addon.xml
  grep -q 'version="4.1.99"' pay/addons/skin.estuary/addon.xml || fail_early boot "GUI_SKIN: the version of the skin could not be raised"
  log "# skin from $GUI_SKIN"
fi
tar -C pay -cf payload.tar .
cat > autostart.sh <<'AS'
mkdir -p /storage/pay /storage/shots
tar -xf /storage/payload.tar -C /storage/pay 2>/dev/null
[ -d /storage/pay/addons ] && { mkdir -p /storage/.kodi/addons; cp -r /storage/pay/addons/. /storage/.kodi/addons/; }
( stty -F /dev/ttyS1 raw -echo; while true; do /bin/sh < /dev/ttyS1 > /dev/ttyS1 2>&1; done ) &
AS
debugfs -w -R "mkdir /.config" p2.img > /dev/null 2>&1 || true
debugfs -w -R "write payload.tar /payload.tar" p2.img > /dev/null
debugfs -w -R "write autostart.sh /.config/autostart.sh" p2.img > /dev/null
dd if=p2.img of=vm.img bs=1M oflag=seek_bytes seek=$((s2 * 512)) conv=notrunc status=none
rm -f p1.img p2.img

# --- start the VM
pgrep -f "Xvfb :98" > /dev/null || { setsid nohup Xvfb :98 -screen 0 1920x1080x24 +extension GLX -nolisten tcp > xvfb.log 2>&1 < /dev/null & sleep 2; }
export DISPLAY=:98 LIBGL_ALWAYS_SOFTWARE=1 SDL_VIDEODRIVER=x11
setsid nohup qemu-system-x86_64 -enable-kvm -machine q35 -m 6144 -smp 4 -cpu host \
  -display sdl,gl=on -device virtio-vga-gl,xres=1920,yres=1080 -nic none \
  -audiodev none,id=snd0 -device ich9-intel-hda -device hda-duplex,audiodev=snd0 \
  -monitor unix:$W/qm.sock,server,nowait -serial file:$W/serial.log -serial unix:$W/sh.sock,server,nowait \
  -drive if=none,id=d0,file=$W/vm.img,format=raw -device virtio-blk-pci,drive=d0 \
  -kernel "$KERNEL" -append "boot=UUID=$BOOTUUID disk=UUID=$DISKUUID grub_portable quiet video=Virtual-1:1920x1080 console=ttyS0,115200" \
  > qemu.log 2>&1 < /dev/null &
sleep 4
stopvm() { python3 -I - "$W" << 'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
try:
    s.connect(sys.argv[1] + '/qm.sock'); s.settimeout(3); time.sleep(0.3)
    try: s.recv(65536)
    except Exception: pass
    s.sendall(b'quit\n'); time.sleep(1)
except Exception: pass
PY
}
trap stopvm EXIT
G="python3 -I $KIT/g.py $W"
up=0
for i in $(seq 1 72); do
  sleep 5
  $G "grep -a -c GL_RENDERER /storage/.kodi/temp/kodi.log" 2> /dev/null | tr -d '\r' | grep -qx 1 && { up=1; break; }
done
[ $up = 1 ] || { cp serial.log out/serial.log 2> /dev/null; fail_early boot "Kodi did not write GL_RENDERER within 6 min (out/serial.log)"; }
sleep 20
R=$($G "grep -a GL_RENDERER /storage/.kodi/temp/kodi.log | head -n 1 | cut -c1-160" | tr -d '\r' | grep -a GL_RENDERER)
log "GUISMOKE-ITEM boot: PASSED | $R"

# --- the tests inside Kodi (smoke.py writes /storage/smoke.out, one SMOKE-ITEM line each)
$G "rm -f /storage/smoke.out; kodi-send --action='RunScript(/storage/pay/smoke.py)' > /dev/null"
for i in $(seq 1 60); do
  sleep 5
  $G "grep -a -c '^SMOKE-DONE' /storage/smoke.out 2>/dev/null" | tr -d '\r' | grep -qx 1 && break
done
rm -rf out; mkdir -p out
python3 -I "$KIT/g.py" "$W" -t 120 -get /storage/smoke.out out/smoke.out | tee -a "$L" > /dev/null
# kodi.log is summarised in the guest (a 100 KB+ log over the serial line is not worth it)
$G "grep -a -c -E 'Segmentation fault|terminate called|Unhandled exception|CRASH|Assertion .* failed' /storage/.kodi/temp/kodi.log; grep -a -c -E ' (ERROR|error) <' /storage/.kodi/temp/kodi.log; wc -l < /storage/.kodi/temp/kodi.log; grep -a -E 'Segmentation fault|terminate called|Unhandled exception|CRASH|Assertion .* failed' /storage/.kodi/temp/kodi.log | head -n 3 | cut -c1-200" | sed 's/[^[:print:]]//g' | grep -a -E '^[0-9]+$|^[0-9]{4}-' > out/kodi-summary.txt
shots=$(grep -a -o 'SMOKE-SHOT [a-z_0-9]*' out/smoke.out 2> /dev/null | cut -d' ' -f2)
for s in $shots; do python3 -I "$KIT/g.py" "$W" -t 600 -get /storage/shots/$s.png out/$s.png | tee -a "$L"; done
# --- the verdict, counted from the lines (nothing is passed by default)
python3 -I "$KIT/gui_smoke_verdict.py" out >> "$L"
rc=$?
tail -n 2 "$L"
exit $rc
