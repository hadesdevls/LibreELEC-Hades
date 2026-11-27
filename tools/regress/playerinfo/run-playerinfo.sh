#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# run-playerinfo.sh: the player information of Hades in the Kodi tree of REGRESS_KODI_REF (Estuary's panel, the
# generated pages for other skins in system/hades) and the language files, without Kodi, each check with its other
# direction (a broken copy of the files must fail it):
#   pocheck     settings.xml parses; each strings.po is well formed, without a duplicate id; the ids of Hades in
#               Estuary's files in order (po_check.py)
#   picheck     make-playerinfo.py --check (system/hades is what it makes of Estuary's files); validate_skin.py (the
#               strings in four languages, $VAR/$MAP/includes, the properties read and published, tabs and pages, and
#               no text of the pages that says TV-led in native Dolby Vision Low-Latency) with
#               its broken copies (validate_skin_mutants.py); validate_hades_dialog.py (the generated pages)
#   skinpasses  the row of the GPU time of the passes (hades.dv.passes) in the generated dialog and in Estuary's file,
#               and every text of the panel fits its label as Kodi measures it, in en_gb, es_es, es_ar and es_mx
#               (skin_check.py: FreeType through ctypes, the fonts of the tree)
#   settings    the layout of the settings (settings_layout_check.py): every setting id of 1.1 still there (a value set
#               in 1.1 is kept, and 1.1 reads it back), the groups and their order, levels, parents, the override files
#               of the build, the strings of the layout in four languages; with its broken copies
# usage: run-playerinfo.sh all | verdict [<log>] | <stage>...      (settings: lib/regress.sh)
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=../lib/regress.sh
. "$HERE/../lib/regress.sh"
rg_init playerinfo

setup() { K=$(rg_kodi) || rg_fail "no Kodi tree of $REGRESS_KODI_REF"; }
# copy <dest> <path>...: paths of the Kodi tree copied (the copies are changed, never the tree)
copy() {
  local d=$1 p
  shift
  rg_fresh "$d"
  for p in "$@"; do mkdir -p "$d/$(dirname "$p")" && cp -a "$K/$p" "$d/$p" || return 1; done
}
# edit <file> <old> <new>: an exact edit of a copy (the text there exactly once)
edit() {
  $RG_PY - "$1" "$2" "$3" <<'PY'
import sys
p, old, new = sys.argv[1:4]
t = open(p, encoding='utf-8', newline='').read()
if t.count(old) != 1:
    print('EDIT NOT APPLIED to %s: the text is there %d times: %r' % (p, t.count(old), old[:80]))
    sys.exit(3)
open(p, 'w', encoding='utf-8', newline='').write(t.replace(old, new))
PY
}

# --------------------------------------------------------------------------------------------------- pocheck
PO_FILES="system/settings/settings.xml addons/resource.language.en_gb/resources/strings.po
  addons/skin.estuary/language/resource.language.en_gb/strings.po addons/skin.estuary/language/resource.language.es_es/strings.po
  addons/skin.estuary/language/resource.language.es_ar/strings.po addons/skin.estuary/language/resource.language.es_mx/strings.po"
