#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Host test of the shares that samba-config writes for the drives under /media (SAMBA_AUTOSHARE).
# The folder of a drive is named after its label (udevil: the first 20 bytes, stripped, no "/" and,
# in Hades, no control characters), so whoever made the drive chooses it. The test runs samba-config
# INSIDE the system tree of an image (the image's busybox and testparm, in a user and mount namespace
# of its own, without privilege; /run and /var on tmpfs) with drive folders named as such labels can
# be, and reads the result with the image's testparm, which is how Samba reads it.
#
# Usage: autoshare_test.sh <system tree of an image> <samba-config> [<reference samba-config>]
# With a reference (the samba-config before a change), a pass with ordinary names only must give the
# same smb.conf byte for byte, and the reference's own faults are listed (they are expected).
# WORK=<folder> keeps the outputs there. Exit 0 when every check passes, 1 when one fails, 2 when
# the test could not run.
set -u
export LC_ALL=C
SYS=$1; SCRIPT=$2; REF=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
W=${WORK:-$(mktemp -d)}
[ -x "$SYS/usr/bin/busybox" ] && [ -x "$SYS/usr/bin/testparm" ] || { echo "not a system tree with testparm: $SYS"; exit 2; }
for d in dev proc storage run var; do [ -d "$SYS/$d" ] || { echo "the tree has no /$d"; exit 2; }; done
rm -rf "$W/storage"; mkdir -p "$W/storage/test"
cp "$SCRIPT" "$W/storage/test/samba-config" || exit 2
if [ -n "$REF" ]; then mkdir -p "$W/storage/ref"; cp "$REF" "$W/storage/ref/samba-config" || exit 2; fi
cat > "$W/storage/run.sh" <<'EOF'
#!/bin/sh
# Inside the image: for each samba-config, a pass with ordinary names and one with every kind of name.
O=/storage/out
rm -rf $O; mkdir -p $O /storage/.cache/services
cat > /storage/.cache/services/samba.conf <<'CONF'
SAMBA_AUTOSHARE="true"
SAMBA_SECURE="true"
SAMBA_USERNAME="tester"
SAMBA_PASSWORD="Pw123456"
CONF
mk() { mkdir -p "/var/media/$1" && printf '%s' "$1" > "/var/media/$1/MARK"; }
pass() {
  rm -rf /run/samba
  /bin/sh /storage/$1/samba-config > $O/$1-$2.log 2>&1
  cp /run/samba/smb.conf $O/$1-$2.conf
  testparm -s /run/samba/smb.conf > $O/$1-$2.testparm 2> $O/$1-$2.testparm-err
  # every path as testparm read it, and where it leads in this tree
  sed -n 's/^[[:space:]]*path = //p' $O/$1-$2.testparm | while IFS= read -r p; do
    r=$(readlink -f -- "$p" 2> /dev/null) || r=
    [ -n "$r" ] && [ -d "$r" ] || r=MISSING
    printf '%s\t%s\n' "$p" "$r"
  done > $O/$1-$2.paths
  ls -A /run/samba/media > $O/$1-$2.links 2> /dev/null || :
}
# the folders Samba keeps its state in, as on the running system
mkdir -p /var/lib/samba/private /var/cache/samba /var/lock
for v in test ref; do
  [ -f /storage/$v/samba-config ] || continue
  # ordinary names: the smb.conf must stay as it was
  rm -rf /var/media; mkdir -p /var/media
  for n in 'Plain' 'Películas' 'My Drive' 'USB 2.0 (64 GB)' 'Back-up_2026' 'Ünïcödé Ωmega'; do mk "$n"; done
  ls -A /var/media > $O/$v-plain.drives
  pass $v plain
  # names that Samba or the shell read otherwise (single quotes: the backslashes are in the name)
  for n in 'Movies%U' '100%' 'Env%$(HOME)' '%' 'x]\npath=\0057' 'a]\nroot preexec=x' 'Ends\' \
           'Two  Spaces' 'Videos' 'videos' 'Update' 'global' 'HOMES' 'IPC$' 'brack[et]s' 'Plain-2'; do
    mk "$n"
  done
  ls -A /var/media > $O/$v-names.drives
  pass $v names
  # a name that leaves a section header without its "]"
  rm -rf /var/media; mkdir -p /var/media
  for n in 'Plain' 'b\nc'; do mk "$n"; done
  ls -A /var/media > $O/$v-break.drives
  pass $v break
done
exit 0
EOF
ROOT=$(mktemp -d)
unshare --user --map-root-user --mount bash -c '
  set -e
  SYS=$1; W=$2; ROOT=$3
  mount --rbind "$SYS" "$ROOT"
  mount --rbind /dev "$ROOT/dev"
  mount --rbind /proc "$ROOT/proc"
  mount --bind "$W/storage" "$ROOT/storage"
  mount -t tmpfs tmpfs "$ROOT/run"
  mount -t tmpfs tmpfs "$ROOT/var"
  mount -o remount,bind,ro "$ROOT"
  exec chroot "$ROOT" /bin/sh /storage/run.sh
' bash "$SYS" "$W" "$ROOT"
rc=$?
rmdir "$ROOT" 2> /dev/null
[ $rc -eq 0 ] || { echo "the run inside the image failed ($rc)"; exit 2; }
echo "outputs: $W/storage/out"
python3 -I "$HERE/autoshare_verdict.py" "$W/storage/out" "$SYS/etc/samba/smb.conf"
