#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-boot.sh: the installer of the image (packages/tools/installer/scripts: installer-bootmenus, and the install steps
# of installer) on the boot menus of three media, with the busybox of the image (boot_test.sh: its sections 1 to 8):
#   today        the menus that scripts/mkimage of the tree writes (its two here-documents, with identifiers made here)
#   older-look   the menus of an image of 1.0 RC6 to 1.0 RC8 (ref/menus-older-look): the look of the menu, the kernel
#                found by its file
#   older-text   the menus of an image of 1.0 RC4 (ref/menus-older-text): a text menu, the kernel found by its file
# An installed system keeps the menus it was installed with when it is updated, and the installer is run from it
# (installer-bootmenus takes older menus and gives them the words of today), so the older forms stay under test.
# Also:
#   notice       kodi-failed-notice (packages/mediacenter/kodi/scripts) as kodi.service runs it, with the busybox of
#                the image (failed_notice_test.sh); a script that does nothing must fail its 7 checks
#   themeupdate  the theme of the boot menu in an update in place (update.sh, theme_update) through the busybox of
#                the initramfs, the menus of today and the theme of REGRESS_LE_OLD (default v1.0) as the one updated
#                from (theme_update_test.sh); the update.sh of REGRESS_LE_OLD, which has no theme_update, must fail
#                its 7 checks
#   installer    what the init of the initramfs leaves for modprobe in a start of the boot entry "Installer"
#                (installer_modprobe_test.sh): no driver of NVIDIA or of Bluetooth loads by its alias, with the
#                busybox of the initramfs and the modules of the build folder; the init of REGRESS_LE_OLD (default
#                v1.0), which leaves out NVIDIA only, must fail its 3 checks
#   picture      the pictures of hades-picture (packages/sysutils/hades-picture/src/picture.c, picture-test.c), built
#                with the toolchain of the build folder together with ply-image.c of its plymouth-lite: each picture
#                on screens of 17 sizes pixel for pixel as ply-image draws it, each boot.png of the theme of GRUB, the
#                cards of the notices over the splash screens, the version written on the card of "Kodi could not
#                start" with the letters of notice/font-*, and that line against the typeface itself
#                (version_check.py); a picture.c that puts the picture one pixel off, and one that spaces the letters
#                a pixel wider, must fail
#   scripts      what boot-picture, kodi-display-notice and kodi-failed-notice ask hades-picture to draw, with a
#                hades-picture of the test and the busybox of the image (picture_scripts_test.sh); scripts that do
#                nothing must fail its 17 checks
#   netwait      the wait of "Wait for network before starting Kodi": network-wait (packages/sysutils/wait-time-sync)
#                and what clock-measure keeps for it (clock-trusted), with the busybox of the image and fakes of
#                wait-time-sync, connmand-wait-online and logger (network_wait_test.sh); each of its 7 mutations (a
#                rule of either script taken out) must fail a check
#   notreturned  the drivers that the journal of a start, or the copy of its kernel log, shows starting under
#                initcall_debug and not returned by its last line: not-returned.awk and not_returned() of collect.sh
#                (packages/sysutils/hades-diag/scripts), with the busybox of the image (not_returned_test.sh); an awk
#                that does not count the returns, and one that counts the PCI fixups, must fail 5 and 9 checks
#   afternvidia  after-nvidia (packages/graphics/nvidia/scripts), which kmod runs in place of loading snd_virtuoso:
#                the module is loaded once nvidia has started, at once where there is nothing to wait for, and within
#                its limits, with the busybox of the image, a /sys of files and a fake modprobe (after_nvidia_test.sh);
#                each of its 7 mutations must fail a check
#   nvidiasleep  NVIDIA's power management interface for the 580 driver (packages/graphics/nvidia/scripts): the sleep hook
#                writes suspend, hibernate and resume to /proc/driver/nvidia/suspend as systemd-sleep runs it, and nothing
#                with the 620 driver; stack-links writes the option that keeps the video memory for the legacy driver only
#                (nvidia_sleep_test.sh, with the busybox of the image); each of its 6 mutations must fail a check
#   sleepreport  the lines that each sleep leaves in the kernel log and that the diagnostics print (sleep.sh of
#                packages/sysutils/hades-diag/scripts): what the firmware offers (deep, a low-power S0 idle in the
#                FADT, an LPS0 device), what may wake the PC and the input devices that could and are not armed, how
#                the sleep went and what woke it, on a /sys and a /proc of files like the PC of issue 32 and one with
#                deep, with the busybox of the image (sleep_report_test.sh); each of its 7 mutations must fail a check
#   wake         what may wake the PC from sleep (wake.sh of packages/sysutils/hades-wake, which udev, the sleep hook and
#                the setting "Wake from sleep" run) on a /sys of files with the USB devices of issue 32 and others (a
#                Media Center IR receiver, a mouse, a game pad, a TV tuner with IR, an IR receiver on the board), each
#                HID interface with a report descriptor of bytes: each choice of the setting, a device plugged in, a
#                device seen before all its interfaces, the hook after a sleep, another device on a port, and what
#                sleep.sh then says of each device (wake_test.sh, with the busybox of the image); each of its 14
#                mutations must fail a check
#   wakesettings the setting "Wake from sleep" of the LibreELEC settings add-on (LibreELEC-settings-9093, system.py and
#                its strings, with stand-ins for Kodi: wake_settings_test.py) on the add-on of the recipe (archive
#                checked against PKG_SHA256, every patch with -F0), and the same with 9093 reversed, which must fail
# Not a stage (it needs an image and KVM): picture_race_qemu.sh, Kodi's "hades-picture release" that comes while a
# picture of the start is being drawn, in QEMU (its head says what it checks).
# Host test only: it writes under $REGRESS_WORK/boot and reads the tree at $REGRESS_LE_REF and the busybox of the build
# folder ($REGRESS_LE_BUILD/install_pkg/busybox-*).
# usage: run-boot.sh all | verdict [<log>] | <stage>...       stages: menus today olderlook oldertext notice themeupdate installer picture
#                                                              scripts netwait notreturned afternvidia nvidiasleep
#                                                              sleepreport wake wakesettings
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../lib/regress.sh"
rg_init boot
MN=$RG_W/menus
LT=$RG_W/le

