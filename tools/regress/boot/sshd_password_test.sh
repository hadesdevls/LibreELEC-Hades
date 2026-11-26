#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of what sshd is told before it starts and at each reload (packages/network/openssh/scripts/
# sshd-default-password): while root has the default password, no password logins and a banner that says why and where
# to set one; none of that once root has a password of its own, or SSH was turned on with the boot parameter "ssh", or
# Network access is "As in LibreELEC" (the file /storage/.cache/hades-network says libreelec), where SSH takes the
# default password as LibreELEC does. And the line of the diagnostic report that says which network access is in
# effect (packages/sysutils/hades-diag/scripts/net-access.sh). A root folder of files stands for the system; the
# busybox of the image runs them.
#
# Usage: sshd_password_test.sh <busybox> <sshd-default-password> <net-access.sh>
# Then each of its mutations must fail at least one check. Exit 0 when every check passes and every mutation is caught,
# 1 otherwise, 2 when the test could not run.
set -u
BBI=${1:-}; SP=${2:-}; NA=${3:-}
[ -x "$BBI" ] && [ -f "$SP" ] && [ -f "$NA" ] || { echo "usage: $0 <busybox> <sshd-default-password> <net-access.sh>"; exit 2; }
T=$(mktemp -d); trap 'rm -rf -- "$T"' EXIT
mkdir -p "$T/bb"
for a in $($BBI --list); do ln -s "$BBI" "$T/bb/$a"; done
[ -e "$T/bb/cryptpw" ] || { echo "no cryptpw in the busybox"; exit 2; }
FAILS=0
check() { if [ "$2" = 0 ]; then echo "  ok   $1"; else echo "  FAIL $1"; FAILS=$((FAILS + 1)); fi; }
R=$T/root; C=$R/run/libreelec/sshd_config.d
DEF=$(PATH="$T/bb" cryptpw -m sha512 -S defaultsalt1234 libreelec)
OWN=$(PATH="$T/bb" cryptpw -m sha512 -S ownsalt12345678 something-else)
OTHERSALT=$(PATH="$T/bb" cryptpw -m sha512 -S anothersalt9876 libreelec)
sys() { # sys <hash of root now> <cmdline> [libreelec]
  rm -rf "$R"; mkdir -p "$R/storage/.cache" "$R/usr/cache" "$R/proc"
  echo "root:$DEF:19000:0:99999:7:::" > "$R/usr/cache/shadow"
  echo "root:$1:19000:0:99999:7:::" > "$R/storage/.cache/shadow"
  echo "$2" > "$R/proc/cmdline"
  [ -n "${3:-}" ] && echo "$3" > "$R/storage/.cache/hades-network"
  return 0
}
run() { PATH="$T/bb" HADES_SSHD_ROOT="$R" /usr/bin/timeout 20 "$T/bb/sh" "$1" ${2:-} > "$T/out" 2>&1; RC=$?; }