# po_mutant <name> <file> <python: the text t of the file changed>: a copy with one fault; po_check.py must fail it
po_mutant() {
  local d=$RG_W/po-$1 f=$2 r
  # shellcheck disable=SC2086
  copy "$d" $PO_FILES || { rg_item "pocheck-mutant-$1" FAILED "the copy could not be made"; return; }
  $RG_PY -c "import sys; p = sys.argv[1]; t = open(p, encoding='utf-8').read(); $3; open(p, 'w', encoding='utf-8').write(t)" "$d/$f"
  r=$($RG_PY "$HERE/po_check.py" "$d" 2>&1 | tail -n 1)
  rg_item "pocheck-mutant-$1" "$(echo "$r" | grep -q '^PO-CHECK FAILED' && echo PASSED || echo FAILED)" "a copy with $4: $r"
  rg_rm "$d"
}
stage_pocheck() {
  setup
  local L=$RG_W/logs/po-check.log v
  rg_log "$L" $RG_PY "$HERE/po_check.py" "$K"
  v=$(rg_pass "$L" '^PO-CHECK ALL PASSED')
  rg_item pocheck "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -c '^ok' "$L") files ok"
  po_mutant dup addons/skin.estuary/language/resource.language.en_gb/strings.po \
    "i = t.index('msgctxt \"#'); j = t.index('msgctxt \"#', i + 1); t = t + '\n' + t[i:j]" "an entry of Estuary's en_gb given twice"
  po_mutant order addons/skin.estuary/language/resource.language.es_es/strings.po \
    "import re; ids = [m for m in re.finditer(r'^msgctxt \"#(5[12][0-9]{3})\"\$', t, re.M) if 51740 <= int(m.group(1)) < 52000]; a, b = ids[0], ids[1]; t = t[:a.start()] + b.group(0) + t[a.end():b.start()] + a.group(0) + t[b.end():]" \
    "two ids of Hades in es_es out of order"
  po_mutant bare addons/skin.estuary/language/resource.language.es_es/strings.po \
    "t = t + '\\nmsgid \"An entry without msgctxt\"\\nmsgstr \"Una entrada sin msgctxt\"\\n'" \
    "an entry of es_es without msgctxt"
  po_mutant msgid addons/skin.estuary/language/resource.language.es_ar/strings.po \
    "i = t.index('\\nmsgid \"', t.index('msgctxt \"#3')); t = t[:i] + '\\nmsgid \"X' + t[i + 9:]" \
    "an Estuary string of es_ar with another msgid than en_gb"
  po_mutant form addons/resource.language.en_gb/resources/strings.po \
    "i = t.index('\nmsgstr \"', t.index('msgctxt \"#')); t = t[:i] + '\nmsgstr_x \"' + t[i + 9:]" "an entry of the core strings that is not msgctxt/msgid/msgstr"
}