busybox_of_image() {
  : "${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder}"
  ls "$REGRESS_LE_BUILD"/install_pkg/busybox-*/usr/bin/busybox 2> /dev/null | sort | tail -n 1
}

# the menus that scripts/mkimage writes for the Generic image: the variables of the boot entries (from
# HIDE_INTERNAL_DISKS to BOOT_OPTIONS) and the two here-documents of syslinux.cfg and grub.cfg, run with identifiers made
# here; status 1 when they are not where they were
menus_of_mkimage() {
  local out=$1 mk=$LT/scripts/mkimage
  rg_fresh "$out"
  awk '/^HIDE_INTERNAL_DISKS=/, /^BOOT_OPTIONS=/ { print }
       /^  cat <<EOF >"\$\{IMG_TMP\}\/(syslinux|grub)\.cfg"$/, /^EOF$/ { print }' "$mk" > "$out/menus.sh"
  [ "$(grep -c '^  cat <<EOF >' "$out/menus.sh")" = 2 ] && grep -q '^BOOT_OPTIONS=' "$out/menus.sh" ||
    { echo "the boot entries are not where they were in scripts/mkimage"; return 1; }
  ( IMG_TMP=$out UUID_SYSTEM=0A1B-2C3D UUID_STORAGE=0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d KERNEL_NAME=KERNEL
    . "$out/menus.sh" ) < /dev/null || { echo "the boot entries of scripts/mkimage did not run"; return 1; }
  mv "$out/grub.cfg" "$out/EFI_BOOT_grub.cfg" || return 1
  echo "menus of scripts/mkimage: grub.cfg $(grep -c '^menuentry ' "$out/EFI_BOOT_grub.cfg") entries, syslinux.cfg $(grep -c '^LABEL ' "$out/syslinux.cfg") entries; $(head -n 1 "$out/syslinux.cfg")"
}

stage_menus() {
  rg_le_tree "$LT" scripts/mkimage packages/tools/installer/scripts || { rg_item menus FAILED "the tree at $REGRESS_LE_REF"; return 1; }
  rg_fresh "$MN"
  if rg_log "$RG_W/logs/menus.log" menus_of_mkimage "$MN/today" &&
     grep -q -E '^# hades-features:.* other-disks-hidden( |$)' "$MN/today/EFI_BOOT_grub.cfg" &&
     [ "$(grep -c '^menuentry ' "$MN/today/EFI_BOOT_grub.cfg")" = 4 ] && [ "$(grep -c '^LABEL ' "$MN/today/syslinux.cfg")" = 4 ]; then
    rg_item menus PASSED "$(grep -a '^menus of' "$RG_W/logs/menus.log")"
  else
    rg_item menus FAILED "$(grep -a -v '^== ' "$RG_W/logs/menus.log" | tail -n 1)"
  fi
  cp -a "$HERE/ref/menus-older-look" "$HERE/ref/menus-older-text" "$MN/"
  local bb
  bb=$(busybox_of_image)
  if [ -n "$bb" ] && "$bb" sh -c 'exit 0' 2> /dev/null; then rg_item busybox PASSED "$bb"
  else rg_item busybox FAILED "no busybox of the image in $REGRESS_LE_BUILD/install_pkg"; fi
}

