#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# FFmpeg: a separate decoder for H.264 MVC (stereoscopic 3D, as on Blu-ray 3D), named h264_mvc.
#
# Dom Cobley's series "H.264 MVC decoding" (FFmpeg pull request 24303, 18 commits) adds multiview
# decoding to FFmpeg's H.264 decoder itself: it changes how every H.264 stream goes in and comes out of
# the decoder (the entry point, with hardware acceleration too) and how damage is concealed. FFmpeg has
# not taken it yet. Here the series is applied to a private COPY of the decoder's sources, which is
# built as a second decoder under another name. The stock decoder is not touched: its sources are the
# same bytes, and so are its objects. A player asks for h264_mvc by name, for a stream it knows to be
# MVC, and for nothing else.
#
# What this script does, in the FFmpeg source tree it is given, after all patches have been applied:
#   1. copies the sources of the decoder that are compiled against the headers the series changes
#      into libavcodec/h264mvc/;
#   2. applies the series to those copies, each commit as its author wrote it, without fuzz. Parts
#      of the series for files that are not copied (the parser, the mov demuxer, a bitstream filter,
#      documentation and tests) are left out: they are not needed to decode, and they would change
#      stock code. What that costs: the parameter sets of the second view (the subset SPS, NAL unit
#      type 15) reach the copy where they are among the NAL units of the stream or in the 'avcC'
#      record of its extradata, which the copy's own parser reads. Where a file has them elsewhere
#      the player has to put them there (Kodi does, for the 'mvcC' box that a Matroska file made
#      of a 3D Blu-ray has behind that record). An MP4 file, whose 'mvcC' box the series teaches
#      the mov demuxer to read, plays its base view;
#   3. applies the project's own fixes to the copies (fixes/*.patch), without fuzz;
#   4. gives the copy its own name (h264_mvc) and makes it a software decoder (no hardware
#      acceleration is ever offered by it);
#   5. writes a header that renames every global symbol the copies define (h264mvc.syms), so that
#      the two decoders can live in one library, and a Makefile for the new folder. A global symbol
#      of the copies that the list does not have keeps its name, and the library then does not link
#      ("multiple definition"): nothing is built with a symbol that the two decoders share unseen.
# The stock build files learn about the new decoder from a patch of their own (three added lines).
#
# Usage: twin.sh <ffmpeg source directory> <directory of this script's package files>
set -eu

SRC=$1
PKG=$2
A=libavcodec
T=$SRC/$A/h264mvc

# the sources of the decoder: every file of libavcodec that includes h264dec.h, h264_ps.h or
# h2645_parse.h and belongs to the software decoder (the hardware decoders and the parser stay stock)
TWIN_C="h264dec.c h264_slice.c h264_refs.c h264_ps.c h264_picture.c h264_direct.c h264_parse.c
        h2645_parse.c h264_cabac.c h264_cavlc.c h264_loopfilter.c h264_mb.c h264_sei.c"
# headers and templates that those include by name and that must be the copies
TWIN_H="h264dec.h h264_ps.h h264_parse.h h264_sei.h h2645_parse.h h264_mvpred.h
        h264_mb_template.c h264_mc_template.c"

die() { echo "h264-mvc: $*" >&2; exit 1; }

