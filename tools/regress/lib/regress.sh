# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# regress.sh: what the runners of tools/regress share (sourced by each run-<group>.sh). Host tests only: a runner
# writes only under $REGRESS_WORK/<group> (and the builds it shares under $REGRESS_WORK/_shared), and never writes
# into the trees, the build folder or the inputs it reads.
#
# Settings (environment); those without a default are required by the runners that use them:
#   REGRESS_WORK       the work folder (required)
#   REGRESS_KODI_GIT   the Kodi repository; REGRESS_KODI_REF the ref under test (default HEAD). The tree is taken
#                      with git archive, never read in place
#   REGRESS_LE_GIT     the LibreELEC repository (default: the one that has this folder); REGRESS_LE_REF (default
#                      HEAD): the recipes (version, PKG_SHA256, patches) of libplacebo and FFmpeg are read from it
#   REGRESS_LE_BUILD   a LibreELEC build folder of that tree (read only): the GLES/EGL and Vulkan headers, the
#                      toolchain's glad, the Linux headers
#   REGRESS_SOURCES    folders (separated by spaces) that may hold <package>/<archive> of the recipes; each archive
#                      is checked against the PKG_SHA256 of its recipe
#   REGRESS_INPUTS     folders (separated by spaces) of the inputs: each file named <sha256> or <sha256>.<ext>
#                      (required by the groups that have inputs)
#   REGRESS_JOBS       compile jobs (default 8)
# Output: one line per check, "REGRESS-ITEM <group>/<name>: PASSED|FAILED|NOT-RUN | <detail>", counted by
# regress_verdict.py line by line.

set -o pipefail
export LC_ALL=C TZ=UTC
RG_LIB=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RG_REGRESS=$(dirname "$RG_LIB")
: "${REGRESS_WORK:?set REGRESS_WORK to a work folder}"
REGRESS_LE_GIT=${REGRESS_LE_GIT:-$(cd "$RG_REGRESS/../.." && pwd)}
REGRESS_LE_REF=${REGRESS_LE_REF:-HEAD}
REGRESS_KODI_REF=${REGRESS_KODI_REF:-HEAD}
RG_JOBS=${REGRESS_JOBS:-8}
REGRESS_INPUTS=${REGRESS_INPUTS:-}
RG_SHARED=$REGRESS_WORK/_shared
RG_PY="python3 -I -B"
# the software GPU: zink on lavapipe (Mesa of the host)
RG_GPU_ENV="MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_ALWAYS_SOFTWARE=1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json"
RG_LPDIR=packages/addons/addon-depends/multimedia-tools-depends/libplacebo

rg_fail() { echo "FAIL: $*"; exit 1; }

# rg_init <group>: the group's work folder $RG_W (logs/, tmp/), mktemp inside it
rg_init() {
  RG_GROUP=$1
  RG_W=$REGRESS_WORK/$1
  mkdir -p "$RG_W/logs" "$RG_W/tmp" "$RG_SHARED" || rg_fail "cannot make $RG_W"
  export TMPDIR=$RG_W/tmp
}

