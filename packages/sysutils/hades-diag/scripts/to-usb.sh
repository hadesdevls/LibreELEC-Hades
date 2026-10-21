#!/bin/sh
# Hades diagnostics: what they saved on the boot partition (/flash/hades-diag, every file of it through their
# filter already) copied to a USB drive. It is the way to get them from a system on a disk inside the PC, whose
# boot partition Windows does not show:
#   to-usb.sh list               the drives that it can be copied to, one per line: <KiB free><tab><mount point>
#   to-usb.sh copy <mount point> the copy, into <drive>/hades-diag/<date and time> (LibreELEC settings, System)
#   to-usb.sh auto <device>      run once a drive is mounted (hades-diag-usb@.service): a drive with a folder
#                                named hades-diag at its top gets the copy in there, with nothing on the screen,
#                                and a note there, NOT-COPIED.txt, when it does not
# The folder is read into RAM while nothing of the diagnostics writes to the boot partition (their lock), written
# beside its final name, read back from the drive and compared with the SHA-256 of every file, and only then given
# its name, with those sums in it (SHA256SUMS.txt). Nothing that is on the drive is written over. Folders that a
# version before 15 wrote without the filter, and that the start of this one has not put through it yet (they
# have no file .filtered), are left out.
# Exit status: 0 copied; 10 nothing to copy; 11 not a drive that it is copied to; 12 no room on the drive; 13 what
# is read back from the drive is not what was written; 14 it cannot be written; 15 the boot partition, or the
# drive, was busy for too long; 16 not enough free memory; 2 not a command of this script.
D=/flash/hades-diag
MOUNTS=/proc/mounts
BLOCK=/sys/class/block
RUN=/run
TMP=/tmp
DROP=/proc/sys/vm/drop_caches

# Mount points as /proc/mounts writes them: a space, a tab and a backslash are \040, \011 and \134.
esc() { printf '%s' "$1" | sed -e 's/\\/\\134/g' -e 's/ /\\040/g' -e 's/	/\\011/g'; }
unesc() { printf '%s' "$1" | sed -e 's/\\040/ /g' -e 's/\\011/	/g' -e 's/\\134/\\/g'; }

# The device and the options of what is mounted at $1 (the last one mounted there, the one that is seen).
mounted() { M=$(esc "$1") awk '$2 == ENVIRON["M"] { d = $1; o = $4 } END { if (d != "") print d, o }' $MOUNTS; }

# KiB free on the file system of $1 (from the left of the line: a mount point may have spaces).
free_kib() { df -P -k "$1" 2>/dev/null | awk 'NR == 2 { print $4 + 0 }'; }

# A drive that the diagnostics go to: mounted under /var/media, where drives are mounted on their own, with
# writing allowed, and a disk on USB, a removable one or an SD card, by the rule that the system mounts disks
# with (95-udevil-mount.rules): never a disk inside the PC, which is mounted read-only anyway where it is shown.
eligible() {
  local m="$1" dev opts name disk path
  case "$m" in /var/media/?*|/media/?*) ;; *) return 1 ;; esac
  set -- $(mounted "$m")
  [ $# -eq 2 ] || return 1
  dev=$1 opts=$2
  case ",$opts," in *,rw,*) ;; *) return 1 ;; esac
  case "$dev" in /dev/sd[a-z]*|/dev/mmcblk[0-9]*) ;; *) return 1 ;; esac
  name=${dev#/dev/}
  [ -e "$BLOCK/$name" ] || return 1
  path=$(readlink -f "$BLOCK/$name")
  disk=$name
  [ -e "$BLOCK/$name/partition" ] && disk=${path%/*} && disk=${disk##*/}
  case "$path" in */usb[0-9]*) return 0 ;; esac
  [ "$(cat "$BLOCK/$disk/removable" 2>/dev/null)" = 1 ] && return 0
  case "$disk" in mmcblk*) [ "$(cat "$BLOCK/$disk/device/type" 2>/dev/null)" = SD ] && return 0 ;; esac
  return 1
}

# The boot partition of a LibreELEC (another drive with Hades on it): an update needs room there.
boot_partition() { [ -f "$1/KERNEL" ] && [ -f "$1/SYSTEM" ]; }