[ -f "$SRC/$A/h264dec.c" ] || die "no FFmpeg source in $SRC"
[ ! -e "$T" ] || die "$T exists already"
[ -s "$PKG/h264mvc.syms" ] || die "no list of symbols"
ls "$PKG"/series/*.patch > /dev/null 2>&1 || die "no series"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/src/$A" "$STAGE/part"
for f in $TWIN_C $TWIN_H; do
  [ -f "$SRC/$A/$f" ] || die "missing in the source: $A/$f"
  cp "$SRC/$A/$f" "$STAGE/src/$A/$f"
done

# The part of a patch that is for the copied files: its mail header, and the "diff --git" sections
# of those files. Prints nothing if no section is for them.
part_of() {
  awk -v list="$(echo $TWIN_C $TWIN_H)" '
    BEGIN { n = split(list, a, " "); for (i = 1; i <= n; i++) want["libavcodec/" a[i]] = 1; head = 1; keep = 0; any = 0 }
    /^diff --git a\// {
      head = 0
      path = $3; sub(/^a\//, "", path)
      keep = (path in want)
      if (keep) any = 1
    }
    head { hdr = hdr $0 "\n"; next }
    keep { body = body $0 "\n" }
    END { if (any) printf "%s%s", hdr, body }
  ' "$1"
}

apply_all() { # <directory with patches> <what they are>
  for p in $(ls "$1"/*.patch 2> /dev/null | sort); do
    part_of "$p" > "$STAGE/part/p"
    if [ ! -s "$STAGE/part/p" ]; then
      echo "h264-mvc: $2 $(basename "$p"): nothing for the decoder's own files, left out"
      continue
    fi
    ( cd "$STAGE/src" && patch -p1 --fuzz=0 --no-backup-if-mismatch -s < "$STAGE/part/p" ) ||
      die "$2 $(basename "$p") does not apply without fuzz"
    echo "h264-mvc: $2 $(basename "$p"): applied"
  done
}
apply_all "$PKG/series" "series"
if ls "$PKG"/fixes/*.patch > /dev/null 2>&1; then apply_all "$PKG/fixes" "fix"; fi
[ -z "$(find "$STAGE/src" -name '*.rej' -o -name '*.orig')" ] || die "rejects were left"

mkdir "$T"
for f in $TWIN_C $TWIN_H; do cp "$STAGE/src/$A/$f" "$T/$f"; done

# its own name, and no hardware acceleration
once() { # <file> <fixed string that must be there exactly once>
  [ "$(grep -c -F -- "$2" "$1")" = 1 ] || die "expected exactly once in $(basename "$1"): $2"
}
once "$T/h264dec.c" '.p.name                = "h264",'
once "$T/h264dec.c" 'CODEC_LONG_NAME("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),'
sed -i \
  -e 's|\.p\.name                = "h264",|.p.name                = "h264_mvc",|' \
  -e 's|CODEC_LONG_NAME("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),|CODEC_LONG_NAME("H.264 MVC / stereoscopic 3D (separate software decoder)"),|' \
  "$T/h264dec.c"
once "$T/h264dec.c" '.p.name                = "h264_mvc",'

cat > "$T/h264mvc_nohw.h" << 'EOF'
/*
 * Generated by h264-mvc/twin.sh. This copy of the H.264 decoder is a software decoder: every hardware
 * acceleration that the stock decoder is configured with is off here, whatever config_components.h says.
 */
#ifndef AVCODEC_H264MVC_NOHW_H
#define AVCODEC_H264MVC_NOHW_H

#undef  CONFIG_H264_DXVA2_HWACCEL
#define CONFIG_H264_DXVA2_HWACCEL 0
#undef  CONFIG_H264_D3D11VA_HWACCEL
#define CONFIG_H264_D3D11VA_HWACCEL 0
#undef  CONFIG_H264_D3D11VA2_HWACCEL
#define CONFIG_H264_D3D11VA2_HWACCEL 0
#undef  CONFIG_H264_D3D12VA_HWACCEL
#define CONFIG_H264_D3D12VA_HWACCEL 0
#undef  CONFIG_H264_NVDEC_HWACCEL
#define CONFIG_H264_NVDEC_HWACCEL 0
#undef  CONFIG_H264_VAAPI_HWACCEL
#define CONFIG_H264_VAAPI_HWACCEL 0
#undef  CONFIG_H264_VDPAU_HWACCEL
#define CONFIG_H264_VDPAU_HWACCEL 0
#undef  CONFIG_H264_VIDEOTOOLBOX_HWACCEL
#define CONFIG_H264_VIDEOTOOLBOX_HWACCEL 0
#undef  CONFIG_H264_VULKAN_HWACCEL
#define CONFIG_H264_VULKAN_HWACCEL 0