# rg_main "<stages of all, in order>" <command line>: a runner's command line. "all": every stage in order (each a
# function stage_<name>, run in a subshell) into $RG_W/logs/run.log between REGRESS-STAGE lines, then the verdict
# into $RG_W/logs/verdict.txt, exit 0 only when it passed; "verdict [<log>]": the verdict of a log; else the stages
# named, in that order, each to the terminal, stopping at the first that fails.
rg_main() {
  local all=$1 s t0 LOG
  shift
  [ $# -gt 0 ] || { echo "usage: $(basename "$0") all | verdict [<log>] | <stage>... (stages: $all)"; exit 2; }
  case $1 in
    all)
      LOG=$RG_W/logs/run.log
      : > "$LOG"
      echo "$(basename "$0") all, $(date -u +%FT%TZ); LibreELEC $REGRESS_LE_REF $(git -C "$REGRESS_LE_GIT" rev-parse "$REGRESS_LE_REF" 2> /dev/null)${REGRESS_KODI_GIT:+; Kodi $REGRESS_KODI_REF $(rg_kodi_commit)}" | tee -a "$LOG"
      for s in $all; do
        t0=$(date +%s)
        echo "REGRESS-STAGE $s START" | tee -a "$LOG"
        ( "stage_$s" ) 2>&1 | tee -a "$LOG"
        [ "${PIPESTATUS[0]}" = 0 ] && echo "REGRESS-STAGE $s END" | tee -a "$LOG"
        echo "stage $s: $(( $(date +%s) - t0 )) s" | tee -a "$LOG"
      done
      # shellcheck disable=SC2086
      $RG_PY "$RG_LIB/regress_verdict.py" "$LOG" $all | tee "$RG_W/logs/verdict.txt"
      exit "${PIPESTATUS[0]}" ;;
    verdict)
      # shellcheck disable=SC2086
      $RG_PY "$RG_LIB/regress_verdict.py" "${2:-$RG_W/logs/run.log}" $all
      exit $? ;;
    *)
      for s in "$@"; do
        case " $all " in *" $s "*) ( "stage_$s" ) || exit 1 ;; *) echo "no stage $s (stages: $all)"; exit 2 ;; esac
      done ;;
  esac
}

# rg_item <name> <PASSED|FAILED|NOT-RUN> <detail...>
rg_item() {
  local name=$1 v=$2
  shift 2
  case $v in PASSED|FAILED|NOT-RUN) ;; *) set -- "(verdict '$v' is none of the three) $*"; v=FAILED ;; esac
  echo "REGRESS-ITEM $RG_GROUP/$name: $v | $*"
}

# rg_rm <path>...: deletes paths of this run only (under $REGRESS_WORK), each by its own name
rg_rm() {
  local t
  for t in "$@"; do
    case "$t" in
      "$REGRESS_WORK"/?*) case "$t" in *..*) echo "refuse to delete $t" ;; *) rm -rf -- "$t" ;; esac ;;
      *) echo "refuse to delete $t" ;;
    esac
  done
  return 0
}
rg_fresh() { rg_rm "$@"; mkdir -p "$@"; }

# rg_locked <name> <command...>: one at a time across the lanes that share $RG_SHARED
rg_locked() {
  local n=$1
  shift
  ( flock 9 && "$@" ) 9> "$RG_SHARED/.lock-$n"
}

# --------------------------------------------------------------------------------------------------- logs, verdicts
# rg_log <log> <command...>: the whole output of a command into <log>, then "== exit <status> after <s> s"
rg_log() {
  local L=$1 t0 rc
  shift
  t0=$(date +%s)
  { "$@"; } > "$L" 2>&1 < /dev/null
  rc=$?
  echo "== exit $rc after $(( $(date +%s) - t0 )) s" >> "$L"
  return $rc
}
rg_exit() { grep -a -o '^== exit [0-9]*' "$1" 2> /dev/null | tail -n 1 | cut -d' ' -f3; }
rg_last() { grep -a -v '^== ' "$1" 2> /dev/null | grep -a -v '^$' | tail -n 1 | cut -c1-200; }
# sanitizer reports in a log
rg_san() {
  grep -a -c -E 'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer|WARNING: ThreadSanitizer|SUMMARY: (Address|Undefined|Thread)Sanitizer' "$1" 2> /dev/null
}
# rg_pass <log> <ERE of the line that says it passed> [<ERE of a failed check>]: the verdict of a run (made by rg_log)
# that must pass: it ended with status 0, no sanitizer report, no failed check, and the line is there
rg_pass() {
  local L=$1 re=$2 fre=${3:-'^FAIL'} e s n p
  [ -f "$L" ] || { echo "FAILED no log"; return; }
  e=$(rg_exit "$L"); s=$(rg_san "$L"); n=$(grep -a -c -E "$fre" "$L"); p=$(grep -a -E "$re" "$L" | tail -n 1 | cut -c1-200)
  if [ -z "$e" ]; then echo "FAILED it did not end: $(rg_last "$L")"
  elif [ "${s:-0}" -gt 0 ]; then echo "FAILED $s sanitizer report lines, exit $e"
  elif [ "$n" -gt 0 ]; then echo "FAILED $n failed checks, exit $e: $(grep -a -m1 -E "$fre" "$L" | cut -c1-180)"
  elif [ -z "$p" ]; then echo "FAILED no line /$re/, exit $e; last: $(rg_last "$L")"
  elif [ "$e" != 0 ]; then echo "FAILED exit $e; $p"
  else echo "PASSED $p"; fi
}
# rg_fails <log> <number of failed checks> [<ERE of a failed check>]: a run on a mutant that must fail, with exactly
# that many failed checks ("-" for any number above 0)
rg_fails() {
  local L=$1 want=$2 fre=${3:-'^FAIL'} n e
  [ -f "$L" ] || { echo "FAILED no log"; return; }
  e=$(rg_exit "$L"); n=$(grep -a -c -E "$fre" "$L")
  if [ -z "$e" ]; then echo "FAILED it did not end"
  elif [ "$want" = - ] && [ "$n" -gt 0 ]; then echo "PASSED it fails as it must: $n failed checks, exit $e"
  elif [ "$n" = "$want" ] && [ "$n" -gt 0 ]; then echo "PASSED it fails as it must: $n failed checks, exit $e"
  elif [ "$n" -gt 0 ]; then echo "FAILED it fails, but $n failed checks where $want are expected"
  else echo "FAILED nothing fails on the mutant: exit $e; $(rg_last "$L")"; fi
}
# rg_say <verdict line made by rg_pass/rg_fails> -> PASSED|FAILED (its first word)
rg_say() { echo "${1%% *}"; }
rg_rest() { echo "${1#* }"; }