# medium <name> <menus folder> <look 0|1>: boot_test.sh on that medium, one item a section and one for the whole
medium() {
  local n=$1 menus=$2 look=$3 L W bb x want s ok bad r total
  L=$RG_W/logs/$n.log W=$RG_W/run-$n
  bb=$(busybox_of_image)
  rg_fresh "$W"
  if [ ! -f "$menus/EFI_BOOT_grub.cfg" ] || [ -z "$bb" ]; then
    for s in s1 s2 s3 s4 s5 s6 s7 s8 total; do rg_item "$n-$s" FAILED "no menus ($menus) or no busybox of the image"; done
    return 1
  fi
  rg_log "$L" bash "$HERE/boot_test.sh" "$LT/packages/tools/installer/scripts" "$bb" "$menus" "$W" "$look"
  total=0
  for x in "1 2" "2 2" "3 2" "4 2" "5 7" "6 9" "7 $((12 + look))" "8 22"; do
    set -- $x
    s=$1 want=$2 total=$((total + want))
    ok=$(grep -a -c -E "^ok   $s " "$L"); bad=$(grep -a -c -E "^FAIL $s " "$L")
    if [ "$ok" = "$want" ] && [ "$bad" = 0 ]; then rg_item "$n-s$s" PASSED "$ok of $want"
    else rg_item "$n-s$s" FAILED "$ok of $want ok, $bad FAIL: $(grep -a -m2 -E "^FAIL $s " "$L" | tr '\n' ' ' | cut -c1-200)"; fi
  done
  r=$(grep -a -m1 '^RESULT ' "$L")
  if [ "$r" = "RESULT pass=$total fail=0" ]; then rg_item "$n-total" PASSED "$r"
  else rg_item "$n-total" FAILED "${r:-no RESULT line}, RESULT pass=$total fail=0 expected; $(rg_last "$L")"; fi
}

stage_today() { medium today "$MN/today" 1; }
stage_olderlook() { medium older-look "$MN/menus-older-look" 1; }
stage_oldertext() { medium older-text "$MN/menus-older-text" 0; }

stage_notice() {
  local bb L v S=$RG_W/le-notice/packages/mediacenter/kodi/scripts/kodi-failed-notice
  bb=$(busybox_of_image)
  rg_le_tree "$RG_W/le-notice" packages/mediacenter/kodi/scripts/kodi-failed-notice
  if [ -z "$bb" ] || [ ! -f "$S" ]; then
    rg_item failed-notice FAILED "no busybox of the image, or no kodi-failed-notice at $REGRESS_LE_REF"
    rg_item failed-notice.empty FAILED "no busybox of the image"; return 1
  fi
  L=$RG_W/logs/failed-notice.log
  rg_log "$L" bash "$HERE/failed_notice_test.sh" "$bb" "$S"
  v=$(rg_pass "$L" '^failed-notice: [0-9]+ checks passed, 0 failed$' '^FAIL ')
  rg_item failed-notice "$(rg_say "$v")" "$(rg_rest "$v")"
  : > "$RG_W/empty-notice"
  L=$RG_W/logs/failed-notice-empty.log
  rg_log "$L" bash "$HERE/failed_notice_test.sh" "$bb" "$RG_W/empty-notice"
  v=$(rg_fails "$L" 7 '^FAIL ')
  rg_item failed-notice.empty "$(rg_say "$v")" "$(rg_rest "$v")"
}

