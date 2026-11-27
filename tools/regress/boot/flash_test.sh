#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of how the init of the initramfs (packages/sysutils/busybox/scripts/init) mounts the boot partition
# (mount_flash, hades_mount_flash): the wait, the USB controllers taken out of DMA translation when the partition is not
# there (after the wait, or after a shorter one when the kernel log has a fault of the IOMMU for one of them), what is left
# translated and why, and what the screen says when it is not found at all (hades_flash_report). The functions are
# taken from the init (from its comment "# Hades: the boot partition (mount_flash)" to the end of mount_flash, and
# sys_path) and run by the busybox of an initramfs on a /sys and a /proc of files like those of a laptop with an Intel
# Core Ultra (issue 35): the xHCI of the chipset (00:14.0) in a group with its shared memory and its Wi-Fi, the xHCI of
# the Type-C ports (00:0d.0) in a group of its own, the graphics outside the IOMMU. mount, dmesg, blkid and usleep are
# stand-ins: mount succeeds when a case says so (at once, at the second try, never, or once the group of 00:14.0 is in
# passthrough), and
# each usleep is a second of /proc/uptime.
#
# Once the partition is mounted, a start with DMA translation sets kernel.iommu_group_type_locked (hades_iommu_lock), and
# says so in the kernel log when the kernel has no such switch; that is done before /flash/post-flash.sh runs (read in the
# text of mount_flash: the hook is at a fixed path of the boot partition).
#
# Usage: flash_test.sh <busybox> <init>
# Then each of its mutations (a copy of the init with one rule taken out) must fail at least one check: "mutation Mn
# caught". Exit 0 when every check passes and every mutation is caught, 1 otherwise, 2 when the test could not run.
set -u
export LC_ALL=C
BBI=${1:-}; INIT=${2:-}
[ -x "$BBI" ] && [ -f "$INIT" ] || { echo "usage: $0 <busybox> <init>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $("$BBI" --list); do ln -s "$BBI" "$T/bb/$a"; done

# the functions of the init under test ($1) into $2
extract() {
  awk '/^sys_path\(\) \{$/ {on = 1} on {print} on && /^\}$/ {exit}' "$1" > "$2"
  awk '/^# Hades: the boot partition \(mount_flash\)/ {on = 1} on {print} on && /^mount_flash\(\) \{$/ {last = 1} last && /^\}$/ {exit}' "$1" >> "$2"
  grep -q '^hades_mount_flash() {$' "$2" && grep -q '^mount_flash() {$' "$2" && [ "$(tail -n 1 "$2")" = "}" ]
}

R=$T/root
pcidev() { # address class vendor device [driver] [group]
  local d=$R/sys/devices/pci0000:00/$1
  mkdir -p "$d"
  echo "$2" > "$d/class"; echo "$3" > "$d/vendor"; echo "$4" > "$d/device"
  ln -s "../../../devices/pci0000:00/$1" "$R/sys/bus/pci/devices/$1"
  if [ -n "${5:-}" ]; then
    mkdir -p "$R/sys/bus/pci/drivers/$5"; : >> "$R/sys/bus/pci/drivers/$5/bind"; : >> "$R/sys/bus/pci/drivers/$5/unbind"
    ln -s "../../../bus/pci/drivers/$5" "$d/driver"
  fi
  if [ -n "${6:-}" ]; then
    mkdir -p "$R/sys/kernel/iommu_groups/$6/devices"
    ln -s "../../../kernel/iommu_groups/$6" "$d/iommu_group"
    ln -s "../../../../devices/pci0000:00/$1" "$R/sys/kernel/iommu_groups/$6/devices/$1"
  fi
}
group() { echo "$2" > "$R/sys/kernel/iommu_groups/$1/type"; }
# the computer of issue 35, as far as this needs it; $1: the kind of domain of the groups (DMA-FQ for Run, identity for
# the entry "Run (internal disks read-only)")
laptop() {
  rm -rf -- "$R"; mkdir -p "$R/sys/bus/pci/devices" "$R/sys/block" "$R/sys/bus/usb/devices" "$R/proc/sys/kernel"
  pcidev 0000:00:14.0 0x0c0330 0x8086 0x7e7d xhci_pch 7
  pcidev 0000:00:14.2 0x050000 0x8086 0x7e7f "" 7
  pcidev 0000:00:14.3 0x028000 0x8086 0x7e40 "" 7
  pcidev 0000:00:0d.0 0x0c0330 0x8086 0x7ec0 xhci_tcss 5
  pcidev 0000:00:02.0 0x030000 0x8086 0x7d51 i915
  group 7 "$1"; group 5 "$1"
  echo "10.00 20.00" > "$R/proc/uptime"
  echo "7.4.0-rc4" > "$R/proc/sys/kernel/osrelease"
  if [ "$1" = identity ]; then
    echo "boot=UUID=2611-1611 disk=UUID=x grub_portable quiet" > "$R/proc/cmdline"
  else
    echo "boot=UUID=2611-1611 disk=UUID=x grub_portable quiet initcall_blacklist=nvme_init,ahci_pci_driver_init lockdown=integrity iommu.passthrough=0 intel_iommu=on,igfx_off" > "$R/proc/cmdline"
  fi
  : > "$R/kmsg"; : > "$R/dmesg"; : > "$R/blkid"
}
# a USB stick that the kernel sees (sda, its partitions, its USB device)
stick() {
  mkdir -p "$R/sys/devices/pci0000:00/0000:00:14.0/usb3/3-1/3-1:1.0/host0/target0:0:0/0:0:0:0/block/sda"
  ln -s "../devices/pci0000:00/0000:00:14.0/usb3/3-1/3-1:1.0/host0/target0:0:0/0:0:0:0/block/sda" "$R/sys/block/sda"
  echo 60751872 > "$R/sys/block/sda/size"
  local u=$R/sys/devices/pci0000:00/0000:00:14.0/usb3/3-1
  echo 0951 > "$u/idVendor"; echo 1666 > "$u/idProduct"; echo DataTraveler 3.0 > "$u/product"; echo 5000 > "$u/speed"
  echo 08 > "$u/3-1:1.0/bInterfaceClass"
  ln -s "../../../devices/pci0000:00/0000:00:14.0/usb3/3-1" "$R/sys/bus/usb/devices/3-1"
  ln -s "../../../devices/pci0000:00/0000:00:14.0/usb3" "$R/sys/bus/usb/devices/usb3"
  printf '%s\n' '/dev/sda1: LABEL="LIBREELEC" UUID="2611-1611" TYPE="vfat"' '/dev/sda2: LABEL="STORAGE" UUID="x" TYPE="ext4"' > "$R/blkid"
}

# the script that a case runs: the functions, the stand-ins, and $1 (what it calls)
harness() {
  cat > "$T/run.sh" <<EOF
. "$T/fn.sh"
HADES_BOOT_SYS=$R/sys
HADES_BOOT_PROC=$R/proc
HADES_BOOT_KMSG=$R/kmsg
HADES_FLASH_WAIT=3
HADES_FLASH_FAULT_WAIT=2
MACHINE_UID=
progress() { :; }
wakeonlan() { :; }
error() { echo "*** Error in mount_flash: \$1: \$2 ***" >&2; return 1; }
mount_part() { echo "mount_part \$*" >> "$R/calls"; }
dmesg() { cat "$R/dmesg"; }
blkid() { cat "$R/blkid"; }
usleep() {
  local u r
  read u r < "$R/proc/uptime"; u=\${u%.*}
  echo "\$((u + 1)).00 0" > "$R/proc/uptime"
  echo s >> "$R/sleeps"
}
mount() {
  echo "mount \$*" >> "$R/calls"
  case "\$(cat "$R/when" 2>/dev/null)" in
    now) return 0 ;;
    second)
      echo t >> "$R/tries"
      [ \$(grep -c . "$R/tries") -ge 2 ] && return 0 ;;
    passthrough)
      if [ "\$(cat "$R/sys/kernel/iommu_groups/7/type")" = identity ]; then
        echo a >> "$R/after"
        [ \$(grep -c . "$R/after") -ge 3 ] && return 0
      fi ;;
  esac
  echo "mount: mounting \$3 on \$4 failed: No such file or directory" >&2
  return 1
}
$1
echo "returned \$?"
EOF
}
run() { # what to call
  harness "$1"
  rm -f "$R/calls" "$R/sleeps" "$R/after" "$R/tries"
  env -i PATH="$T/bb" "$T/bb/sh" "$T/run.sh" > "$T/out" 2>&1
}