# rg_edit <file> <python file with the pairs>: exact edits of a copy, each text found exactly once (status 3 if not);
# the python file defines PAIRS = [(old, new), ...]
rg_edit() {
  $RG_PY - "$1" "$2" <<'PY'
import runpy, sys
path, spec = sys.argv[1], sys.argv[2]
pairs = runpy.run_path(spec)['PAIRS']
text = open(path, encoding='utf-8').read()
for old, new in pairs:
    n = text.count(old)
    if n != 1:
        print('EDIT NOT APPLIED to %s: the text is there %d times: %r' % (path, n, old[:100]))
        sys.exit(3)
    text = text.replace(old, new)
open(path, 'w', encoding='utf-8').write(text)
print('edit applied to %s (%d pairs)' % (path, len(pairs)))
PY
}

# --------------------------------------------------------------------------------------------------- inputs
# rg_input <sha256>: the path of that input in $REGRESS_INPUTS, or status 1
rg_input() {
  local d f
  for d in ${REGRESS_INPUTS:-}; do
    for f in "$d/$1" "$d/$1".*; do
      [ -f "$f" ] && { echo "$f"; return 0; }
    done
  done
  return 1
}
# rg_inputs <list> <dest>: every input of a list (lines "<sha256> <bytes> <name>", separated by tabs; # comments)
# linked as <dest>/<name> once its size and SHA256 are checked. One "INPUTS" line; status 1 when one is missing or
# differs (each said on its own line). A name may have folders (fidelity/p3/meta.bin).
rg_inputs() {
  local list=$1 dest=$2 sha bytes name rest f n=0 bad=0
  [ -f "$list" ] || { echo "INPUTS: no list $list"; return 1; }
  [ -n "$REGRESS_INPUTS" ] || { echo "INPUTS: REGRESS_INPUTS is not set (the folders of the inputs)"; return 1; }
  mkdir -p "$dest"
  while IFS=$'\t' read -r sha bytes name rest; do
    case "$sha" in ''|'#'*) continue ;; esac
    n=$((n + 1))
    case "$name" in ''|/*|*..*) echo "INPUT BAD NAME: '$name'"; bad=$((bad + 1)); continue ;; esac
    if ! f=$(rg_input "$sha"); then echo "INPUT MISSING: $name ($sha, $bytes bytes)"; bad=$((bad + 1)); continue; fi
    if [ "$(stat -L -c %s "$f")" != "$bytes" ]; then echo "INPUT WRONG SIZE: $name ($f: $(stat -L -c %s "$f") bytes, $bytes expected)"; bad=$((bad + 1)); continue; fi
    if [ "$(sha256sum < "$f" | cut -c1-64)" != "$sha" ]; then echo "INPUT WRONG SHA256: $name ($f)"; bad=$((bad + 1)); continue; fi
    mkdir -p "$dest/$(dirname "$name")"
    ln -sfn "$f" "$dest/$name"
  done < "$list"
  echo "INPUTS $(basename "$list"): $((n - bad)) of $n found and verified"
  [ $bad = 0 ] && [ $n -gt 0 ]
}
# rg_source <package> <archive>: the archive in $REGRESS_SOURCES, or status 1
rg_source() {
  local d
  for d in ${REGRESS_SOURCES:-}; do
    [ -f "$d/$1/$2" ] && { echo "$d/$1/$2"; return 0; }
    [ -f "$d/$2" ] && { echo "$d/$2"; return 0; }
  done
  return 1
}
# rg_pkgvar <package dir> <VARIABLE>: a variable of a recipe at $REGRESS_LE_REF
rg_pkgvar() {
  git -C "$REGRESS_LE_GIT" show "$REGRESS_LE_REF:$1/package.mk" | sed -n "s/^$2=\"\(.*\)\"\$/\1/p" | head -n 1
}

# --------------------------------------------------------------------------------------------------- trees, headers
# rg_kodi: the Kodi tree of $REGRESS_KODI_REF (git archive, shared and read only by convention): echoes its folder
rg_kodi() {
  : "${REGRESS_KODI_GIT:?set REGRESS_KODI_GIT to the Kodi repository}"
  local c d
  c=$(git -C "$REGRESS_KODI_GIT" rev-parse --verify -q "$REGRESS_KODI_REF^{commit}") || { echo "no Kodi ref $REGRESS_KODI_REF" >&2; return 1; }
  d=$RG_SHARED/kodi-$c
  rg_locked kodi-$c bash -c '
    [ -f "$1/.regress-done" ] && exit 0
    rm -rf -- "$1.part" && mkdir -p "$1.part" &&
    git -C "$2" archive "$3" | tar -x -C "$1.part" && mv "$1.part" "$1" && touch "$1/.regress-done"' _ "$d" "$REGRESS_KODI_GIT" "$c" \
    || { echo "git archive of Kodi $c failed" >&2; return 1; }
  echo "$d"
}
# rg_kodi_commit: the commit under test
rg_kodi_commit() { git -C "$REGRESS_KODI_GIT" rev-parse --verify -q "$REGRESS_KODI_REF^{commit}"; }
# rg_le_tree <dest> <path>...: paths of the LibreELEC tree at $REGRESS_LE_REF (git archive)
rg_le_tree() {
  local dest=$1
  shift
  rg_fresh "$dest"
  git -C "$REGRESS_LE_GIT" archive "$REGRESS_LE_REF" "$@" | tar -x -C "$dest"
}
# rg_glinc: GLES2, GLES3, EGL and KHR headers of the build folder's sysroot (Khronos), linked into a folder of their
# own (never the sysroot's whole include folder): echoes it
rg_glinc() {
  : "${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder}"
  local s=$REGRESS_LE_BUILD/toolchain/x86_64-libreelec-linux-gnu/sysroot/usr/include d=$RG_SHARED/glinc x
  for x in GLES2 GLES3 EGL KHR; do [ -d "$s/$x" ] || { echo "no $s/$x" >&2; return 1; }; done
  mkdir -p "$d"
  for x in GLES2 GLES3 EGL KHR; do ln -sfn "$s/$x" "$d/$x"; done
  echo "$d"
}
# rg_vkinc: the Vulkan headers of the build folder
rg_vkinc() {
  : "${REGRESS_LE_BUILD:?set REGRESS_LE_BUILD to a LibreELEC build folder}"
  ls -d "$REGRESS_LE_BUILD"/build/vulkan-headers-*/include 2> /dev/null | sort | tail -n 1
}
# rg_vastub: the stub of va/va.h for the bridge on a host without libva
rg_vastub() { echo "$RG_LIB/stub"; }