stage_themeupdate() {
  local bb old=${REGRESS_LE_OLD:-v1.0} T=$RG_W/le-theme O=$RG_W/le-old L v
  local U=projects/Generic/filesystem/usr/share/bootloader/update.sh TH=packages/tools/grub/theme/hades
  local G=$MN/today/EFI_BOOT_grub.cfg SY=$MN/today/syslinux.cfg
  bb=${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder}/initramfs/usr/bin/busybox
  if [ ! -x "$bb" ] || [ ! -f "$G" ] || [ ! -f "$SY" ] ||
     ! git -C "$REGRESS_LE_GIT" rev-parse --verify -q "$old^{commit}" > /dev/null; then
    rg_item theme-update FAILED "no busybox of the initramfs, no menus of today (stage menus) or no ref $old"
    rg_item theme-update.old FAILED "the same"; return 1
  fi
  rg_le_tree "$T" $U $TH
  rg_fresh "$O"
  git -C "$REGRESS_LE_GIT" archive "$old" $U $TH | tar -x -C "$O"
  L=$RG_W/logs/theme-update.log
  rg_log "$L" bash "$HERE/theme_update_test.sh" "$bb" "$T/$U" "$T/$TH" "$O/$TH" "$G" "$SY"
  v=$(rg_pass "$L" '^theme-update: [0-9]+ checks passed, 0 failed$' '^FAIL ')
  rg_item theme-update "$(rg_say "$v")" "$(rg_rest "$v")"
  L=$RG_W/logs/theme-update-old.log
  rg_log "$L" bash "$HERE/theme_update_test.sh" "$bb" "$O/$U" "$T/$TH" "$O/$TH" "$G" "$SY"
  v=$(rg_fails "$L" 7 '^FAIL ')
  rg_item theme-update.old "$(rg_say "$v")" "$(rg_rest "$v") (update.sh of $old)"
}

stage_installer() {
  local bb old=${REGRESS_LE_OLD:-v1.0} T=$RG_W/le-installer O=$RG_W/le-installer-old L v mr kv
  local I=packages/sysutils/busybox/scripts/init
  bb=${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder}/initramfs/usr/bin/busybox
  mr=$(ls -d "$REGRESS_LE_BUILD"/install_pkg/linux-*/usr/lib/kernel-overlays/base 2> /dev/null | sort | tail -n 1)
  kv=$(ls "$mr/lib/modules" 2> /dev/null | sort | tail -n 1)
  if [ ! -x "$bb" ] || [ -z "$kv" ] || ! git -C "$REGRESS_LE_GIT" rev-parse --verify -q "$old^{commit}" > /dev/null; then
    rg_item installer-modprobe FAILED "no busybox of the initramfs, no modules in the build folder or no ref $old"
    rg_item installer-modprobe.old FAILED "the same"; return 1
  fi
  rg_le_tree "$T" $I
  rg_fresh "$O"
  git -C "$REGRESS_LE_GIT" archive "$old" $I | tar -x -C "$O"
  L=$RG_W/logs/installer-modprobe.log
  rg_log "$L" bash "$HERE/installer_modprobe_test.sh" "$bb" "$T/$I" "$mr" "$kv"
  v=$(rg_pass "$L" '^passed: [0-9]+, failed: 0, not run: 0$' '^(FAIL |PASSED, BUT)')
  rg_item installer-modprobe "$(rg_say "$v")" "$(rg_rest "$v") (modules of Linux $kv)"
  L=$RG_W/logs/installer-modprobe-old.log
  rg_log "$L" bash "$HERE/installer_modprobe_test.sh" "$bb" "$O/$I" "$mr" "$kv"
  v=$(rg_fails "$L" 3 '^FAIL ')
  rg_item installer-modprobe.old "$(rg_say "$v")" "$(rg_rest "$v") (the init of $old)"
}

