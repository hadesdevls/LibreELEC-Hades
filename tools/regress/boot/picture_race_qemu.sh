#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# LibreELEC Hades: Kodi's "hades-picture release" that comes while a picture of the start is being drawn, in QEMU/KVM.
# Usage: picture_race_qemu.sh <image.img.gz> [<kernel file>]     (REGRESS_LE_BUILD: a build folder, for its toolchain)
# hades-picture.c of this tree and two mutants of it are built with the toolchain of the build folder and run in a guest of
# the image, which has two outputs of different sizes (virtio-vga, the second forced on), so that the pictures are held
# through DRM. The service (by hand, its own socket) is asked for a picture of the start that takes seconds to load
# (12000x12000), and "release" comes 0.4 s later, before HELD_FLAG is written: as on 22 November 2026 in issue 14, where
# Kodi then could not get the DRM master and started on no display behind the picture of the start. What is checked,
# from the DRM clients in debugfs, at the moment "release" has answered (when Kodi would start and ask for the DRM
# master: RELEASE-MASTER) and once the picture is shown (RACE-MASTER):
#   tree     the service holds no DRM master when "release" answers, nor after, and says that Kodi was started meanwhile
#   mutant   the code of 1.1 (no BUSY_FLAG, no second look at KODI_FLAG in show()): the service holds the DRM master
#            after the picture is shown (the fault of 1.1 is there: the test can see it)
#   nowait   the code before BUSY_FLAG ("release" does not wait for a picture that is being drawn): the service lets go
#            once the picture is shown, but holds the DRM master when "release" answers, when Kodi asks for it (the
#            time that was left: the test can see it)
#   normal   with the tree: a picture of the start, then "release" (held: the service is asked and lets go), then
#            another picture of the start (refused: Kodi was started)
# One VM at a time (it refuses to start while any other qemu-system-x86_64 runs). About 2 minutes. No display window.
set -uo pipefail
IMGGZ=${1:?usage: picture_race_qemu.sh <image.img.gz> [kernel]}
KERNEL=${2:-${IMGGZ%.img.gz}.kernel}
B=${REGRESS_LE_BUILD:-}
W=${PICTURE_RACE_W:-${HADES_REGRESS:-$HOME/hades-regress}/picture-race}
KIT=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$KIT/../../.." && pwd)
export LC_ALL=C PATH=$PATH:/usr/sbin:/sbin
mkdir -p "$W" && cd "$W" || exit 2
L=$W/picture-race.log
: > "$L"
log() { echo "$*" | tee -a "$L"; }
P=0 F=0 N=0
item() {
  log "PICTURERACE-ITEM $1: $2 | $3"
  case $2 in PASSED) P=$((P + 1)) ;; FAILED) F=$((F + 1)) ;; *) N=$((N + 1)) ;; esac
}
finish() {
  [ -n "${QPID:-}" ] && kill "$QPID" 2> /dev/null
  log "PICTURERACE-DONE $P passed, $F failed, $N not run"
  if [ $F -eq 0 ] && [ $N -eq 0 ] && [ $P -eq 4 ]; then log "PICTURERACE-VERDICT PASSED"; exit 0; fi
  log "PICTURERACE-VERDICT FAILED"; exit 1
}
early() { for i in tree mutant nowait normal; do item $i NOT-RUN "$1"; done; finish; }
[ -f "$IMGGZ" ] || early "no image $IMGGZ"
[ -f "$KERNEL" ] || early "no kernel $KERNEL"
[ -n "$B" ] && [ -d "$B/toolchain" ] || early "REGRESS_LE_BUILD is not a build folder with a toolchain"
for t in qemu-system-x86_64 sfdisk debugfs resize2fs e2fsck blkid python3; do command -v $t > /dev/null 2>&1 || [ -x /usr/sbin/$t ] || early "missing program $t"; done
[ -c /dev/kvm ] && [ -w /dev/kvm ] || early "no usable /dev/kvm"
pgrep -f '^qemu-system-x86_64' > /dev/null && early "another qemu-system-x86_64 runs (one VM at a time): wait for it"
log "# image $(basename "$IMGGZ") sha256 $(sha256sum "$IMGGZ" | cut -c1-16)... tree $(git -C "$TREE" log -1 --format=%h 2> /dev/null) start $(date -u +%FT%TZ)"

