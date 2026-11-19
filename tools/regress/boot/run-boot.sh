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
# Host test only: it writes under $REGRESS_WORK/boot and reads the tree at $REGRESS_LE_REF and the busybox of the build
# folder ($REGRESS_LE_BUILD/install_pkg/busybox-*).
# usage: run-boot.sh all | verdict [<log>] | <stage>...       stages: menus today olderlook oldertext notice themeupdate installer picture
#                                                              scripts netwait
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

rg_main "menus today olderlook oldertext notice themeupdate installer picture scripts netwait" "$@"