stage_picture() {
  local B=${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder} T=$RG_W/le-picture L v cc sr ply ld S
  cc=$B/toolchain/bin/x86_64-libreelec-linux-gnu-gcc
  sr=$B/toolchain/x86_64-libreelec-linux-gnu/sysroot
  ld=$sr/usr/lib/ld-linux-x86-64.so.2
  ply=$(ls -d "$B"/build/plymouth-lite-* 2> /dev/null | sort | tail -n 1)
  rg_le_tree "$T" packages/sysutils/hades-picture/src distributions/LibreELEC/splash packages/tools/grub/theme/hades \
    packages/mediacenter/kodi/brand/notice packages/mediacenter/kodi/brand/src/Jost-VariableFont_wght.ttf
  S=$T/packages/sysutils/hades-picture/src
  if [ ! -x "$cc" ] || [ ! -x "$ld" ] || ! grep -q ply_image_fit "$ply/ply-image.c" 2> /dev/null || [ ! -f "$S/picture.c" ]; then
    rg_item picture FAILED "no toolchain, no plymouth-lite with plymouth-lite-9000 in $B, or no hades-picture at $REGRESS_LE_REF"
    rg_item picture.mutant FAILED "the same"; return 1
  fi
  # (the mutants: the picture one pixel to the right of where ply-image puts it; the letters a pixel further apart)
  sed 's#\*left = (sw - \*nw) / 2;#*left = (sw - *nw) / 2 + 1;#' "$S/picture.c" > "$RG_W/picture-mutant.c"
  sed 's#pen += adv\[c\];#pen += adv[c] + 256;#' "$S/picture.c" > "$RG_W/picture-textmutant.c"
  cmp -s "$S/picture.c" "$RG_W/picture-mutant.c" && { rg_item picture.mutant FAILED "the mutant is not made"; return 1; }
  cmp -s "$S/picture.c" "$RG_W/picture-textmutant.c" && { rg_item picture.version-mutant FAILED "the mutant is not made"; return 1; }
  for x in picture:"$S/picture.c" mutant:"$RG_W/picture-mutant.c" textmutant:"$RG_W/picture-textmutant.c"; do
    "$cc" -O2 -std=gnu11 -I"$S" -I"$ply" "-DPLY_IMAGE_C=\"$ply/ply-image.c\"" -o "$RG_W/test-${x%%:*}" \
      "$S/picture-test.c" "${x#*:}" "$ply/ply-frame-buffer.c" -lpng16 -lz -lm > "$RG_W/logs/build-${x%%:*}.log" 2>&1 ||
      { rg_item picture FAILED "the test did not build: $(rg_last "$RG_W/logs/build-${x%%:*}.log")"; return 1; }
  done
  rg_fresh "$RG_W/tmp-picture"
  L=$RG_W/logs/picture.log
  rg_log "$L" "$ld" --library-path "$sr/usr/lib" "$RG_W/test-picture" "$T/distributions/LibreELEC" \
    "$T/packages/tools/grub/theme/hades" "$T/packages/mediacenter/kodi/brand/notice" "$RG_W/tmp-picture"
  v=$(rg_pass "$L" '^picture: [0-9]+ checks passed, 0 failed$' '^FAIL ')
  rg_item picture "$(rg_say "$v")" "$(rg_rest "$v")"
  L=$RG_W/logs/picture-version.log
  rg_log "$L" $RG_PY "$HERE/version_check.py" "$RG_W/tmp-picture/version-1080.png" \
    "$T/packages/mediacenter/kodi/brand/src/Jost-VariableFont_wght.ttf" "$T/packages/mediacenter/kodi/brand/notice" \
    "LibreELEC (Hades 1.1): devel-20261118180313-3ea7eec"
  v=$(rg_pass "$L" '^version-check: PASSED$' '^version-check: FAILED')
  rg_item picture.version "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a '^the line' "$L" | cut -c1-120)"
  rg_fresh "$RG_W/tmp-picture"
  L=$RG_W/logs/picture-textmutant.log
  rg_log "$L" "$ld" --library-path "$sr/usr/lib" "$RG_W/test-textmutant" "$T/distributions/LibreELEC" \
    "$T/packages/tools/grub/theme/hades" "$T/packages/mediacenter/kodi/brand/notice" "$RG_W/tmp-picture"
  L=$RG_W/logs/picture-version-mutant.log
  rg_log "$L" $RG_PY "$HERE/version_check.py" "$RG_W/tmp-picture/version-1080.png" \
    "$T/packages/mediacenter/kodi/brand/src/Jost-VariableFont_wght.ttf" "$T/packages/mediacenter/kodi/brand/notice" \
    "LibreELEC (Hades 1.1): devel-20261118180313-3ea7eec"
  v=$(rg_fails "$L" 1 '^version-check: FAILED')
  rg_item picture.version-mutant "$(rg_say "$v")" "$(rg_rest "$v") (the letters a pixel further apart)"
  rg_fresh "$RG_W/tmp-picture"
  L=$RG_W/logs/picture-mutant.log
  rg_log "$L" "$ld" --library-path "$sr/usr/lib" "$RG_W/test-mutant" "$T/distributions/LibreELEC" \
    "$T/packages/tools/grub/theme/hades" "$T/packages/mediacenter/kodi/brand/notice" "$RG_W/tmp-picture"
  v=$(rg_fails "$L" - '^FAIL ')
  rg_item picture.mutant "$(rg_say "$v")" "$(rg_rest "$v") (the picture one pixel off)"
}

