#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# boot_test.sh: a host test of the installer of the image (packages/tools/installer/scripts: installer-bootmenus and
# the installer itself) on the boot menus of one medium, with the busybox of the image, in a folder of its own. Nothing
# outside that folder is written: the installer's paths are moved into a fake medium there, and the tools that write a
# disk are stubs that log their calls.
#   1  the medium's menus give, for an internal disk, what the installer for Windows gives (Engine.ForInternalDisk: the
#      parameter that hides the internal disks replaced by hades.otherdisks=hide, without its padding), with the
#      identifiers as placeholders, and what installer-bootmenus adds to menus older than those of today (the lockdown
#      and the DMA translation on Run and Live where they lack them; the two lines that start the kernel of the
#      system partition in each GRUB entry)
#   2  a medium last started in Live, or with the internal disks shown by default, still gives Run
#   3  the installer run from an installed system (its menus already have hades.otherdisks=hide) gives the same
#   4  menus without the hiding, the lockdown and the DMA translation (as scripts/mkimage leaves them in the .ova) get them
#   5  what is refused, with nothing on standard output (7)
#   6  mutations of installer-bootmenus that the checks above must catch (9)
#   7  the installer's two functions of the boot menus on the fake medium (12; 13 with the look of the menu)
#   8  the install steps of the installer against the stubs: every failure ends in "Install Failed" with nothing
#      written after it, and only a run of every step ends in "Install Complete" (15), and mutations of the installer
#      that they must catch (7)
# usage: boot_test.sh <installer scripts folder> <busybox> <menus folder: EFI_BOOT_grub.cfg, syslinux.cfg> <work folder,
#        empty> <1: the medium carries the look of the boot menu (EFI/BOOT/hades, hades), 0: not>
# Output: "ok   <n> ..." and "FAIL <n> ..." lines, then "RESULT pass=<n> fail=<n>".
set -u
T=$1
BB=$2
M=$3
R=$4
LOOK=$5
[ -d "$T" ] && [ -x "$BB" ] && [ -f "$M/EFI_BOOT_grub.cfg" ] && [ -f "$M/syslinux.cfg" ] && [ -d "$R" ] ||
  { echo "usage: boot_test.sh <installer scripts> <busybox> <menus> <empty work folder> <0|1>"; exit 2; }
[ -z "$(ls -A "$R")" ] || { echo "the work folder $R is not empty"; exit 2; }
mkdir -p "$R/bin"
# a PATH of busybox applets only, as on the image
for a in sh awk sed grep head tail cat cp ls rm mkdir printf tr wc chmod cut rmdir mv; do ln -s "$BB" "$R/bin/$a"; done
# (the comparisons of the test itself use the tools of the host)
ln -s "$(command -v cmp)" "$R/bin/cmp"
ln -s "$(command -v diff)" "$R/bin/diff"
export PATH="$R/bin"
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); echo "ok   $*"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL $*"; }
HELPER="$T/installer-bootmenus"
run() { sh "$HELPER" "$@"; }

# the menus of an internal disk as section 1 says (an implementation of their rule of its own, to compare with)
win() {
  sed -e 's/initcall_blacklist=nvme_init,ahci_pci_driver_init/hades.otherdisks=hide/' \
      -e 's/boot=UUID=[^ ]*/boot=UUID=@UUID_SYSTEM@/' -e 's/disk=UUID=[^ ]*/disk=UUID=@UUID_STORAGE@/' "$1" |
  awk -v s='	search --no-floppy --fs-uuid --set=root --hint="${root}" ' -v f='	if [ ! -f /KERNEL ]; then search --set -f /KERNEL; fi' '
    /^menuentry "/ { e = $0; sub(/^menuentry "/, "", e); sub(/".*/, "", e) }
    /^LABEL / { e = $2 }
    $0 == "\tsearch --set -f /KERNEL" { print s "@UUID_SYSTEM@"; print f; next }
    index($0, s) == 1 { print s "@UUID_SYSTEM@"; next }
    (/^\tlinux / || /^  APPEND /) && (e == "Run" || e == "Live" || e == "run" || e == "live") {
      if ($0 !~ / lockdown=/) $0 = $0 " lockdown=integrity"
      if ($0 !~ / iommu\.passthrough=/) $0 = $0 " iommu.passthrough=0"
      if ($0 !~ / intel_iommu=/) $0 = $0 " intel_iommu=on,igfx_off"
    }
    { print }'
}