cases() {
  local s=$1
  echo "1. the default password: no password logins, and a banner that says why and where to set one"
  sys "$DEF" "BOOT_IMAGE=/KERNEL quiet"; run "$s"
  [ $RC = 0 ] && grep -qx 'PasswordAuthentication no' "$C/50-default-password.conf" && grep -qx 'KbdInteractiveAuthentication no' "$C/50-default-password.conf" &&
    grep -qx 'Banner /run/libreelec/sshd_config.d/default-password-banner.txt' "$C/50-default-password.conf"
  check "1a the settings for sshd, the banner by its path on the system" $?
  grep -q 'password logins are off while root has the default password' "$C/default-password-banner.txt" &&
    grep -q 'Settings > LibreELEC > Services > SSH > SSH Password' "$C/default-password-banner.txt" &&
    grep -q 'Ajustes > LibreELEC > Servicios > SSH > Contraseña SSH' "$C/default-password-banner.txt" &&
    ! grep -q 'libreelec$\|libreelec ' "$C/default-password-banner.txt"
  check "1b the banner: why, and where, in English and Spanish; never the password itself" $?
  run "$s" check; [ $RC = 0 ]; check "1c check: the default password" $?
  echo "2. the default password set again, with another salt: the same"
  sys "$OTHERSALT" "quiet"; run "$s"; grep -qx 'PasswordAuthentication no' "$C/50-default-password.conf"; check "2 another salt" $?
  echo "3. a password of its own: password logins, no banner (and the ones of before gone)"
  run "$s"; sed -i "s|^root:[^:]*|root:$OWN|" "$R/storage/.cache/shadow"; run "$s"
  [ $RC = 0 ] && [ ! -e "$C/50-default-password.conf" ] && [ ! -e "$C/default-password-banner.txt" ]; check "3a nothing for sshd" $?
  run "$s" check; [ $RC = 1 ]; check "3b check: not the default password" $?
  echo "4. SSH turned on with the boot parameter ssh: password logins, as in LibreELEC, and a warning"
  sys "$DEF" "quiet ssh"; run "$s"
  [ ! -e "$C/50-default-password.conf" ] && [ ! -e "$C/default-password-banner.txt" ] && grep -q '^WARNING: root still has the default password' "$T/out"
  check "4 the boot parameter" $?
  echo "5. Network access As in LibreELEC: SSH takes the default password as LibreELEC does, and says so"
  sys "$DEF" "quiet" libreelec; run "$s"
  [ ! -e "$C/50-default-password.conf" ] && [ ! -e "$C/default-password-banner.txt" ] &&
    grep -q 'password logins are allowed, as in LibreELEC' "$T/out"
  check "5a As in LibreELEC" $?
  sys "$DEF" "quiet" "something else"; run "$s"; grep -qx 'PasswordAuthentication no' "$C/50-default-password.conf"
  check "5b a file that says something else: refused" $?
  echo "6. the report says the network access in effect"
  local na=$2 S=$R/storage/.cache/services
  rm -rf "$R"; mkdir -p "$S" "$R/storage/.kodi/userdata"
  echo 'RULES=home' > "$S/iptables.conf"; echo 'SAMBA_SECURE="true"' > "$S/samba.disabled"
  { echo '<settings version="2">'; echo '    <setting id="services.airplay" default="true">false</setting>'; echo '</settings>'; } \
    > "$R/storage/.kodi/userdata/guisettings.xml"
  out=$(PATH="$T/bb" HADES_ROOT="$R" "$T/bb/sh" "$na")
  [ "$out" = "network access: Recommended (samba=off ssh=refuses firewall=home airplay=off)" ]; check "6a a new system: $out" $?
  echo 'SAMBA_SECURE="false"' > "$S/samba.conf"; rm "$S/samba.disabled"; rm "$S/iptables.conf"; touch "$S/iptables.disabled"
  echo libreelec > "$R/storage/.cache/hades-network"; sed -i 's/>false</>true</' "$R/storage/.kodi/userdata/guisettings.xml"
  out=$(PATH="$T/bb" HADES_ROOT="$R" "$T/bb/sh" "$na")
  [ "$out" = "network access: As in LibreELEC (samba=open ssh=accepts firewall=none airplay=on)" ]; check "6b As in LibreELEC: $out" $?
  sed -i 's/>true</>false</' "$R/storage/.kodi/userdata/guisettings.xml"
  out=$(PATH="$T/bb" HADES_ROOT="$R" "$T/bb/sh" "$na")
  [ "$out" = "network access: own settings (samba=open ssh=accepts firewall=none airplay=off)" ]; check "6c one changed afterwards: $out" $?
  echo 'SAMBA_SECURE="true"' > "$S/samba.disabled"
  out=$(PATH="$T/bb" HADES_ROOT="$R" "$T/bb/sh" "$na")
  case "$out" in *"samba=off "*) r=0 ;; *) r=1 ;; esac; check "6d Samba turned off: $out" $r
}

echo "sshd-default-password: $SP"
sed 's/@ROOT_PASSWORD@/libreelec/' "$SP" > "$T/sdp"
cases "$T/sdp" "$NA"
TREE_FAILS=$FAILS
echo "$TREE_FAILS check(s) failed"
MUT_MISSED=0
mutation() {
  local m=$T/mut-$1 src=$T/sdp
  [ "${4:-}" = n ] && src=$NA
  sed -e "$3" "$src" > "$m"
  if cmp -s "$m" "$src"; then echo "mutation M$1 ($2): its text is no longer in the script: write it again"; MUT_MISSED=$((MUT_MISSED + 1)); return; fi
  FAILS=0
  if [ "${4:-}" = n ]; then cases "$T/sdp" "$m" > "$T/mut-$1.log" 2>&1; else cases "$m" "$NA" > "$T/mut-$1.log" 2>&1; fi
  if [ $FAILS -gt 0 ]; then echo "mutation M$1 ($2) caught: $FAILS check(s) failed"
  else echo "mutation M$1 ($2) NOT caught"; MUT_MISSED=$((MUT_MISSED + 1)); fi
}
mutation 1 "no banner for sshd" 's|^    printf .PasswordAuthentication no\\nKbdInteractiveAuthentication no\\nBanner %s\\n. "${BANNER#${ROOT}}" >"${CONF}"$|    printf "PasswordAuthentication no\\nKbdInteractiveAuthentication no\\n" >"${CONF}"|'
mutation 2 "an old banner left" 's|^rm -f "${CONF}" "${BANNER}"$|rm -f "${CONF}"|'
mutation 3 "the salt of the default password not tried" 's|^      \[ "$(cryptpw -m sha512 -S "${salt}" "${DEFAULT_PASSWORD}" 2>/dev/null)" = "${current}" \] && return 0$|      :|'
mutation 4 "As in LibreELEC not read" 's|^  elif ssh_as_libreelec; then$|  elif false; then|'
mutation 5 "the report: AirPlay not counted" 's/^  open:accepts:none:on) state="As in LibreELEC" ;;$/  open:accepts:none:*) state="As in LibreELEC" ;;/' n
[ $TREE_FAILS = 0 ] && [ $MUT_MISSED = 0 ] && { echo "SSHD PASSED"; exit 0; }
echo "SSHD FAILED: $TREE_FAILS check(s) failed, $MUT_MISSED mutation(s) not caught"
exit 1