stage_scripts() {
  local bb L v T=$RG_W/le-scripts E=$RG_W/empty-scripts
  local BP=packages/sysutils/hades-picture/scripts/boot-picture K=packages/mediacenter/kodi/scripts
  bb=$(busybox_of_image)
  rg_le_tree "$T" $BP $K/kodi-display-notice $K/kodi-failed-notice
  if [ -z "$bb" ] || [ ! -f "$T/$BP" ]; then
    rg_item picture-scripts FAILED "no busybox of the image, or no boot-picture at $REGRESS_LE_REF"
    rg_item picture-scripts.empty FAILED "the same"; return 1
  fi
  L=$RG_W/logs/picture-scripts.log
  rg_log "$L" bash "$HERE/picture_scripts_test.sh" "$bb" "$T/$BP" "$T/$K/kodi-display-notice" "$T/$K/kodi-failed-notice"
  v=$(rg_pass "$L" '^picture-scripts: [0-9]+ checks passed, 0 failed$' '^FAIL ')
  rg_item picture-scripts "$(rg_say "$v")" "$(rg_rest "$v")"
  rg_fresh "$E"
  : > "$E/bp"; : > "$E/dn"; : > "$E/fn"
  L=$RG_W/logs/picture-scripts-empty.log
  rg_log "$L" bash "$HERE/picture_scripts_test.sh" "$bb" "$E/bp" "$E/dn" "$E/fn"
  v=$(rg_fails "$L" 17 '^FAIL ')
  rg_item picture-scripts.empty "$(rg_say "$v")" "$(rg_rest "$v")"
}

stage_netwait() {
  local bb L v m T=$RG_W/le-netwait
  local NW=packages/sysutils/wait-time-sync/scripts/network-wait CM=packages/sysutils/busybox/scripts/clock-measure
  bb=$(busybox_of_image)
  rg_le_tree "$T" $NW $CM
  if [ -z "$bb" ] || [ ! -f "$T/$NW" ] || [ ! -f "$T/$CM" ]; then
    rg_item network-wait FAILED "no busybox of the image, or no network-wait or clock-measure at $REGRESS_LE_REF"; return 1
  fi
  L=$RG_W/logs/network-wait.log
  rg_log "$L" bash "$HERE/network_wait_test.sh" "$bb" "$T/$NW" "$T/$CM"
  v=$(rg_pass "$L" '^network-wait: [0-9]+ checks passed, 0 failed$' '^FAIL ')
  rg_item network-wait "$(rg_say "$v")" "$(rg_rest "$v")"
  for m in M1 M2 M3 M4 M5 M6 M7; do
    if grep -a -q "^mutation $m caught" "$L"; then rg_item network-wait.mut.$m PASSED "$(grep -a -m1 "^mutation $m caught" "$L" | cut -c1-150)"
    else rg_item network-wait.mut.$m FAILED "$(grep -a -m1 "MUTATION $m" "$L" || echo 'no line of it')"; fi
  done
}

stage_notreturned() {
  local bb L v T=$RG_W/le-notreturned S=packages/sysutils/hades-diag/scripts
  bb=$(busybox_of_image)
  rg_le_tree "$T" $S/not-returned.awk $S/collect.sh
  if [ -z "$bb" ] || [ ! -f "$T/$S/not-returned.awk" ] || [ ! -f "$T/$S/collect.sh" ]; then
    rg_item not-returned FAILED "no busybox of the image, or no not-returned.awk or collect.sh at $REGRESS_LE_REF"
    rg_item not-returned.returns FAILED "the same"; rg_item not-returned.fixups FAILED "the same"; return 1
  fi
  L=$RG_W/logs/not-returned.log
  rg_log "$L" bash "$HERE/not_returned_test.sh" "$bb" "$T/$S/not-returned.awk" "$T/$S/collect.sh"
  v=$(rg_pass "$L" '^not-returned: [0-9]+ checks passed, 0 failed$' '^FAIL ')
  rg_item not-returned "$(rg_say "$v")" "$(rg_rest "$v")"
  # (the mutants: the returns not counted; the PCI fixups of initcall_debug counted as starts of drivers)
  sed 's/^\t\tpend\[k\]--$/\t\tpend[k] += 0/' "$T/$S/not-returned.awk" > "$RG_W/nr-returns.awk"
  sed 's/index(\$0, "kernel: " s)/index($0, s)/' "$T/$S/not-returned.awk" > "$RG_W/nr-fixups.awk"
  for x in returns:5 fixups:9; do
    if cmp -s "$T/$S/not-returned.awk" "$RG_W/nr-${x%%:*}.awk"; then
      rg_item "not-returned.${x%%:*}" FAILED "the mutant is not made"; continue
    fi
    L=$RG_W/logs/not-returned-${x%%:*}.log
    rg_log "$L" bash "$HERE/not_returned_test.sh" "$bb" "$RG_W/nr-${x%%:*}.awk" "$T/$S/collect.sh"
    v=$(rg_fails "$L" "${x#*:}" '^FAIL ')
    rg_item "not-returned.${x%%:*}" "$(rg_say "$v")" "$(rg_rest "$v")"
  done
}