# --- hades-picture of the tree and its mutant, with the toolchain of the build folder (as package.mk builds it)
SR=$B/toolchain/x86_64-libreelec-linux-gnu/sysroot
CC=$B/toolchain/bin/x86_64-libreelec-linux-gnu-gcc
S=$TREE/packages/sysutils/hades-picture/src
rm -rf pay; mkdir pay
python3 -I - "$S/hades-picture.c" mutant.c nowait.c << 'PY' || early "the mutants could not be made (their text is no longer in hades-picture.c: write it again)"
import sys
s = open(sys.argv[1]).read()
look = "\tif (boot && access(KODI_FLAG, F_OK) == 0) {\n\t\tint n = let_go();"
busy = "if (access(HELD_FLAG, F_OK) && access(BUSY_FLAG, F_OK)) {"
assert s.count(look) == 1 and s.count(busy) == 1, "mutant text not found once"
nowait = s.replace(busy, "if (access(HELD_FLAG, F_OK)) {")
open(sys.argv[3], "w").write(nowait)
open(sys.argv[2], "w").write(nowait.replace(look, "\tif (0) {\n\t\tint n = let_go();"))
PY
for v in tree mutant nowait; do
  src=$S/hades-picture.c; [ $v != tree ] && src=$v.c
  "$CC" -O2 -std=gnu11 -Wall -Wextra -I"$SR/usr/include/libdrm" -I"$SR/usr/include" -I"$S" -o pay/hades-picture-$v "$src" \
    "$S/picture.c" -L"$SR/usr/lib" -ldrm -Wl,-Bstatic -lpng16 -lz -Wl,-Bdynamic -lm > cc-$v.log 2>&1 || early "hades-picture ($v) did not compile: $W/cc-$v.log"