FAILS=0
NOTRUN=0
check() {
  if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); [ -n "${FLASH_DEBUG:-}" ] && sed 's/^/     | /' "$T/out"; fi
}
has() { grep -q -F -- "$1" "$T/out"; }
kmsg() { grep -q -F -- "$1" "$R/kmsg" && ! grep -q -v '^hades-boot: ' "$R/kmsg"; }
sleeps() { [ -f "$R/sleeps" ] && grep -c . "$R/sleeps" || echo 0; }
unbound() { cat "$R/sys/bus/pci/drivers/$1/unbind"; }
bound() { cat "$R/sys/bus/pci/drivers/$1/bind"; }

cases() {
  extract "$1" "$T/fn.sh" || { echo "  FAIL the functions are not in the init"; FAILS=$((FAILS + 1)); return; }

  echo "1. Run, the stick there at once: mounted on the first try, nothing else done or said"
  laptop DMA-FQ; stick; echo now > "$R/when"
  run 'hades_mount_flash UUID=2611-1611'
  has "returned 0" && [ "$(sleeps)" = 0 ] && [ ! -s "$R/kmsg" ] && [ "$(cat "$R/sys/kernel/iommu_groups/7/type")" = DMA-FQ ] &&
    [ -z "$(unbound xhci_pch)$(unbound xhci_tcss)" ] && grep -q -x "mount -o ro,noatime UUID=2611-1611 /flash" "$R/calls"
  check "1 at once" $?

  echo "2. Run, never there, no fault: after the wait both groups of USB go to passthrough (one each), then the report"
  laptop DMA-FQ
  printf '%s\n' "<6>[    0.201310] iommu: Default domain type: Translated (set via kernel command line)" \
    "<3>[    3.100000] usb 3-1: device descriptor read/64, error -110" \
    "<3>[   25.422810] sd 0:0:0:0: [sda] Synchronize Cache(10) failed: Result: hostbyte=0x01 driverbyte=DRIVER_OK" > "$R/dmesg"
  run 'hades_mount_flash UUID=2611-1611'
  has "returned 1" && [ "$(sleeps)" = 4 ] &&
    [ "$(cat "$R/sys/kernel/iommu_groups/7/type")" = identity ] && [ "$(cat "$R/sys/kernel/iommu_groups/5/type")" = identity ] &&
    [ "$(unbound xhci_pch)" = 0000:00:14.0 ] && [ "$(unbound xhci_tcss)" = 0000:00:0d.0 ] &&
    [ "$(bound xhci_pch)" = 0000:00:14.0 ] && [ "$(bound xhci_tcss)" = 0000:00:0d.0 ] &&
    kmsg "LibreELEC Hades: no boot drive with DMA translation on the USB controllers (none after 3 s)"
  check "2a passthrough after the wait, drivers let go and taken again" $?
  has "00:14.0 [8086:7e7d], 00:14.2 [8086:7e7f], 00:14.3 [8086:7e40] (group 7)" && has "00:0d.0 [8086:7ec0] (group 5)"
  check "2b the devices of each group are named" $?
  has "LibreELEC Hades did not find its boot partition UUID=2611-1611 (waited 4 s; 14 s since the kernel started)." &&
    ! has "mount said" &&
    has "Entry: internal disks hidden, kernel locked down, DMA translation; kernel 7.4.0-rc4" &&
    has "Disks: none. File systems: none" &&
    has "USB controllers: 00:0d.0 [8086:7ec0] xhci_tcss identity, 00:14.0 [8086:7e7d] xhci_pch identity. USB devices: 0; storage: none" &&
    has "Kernel log: 0 fault(s) of the IOMMU, 1 error(s) of USB" &&
    has "First error: usb 3-1: device descriptor read/64, error -110" && ! has "First fault" &&
    has "The USB controllers were taken out of DMA translation, and the drive is still not there." &&
    has 'Try: restart, choose "Run (internal disks read-only)", or another USB port. A photo of this screen is a report.' &&
    has "*** Error in mount_flash: mount_common: Could not mount UUID=2611-1611 ***"
  check "2c the report on the screen, before the error" $?
  [ "$(grep -n 'Try: restart' "$T/out" | cut -d: -f1)" -lt "$(grep -n 'Error in mount_flash' "$T/out" | cut -d: -f1)" ] &&
    kmsg "Try: restart"
  check "2d the report comes before the shell, and is in the kernel log too" $?

  echo "3. Run, the stick only seen in passthrough: found, the wait begun again, and said"
  laptop DMA-FQ; echo passthrough > "$R/when"
  run 'hades_mount_flash UUID=2611-1611'
  has "returned 0" && kmsg "the boot partition was found with the USB controllers in passthrough" && ! has "did not find"
  check "3 found after the step" $?

  echo "4. Run, a fault of the IOMMU for 00:14.0 in the kernel log: the step after the shorter wait (2 s), not the whole one"
  laptop DMA-FQ; echo passthrough > "$R/when"
  echo "<3>[    2.500000] DMAR: [DMA Read NO_PASID] Request device [00:14.0] fault addr 0x7a3f1000 [fault reason 0x06] PTE Read access is not set" > "$R/dmesg"
  run 'hades_mount_flash UUID=2611-1611'
  # (1 s before the step, 2 after it until the third try in passthrough)
  has "returned 0" && [ "$(sleeps)" = 3 ] && kmsg "(an IOMMU fault for one of them in the kernel log, none after 2 s)"
  r=$?; check "4 fault of the IOMMU ($(sleeps) s)" $r

  echo "4b. Run, the same fault, the stick there at the second try: nothing changed, nothing said"
  laptop DMA-FQ; stick; echo second > "$R/when"
  echo "<3>[    2.500000] DMAR: [DMA Read NO_PASID] Request device [00:14.0] fault addr 0x7a3f1000 [fault reason 0x06] PTE Read access is not set" > "$R/dmesg"
  run 'hades_mount_flash UUID=2611-1611'
  has "returned 0" && [ "$(sleeps)" = 1 ] && [ ! -s "$R/kmsg" ] && [ "$(cat "$R/sys/kernel/iommu_groups/7/type")" = DMA-FQ ] &&
    [ -z "$(unbound xhci_pch)$(unbound xhci_tcss)" ]
  check "4b a fault, the drive slow: translation kept" $?

  echo "5. Run, a fault of the IOMMU for another device (00:1f.3): the step only after the wait"
  laptop DMA-FQ; echo passthrough > "$R/when"
  echo "<3>[    2.500000] DMAR: [DMA Write NO_PASID] Request device [00:1f.3] fault addr 0x1000 [fault reason 0x05] PTE Write access is not set" > "$R/dmesg"
  run 'hades_mount_flash UUID=2611-1611'
  has "returned 0" && [ "$(sleeps)" -ge 3 ] && kmsg "(none after 3 s)"
  r=$?; check "5 a fault of another device ($(sleeps) s)" $r

  echo "6. The entry Run (internal disks read-only), never there: nothing changed, the report says the entry"
  laptop identity
  run 'hades_mount_flash UUID=2611-1611'
  has "returned 1" && [ "$(sleeps)" = 2 ] && [ -z "$(unbound xhci_pch)$(unbound xhci_tcss)" ] &&
    [ "$(cat "$R/sys/kernel/iommu_groups/7/type")" = identity ] && has "Entry: as LibreELEC starts; kernel" &&
    ! has "The USB controllers" && ! kmsg "LibreELEC Hades: no boot drive with"
  check "6 internal entry" $?

  echo "7. A controller of Renesas (it takes its firmware from a file) in its group: left translated, and said"
  laptop DMA-FQ
  pcidev 0000:05:00.0 0x0c0330 0x1912 0x0014 xhci_renesas 9; group 9 DMA-FQ
  run 'hades_mount_flash UUID=2611-1611'
  [ "$(cat "$R/sys/kernel/iommu_groups/9/type")" = DMA-FQ ] && [ -z "$(unbound xhci_renesas)" ] &&
    has "05:00.0 [1912:0014]: 05:00.0 [1912:0014] is a controller of Renesas, which takes its firmware from a file" &&
    [ "$(cat "$R/sys/kernel/iommu_groups/7/type")" = identity ]
  check "7 Renesas kept" $?

  echo "8. A group with graphics that a driver has: left translated, and said"
  laptop DMA-FQ
  pcidev 0000:c4:00.0 0x030000 0x1002 0x164e amdgpu 11
  pcidev 0000:c4:00.3 0x0c0330 0x1022 0x15b6 xhci_amd 11; group 11 DMA-FQ
  run 'hades_mount_flash UUID=2611-1611'
  [ "$(cat "$R/sys/kernel/iommu_groups/11/type")" = DMA-FQ ] && [ -z "$(unbound amdgpu)" ] &&
    [ -z "$(unbound xhci_amd)" ] && has "its group has the graphics c4:00.0 [1002:164e], which would lose the screen"
  check "8 graphics kept" $?

  echo "9. A bridge in the group of a controller: its port driver is not let go of"
  laptop DMA-FQ
  pcidev 0000:00:1c.0 0x060400 0x8086 0x7e38 pcieport 13
  pcidev 0000:02:00.0 0x0c0330 0x1b21 0x2142 xhci_asm 13; group 13 DMA-FQ
  run 'hades_mount_flash UUID=2611-1611'
  [ "$(cat "$R/sys/kernel/iommu_groups/13/type")" = identity ] && [ -z "$(unbound pcieport)" ] &&
    [ "$(unbound xhci_asm)" = 0000:02:00.0 ] && [ "$(bound xhci_asm)" = 0000:02:00.0 ]
  check "9 bridge kept bound" $?

  echo "10. The kernel refuses passthrough to a group: left translated, said, and the driver takes it again"
  if [ "$(id -u)" = 0 ]; then
    echo "  not run: a file that cannot be written stands for the refusal, and root writes it"; NOTRUN=$((NOTRUN + 1))
  else
  laptop DMA-FQ
  chmod 444 "$R/sys/kernel/iommu_groups/5/type"
  run 'hades_mount_flash UUID=2611-1611'
  [ "$(cat "$R/sys/kernel/iommu_groups/5/type")" = DMA-FQ ] && [ "$(unbound xhci_tcss)" = 0000:00:0d.0 ] &&
    [ "$(bound xhci_tcss)" = 0000:00:0d.0 ] &&
    has "00:0d.0 [8086:7ec0]: the kernel kept its group translated"
  check "10 refused" $?
  fi

  echo "11. The report with the stick seen: its disk, file systems, USB device, the fault of the IOMMU, and what mount said"
  laptop DMA-FQ; stick
  echo "<3>[    2.500000] DMAR: [DMA Read NO_PASID] Request device [00:14.0] fault addr 0x7a3f1000 [fault reason 0x06] PTE Read access is not set" > "$R/dmesg"
  run 'hades_mount_flash UUID=2611-1611'
  has "Disks: sda USB 31.1 GB. File systems: sda1 vfat LIBREELEC 2611-1611; sda2 ext4 STORAGE x" &&
    has "USB devices: 1; storage: 3-1 [0951:1666] DataTraveler 3.0 5000 Mb/s" &&
    has "Kernel log: 1 fault(s) of the IOMMU, 0 error(s) of USB" &&
    has "First fault: DMAR: [DMA Read NO_PASID] Request device [00:14.0] fault addr 0x7a3f1000" &&
    has "mount said: mount: mounting UUID=2611-1611 on /flash failed: No such file or directory"
  check "11 report of what is seen" $?

  echo "12. mount_flash: UUID= and LABEL= are waited for here, a network boot partition as before"
  laptop DMA-FQ; echo now > "$R/when"
  run 'boot=UUID=2611-1611; mount_flash'
  grep -q -x "mount -o ro,noatime UUID=2611-1611 /flash" "$R/calls" && ! grep -q mount_part "$R/calls"
  r1=$?
  run 'boot=LABEL=LIBREELEC; mount_flash'
  grep -q -x "mount -o ro,noatime LABEL=LIBREELEC /flash" "$R/calls"
  r2=$?
  run 'boot=NFS=10.0.0.1:/srv; mount_flash'
  grep -q -x "mount_part NFS=10.0.0.1:/srv /flash ro,noatime" "$R/calls"
  r3=$?
  [ $r1 = 0 ] && [ $r2 = 0 ] && [ $r3 = 0 ]
  check "12 the kinds of boot= ($r1 $r2 $r3)" $?

  echo "13. Once it is mounted, a start with DMA translation keeps the groups of the IOMMU as they are; others do not"
  L=$R/proc/sys/kernel/iommu_group_type_locked
  laptop DMA-FQ; echo now > "$R/when"
  run 'boot=UUID=2611-1611; mount_flash'
  [ "$(cat "$L" 2>/dev/null)" = 1 ] && [ ! -s "$R/kmsg" ]
  r1=$?
  laptop identity; echo now > "$R/when"
  run 'boot=UUID=2611-1611; mount_flash'
  [ ! -e "$L" ]
  r2=$?
  laptop DMA-FQ; echo now > "$R/when"; mkdir "$L"
  run 'boot=UUID=2611-1611; mount_flash'
  grep -q -x "hades-iommu: this kernel cannot keep the domains of the IOMMU as they are (kernel.iommu_group_type_locked): a program that runs as root can take a device out of DMA translation" "$R/kmsg"
  r3=$?
  [ $r1 = 0 ] && [ $r2 = 0 ] && [ $r3 = 0 ]
  check "13 the switch of the IOMMU ($r1 $r2 $r3)" $?

  echo "14. The groups of the IOMMU are kept as they are before /flash/post-flash.sh runs"
  r1=$(awk '/^mount_flash\(\) \{$/ {on = 1} on && /^  hades_iommu_lock$/ {print NR; exit}' "$T/fn.sh")
  r2=$(awk '/^mount_flash\(\) \{$/ {on = 1} on && /^ *\. \/flash\/post-flash\.sh$/ {print NR; exit}' "$T/fn.sh")
  [ -n "$r1" ] && [ -n "$r2" ] && [ "$r1" -lt "$r2" ]
  check "14 the switch before the hook (lines ${r1:-none} and ${r2:-none})" $?
}

