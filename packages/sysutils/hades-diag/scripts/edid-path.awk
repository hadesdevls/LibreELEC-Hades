# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
#
# Hades: where an EDID comes from and what it declares that a device between the PC and the display changes, for the
# report (collect.sh, under each connector): the HDMI address of its HDMI VSDB (HDMI 1.4b 8.7: 4.0.0.0 on an input of
# the TV, 2.2.0.0 behind a sound bar, an AV receiver or a switch on its input 2), the version of its Dolby Vision
# block and the sound formats of its audio data blocks, as Kodi's EdidHdmiPath.h reads them. Through such a device
# these are what it declares (an LG C5 behind a TCL sound bar: Dolby Vision v1 instead of v2, DTS and DTS-HD that the
# TV has not), which the report would otherwise show as the TV's.
# input: the bytes of an EDID in decimal, as "od -An -v -tu1" writes them; output: one line, nothing without a
# CTA-861 extension
{ for (i = 1; i <= NF; i++) b[n++] = $i + 0 }
END {
  addr = -1; dv = -1; pcm = 0; cta = 0
  for (x = 128; x + 128 <= n; x += 128) {
    if (b[x] != 2) continue
    cta = 1; end = b[x + 2]
    for (i = 4; i < end && i < 128;) {
      tag = int(b[x + i] / 32); len = b[x + i] % 32
      avail = (i + 1 + len <= 128) ? len : 127 - i
      p = x + i + 1
      if (tag == 1) {
        for (k = 0; k + 3 <= avail; k += 3) {
          code = int(b[p + k] / 8) % 16; fmt[code] = 1
          if (code == 1 && b[p + k] % 8 + 1 > pcm) pcm = b[p + k] % 8 + 1
        }
      } else if (tag == 3 && avail >= 5 && b[p] == 3 && b[p + 1] == 12 && b[p + 2] == 0 && addr < 0) {
        addr = b[p + 3] * 256 + b[p + 4]
      } else if (tag == 7 && avail >= 5 && b[p] == 1 && b[p + 1] == 70 && b[p + 2] == 208 && b[p + 3] == 0 && dv < 0) {
        dv = int(b[p + 4] / 32)
      }
      i += 1 + len
    }
  }
  if (!cta) exit
  if (addr < 0 || addr == 65535) where = "hdmi address none"
  else {
    nib[0] = int(addr / 4096); nib[1] = int(addr / 256) % 16; nib[2] = int(addr / 16) % 16; nib[3] = addr % 16
    text = sprintf("%x.%x.%x.%x", nib[0], nib[1], nib[2], nib[3])
    depth = 0; while (depth < 4 && nib[depth]) depth++
    valid = depth > 0; for (k = depth; k < 4; k++) if (nib[k]) valid = 0
    if (!valid) where = "hdmi address " text ": says nothing of where the PC is"
    else if (depth == 1) where = "hdmi address " text ": on an input of the display itself"
    else where = "hdmi address " text ": through " (depth - 1) " device" (depth > 2 ? "s" : "") \
                 " (a sound bar, an AV receiver or a switch): what follows is what that device declares"
  }
  s = ""
  if (12 in fmt) s = s ", TrueHD"
  if (10 in fmt) s = s ", DD+"
  if (11 in fmt) s = s ", DTS-HD"
  if (7 in fmt) s = s ", DTS"
  if (2 in fmt) s = s ", AC-3"
  if ((1 in fmt) && pcm) s = s ", PCM " pcm " ch"
  printf "%s; dolby vision block: %s; sound: %s\n", where, (dv < 0 ? "none" : "v" dv), (s == "" ? "none" : substr(s, 3))
}