# --------------------------------------------------------------------------------------------------- picheck
stage_picheck() {
  setup
  local L=$RG_W/logs/pi-check.log v M n
  # make-playerinfo.py --check: system/hades is what it makes from Estuary's files (it reads the tree, writes nothing)
  L=$RG_W/logs/make-playerinfo.log
  rg_log "$L" bash -c 'cd "$1" && exec '"$RG_PY"' tools/hades-playerinfo/make-playerinfo.py --check' _ "$K"
  v=$(rg_pass "$L" '^make-playerinfo: .*up to date$')
  rg_item make-playerinfo "$(rg_say "$v")" "$(rg_rest "$v")"
  # its other direction: a copy whose include of Estuary has another width is not up to date
  M=$RG_W/mkpi
  copy "$M" tools/hades-playerinfo system/hades addons/skin.estuary || rg_fail "copy"
  $RG_PY - "$M/addons/skin.estuary/xml/Includes_HadesPlayerInfo.xml" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
s2 = re.sub(r"<width>(\d+)</width>", lambda m: "<width>%d</width>" % (int(m.group(1)) + 7), s, count=1)
assert s2 != s
open(p, "w", encoding="utf-8").write(s2)
PY
  L=$RG_W/logs/make-playerinfo-changed.log
  rg_log "$L" bash -c 'cd "$1" && exec '"$RG_PY"' tools/hades-playerinfo/make-playerinfo.py --check' _ "$M"
  v=$(rg_exit "$L")
  rg_item make-playerinfo-changed "$([ "$v" = 1 ] && echo PASSED || echo FAILED)" "a copy with a width of an include changed: --check exit $v (1: not up to date); $(rg_last "$L" | cut -c1-120)"
  rg_rm "$M"
  # validate_skin.py (with the labels that the panel shows, ref/labels.txt), and every broken copy of it must fail
  L=$RG_W/logs/validate-skin.log
  rg_log "$L" $RG_PY "$HERE/validate_skin.py" "$K" "$HERE/ref/labels.txt"
  v=$(rg_pass "$L" '^skin checks: all passed')
  rg_item validate-skin "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -m1 '^note: labels of the reference' "$L" | cut -c7-)"
  # the labels' other direction: a reference with a label that the panel does not show
  cat "$HERE/ref/labels.txt" > "$RG_W/labels-more.txt" && echo 'hades.no.such.label' >> "$RG_W/labels-more.txt"
  L=$RG_W/logs/validate-skin-labels.log
  rg_log "$L" $RG_PY "$HERE/validate_skin.py" "$K" "$RG_W/labels-more.txt"
  rg_item validate-skin-labels "$([ "$(rg_exit "$L")" = 1 ] && grep -a -q 'showed hades.no.such.label' "$L" && echo PASSED || echo FAILED)" \
    "a reference with a label the panel does not show: exit $(rg_exit "$L"); $(grep -a -m1 'hades.no.such.label' "$L" | cut -c1-110)"
  L=$RG_W/logs/validate-skin-mutants.log
  rg_log "$L" $RG_PY "$HERE/validate_skin_mutants.py" "$K" "$RG_W/mut-skin" "$HERE/validate_skin.py"
  n=$(grep -a -c '^mutant ' "$L")
  v=$(grep -a -m1 '^mutants that survived or an unbroken copy that failed: ' "$L")
  if [ "$v" = "mutants that survived or an unbroken copy that failed: 0" ] && [ "$n" -ge 1 ] && [ "$(rg_exit "$L")" = 0 ]; then
    rg_item validate-skin-mutants PASSED "$n broken copies, every one fails validate_skin.py; the unbroken one passes"
  else rg_item validate-skin-mutants FAILED "${v:-no result line} ($n mutants); $(grep -a -E 'SURVIVED|not in|Error' "$L" | head -n 2 | cut -c1-120 | tr '\n' ';')"; fi
  # validate_hades_dialog.py, and a copy of system/hades with a variable that HadesIncludes.xml does not define
  L=$RG_W/logs/validate-hades-dialog.log
  rg_log "$L" $RG_PY "$HERE/validate_hades_dialog.py" "$K"
  v=$(rg_pass "$L" '^generated pages: all checks passed')
  rg_item validate-hades-dialog "$(rg_say "$v")" "$(rg_rest "$v")"
  M=$RG_W/hd-mut
  copy "$M" system/hades addons/skin.estuary/language/resource.language.en_gb/strings.po addons/resource.language.en_gb/resources/strings.po || rg_fail "copy"
  n=$(grep -o -m1 '\$VAR\[[A-Za-z0-9_]*\]' "$M/system/hades/DialogHadesPlayerInfo.xml" | head -n 1)
  L=$RG_W/logs/validate-hades-dialog-mutant.log
  if [ -n "$n" ] && $RG_PY -c "import sys; p, o = sys.argv[1:3]; t = open(p, encoding='utf-8').read(); open(p, 'w', encoding='utf-8').write(t.replace(o, '\$VAR[HadesNoSuchVariable]', 1))" "$M/system/hades/DialogHadesPlayerInfo.xml" "$n"; then
    rg_log "$L" $RG_PY "$HERE/validate_hades_dialog.py" "$M"
    v=$(rg_exit "$L")
    rg_item validate-hades-dialog-mutant "$([ "$v" = 1 ] && grep -a -q 'HadesNoSuchVariable' "$L" && echo PASSED || echo FAILED)" \
      "a copy of the generated dialog with $n made \$VAR[HadesNoSuchVariable]: exit $v; $(grep -a -m1 'HadesNoSuchVariable' "$L" | cut -c1-100)"
  else rg_item validate-hades-dialog-mutant FAILED "no \$VAR in the generated dialog to change"; fi
  rg_rm "$M"
  # and a copy of system/hades where the first control that runs PlayerControl(HadesDVOutput) closes the dialog instead
  M=$RG_W/hd-out
  copy "$M" system/hades addons/skin.estuary/language/resource.language.en_gb/strings.po addons/resource.language.en_gb/resources/strings.po || rg_fail "copy"
  L=$RG_W/logs/validate-hades-dialog-output.log
  if $RG_PY -c "import sys; p = sys.argv[1]; t = open(p, encoding='utf-8').read(); assert 'PlayerControl(HadesDVOutput)' in t; open(p, 'w', encoding='utf-8').write(t.replace('PlayerControl(HadesDVOutput)', 'Close', 1))" "$M/system/hades/DialogHadesPlayerInfo.xml"; then
    rg_log "$L" $RG_PY "$HERE/validate_hades_dialog.py" "$M"
    v=$(rg_exit "$L")
    rg_item validate-hades-dialog-output "$([ "$v" = 1 ] && grep -a -q 'the output of this video is not reachable' "$L" && echo PASSED || echo FAILED)" \
      "a copy of the generated dialog with the first PlayerControl(HadesDVOutput) made Close: exit $v; $(grep -a -m1 'the output of this video' "$L" | cut -c1-110)"
  else rg_item validate-hades-dialog-output FAILED "no PlayerControl(HadesDVOutput) in the generated dialog to change"; fi
  rg_rm "$M"
  # and one where the first control that runs PlayerControl(HadesUpscaler) closes the dialog instead
  M=$RG_W/hd-ups
  copy "$M" system/hades addons/skin.estuary/language/resource.language.en_gb/strings.po addons/resource.language.en_gb/resources/strings.po || rg_fail "copy"
  L=$RG_W/logs/validate-hades-dialog-upscaler.log
  if $RG_PY -c "import sys; p = sys.argv[1]; t = open(p, encoding='utf-8').read(); assert 'PlayerControl(HadesUpscaler)' in t; open(p, 'w', encoding='utf-8').write(t.replace('PlayerControl(HadesUpscaler)', 'Close', 1))" "$M/system/hades/DialogHadesPlayerInfo.xml"; then
    rg_log "$L" $RG_PY "$HERE/validate_hades_dialog.py" "$M"
    v=$(rg_exit "$L")
    rg_item validate-hades-dialog-upscaler "$([ "$v" = 1 ] && grep -a -q 'the upscaler of the videos of this kind is not reachable' "$L" && echo PASSED || echo FAILED)" \
      "a copy of the generated dialog with the first PlayerControl(HadesUpscaler) made Close: exit $v; $(grep -a -m1 'the upscaler of the videos' "$L" | cut -c1-110)"
  else rg_item validate-hades-dialog-upscaler FAILED "no PlayerControl(HadesUpscaler) in the generated dialog to change"; fi
  rg_rm "$M"
}