# 1: the medium's menus give what section 1 says
for k in grub:EFI_BOOT_grub.cfg syslinux:syslinux.cfg; do
  kind=${k%%:*}; src=$M/${k#*:}
  if run "$kind" "$src" > "$R/$kind.out" 2> "$R/$kind.err" && win "$src" | diff - "$R/$kind.out" > "$R/$kind.diff"; then
    ok "1 $kind: the medium's menus give the installed system's menus"
  else
    bad "1 $kind: $(cat "$R/$kind.err") $(head -n 5 "$R/$kind.diff")"
  fi
done

# 2: a medium last started in Live, or with the internal disks shown by default, still gives Run
sed -e 's/^set default=.*/set default="Run (internal disks read-only)"/' "$M/EFI_BOOT_grub.cfg" > "$R/g2.cfg"
sed -e 's/^DEFAULT .*/DEFAULT live/' -e 's/^SAY Wait for .* mode/SAY Wait for live mode/' "$M/syslinux.cfg" > "$R/s2.cfg"
run grub "$R/g2.cfg" | cmp -s - "$R/grub.out" && ok "2 grub: default Run" || bad "2 grub: default not normalised"
run syslinux "$R/s2.cfg" | cmp -s - "$R/syslinux.out" && ok "2 syslinux: default run" || bad "2 syslinux: default not normalised"

# 3: the installer run from an installed system (its menus already have hades.otherdisks=hide)
sed -e 's/@UUID_SYSTEM@/ABCD-1234/' -e 's/@UUID_STORAGE@/0000-1111/' "$R/grub.out" > "$R/g3.cfg"
sed -e 's/@UUID_SYSTEM@/ABCD-1234/' -e 's/@UUID_STORAGE@/0000-1111/' "$R/syslinux.out" > "$R/s3.cfg"
run grub "$R/g3.cfg" | cmp -s - "$R/grub.out" && ok "3 grub: same from an installed system" || bad "3 grub"
run syslinux "$R/s3.cfg" | cmp -s - "$R/syslinux.out" && ok "3 syslinux: same from an installed system" || bad "3 syslinux"

# 4: menus without the hiding, the lockdown and the DMA translation (as scripts/mkimage leaves them in the .ova)
sed -e 's/ initcall_blacklist=nvme_init,ahci_pci_driver_init//' -e 's/ lockdown=integrity//' \
    -e 's/ iommu\.passthrough=0 intel_iommu=on,igfx_off//' "$M/EFI_BOOT_grub.cfg" > "$R/g4.cfg"
sed -e 's/ initcall_blacklist=nvme_init,ahci_pci_driver_init//' -e 's/ lockdown=integrity//' \
    -e 's/ iommu\.passthrough=0 intel_iommu=on,igfx_off//' "$M/syslinux.cfg" > "$R/s4.cfg"
# (the helper puts the words that an entry lacks at its end, and the menus of today have the options of the boot
# choices between the lockdown and the DMA translation: the kernel takes its words in any order, so these menus are
# compared word for word in each entry, with the words of each kernel line sorted)
cat > "$R/norm.awk" << 'EOA'
/^\tlinux / || /^  APPEND / {
  match($0, /^[ \t]*[A-Za-z]+ /)
  lead = substr($0, 1, RLENGTH)
  n = split(substr($0, RLENGTH + 1), w, " ")
  for (i = 2; i <= n; i++) { v = w[i]; for (j = i - 1; j >= 1 && w[j] > v; j--) w[j + 1] = w[j]; w[j + 1] = v }
  out = lead
  for (i = 1; i <= n; i++) out = out (i > 1 ? " " : "") w[i]
  print out
  next
}
{ print }
EOA
awk -f "$R/norm.awk" "$R/grub.out" > "$R/grub.norm"
awk -f "$R/norm.awk" "$R/syslinux.out" > "$R/syslinux.norm"
run grub "$R/g4.cfg" | awk -f "$R/norm.awk" | cmp -s - "$R/grub.norm" && ok "4 grub: hiding, lockdown and DMA translation put back" || bad "4 grub"
run syslinux "$R/s4.cfg" | awk -f "$R/norm.awk" | cmp -s - "$R/syslinux.norm" && ok "4 syslinux: hiding, lockdown and DMA translation put back" || bad "4 syslinux"

# 5: what is refused, with nothing on standard output
neg() {
  local name="$1" kind="$2" file="$3" out
  out=$(run "$kind" "$file" 2> "$R/neg.err")
  if [ $? -ne 0 ] && [ -z "$out" ]; then ok "5 refused: $name ($(cat "$R/neg.err"))"; else bad "5 not refused: $name"; fi
}
sed -e '1d' "$M/EFI_BOOT_grub.cfg" > "$R/n1.cfg"; neg "no feature line" grub "$R/n1.cfg"
sed -e '1s/ other-disks-hidden//' "$M/syslinux.cfg" > "$R/n2.cfg"; neg "feature line of an older image" syslinux "$R/n2.cfg"
sed -e '/^menuentry "Live"/,/^}/d' "$M/EFI_BOOT_grub.cfg" > "$R/n3.cfg"; neg "no Live entry" grub "$R/n3.cfg"
sed -e '/^LABEL internal/,$d' "$M/syslinux.cfg" > "$R/n4.cfg"; neg "no internal entry" syslinux "$R/n4.cfg"
neg "no such file" grub "$R/none.cfg"
neg "the syslinux menu read as GRUB" grub "$M/syslinux.cfg"
neg "bad kind" efi "$M/syslinux.cfg"

# 6: mutations of the helper that the checks above must catch
mut() {
  local name="$1" expr="$2"
  sed -e "$expr" "$HELPER" > "$R/mut.sh"
  if cmp -s "$R/mut.sh" "$HELPER"; then bad "6 mutation not applied: $name"; return; fi
  if N="$R/norm.awk" GN="$R/grub.norm" SN="$R/syslinux.norm" sh -c 'sh "$0" grub "$1" | cmp -s - "$2" &&
      sh "$0" syslinux "$3" | cmp -s - "$4" &&
      sh "$0" grub "$5" | awk -f "$N" | cmp -s - "$GN" && sh "$0" syslinux "$6" | awk -f "$N" | cmp -s - "$SN" &&
      sh "$0" grub "$7" | cmp -s - "$2" && sh "$0" syslinux "$8" | cmp -s - "$4"' \
      "$R/mut.sh" "$M/EFI_BOOT_grub.cfg" "$R/grub.out" "$M/syslinux.cfg" "$R/syslinux.out" "$R/g4.cfg" "$R/s4.cfg" \
      "$R/g2.cfg" "$R/s2.cfg" &&
      ! sh "$R/mut.sh" grub "$R/n1.cfg" > /dev/null 2>&1; then
    bad "6 mutation survives: $name"
  else
    ok "6 mutation caught: $name"
  fi
}
mut "no hide in place" 's/w\[i\] = hide_others$/w[i] = w[i]/'
mut "no lockdown added" 's/if (hides \&\& !has_lock) out = out " " lock/if (0) out = out/'
mut "no DMA translation added" 's/if (hides \&\& !has_pt) out = out " " dma_pt/if (0) out = out/'
mut "default kept" 's/print "set default=\\"Run\\""; next/print; next/'
mut "syslinux default kept" 's/print "DEFAULT run"; next/print; next/'
mut "no storage placeholder" 's/w\[i\] = "disk=UUID=@UUID_STORAGE@"/w[i] = w[i]/'
mut "entry Live not hidden" 's/e == "Run" || e == "Live"/e == "Run"/'
mut "no feature check" 's/^printf .%s\\n. "$OUT" | head -n 1 | grep -q -E/true || printf "%s\\n" "$OUT" | head -n 1 | grep -q -E/'
mut "no search of the system partition" 's/^    if (!found) {$/    if (0) {/'

# 7: the installer: syntax, and its two functions with the paths of a fake medium
sh -n "$T/installer" && ok "7 installer: syntax (busybox sh -n)" || bad "7 installer: syntax"
sh -n "$HELPER" && ok "7 helper: syntax" || bad "7 helper: syntax"
F=$R/fake
mkdir -p "$F/flash/EFI/BOOT" "$F/flash/licenses" "$F/grub" "$F/lib" "$F/tmp" "$F/part1/EFI/BOOT"
cp "$M/EFI_BOOT_grub.cfg" "$F/flash/EFI/BOOT/grub.cfg"
cp "$M/syslinux.cfg" "$F/flash/syslinux.cfg"
echo kernelsum > "$F/flash/KERNEL.md5"; echo systemsum > "$F/flash/SYSTEM.md5"
echo notice > "$F/flash/NOTICE.md"; echo other > "$F/flash/NOTICE.other.md"; echo gpl > "$F/flash/licenses/GPL-2.0"
echo "diag switch text" > "$F/flash/DIAG-ON.TXT"
echo grubx64 > "$F/grub/bootx64.efi"; echo grubia32 > "$F/grub/bootia32.efi"
if [ "$LOOK" = 1 ]; then
  # the look of the boot menu on the medium, which the installer copies
  mkdir -p "$F/flash/EFI/BOOT/hades/1920x1080" "$F/flash/hades"; echo theme > "$F/flash/EFI/BOOT/hades/theme.cfg"
  echo look > "$F/flash/EFI/BOOT/hades/1920x1080/theme.txt"; echo menu > "$F/flash/hades/menu.cfg"
fi
cp "$HELPER" "$F/lib/installer-bootmenus"
# the two functions, and dbglg, taken out of the installer, with the paths moved into the fake medium
sed -n -e '/^dbglg() {/,/^}/p' -e '/^hades_prepare_boot() {/,/^}/p' -e '/^hades_install_boot() {/,/^}/p' "$T/installer" |
  sed -e "s|/usr/share/grub/|$F/grub/|g" -e "s|/usr/lib/libreelec/|$F/lib/|g" -e "s|/flash/|$F/flash/|g" > "$R/funcs.sh"
cat > "$R/drive.sh" << EOT
TMPDIR=$F/tmp; LOGFILE=$F/tmp/install.log; IMAGE_KERNEL=KERNEL; IMAGE_SYSTEM=SYSTEM
. $R/funcs.sh
hades_prepare_boot || { echo prepare-failed; exit 1; }
UUID_SYSTEM=1A2B-3C4D; UUID_STORAGE=11111111-2222-3333-4444-555555555555
hades_install_boot $F/part1 >> \$LOGFILE 2>&1
[ -e $F/tmp/install_failed ] && { cat $F/tmp/install_failed; exit 2; }
exit 0
EOT
if sh "$R/drive.sh"; then ok "7 installer functions ran"; else bad "7 installer functions: $?"; fi
P=$F/part1
[ "$(cat "$P/EFI/BOOT/bootx64.efi")" = grubx64 ] && [ "$(cat "$P/EFI/BOOT/bootia32.efi")" = grubia32 ] &&
  ok "7 GRUB of the image in EFI/BOOT" || bad "7 GRUB"
[ ! -e "$P/EFI/BOOT/ldlinux.e64" ] && ok "7 no syslinux EFI loader" || bad "7 syslinux EFI loader"
if [ "$LOOK" = 1 ]; then
  [ "$(cat "$P/EFI/BOOT/hades/theme.cfg" 2> /dev/null)" = theme ] && [ "$(cat "$P/EFI/BOOT/hades/1920x1080/theme.txt" 2> /dev/null)" = look ] &&
    [ "$(cat "$P/hades/menu.cfg" 2> /dev/null)" = menu ] && [ ! -e "$P/EFI/BOOT/hades.new" ] && [ ! -e "$P/hades.new" ] &&
    ok "7 the look of the boot menu copied, for GRUB and for BIOS" || bad "7 the look of the boot menu not copied"
fi
sed -e 's/@UUID_SYSTEM@/1A2B-3C4D/g' -e 's/@UUID_STORAGE@/11111111-2222-3333-4444-555555555555/g' "$R/grub.out" |
  cmp -s - "$P/EFI/BOOT/grub.cfg" && ok "7 grub.cfg with the new identifiers" || bad "7 grub.cfg"
sed -e 's/@UUID_SYSTEM@/1A2B-3C4D/g' -e 's/@UUID_STORAGE@/11111111-2222-3333-4444-555555555555/g' "$R/syslinux.out" |
  cmp -s - "$P/syslinux.cfg" && ok "7 syslinux.cfg with the new identifiers" || bad "7 syslinux.cfg"
[ "$(cat "$P/DIAG-OFF.TXT")" = "diag switch text" ] && [ ! -e "$P/DIAG-ON.TXT" ] && ok "7 diagnostics switch, off" || bad "7 DIAG"
[ -f "$P/NOTICE.md" ] && [ -f "$P/NOTICE.other.md" ] && [ -f "$P/licenses/GPL-2.0" ] && [ -f "$P/KERNEL.md5" ] && [ -f "$P/SYSTEM.md5" ] &&
  ok "7 notice, licences, checksums" || bad "7 notice/licences"
# without the GRUB of the image, or with a medium that is not Hades', nothing is prepared
rm "$F/grub/bootx64.efi"
sh -c ". $R/funcs.sh; TMPDIR=$F/tmp; LOGFILE=$F/tmp/install.log; hades_prepare_boot" && bad "7 prepared without bootx64.efi" || ok "7 refused without bootx64.efi"
echo grubx64 > "$F/grub/bootx64.efi"
sed -i -e '1d' "$F/flash/EFI/BOOT/grub.cfg"
sh -c ". $R/funcs.sh; TMPDIR=$F/tmp; LOGFILE=$F/tmp/install.log; hades_prepare_boot" && bad "7 prepared from foreign menus" || ok "7 refused with foreign menus"
# an empty identifier is reported
cp "$M/EFI_BOOT_grub.cfg" "$F/flash/EFI/BOOT/grub.cfg"
rm -f "$F/tmp/install_failed"
sh -c ". $R/funcs.sh; TMPDIR=$F/tmp; LOGFILE=$F/tmp/install.log; hades_prepare_boot; UUID_SYSTEM=; UUID_STORAGE=x; mkdir -p $F/part1e/EFI/BOOT; hades_install_boot $F/part1e > /dev/null 2>&1"
[ -s "$F/tmp/install_failed" ] && ok "7 empty identifier reported" || bad "7 empty identifier not reported"

# 8: the steps of the install, from the installer itself, against stubs of the disk tools: every failure ends in
# "Install Failed" with nothing written after it, and only a run of every step ends in "Install Complete"
E=$R/steps
mkdir -p "$E/stub" "$E/syslinux"
echo mbr > "$E/syslinux/mbr.bin"; echo gptmbr > "$E/syslinux/gptmbr.bin"
cp "$M/EFI_BOOT_grub.cfg" "$F/flash/EFI/BOOT/grub.cfg"
echo kernel > "$F/flash/KERNEL"; echo system > "$F/flash/SYSTEM"; echo backup > "$F/flash/backup.tar.bz2"
# a stub: logs its call, fails when $STUBFAIL names it, and does what the step needs of it
stub() {
  printf '#!%s sh\necho "$0 $*" >> "$E/calls"\ncase " $STUBFAIL " in *" %s "*) exit 1 ;; esac\n%s\nexit 0\n' \
    "$BB" "$1" "$2" > "$E/stub/$1"
  chmod 0755 "$E/stub/$1"
}
stub dd ''
stub parted 'case "$*" in *mkpart*) case " $STUBFAIL " in *" nonode "*) ;; *) n=$(ls "$DEV"? 2>/dev/null | wc -l); : > "$DEV$((n + 1))" ;; esac ;; esac'
stub partprobe 'case " $STUBFAIL " in *" killed "*) kill -9 $PPID ;; esac'
stub mkfs.vfat ''
stub dosfslabel ''
stub mke2fs ''
stub tune2fs ''
stub blkid 'case "$*" in *"$DEV"1*) echo ID_FS_UUID=1A2B-3C4D ;; *) echo ID_FS_UUID=11111111-2222-3333-4444-555555555555 ;; esac'
stub syslinux ''
stub sync ''
stub sleep ''
stub tar ''
stub unzip ''
# mount marks the folder as mounted (unless the mount "happens" without mounting: silentmount)
stub mount 'case " $STUBFAIL " in *" silentmount "*) exit 0 ;; esac; for a; do d=$a; done; : > "$d.mounted"'
stub mountpoint 'for a; do d=$a; done; [ -e "$d.mounted" ]; exit $?'
stub umount 'for a; do d=$a; done; case "$d" in *part1) case " $STUBFAIL " in *" umount1 "*) exit 1 ;; esac ;; esac; rm -f "$d.mounted"'
# cp, real, but failing for the system or for GRUB when asked
printf '#!%s sh\ncase " $STUBFAIL " in *" cpsystem "*) case "$*" in *SYSTEM*/SYSTEM) exit 1 ;; esac ;; esac\ncase " $STUBFAIL " in *" cpgrub "*) case "$*" in *bootx64.efi*) exit 1 ;; esac ;; esac\nexec %s cp "$@"\n' "$BB" "$BB" > "$E/stub/cp"
chmod 0755 "$E/stub/cp"
# whiptail: the gauge drains its input, a message box says its title
printf '#!%s sh\nt=""\nwhile [ $# -gt 0 ]; do case "$1" in --title) t=$2; shift ;; --gauge) cat > /dev/null; exit 0 ;; --msgbox) m=$2 ;; esac; shift; done\necho "$t|$m" >> "$E/boxes"\n' "$BB" > "$E/stub/whiptail"
chmod 0755 "$E/stub/whiptail"

# what the installer runs: msg_progress_install, the helpers, and the end of do_install_quick from the progress bar
# on; the paths moved into the fake medium, block devices as plain files
INST=${INST:-$T/installer}
mkinst() {
  {
    sed -n -e '/^dbglg() {/,/^}/p' -e '/^msg_progress_install() {/,/^}/p' -e '/^hades_fail() {/,/^}/p' \
      -e '/^hades_wait_block() {/,/^}/p' -e '/^hades_prepare_boot() {/,/^}/p' -e '/^hades_install_boot() {/,/^}/p' "$1"
    echo 'run_steps() {'
    sed -n -e '/^  # start the progress bar/,/^}/p' "$1"
  } | sed -e "s|/usr/share/syslinux/|$E/syslinux/|g" -e "s|/usr/share/grub/|$F/grub/|g" -e "s|/usr/lib/libreelec/|$F/lib/|g" \
      -e "s|/flash/|$F/flash/|g" -e 's|\[ ! -b "\$1" \]|[ ! -e "$1" ]|' > "$E/steps.sh"
  cat > "$E/drive.sh" << EOT
TMPDIR=$F/tmp; LOGFILE=$F/tmp/install.log; IMAGE_KERNEL=KERNEL; IMAGE_SYSTEM=SYSTEM
INSTALL_DEVICE=\$DEV; PART1=1; PART2=2; BACKTITLE=t; PRODUCT_NAME="LibreELEC Hades"
PARTSIZE_SYSTEM=1024; PARTSIZE_SYSTEM_OFFSET=8192; DISKLABEL_SYSTEM=System; DISKLABEL_STORAGE=Storage
. $E/steps.sh
hades_prepare_boot || exit 9
rm -f "\$TMPDIR/install_failed" "\$TMPDIR/install_done" "\$TMPDIR/install_notes"
run_steps
EOT
}
steps() {
  # $1: name, $2: what fails, $3: GPT, $4: UEFI, $5: backup (run by the busybox of the image)
  rm -rf "$E/disk" "$E/calls" "$E/boxes" "$F/tmp"
  mkdir -p "$E/disk" "$F/tmp"
  : > "$E/disk/dev"
  PATH="$E/stub:$R/bin" STUBFAIL="$2" DEV="$E/disk/dev" E="$E" GPT="$3" UEFI="$4" BACKUP_UNPACK="$5" \
    "$BB" sh "$E/drive.sh" > "$E/out-$1" 2>&1
  BOX=$(cat "$E/boxes" 2> /dev/null)
}
complete() { case "$BOX" in "LibreELEC Hades Install Complete|"*) return 0 ;; esac; return 1; }
failed() { case "$BOX" in *"Install Failed|"*"$1"*) return 0 ;; esac; return 1; }
called() { grep -q "/$1 " "$E/calls" 2> /dev/null; }
P=$F/tmp/part1
booted_files() { [ -e "$P/EFI/BOOT/bootx64.efi" ] || [ -e "$P/EFI/BOOT/grub.cfg" ] || [ -e "$P/syslinux.cfg" ]; }
mounted() { [ -e "$P.mounted" ]; }
section8() {
  mkinst "$INST"
  steps ok "" 1 1 0
  { complete && [ "$(cat "$P/SYSTEM")" = system ] && [ -e "$P/EFI/BOOT/bootx64.efi" ] && ! mounted; } && ok "8 every step: Install Complete" || bad "8 every step: $BOX"
  steps mbr "" 0 0 0
  { complete && grep -q "mklabel msdos" "$E/calls" && [ "$(head -c 3 "$E/disk/dev")" = mbr ] && [ -e "$P/syslinux.cfg" ]; } && ok "8 MBR, legacy BIOS: Install Complete" || bad "8 MBR: $BOX"
  steps mkfs mkfs.vfat 1 1 0
  { failed "could not be formatted" && ! called mount && [ ! -e "$P/KERNEL" ]; } && ok "8 mkfs.vfat fails: stops there" || bad "8 mkfs.vfat: $BOX"
  steps mke2fs mke2fs 1 1 0
  { failed "storage partition could not be formatted" && ! called mount; } && ok "8 mke2fs fails: stops there" || bad "8 mke2fs: $BOX"
  steps parted parted 1 1 0
  { failed "partition table" && ! called mkfs.vfat; } && ok "8 parted fails: stops there" || bad "8 parted: $BOX"
  steps nonode nonode 1 1 0
  { failed "did not appear" && ! called mkfs.vfat; } && ok "8 no partition node: stops there" || bad "8 nonode: $BOX"
  steps mount mount 1 1 0
  { failed "could not be mounted" && [ ! -e "$P/KERNEL" ] && ! called syslinux; } && ok "8 mount fails: nothing written" || bad "8 mount: $BOX"
  steps silent silentmount 1 1 0
  { failed "could not be mounted" && [ ! -e "$P/KERNEL" ]; } && ok "8 mount that mounts nothing: nothing written into memory" || bad "8 silent mount: $BOX"
  steps sysbios syslinux 0 0 0
  { failed "syslinux" && [ ! -e "$P/KERNEL" ] && ! mounted; } && ok "8 syslinux fails in legacy BIOS: stops, unmounted" || bad "8 syslinux BIOS: $BOX"
  steps sysuefi syslinux 1 1 0
  { complete && case "$BOX" in *"starts only in UEFI"*) true ;; *) false ;; esac; } && ok "8 syslinux fails in UEFI: complete, with the note" || bad "8 syslinux UEFI: $BOX"
  steps cpsys cpsystem 1 1 0
  { failed "system could not be copied" && ! booted_files && ! mounted; } && ok "8 the system copy fails: no boot loader, unmounted" || bad "8 cp SYSTEM: $BOX"
  steps cpgrub cpgrub 1 1 0
  { failed "boot loader or its menus" && ! booted_files && ! mounted; } && ok "8 GRUB copy fails: no boot file left" || bad "8 cp GRUB: $BOX"
  steps umount1 umount1 1 1 0
  { failed "written to the end"; } && ok "8 unmount fails: Install Failed" || bad "8 umount: $BOX"
  steps backup "tar" 1 1 1
  { failed "backup" && [ ! -e "$F/tmp/part2.mounted" ]; } && ok "8 backup restore fails: Install Failed, unmounted" || bad "8 backup: $BOX"
  steps killed killed 1 1 0
  { failed "did not finish"; } && ok "8 steps killed midway: Install Failed" || bad "8 killed: $BOX"
}
section8
# mutations of the installer that section 8 must catch
mut8() {
  local name="$1" expr="$2"
  sed -e "$expr" "$T/installer" > "$R/inst-mut"
  if cmp -s "$R/inst-mut" "$T/installer"; then bad "8 mutation not applied: $name"; return; fi
  local p=$PASS f=$FAIL
  INST="$R/inst-mut" section8 > "$R/mut8.out" 2>&1
  PASS=$p
  if [ "$FAIL" -gt "$f" ]; then FAIL=$f; ok "8 mutation caught: $name"; else FAIL=$f; bad "8 mutation survives: $name"; fi
}
mut8 "mkfs.vfat unchecked" '/mkfs.vfat -F 16/{n;s/.*/      true/}'
mut8 "no mountpoint check" 's/ && mountpoint -q \$TMPDIR\/part1 ||/ ||/'
mut8 "no mark of the end" 's/ || \[ ! -f "\$TMPDIR\/install_done" \]//'
mut8 "boot files left on failure" '/^  rm -f "\$dir\/syslinux.cfg"/d'
mut8 "syslinux fatal in UEFI too" 's/if \[ "\$UEFI" = "1" \]; then/if false; then/'
mut8 "system copy unchecked" '/cp "\/flash\/\$IMAGE_SYSTEM" \$TMPDIR\/part1\/SYSTEM/{n;s/.*/      true/}'
mut8 "hades_fail does not unmount" 's/umount \$m >> \$LOGFILE 2>&1 || echo/true || echo/'
echo "--- installed grub.cfg"; cat "$F/part1/EFI/BOOT/grub.cfg"
echo "--- installed syslinux.cfg (entries)"; grep -E '^(DEFAULT|LABEL|  APPEND)' "$F/part1/syslinux.cfg"
echo "RESULT pass=$PASS fail=$FAIL"