stage_afternvidia() {
  local bb L v m T=$RG_W/le-afternvidia AN=packages/graphics/nvidia/scripts/after-nvidia
  bb=$(busybox_of_image)
  rg_le_tree "$T" $AN
  if [ -z "$bb" ] || [ ! -f "$T/$AN" ]; then
    rg_item after-nvidia FAILED "no busybox of the image, or no after-nvidia at $REGRESS_LE_REF"; return 1
  fi
  L=$RG_W/logs/after-nvidia.log
  rg_log "$L" bash "$HERE/after_nvidia_test.sh" "$bb" "$T/$AN"
  v=$(rg_pass "$L" '^AFTER-NVIDIA PASSED$' '^  FAIL ')
  rg_item after-nvidia "$(rg_say "$v")" "$(rg_rest "$v")"
  for m in M1 M2 M3 M4 M5 M6 M7; do
    if grep -a -q "^mutation $m (.*) caught" "$L"; then rg_item after-nvidia.mut.$m PASSED "$(grep -a -m1 "^mutation $m " "$L" | cut -c1-150)"
    else rg_item after-nvidia.mut.$m FAILED "$(grep -a -m1 "^mutation $m " "$L" || echo 'no line of it')"; fi
  done
}

stage_nvidiasleep() {
  local bb L v m T=$RG_W/le-nvidiasleep SL=packages/graphics/nvidia/scripts/sleep LK=packages/graphics/nvidia/scripts/stack-links
  bb=$(busybox_of_image)
  rg_le_tree "$T" $SL $LK
  if [ -z "$bb" ] || [ ! -f "$T/$SL" ] || [ ! -f "$T/$LK" ]; then
    rg_item nvidia-sleep FAILED "no busybox of the image, or no sleep hook or stack-links at $REGRESS_LE_REF"; return 1
  fi
  L=$RG_W/logs/nvidia-sleep.log
  rg_log "$L" bash "$HERE/nvidia_sleep_test.sh" "$bb" "$T/$SL" "$T/$LK"
  v=$(rg_pass "$L" '^NVIDIA-SLEEP PASSED$' '^  FAIL ')
  rg_item nvidia-sleep "$(rg_say "$v")" "$(rg_rest "$v")"
  for m in M1 M2 M3 M4 M5 M6; do
    if grep -a -q "^mutation $m (.*) caught" "$L"; then rg_item nvidia-sleep.mut.$m PASSED "$(grep -a -m1 "^mutation $m " "$L" | cut -c1-150)"
    else rg_item nvidia-sleep.mut.$m FAILED "$(grep -a -m1 "^mutation $m " "$L" || echo 'no line of it')"; fi
  done
}

stage_sleepreport() {
  local bb L v m T=$RG_W/le-sleepreport SC=packages/sysutils/hades-diag/scripts/sleep.sh
  bb=$(busybox_of_image)
  rg_le_tree "$T" $SC
  if [ -z "$bb" ] || [ ! -f "$T/$SC" ]; then
    rg_item sleep-report FAILED "no busybox of the image, or no sleep.sh at $REGRESS_LE_REF"; return 1
  fi
  L=$RG_W/logs/sleep-report.log
  rg_log "$L" bash "$HERE/sleep_report_test.sh" "$bb" "$T/$SC"
  v=$(rg_pass "$L" '^SLEEP-REPORT PASSED$' '^  FAIL ')
  rg_item sleep-report "$(rg_say "$v")" "$(rg_rest "$v")"
  for m in M1 M2 M3 M4 M5 M6 M7; do
    if grep -a -q "^mutation $m (.*) caught" "$L"; then rg_item sleep-report.mut.$m PASSED "$(grep -a -m1 "^mutation $m " "$L" | cut -c1-150)"
    else rg_item sleep-report.mut.$m FAILED "$(grep -a -m1 "^mutation $m " "$L" || echo 'no line of it')"; fi
  done
}