# Something to copy: the folder has a note or a folder that has been through the filter (and is not one that the
# refilter of collect.sh left unfinished).
something() {
  local f
  for f in "$D"/*; do
    [ -f "$f" ] && return 0
    case "$f" in *.part) continue ;; esac
    [ -d "$f" ] && [ -e "$f/.filtered" ] && return 0
  done
  return 1
}

list() {
  something || exit 10
  awk '{ print $2 }' $MOUNTS | sort -u | while read -r e; do
    m=$(unesc "$e")
    eligible "$m" || continue
    printf '%s\t%s\n' "$(free_kib "$m")" "$m"
  done
  exit 0
}

# A lock, a directory in RAM: waited for at most $2 seconds.
lock() {
  local n=0
  while ! mkdir "$1" 2>/dev/null; do
    n=$((n + 1)); [ $n -le $2 ] || return 1; sleep 1
  done
}

S= HELD=
finish() {
  [ -n "$S" ] && rm -rf "$S" "$S.sums"
  [ -n "$HELD" ] && rmdir "$RUN/hades-flash.lock" 2>/dev/null
  rmdir "$RUN/hades-to-usb.lock" 2>/dev/null
}

# The copy to the drive mounted at $1, into its folder $2 (made where it is not there).
copy() {
  local m="$1" into="$2" need room margin name target part files bytes f b
  something || exit 10
  eligible "$m" || exit 11
  # (one copy at a time: the settings and a drive that is plugged in may ask at once)
  lock "$RUN/hades-to-usb.lock" 300 || exit 15
  trap finish EXIT
  trap 'exit 1' INT TERM HUP
  need=$(du -sk "$D" 2>/dev/null | awk '{ print $1 + 0 }')
  [ "$(free_kib "$TMP")" -gt $((need + 65536)) ] || exit 16
  S=$(mktemp -d "$TMP/hades-to-usb.XXXXXX") || exit 16
  # what the folder is now, while the diagnostics write nothing (collect.sh, watch-dv.sh, shutdown.sh: at most a
  # minute, as they wait for each other)
  lock "$RUN/hades-flash.lock" 60 || exit 15
  HELD=1
  for f in "$D"/*; do
    b=${f##*/}
    if [ -d "$f" ]; then
      case "$b" in *.part) continue ;; esac
      [ -e "$f/.filtered" ] || continue
      cp -r "$f" "$S/" || exit 16
    elif [ -f "$f" ]; then
      cp "$f" "$S/" || exit 16
    fi
  done
  rmdir "$RUN/hades-flash.lock" 2>/dev/null
  HELD=
  # (the copy has been through the filter as well: a drive with Hades of its own does not put it through again)
  : > "$S/.filtered"
  (cd "$S" && find . -type f | sort | while read -r f; do sha256sum "$f" || exit 1; done) > "$S.sums" || exit 16
  files=$(wc -l < "$S.sums")
  bytes=$(find "$S" -type f | while read -r f; do wc -c < "$f"; done | awk '{ s += $1 } END { print s + 0 }')
  [ "$files" -gt 1 ] || exit 10
  # room for it on the drive: what it holds, a cluster of 64 KiB more for each file and folder at most, and what
  # stays free (where the drive has a LibreELEC of its own, what its updates need, as collect.sh leaves it)
  margin=16384
  boot_partition "$m" && margin=131072
  room=$(( bytes / 1024 + (files + $(find "$S" -type d | wc -l)) * 64 + margin ))
  [ "$(free_kib "$m")" -gt "$room" ] || exit 12
  mkdir -p "$m/$into" 2>/dev/null || exit 14
  name=$(date +%Y-%m-%d-%H.%M.%S)
  target="$m/$into/$name"
  n=1
  while [ -e "$target" ] || [ -e "$target.part" ]; do n=$((n + 1)); target="$m/$into/$name-$n"; done
  part="$target.part"
  if ! mkdir "$part" || ! cp -r "$S/." "$part/" || ! sync -f "$part"; then
    rm -rf "$part"; sync -f "$m/$into" 2>/dev/null; exit 14
  fi
  # read back from the drive itself, not from what the system still has of it in memory
  sync; echo 1 > $DROP 2>/dev/null
  if ! (cd "$part" && sha256sum -c -s "$S.sums") || [ "$(cd "$part" && find . -type f | wc -l)" != "$files" ]; then
    rm -rf "$part"; sync -f "$m/$into" 2>/dev/null; exit 13
  fi
  # (-T: never into a folder of that name that came meanwhile, -n: never over anything)
  if ! cp "$S.sums" "$part/SHA256SUMS.txt" || ! mv -n -T "$part" "$target" || [ -e "$part" ] || ! sync -f "$target"; then
    rm -rf "$part"; exit 14
  fi
  printf '%s\t%s\t%s\n' "${target#$m/}" "$files" "$bytes"
  exit 0
}