# --------------------------------------------------------------------------------------------------- skinpasses
# sc_mutant <item> <python: s2 made of s, the text of Includes_HadesPlayerInfo.xml> <what> <ERE of a FAIL line or "">
# [<file of the skin instead of that include>]: the tree as links with that file changed (the change must change it);
# skin_check.py must fail it
sc_mutant() {
  local item=$1 code=$2 what=$3 re=$4 M=$RG_W/sc-$1 f=${5:-addons/skin.estuary/xml/Includes_HadesPlayerInfo.xml} L n
  rg_fresh "$M"
  cp -as "$K/." "$M/" || { rg_item "$item" FAILED "the tree of links"; return; }
  rm -f "$M/$f"
  if ! $RG_PY -c "import re, sys; s = open(sys.argv[1], encoding='utf-8').read(); $code; assert s2 != s, 'the change does not apply'; open(sys.argv[2], 'w', encoding='utf-8').write(s2)" "$K/$f" "$M/$f"; then
    rg_item "$item" FAILED "the mutant does not apply to $f: $what"; rg_rm "$M"; return
  fi
  L=$RG_W/logs/$item.log
  rg_log "$L" $RG_PY "$HERE/skin_check.py" "$M"
  n=$(grep -a -c -E "^FAIL.*${re}" "$L")
  rg_item "$item" "$([ "$(rg_exit "$L")" = 1 ] && [ "$n" -gt 0 ] && echo PASSED || echo FAILED)" \
    "$what: exit $(rg_exit "$L"), $n failed checks; $(grep -a -m1 -E "^FAIL.*${re}" "$L" | cut -c1-120)"
  rg_rm "$M"
}
stage_skinpasses() {
  setup
  local L=$RG_W/logs/skin-check.log v r1 i1 r2 M
  r1=$(grep -c 'hades\.dv\.passes' "$K/system/hades/DialogHadesPlayerInfo.xml")
  i1=$(grep -c 'hades\.dv\.passes' "$K/addons/skin.estuary/xml/DialogPlayerProcessInfo.xml")
  rg_log "$L" $RG_PY "$HERE/skin_check.py" "$K"
  v=$(rg_pass "$L" '^SKIN-CHECK ALL PASSED')
  rg_item skin-check "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -c '^FAIL' "$L") texts cut"
  # its other direction: the tree with every width of the panel (in its include and in the dialog, as a <width> or as
  # the width parameter of an include) made a third of what it is (copies of those two files in a tree of links to the
  # files of the tree)
  M=$RG_W/sc-mut
  rg_fresh "$M"
  cp -as "$K/." "$M/" || rg_fail "the tree of links"
  for f in addons/skin.estuary/xml/Includes_HadesPlayerInfo.xml addons/skin.estuary/xml/DialogPlayerProcessInfo.xml; do
    rm -f "$M/$f"
    $RG_PY - "$K/$f" "$M/$f" <<'PY'
import re, sys
s = open(sys.argv[1], encoding="utf-8").read()
s2 = re.sub(r"<width>(\d+)</width>", lambda m: "<width>%d</width>" % (int(m.group(1)) // 3), s)
s2 = re.sub(r'(<param name="width" value=")(\d+)(")', lambda m: "%s%d%s" % (m.group(1), int(m.group(2)) // 3, m.group(3)), s2)
open(sys.argv[2], "w", encoding="utf-8").write(s2)
PY
  done
  L=$RG_W/logs/skin-check-mutant.log
  rg_log "$L" $RG_PY "$HERE/skin_check.py" "$M"
  rg_item skin-check-mutant "$([ "$(rg_exit "$L")" = 1 ] && [ "$(grep -a -c '^FAIL' "$L")" -gt 0 ] && echo PASSED || echo FAILED)" \
    "every width of the panel a third: exit $(rg_exit "$L"), $(grep -a -c '^FAIL' "$L") texts cut; $(rg_last "$L" | cut -c1-80)"
  rg_rm "$M"
  # the rows of automatic height (a grouplist as high as what it holds, the value a text box as high as its lines up to
  # its max): a copy whose text boxes may hold one short line at most must cut the values of more than one line, and a
  # copy whose rows are at least 200 px high must overflow the columns (a checker that measured no text box, or took an
  # automatic height as 0, passes both)
  local n
  n=$(grep -c -E '<height min="1" max="[^"]+">auto</height>' "$K/addons/skin.estuary/xml/Includes_HadesPlayerInfo.xml")
  if [ "$n" -gt 0 ]; then
    sc_mutant skin-check-textbox-lines 's2 = re.sub(r"<height min=\"1\" max=\"[^\"]+\">auto</height>", "<height min=\"1\" max=\"20\">auto</height>", s)' \
      "every text box of automatic height at most 20 px high (one line, cut)" 'lines of at most'
    sc_mutant skin-check-autorow-room 's2 = re.sub(r"<height min=\"\$PARAM\[rowheight\]\" max=\"400\">auto</height>", "<height min=\"200\" max=\"400\">auto</height>", s)' \
      "every row of automatic height at least 200 px high (the columns overflow)" ''
  else
    rg_item skin-check-textbox-lines NOT-RUN "the panel of this tree has no text box of automatic height"
    rg_item skin-check-autorow-room NOT-RUN "the panel of this tree has no row of automatic height"
  fi
  # (1.2.3) the names of the speakers: a copy whose labels are as wide as their discs again (44 and 54, 1.2.2) must fail
  sc_mutant skin-check-speaker-room-before 's2 = s.replace("<width>$PARAM[room]</width>", "<width>$PARAM[size]</width>")' \
    "the labels of the speakers as wide as their discs (1.2.2)" 'speaker'
  # a button of one page under its columns (the upscaler's, 5562, on the Video page): a copy with it moved up into the
  # column of the video output, and a copy with it on the button of the output of this video in the bar, must fail
  local D=addons/skin.estuary/xml/DialogPlayerProcessInfo.xml
  if grep -q '<control type="button" id="5562">' "$K/$D"; then
    sc_mutant skin-check-page-button-column 's2 = re.sub(r"(<control type=\"button\" id=\"5562\">\s*<left>\d+</left>\s*<top>)\d+(</top>)", r"\g<1>400\g<2>", s)' \
      "the button of the upscaler moved up to 400, into the column of the video output" 'reaches' "$D"
    sc_mutant skin-check-page-button-bar 's2 = re.sub(r"(<control type=\"button\" id=\"5562\">\s*<left>)\d+(</left>\s*<top>)\d+(</top>\s*<width>)\d+(</width>)", r"\g<1>1116\g<2>750\g<3>600\g<4>", s)' \
      "the button of the upscaler on the button of the output of this video, in the bar" 'lies on' "$D"
  else
    rg_item skin-check-page-button-column NOT-RUN "the panel of this tree has no button of the upscaler"
    rg_item skin-check-page-button-bar NOT-RUN "the panel of this tree has no button of the upscaler"
  fi
  M=$RG_W/sp-mut
  copy "$M" system/hades addons/skin.estuary/xml/DialogPlayerProcessInfo.xml || rg_fail "copy"
  sed -i 's/hades\.dv\.passes/hades.dv.nopasses/g' "$M/system/hades/DialogHadesPlayerInfo.xml"
  r2=$(grep -c 'hades\.dv\.passes' "$M/system/hades/DialogHadesPlayerInfo.xml")
  rg_rm "$M"
  if [ "$r1" -ge 1 ] && [ "$i1" -ge 1 ] && [ "$r2" = 0 ]; then
    rg_item skin-passes PASSED "the row of the pass times: $r1 in the generated dialog, $i1 in Estuary's DialogPlayerProcessInfo.xml; a copy without it: found 0 times"
  else rg_item skin-passes FAILED "the row of the pass times: $r1 in the generated dialog, $i1 in Estuary's file; the copy without it: $r2"; fi
}

# --------------------------------------------------------------------------------------------------- settings
# The override files of the build that name settings: LibreELEC's appliance files and Kodi's linux.xml
st_overrides() {
  local LE
  LE=$(cd "$HERE/../../.." && pwd)
  echo "$LE/packages/mediacenter/kodi/config/appliance.xml $LE/packages/mediacenter/kodi/config/appliance-gbm.xml" \
    "$LE/packages/mediacenter/kodi/config/appliance-gbm-generic.xml $1/system/settings/linux.xml"
}
# st_mutant <name> <file of the tree> <python: t changed> <what> [<extra override file>]: a tree of links with that file
# changed (the change must apply); settings_layout_check.py must fail it
st_mutant() {
  local M=$RG_W/st-$1 f=$2 L
  rg_fresh "$M"
  cp -as "$K/." "$M/" || { rg_item "settings-mutant-$1" FAILED "the tree of links"; return; }
  rm -f "$M/$f"
  if ! $RG_PY -c "import sys; t = open(sys.argv[1], encoding='utf-8').read(); t0 = t; $3; assert t != t0, 'the change does not apply'; open(sys.argv[2], 'w', encoding='utf-8').write(t)" "$K/$f" "$M/$f"; then
    rg_item "settings-mutant-$1" FAILED "the mutant does not apply to $f: $4"; rg_rm "$M"; return
  fi
  L=$RG_W/logs/settings-mutant-$1.log
  # shellcheck disable=SC2046
  rg_log "$L" $RG_PY "$HERE/settings_layout_check.py" "$M" "$HERE/ref/settings-ids-1.1.txt" $(st_overrides "$M") ${5:-}
  rg_item "settings-mutant-$1" "$([ "$(rg_exit "$L")" = 1 ] && grep -a -q '^SETTINGS-LAYOUT FAILED' "$L" && echo PASSED || echo FAILED)" \
    "$4: exit $(rg_exit "$L"); $(grep -a -m1 '^FAIL' "$L" | cut -c1-120)"
  rg_rm "$M"
}
stage_settings() {
  setup
  local L=$RG_W/logs/settings-layout.log v X=system/settings/settings.xml
  # shellcheck disable=SC2046
  rg_log "$L" $RG_PY "$HERE/settings_layout_check.py" "$K" "$HERE/ref/settings-ids-1.1.txt" $(st_overrides "$K")
  v=$(rg_pass "$L" '^SETTINGS-LAYOUT ALL PASSED')
  rg_item settings-layout "$(rg_say "$v")" "$(rg_rest "$v"); $(grep -a -c '^ok' "$L") checks ok"
  # its other direction: a setting of 1.1 gone, the setting of Kodi back where it was, a child before its parent, an
  # override file that names a setting where it no longer is, a translation missing
  st_mutant gone $X "import re; t = re.sub(r'\n[ \t]*<setting id=\"videoscreen.hadesdvmenus\".*?</setting>', '', t, count=1, flags=re.S)" \
    "a copy without the setting Dolby Vision for the menus (the value of 1.1 would be lost)"
  st_mutant back $X "t = t.replace('label=\"13436\" help=\"62502\"', 'label=\"13436\" help=\"36299\"', 1)" \
    "a copy where Adjust display HDR mode has Kodi's help again"
  st_mutant parent $X "t = t.replace('<setting id=\"videoscreen.hadeshdrassdr\" parent=\"winsystem.ishdrdisplay\"', '<setting id=\"videoscreen.hadeshdrassdr\" parent=\"videoscreen.hadesdvnative\"', 1)" \
    "a copy where Play HDR videos as SDR hangs from a setting of a later group"
  st_mutant unmapped xbmc/settings/dialogs/GUIDialogSettingsBase.cpp "t = t.replace('if (labelId == 62082 || labelId == 62083)', 'if (labelId == -62082)', 1)" \
    "a copy where the upscalers AiUpscale (film) and (animation) are not mapped to Estuary (English in Spanish)"
  cat > "$RG_W/override-old-place.xml" <<'XML'
<?xml version="1.0" encoding="utf-8" ?>
<settings version="1">
  <section id="system">
    <category id="display">
      <group id="8">
        <setting id="videoscreen.hadeshdrassdr">
          <visible>false</visible>
        </setting>
      </group>
    </category>
  </section>
</settings>
XML
  st_mutant override addons/skin.estuary/language/resource.language.en_gb/strings.po "t = t + ' '" \
    "an override file that names Play HDR videos as SDR in the group it was in up to 1.1" "$RG_W/override-old-place.xml"
  st_mutant spanish addons/skin.estuary/language/resource.language.es_ar/strings.po \
    "import re; t = re.sub(r'(msgctxt \"#54500\"\nmsgid \"[^\"]*\"\nmsgstr \")[^\"]*\"', r'\1\"', t, count=1)" \
    "a copy of es_ar without the translation of the heading Other videos as Dolby Vision"
  rm -f "$RG_W/override-old-place.xml"
}

rg_main "pocheck picheck skinpasses settings" "$@"