stage_wake() {
  local bb L m v T=$RG_W/le-wake W=packages/sysutils/hades-wake SC=packages/sysutils/hades-diag/scripts/sleep.sh
  bb=$(busybox_of_image)
  rg_le_tree "$T" $W $SC
  if [ -z "$bb" ] || [ ! -f "$T/$W/scripts/wake.sh" ]; then
    rg_item wake FAILED "no busybox of the image, or no wake.sh at $REGRESS_LE_REF"; return 1
  fi
  L=$RG_W/logs/wake.log
  rg_log "$L" bash "$HERE/wake_test.sh" "$bb" "$T/$W/scripts/wake.sh" "$T/$SC" "$T/$W/udev.d/70-hades-wake.rules" \
    "$T/$W/sleep.d/hades-wake"
  v=$(rg_pass "$L" '^WAKE PASSED$' '^  FAIL ')
  rg_item wake "$(rg_say "$v")" "$(rg_rest "$v")"
  for m in M1 M2 M3 M4 M5 M6 M7 M8 M9 M10 M11 M12 M13 M14; do
    if grep -a -q "^mutation $m (.*) caught" "$L"; then rg_item wake.mut.$m PASSED "$(grep -a -m1 "^mutation $m " "$L" | cut -c1-150)"
    else rg_item wake.mut.$m FAILED "$(grep -a -m1 "^mutation $m " "$L" || echo 'no line of it')"; fi
  done
}

# the setting "Wake from sleep" of the LibreELEC settings add-on: the add-on of the recipe (archive checked against
# PKG_SHA256, every patch with -F0), with stand-ins for Kodi; and the same with LibreELEC-settings-9093 reversed (and
# every patch after it, the last first), which must fail
stage_wakesettings() {
  local P=packages/mediacenter/LibreELEC-settings ver sha tb S=$RG_W/wakesettings L p n v
  ver=$(rg_pkgvar $P PKG_VERSION); sha=$(rg_pkgvar $P PKG_SHA256)
  tb=$(rg_source LibreELEC-settings "LibreELEC-settings-$ver.tar.gz")
  rg_fresh $S/src $S/without
  rg_le_tree $S/le $P/patches
  if [ -z "$tb" ] || [ "$(sha256sum < "$tb" | cut -c1-64)" != "$sha" ]; then
    rg_item wake-settings FAILED "no LibreELEC-settings-$ver.tar.gz with PKG_SHA256 in REGRESS_SOURCES"
    rg_item wake-settings.without-9093 FAILED "no LibreELEC-settings source"; return 1
  fi
  tar -xzf "$tb" --strip-components=1 -C $S/src
  for p in $(ls $S/le/$P/patches/*.patch | sort); do
    patch -d $S/src -p1 -F0 -s --no-backup-if-mismatch < "$p" ||
      { rg_item wake-settings FAILED "patch $(basename $p) does not apply with -F0"; return 1; }
  done
  L=$RG_W/logs/wake-settings.log
  rg_log "$L" python3 -I "$HERE/wake_settings_test.py" $S/src
  v=$(rg_pass "$L" '^ALL PASSED$')
  rg_item wake-settings "$(rg_say "$v")" "$(rg_rest "$v") ($(grep -c '^ok:' $L) ok)"
  cp -a $S/src/. $S/without/
  for p in $(ls $S/le/$P/patches/LibreELEC-settings-[0-9]*-*.patch | sort -r); do
    n=$(basename "$p" | sed -E 's/^LibreELEC-settings-([0-9]+)-.*/\1/')
    [ "$n" -ge 9093 ] || continue
    patch -R -p1 -F0 -s --no-backup-if-mismatch -d $S/without < "$p" ||
      { rg_item wake-settings.without-9093 FAILED "$(basename $p) does not reverse"; return 1; }
  done
  L=$RG_W/logs/wake-settings.without-9093.log
  rg_log "$L" python3 -I "$HERE/wake_settings_test.py" $S/without
  [ "$(rg_exit $L)" != 0 ] && rg_item wake-settings.without-9093 PASSED "it fails without 9093: exit $(rg_exit $L); $(rg_last $L | cut -c1-120)" ||
    rg_item wake-settings.without-9093 FAILED "it passes without 9093"
  rg_rm $S/without
}

rg_main "menus today olderlook oldertext notice themeupdate installer picture scripts netwait notreturned afternvidia nvidiasleep sleepreport wake wakesettings" "$@"