# What NOT-COPIED.txt says for an exit status of copy.
why() {
  case $1 in
    10) echo "There was nothing to copy: the diagnostics are off on that PC, or have saved nothing yet. Turn them on (tick
\"Save diagnostics there\" in the installer when you install or update), start the PC, wait five minutes, and plug this
drive in again.|No había nada que copiar: el diagnóstico está apagado en ese PC, o todavía no ha guardado nada.
Enciéndelo (marca \"Guardar diagnóstico ahí\" en el instalador al instalar o actualizar), arranca el PC, espera cinco
minutos y vuelve a conectar esta unidad." ;;
    11) echo "This drive cannot be used for it: it has to be a USB drive that the system mounts with writing allowed.|Esta
unidad no sirve para eso: tiene que ser una unidad USB que el sistema monte con escritura." ;;
    12) echo "There is not enough room on this drive.|No hay sitio suficiente en esta unidad." ;;
    13) echo "What was read back from this drive was not what had been written: the drive may be failing. Nothing was left
on it: try another drive.|Lo que se releyó de esta unidad no era lo que se había escrito: puede que la unidad falle. No
se dejó nada en ella: prueba con otra." ;;
    14) echo "The copy could not be written to this drive. Nothing was left on it.|No se pudo escribir la copia en esta
unidad. No se dejó nada en ella." ;;
    15) echo "The PC was busy writing its diagnostics, or another copy, for too long. Plug this drive in again.|El PC
estuvo demasiado tiempo escribiendo su diagnóstico, u otra copia. Vuelve a conectar esta unidad." ;;
    16) echo "The PC did not have enough free memory for the copy.|El PC no tenía memoria libre suficiente para la
copia." ;;
    *) echo "Something went wrong ($1).|Algo salió mal ($1)." ;;
  esac
}

# A drive that was just mounted ($1, its device): only one with a folder named hades-diag at its top, and not the
# boot partition of a LibreELEC (that one has a folder of that name for its own diagnostics).
auto() {
  local dev="$1" e m into status text
  e=$(DEV=$dev awk '$1 == ENVIRON["DEV"] { m = $2 } END { print m }' $MOUNTS)
  [ -n "$e" ] || exit 0
  m=$(unesc "$e")
  into=$(ls -a "$m" 2>/dev/null | grep -i -x 'hades-diag' | head -n 1)
  [ -n "$into" ] && [ -d "$m/$into" ] || exit 0
  if boot_partition "$m"; then
    echo "$dev: the boot partition of a LibreELEC, with diagnostics of its own: nothing is copied to it"
    exit 0
  fi
  echo "$dev: it has a folder $into at its top: the diagnostics are copied into it"
  text=$( (copy "$m" "$into") )
  status=$?
  if [ $status -eq 0 ]; then
    rm -f "$m/$into/NOT-COPIED.txt"
    sync -f "$m/$into" 2>/dev/null
    echo "$dev: copied, and read back the same: $text"
    exit 0
  fi
  echo "$dev: not copied (exit status $status)"
  text=$(why $status)
  { echo "LibreELEC Hades did not copy its diagnostics to this drive. (En español, más abajo.)"
    echo
    echo "${text%%|*}"
    echo
    echo "LibreELEC Hades no copió su diagnóstico a esta unidad."
    echo
    echo "${text#*|}"
  } > "$m/$into/NOT-COPIED.txt" 2>/dev/null && sync -f "$m/$into" 2>/dev/null
  exit $status
}

case "$1" in
  list) list ;;
  copy) [ -n "$2" ] || exit 2; copy "$2" hades-diag ;;
  auto) [ -n "$2" ] || exit 2; auto "$2" ;;
  *) exit 2 ;;
esac