#endif /* AVCODEC_H264MVC_NOHW_H */
EOF
nohw=0
for f in $TWIN_C $TWIN_H; do
  if grep -q '^#include "config_components.h"$' "$T/$f"; then
    sed -i 's|^#include "config_components.h"$|#include "config_components.h"\n#include "h264mvc_nohw.h"|' "$T/$f"
    nohw=$((nohw + 1))
  fi
  # a file that tests one of those switches without that include would keep the stock value
  if grep -q 'CONFIG_H264_[A-Z0-9]*_HWACCEL' "$T/$f" && ! grep -q '^#include "h264mvc_nohw.h"$' "$T/$f"; then
    die "$f tests a hardware acceleration switch and does not include config_components.h"
  fi
done
grep -q '^#include "h264mvc_nohw.h"$' "$T/h264dec.c" || die "h264dec.c did not get the header that turns hardware acceleration off"
grep -q '^#include "h264mvc_nohw.h"$' "$T/h264_slice.c" || die "h264_slice.c did not get the header that turns hardware acceleration off"

# every global symbol of the copies gets a name of its own
{
  echo "/*"
  echo " * Generated by h264-mvc/twin.sh from h264mvc.syms. The copies of the H.264 decoder in this folder define"
  echo " * the same global symbols as the stock decoder: each gets a name of its own here, so that both decoders"
  echo " * can be linked into one library. This header is given to the compiler before each file of this folder."
  echo " */"
  echo "#ifndef AVCODEC_H264MVC_RENAME_H"
  echo "#define AVCODEC_H264MVC_RENAME_H"
  echo
  grep -v '^#' "$PKG/h264mvc.syms" | grep -v '^[[:space:]]*$' | sort -u | while read -r s; do
    case "$s" in
      ff_h264_decoder) n=ff_h264_mvc_decoder ;;
      ff_h264_*)       n=ff_h264mvc_${s#ff_h264_} ;;
      ff_h2645_*)      n=ff_h2645mvc_${s#ff_h2645_} ;;
      avpriv_h264_*)   n=ff_h264mvc_avpriv_${s#avpriv_h264_} ;;
      *)               echo "h264-mvc: a symbol with a name that is not expected: $s" >&2; exit 1 ;;
    esac
    printf '#define %-44s %s\n' "$s" "$n"
  done
  echo
  echo "#endif /* AVCODEC_H264MVC_RENAME_H */"
} > "$T/h264mvc_rename.h"
grep -q '^#define ff_h264_decoder  *ff_h264_mvc_decoder$' "$T/h264mvc_rename.h" || die "the decoder itself is not in the list of symbols"

{
  printf 'clean::\n\t$(RM) $(CLEANSUFFIXES:%%=libavcodec/h264mvc/%%)\n\n'
  printf '# A second H.264 decoder, for MVC: copies of the stock sources with the multiview series applied\n'
  printf '# (h264-mvc/twin.sh). The objects that the stock decoder does not compile against the changed headers\n'
  printf '# are shared with it.\n'
  printf 'OBJS-$(CONFIG_H264_MVC_DECODER) +='
  for f in $TWIN_C; do printf ' \\\n    h264mvc/%s' "${f%.c}.o"; done
  printf ' \\\n    h264data.o h274.o h2645data.o h2645_vui.o h2645_sei.o\n\n'
  printf '# these files include the headers of libavcodec by their bare names, as the stock ones do from there,\n'
  printf '# and see the new names of their own symbols\n'
  printf 'libavcodec/h264mvc/%%.o: CPPFLAGS += -iquote $(SRC_PATH)/libavcodec -include $(SRC_PATH)/libavcodec/h264mvc/h264mvc_rename.h\n'
} > "$T/Makefile"

echo "h264-mvc: $(echo $TWIN_C | wc -w) sources and $(echo $TWIN_H | wc -w) headers in $A/h264mvc, hardware acceleration off in $nohw of them, $(grep -c '^#define [a-z]' "$T/h264mvc_rename.h") symbols renamed"