done
# a picture of the start that takes seconds to load: 12000x12000, one colour (a small file)
python3 -I - pay/slow.png << 'PY' || early "cannot make the slow picture"
import sys, zlib, struct
w = h = 12000
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
c = zlib.compressobj(9)
row = b'\x00' + bytes([40, 40, 60]) * w
data = b''.join([c.compress(row) for _ in range(h)] + [c.flush()])
open(sys.argv[1], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', data) + chunk(b'IEND', b''))
PY
cp "$KIT/picture_race_guest.sh" pay/race.sh
tar -C pay -cf payload.tar .

# --- the guest disk: the image with a bigger storage partition, a root shell on the 2nd serial port, the payload
rm -f vm.img p1.img p2.img qm.sock sh.sock serial.log qemu.log
gzip -dc "$IMGGZ" > vm.img || early "cannot unpack the image"
truncate -s 3G vm.img
field() { sfdisk -d vm.img 2> /dev/null | awk -v d="vm.img$1" -v f="$2" '$1 == d { for (i = 1; i <= NF; i++) if ($i ~ "^" f "=") { v = $(i+1); if ($i != f "=") v = substr($i, length(f) + 2); gsub(/[ ,]/, "", v); print v } }'; }
s1=$(field 1 start); n1=$(field 1 size); s2=$(field 2 start); n2=$(field 2 size)
[ -n "$s1" ] && [ -n "$s2" ] || early "cannot read the partition table of the image"
sfdisk -q --relocate gpt-bak-std vm.img 2> /dev/null
echo ",+" | sfdisk -q -N 2 --no-reread vm.img > /dev/null 2>&1
n2new=$(field 2 size)
dd if=vm.img of=p1.img bs=1M iflag=skip_bytes,count_bytes skip=$((s1 * 512)) count=$((n1 * 512)) status=none
dd if=vm.img of=p2.img bs=1M iflag=skip_bytes,count_bytes skip=$((s2 * 512)) count=$((n2 * 512)) status=none
truncate -s $((n2new * 512)) p2.img; e2fsck -fy p2.img > /dev/null 2>&1 || true; resize2fs -f p2.img > /dev/null 2>&1
BOOTUUID=$(blkid -p -o value -s UUID p1.img 2> /dev/null); DISKUUID=$(blkid -p -o value -s UUID p2.img 2> /dev/null)
[ -n "$BOOTUUID" ] && [ -n "$DISKUUID" ] || early "cannot read the UUIDs of the boot and storage partitions"
debugfs -w -R "rm /.please_resize_me" p2.img > /dev/null 2>&1 || true
cat > autostart.sh << 'AS'
mkdir -p /storage/pay; tar -xf /storage/payload.tar -C /storage/pay 2>/dev/null
( stty -F /dev/ttyS1 raw -echo; while true; do /bin/sh < /dev/ttyS1 > /dev/ttyS1 2>&1; done ) &
AS
debugfs -w -R "mkdir /.config" p2.img > /dev/null 2>&1 || true
debugfs -w -R "write payload.tar /payload.tar" p2.img > /dev/null 2>&1
debugfs -w -R "write autostart.sh /.config/autostart.sh" p2.img > /dev/null 2>&1
dd if=p2.img of=vm.img bs=1M oflag=seek_bytes seek=$((s2 * 512)) conv=notrunc status=none
rm -f p1.img p2.img

# --- the guest
qemu-system-x86_64 -enable-kvm -machine q35 -m 4096 -smp 4 -cpu host \
  -display none -device virtio-vga,max_outputs=2,xres=1920,yres=1080 -nic none \
  -monitor unix:$W/qm.sock,server,nowait -serial file:$W/serial.log -serial unix:$W/sh.sock,server,nowait \
  -drive if=none,id=d0,file=$W/vm.img,format=raw -device virtio-blk-pci,drive=d0 \
  -kernel "$KERNEL" -append "boot=UUID=$BOOTUUID disk=UUID=$DISKUUID grub_portable quiet video=Virtual-1:1920x1080 video=Virtual-2:1280x720e console=ttyS0,115200" \
  > qemu.log 2>&1 < /dev/null &
QPID=$!
# one command in the guest, through its root shell; its output on stdout
g() {
  python3 -I - "$W/sh.sock" "$1" "${2:-60}" << 'PY'
import socket, sys, time, uuid
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(2); time.sleep(0.5)
tag = 'DONE' + uuid.uuid4().hex[:8]
s.sendall(('\n' + sys.argv[2] + '\necho DO""NE' + tag[4:] + '\n').encode())
buf = b''; t0 = time.time()
while time.time() - t0 < float(sys.argv[3]) and tag.encode() not in buf:
    try:
        d = s.recv(65536)
    except socket.timeout:
        continue
    if not d:
        break
    buf += d
out = buf.decode('utf-8', 'replace')
print(out.split(tag)[0] if tag in out else out + '\n[TIMEOUT]')
PY
}
up=""
for i in $(seq 1 40); do
  sleep 3
  [ -S sh.sock ] && g "test -d /storage/pay && echo GUEST-UP" 10 2> /dev/null | grep -q '^GUEST-UP' && { up=1; break; }
done
if [ -z "$up" ]; then
  for i in tree mutant nowait normal; do item $i NOT-RUN "the guest did not come up (serial.log)"; done
  finish
fi
for v in tree mutant nowait; do
  g "sh /storage/pay/race.sh /storage/pay/hades-picture-$v" 120 > race-$v.log
  m=$(sed -n 's/^RACE-MASTER //p' race-$v.log | tr -d '\r' | tail -1)
  rm=$(sed -n 's/^RELEASE-MASTER //p' race-$v.log | tr -d '\r' | tail -1)
  said=$(grep -c 'Kodi was started meanwhile' race-$v.log)
  case $v in
    tree)
      [ "$rm" = n ] && [ "$m" = n ] && [ "$said" -ge 1 ] && item tree PASSED "the DRM master free when release answered (master=$rm) and after (master=$m)" || item tree FAILED "at release master=${rm:-?}, after master=${m:-?}, said=$said (race-tree.log)" ;;
    mutant)
      [ "$m" = y ] && item mutant PASSED "the mutant keeps the DRM master, as 1.1 did (master=$m)" || item mutant FAILED "master=${m:-?}: the test does not see the fault (race-mutant.log)" ;;
    nowait)
      [ "$rm" = y ] && [ "$m" = n ] && item nowait PASSED "without BUSY_FLAG the service holds the DRM master when release answers (master=$rm), and lets go later (master=$m)" || item nowait FAILED "at release master=${rm:-?}, after master=${m:-?}: the test does not see the time that was left (race-nowait.log)" ;;
  esac
done
g "sh /storage/pay/race.sh /storage/pay/hades-picture-tree normal" 120 > race-normal.log
if grep -q '^NORMAL-1 ok kms 2 display' race-normal.log && grep -q '^NORMAL-2 ok released: 2 picture' race-normal.log && \
   grep -q '^NORMAL-3 no Kodi was started' race-normal.log && grep -q '^RACE-MASTER n' race-normal.log; then
  item normal PASSED "shown, released through the service, then refused once Kodi was started"
else
  item normal FAILED "race-normal.log"
fi
g "poweroff -f" 5 > /dev/null 2>&1
sleep 3
finish