echo "init: $INIT"
"$T/bb/sh" -n "$INIT" || { echo "the busybox finds a syntax error in the init"; exit 1; }
cases "$INIT"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"

# mutation <n> <what> <sed program>: the init with one rule taken out must fail a check
MUT_MISSED=0
mutation() {
  local m=$T/mutant-$1
  sed -e "$3" "$INIT" > "$m"
  if cmp -s "$m" "$INIT"; then echo "mutation M$1 ($2): its text is no longer in the init: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  FAILS=0
  cases "$m" > "$T/mutant-$1.log" 2>&1
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
}
mutation 1 "no step on a fault of the IOMMU before the whole wait" 's/^      if hades_usb_faulted && \[ \$n -ge \$HADES_FLASH_FAULT_WAIT \]; then$/      if false; then/'
mutation 2 "no step after the wait" 's/^      elif \[ \$n -ge \$HADES_FLASH_WAIT \]; then$/      elif false; then/'
mutation 3 "a controller of Renesas taken out" 's/^        0x1912\/0x0c03\*) why=/        0xffff\/0x0c03*) why=/'
mutation 4 "a group with graphics taken out" 's/^        \*\/0x03\*) \[ -e /        *\/0xzz*) [ -e /'
mutation 5 "the driver of a bridge let go of" 's/^        0x0604\* | 0x0609\*) continue ;;$/        0xzzzz) continue ;;/'
mutation 6 "the drivers not taken again" 's/^    for x in \$took; do$/    for x in ; do/'
mutation 7 "the wait not begun again after the step" 's/^          n=0$/          :/'
mutation 8 "a group in passthrough already counted as translated" 's/^      DMA | DMA-FQ) echo "\$d" ;;$/      *) echo "$d" ;;/'
mutation 9 "no report before the shell" 's/^      hades_flash_report "\$1" "\$t0" "\$out"$/      :/'
mutation 10 "the boot partition by its file system not waited for here" 's/^    UUID=\* | LABEL=\*) hades_mount_flash /    NONE=*) hades_mount_flash /'
mutation 11 "any line of the IOMMU with the word fault counted as a fault" 's/|IO_PAGE_FAULT|iommu fault'"'"'$/|[Ii][Oo][Mm][Mm][Uu].*[Ff]ault'"'"'/'
mutation 12 "the level of each line of dmesg kept" 's/^  log=\$(hades_boot_klog | sed -e '"'"'s\/^<\[0-9\]\*>\/\/'"'"' /  log=$(hades_boot_klog | sed /'
mutation 13 "the groups of the IOMMU not kept as they are" 's/^  hades_iommu_lock$/  :/'
mutation 15 "the step at once on a fault of the IOMMU, the drive not waited for" 's/^      if hades_usb_faulted && \[ \$n -ge \$HADES_FLASH_FAULT_WAIT \]; then$/      if hades_usb_faulted; then/'
mutation 14 "what mount said not shown when the partition is there" 's/^    \*" \${1#\*=}"\*) \[ -n /    *NONE*) [ -n /'
mutation 16 "the groups of the IOMMU kept as they are only after the hook" '/^  hades_iommu_lock$/d; s#^  if \[ -f /flash/post-flash\.sh \]; then$#  if [ -f /flash/post-flash.sh ]; then\n    . /flash/post-flash.sh\n  fi\n  hades_iommu_lock\n  if false; then#'
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && [ $NOTRUN = 0 ] && { echo "FLASH PASSED"; exit 0; }
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "FLASH PASSED, BUT $NOTRUN CHECK(S) NOT RUN"; exit 1; }
echo "FLASH FAILED: $TREE_FAILS check(s) of the init failed, $MUT_MISSED mutation(s) not caught"
exit 1