# --------------------------------------------------------------------------------------------------- libplacebo
# rg_libplacebo <dest> [<python edit file for src/... or -> [<ERE of patch names to leave out>]: libplacebo of the
# recipe at $REGRESS_LE_REF (archive checked against PKG_SHA256, every patch of the recipe with -F0) built static
# for the host (OpenGL, dovi; no Vulkan, no lcms) into <dest>/prefix. Made again only when the archive, the patches,
# the edit or the options changed. The edit file defines FILE (path under the source) and PAIRS (see rg_edit).
rg_libplacebo() {
  local D=$1 edit=${2:--} skip=${3:-^$} ver sha tb stamp f p out n=0 inc vk glinc
  ver=$(rg_pkgvar $RG_LPDIR PKG_VERSION); sha=$(rg_pkgvar $RG_LPDIR PKG_SHA256)
  [ -n "$ver" ] && [ -n "$sha" ] || { echo "LP: no version or hash of libplacebo at $REGRESS_LE_REF"; return 1; }
  tb=$(rg_source libplacebo "libplacebo-$ver.tar.gz") || { echo "LP: no libplacebo-$ver.tar.gz in REGRESS_SOURCES"; return 1; }
  vk=$(rg_vkinc); glinc=$(rg_glinc) || return 1
  [ -n "$vk" ] || { echo "LP: no Vulkan headers in $REGRESS_LE_BUILD"; return 1; }
  # (the edit as it is applied: FILE and PAIRS evaluated, so that an edit file that takes its pairs from another file
  # is made again when that file changes)
  stamp=$( { echo "$ver $sha $skip v3"
             [ "$edit" = - ] || $RG_PY -c 'import runpy, sys; g = runpy.run_path(sys.argv[1]); print(repr((g["FILE"], g["PAIRS"])))' "$edit" || echo "edit unreadable"
             for f in $(git -C "$REGRESS_LE_GIT" ls-tree --name-only "$REGRESS_LE_REF" $RG_LPDIR/patches/); do
               basename "$f" | grep -E -q -- "$skip" && continue; git -C "$REGRESS_LE_GIT" show "$REGRESS_LE_REF:$f"; done; } | sha256sum | cut -c1-64)
  if [ -f "$D/prefix/lib/libplacebo.a" ] && [ "$(cat "$D/stamp" 2> /dev/null)" = "$stamp" ]; then
    echo "LP: $D already built from the same archive, patches and edit"; return 0
  fi
  [ "$(sha256sum < "$tb" | cut -c1-64)" = "$sha" ] || { echo "LP: sha256 of $tb is not PKG_SHA256 $sha"; return 1; }
  rg_fresh "$D/src" "$D/patches" "$D/py"; rg_rm "$D/build" "$D/prefix" "$D/stamp"
  local sp; sp=$(ls -d "$REGRESS_LE_BUILD"/toolchain/lib/python3*/site-packages 2> /dev/null | head -n 1)
  [ -n "$sp" ] && [ -d "$sp/glad" ] || { echo "LP: no glad in the build folder's toolchain"; return 1; }
  cp -a "$sp"/glad "$sp"/glad2-*.dist-info "$D/py/" || return 1
  for f in $(git -C "$REGRESS_LE_GIT" ls-tree --name-only "$REGRESS_LE_REF" $RG_LPDIR/patches/); do
    basename "$f" | grep -E -q -- "$skip" && { echo "LP: left out $(basename "$f")"; continue; }
    git -C "$REGRESS_LE_GIT" show "$REGRESS_LE_REF:$f" > "$D/patches/$(basename "$f")"
  done
  tar -xzf "$tb" --strip-components=1 -C "$D/src" || { echo "LP: unpack $tb"; return 1; }
  for p in $(ls "$D"/patches/*.patch 2> /dev/null | sort); do
    out=$(patch -d "$D/src" -p1 -F0 --no-backup-if-mismatch < "$p" 2>&1) || { echo "$out" | tail -n 5; echo "LP: PATCH FAILED $(basename "$p")"; return 1; }
    n=$((n + 1))
  done
  if [ "$edit" != - ]; then
    f=$($RG_PY -c 'import runpy, sys; print(runpy.run_path(sys.argv[1])["FILE"])' "$edit") || return 1
    rg_edit "$D/src/$f" "$edit" || { echo "LP: EDIT NOT APPLIED $edit"; return 3; }
  fi
  inc="-O2 -g0 -I$glinc -I$vk"
  ( export PYTHONPATH=$D/py CC=gcc CXX=g++ CFLAGS="$inc" CXXFLAGS="$inc"
    meson setup "$D/build" "$D/src" --prefix="$D/prefix" --libdir=lib --buildtype=plain --auto-features=disabled \
      -Ddefault_library=static -Dvulkan=disabled -Dvk-proc-addr=disabled -Dd3d11=disabled -Dglslang=disabled \
      -Dshaderc=disabled -Dlcms=disabled -Ddovi=enabled -Dlibdovi=disabled -Ddemos=false -Dtests=false \
      -Dbench=false -Dopengl=enabled -Dgl-proc-addr=enabled > "$D/setup.log" 2>&1 < /dev/null &&
    nice -n 19 ninja -j"$RG_JOBS" -C "$D/build" install > "$D/build.log" 2>&1 < /dev/null ) \
    || { tail -n 20 "$D/setup.log" "$D/build.log"; echo "LP: BUILD FAILED $D"; return 1; }
  [ -f "$D/prefix/lib/libplacebo.a" ] || { echo "LP: no libplacebo.a"; return 1; }
  rg_rm "$D/build"
  echo "$stamp" > "$D/stamp"
  echo "LP: libplacebo $ver built in $D ($n patches of $REGRESS_LE_REF with -F0$([ "$edit" = - ] || echo ", edit $(basename "$edit")"))"
}
# rg_lp_shared: the unchanged libplacebo, shared by the lanes: echoes its prefix
rg_lp_shared() {
  rg_locked lp rg_libplacebo "$RG_SHARED/lp" > "$RG_SHARED/lp.log" 2>&1 || { cat "$RG_SHARED/lp.log" >&2; return 1; }
  echo "$RG_SHARED/lp/prefix"
}

# --------------------------------------------------------------------------------------------------- FFmpeg
# rg_ff_patches <tree>: the patch files of FFmpeg's recipe in a tree made by rg_le_tree, in the order scripts/unpack
# applies them for PROJECT=Generic (postproc, libreelec, projects/Generic/patches/ffmpeg), without ffmpeg-9913 (it
# only builds the h264_mvc twin: tools/regress/mvc)
rg_ff_patches() {
  local d p
  for d in packages/multimedia/ffmpeg/patches/postproc packages/multimedia/ffmpeg/patches/libreelec projects/Generic/patches/ffmpeg; do
    for p in $(ls "$1/$d"/*.patch 2> /dev/null | sort); do
      case $p in *ffmpeg-9913-*) continue ;; esac
      echo "$p"
    done
  done
}
# rg_ff_source <dest> <ERE of patch names left out, or -> <what the build adds to the stamp>: <dest>/src, FFmpeg of the
# recipe at $REGRESS_LE_REF (archive checked against PKG_SHA256) with its patches (rg_ff_patches) applied with -F0,
# but those whose names match the ERE; an "FFSRC" line says what was applied and left out. Status 2 when <dest>/built
# is there from the same archive, patches and stamp (nothing to do); else <dest> is made again from nothing, and the
# caller makes <dest>/built when its build is done.
rg_ff_source() {
  local D=$1 skip=$2 extra=$3 ver sha tb T stamp p n=0 out=""
  ver=$(rg_pkgvar packages/multimedia/ffmpeg PKG_VERSION); sha=$(rg_pkgvar packages/multimedia/ffmpeg PKG_SHA256)
  [ -n "$ver" ] && [ -n "$sha" ] || { echo "FF: no version or hash of ffmpeg at $REGRESS_LE_REF"; return 1; }
  tb=$(rg_source ffmpeg "ffmpeg-$ver.tar.xz") || { echo "FF: no ffmpeg-$ver.tar.xz in REGRESS_SOURCES"; return 1; }
  T=$D.tree
  rg_le_tree "$T" packages/multimedia/ffmpeg/patches projects/Generic/patches/ffmpeg || { echo "FF: the patches of $REGRESS_LE_REF"; return 1; }
  [ "$skip" = - ] && skip='^$'
  stamp=$( { echo "$ver $sha $skip $extra v2"; for p in $(rg_ff_patches "$T"); do basename "$p" | grep -E -q -- "$skip" && continue; cat "$p"; done; } |
           sha256sum | cut -c1-64)
  if [ -f "$D/built" ] && [ "$(cat "$D/stamp" 2> /dev/null)" = "$stamp" ]; then rg_rm "$T"; return 2; fi
  [ "$(sha256sum < "$tb" | cut -c1-64)" = "$sha" ] || { echo "FF: sha256 of $tb is not PKG_SHA256 $sha"; return 1; }
  rg_fresh "$D/src"
  rg_rm "$D/prefix" "$D/build" "$D/built" "$D/stamp"
  tar --no-same-owner -xf "$tb" --strip-components=1 -C "$D/src" || { echo "FF: unpack $tb"; return 1; }
  for p in $(rg_ff_patches "$T"); do
    if basename "$p" | grep -E -q -- "$skip"; then out="$out $(basename "$p")"; continue; fi
    patch -d "$D/src" -p1 -F0 -s --no-backup-if-mismatch < "$p" || { echo "FF: PATCH FAILED $(basename "$p")"; return 1; }
    n=$((n + 1))
  done
  rg_rm "$T"
  [ -f "$D/src/configure" ] || { echo "FF: no configure in $D/src"; return 1; }
  echo "$stamp" > "$D/stamp"
  echo "FFSRC FFmpeg $ver, $n patches of $REGRESS_LE_REF applied with -F0; left out:${out:- none}"
}
# rg_ffmpeg <dest> <configure options...>: that FFmpeg built static for the host into <dest>/prefix (the source is
# not kept). RG_FF_LEAVE_OUT: an ERE of patch names left out (the code before those patches). Made again only when the
# archive, the patches applied or the options changed.
rg_ffmpeg() {
  local D=$1 rc
  shift
  rg_ff_source "$D" "${RG_FF_LEAVE_OUT:--}" "$*"
  rc=$?
  [ $rc = 2 ] && { echo "FF: $D already built from the same archive, patches and options"; return 0; }
  [ $rc = 0 ] || return 1
  ( cd "$D/src" && nice -n 19 ./configure --prefix="$D/prefix" --disable-doc --disable-network --disable-x86asm \
      --enable-static --disable-shared --disable-autodetect "$@" > "$D/configure.log" 2>&1 < /dev/null &&
    nice -n 19 make -j"$RG_JOBS" > "$D/make.log" 2>&1 < /dev/null && make install > "$D/install.log" 2>&1 < /dev/null ) \
    || { tail -n 30 "$D/configure.log" "$D/make.log"; echo "FF: BUILD FAILED $D"; return 1; }
  rg_rm "$D/src"
  : > "$D/built"
  echo "FF: built in $D ($*)"
}
# the FFmpeg the host builds of the bridge link with: HEVC/H.264 decoding, Matroska/MP4/MPEG-TS, dovi_split
RG_FF_BRIDGE="--disable-everything --enable-decoder=h264,hevc --enable-parser=h264,hevc
  --enable-demuxer=h264,hevc,matroska,mov,mpegts --enable-muxer=matroska,mp4,null,h264,hevc,mpegts
  --enable-bsf=trace_headers,hevc_metadata,hevc_mp4toannexb,extract_extradata,dovi_split --enable-protocol=file,pipe
  --enable-ffmpeg --enable-ffprobe"
# rg_ff_bridge: that FFmpeg, shared by the lanes: echoes its prefix
rg_ff_bridge() {
  # shellcheck disable=SC2086
  rg_locked ff-bridge rg_ffmpeg "$RG_SHARED/ff-bridge" $RG_FF_BRIDGE > "$RG_SHARED/ff-bridge.log" 2>&1 \
    || { cat "$RG_SHARED/ff-bridge.log" >&2; return 1; }
  echo "$RG_SHARED/ff-bridge/prefix"
}

# --------------------------------------------------------------------------------------------------- the bridge
# rg_shim <object folder>: gles_shim.o for the gl* symbols the objects of the folder call (the host has Mesa's EGL and
# no libGLESv2 to link with: each entry point through eglGetProcAddress())
rg_shim() {
  local O=$1 glinc
  glinc=$(rg_glinc) || return 1
  nm -u "$O"/*.o | awk '$1=="U" && $2 ~ /^(__real_)?gl[A-Z]/ {sub(/^__real_/, "", $2); print $2}' | sort -u > "$O/gl-symbols.txt"
  $RG_PY "$RG_LIB/gles_shim.py" "$glinc/GLES3/gl32.h" "$glinc/GLES2/gl2ext.h" < "$O/gl-symbols.txt" > "$O/gles_shim.c" || return 1
  gcc -O1 -I"$glinc" -c "$O/gles_shim.c" -o "$O/gles_shim.o"
}
# rg_bridge <tree that has tools/dvbridge> <libplacebo prefix> <FFmpeg prefix> <test .c> <output binary> [<CPATH>]:
# a test of the bridge built for the host, every module of tools/dvbridge but those of RG_BRIDGE_SKIP (default
# dvbridge_fel dvbridge_dmabuf), with pack_sources.h as Kodi's build makes it. EXTRA_CF: more compile flags;
# LDEXTRA: more link flags. The output: <output binary> and <output binary>.build.log (status 1 when it fails).
rg_bridge() {
  local TREE=$1 LP=$2 FF=$3 TEST=$4 OUT=$5 CP=${6:-} O glinc vk b f CF
  O=$OUT.obj
  rg_fresh "$O"
  (
    glinc=$(rg_glinc) && vk=$(rg_vkinc) && [ -n "$vk" ] || { echo "no GL or Vulkan headers"; exit 1; }
    $RG_PY "$RG_REGRESS/oracle/pack_sources.py" "$TREE/tools/dvbridge" "$O" || { echo "FAIL: pack_sources.h"; exit 1; }
    CF="-std=gnu11 -O1 -g -Wall -Wno-unused-function ${EXTRA_CF:-} -I$LP/include -I$FF/include -I$glinc -I$vk -I$TREE/tools/dvbridge -I$O"
    for f in "$TREE"/tools/dvbridge/*.c; do
      b=$(basename "$f" .c)
      case " ${RG_BRIDGE_SKIP:-dvbridge_fel dvbridge_dmabuf} " in *" $b "*) continue ;; esac
      # shellcheck disable=SC2086
      CPATH=$(rg_vastub)${CP:+:$CP} gcc $CF -c "$f" -o "$O/$b.o" 2> "$O/$b.warn" || { cat "$O/$b.warn"; echo "FAIL: $b.c"; exit 1; }
      [ -s "$O/$b.warn" ] && { echo "warnings in $b.c:"; cat "$O/$b.warn"; }
    done
    cp "$TEST" "$O/test.c"
    # shellcheck disable=SC2086
    CPATH=$(rg_vastub)${CP:+:$CP} gcc $CF -c "$O/test.c" -o "$O/test.o" 2> "$O/t.warn" || { cat "$O/t.warn"; echo "FAIL: $(basename "$TEST")"; exit 1; }
    [ -s "$O/t.warn" ] && { echo "warnings in the test:"; head -n 20 "$O/t.warn"; }
    rg_shim "$O" || { echo "FAIL: GL shim"; exit 1; }
    # shellcheck disable=SC2086
    g++ -o "$OUT" "$O"/*.o "$LP/lib/libplacebo.a" -L"$FF/lib" -lavformat -lavcodec -lavutil -lm -latomic -pthread -ldl ${LDEXTRA:-} \
      /usr/lib/x86_64-linux-gnu/libEGL.so.1 || { echo "FAIL: link"; exit 1; }
    echo "BRIDGE-BUILT $OUT"
  ) > "$OUT.build.log" 2>&1 < /dev/null
  local rc=$?
  rg_rm "$O"
  return $rc
}
