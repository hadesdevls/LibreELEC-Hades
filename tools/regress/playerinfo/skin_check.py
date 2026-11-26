#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# skin_check.py: the player information of Estuary (one panel: "estuary: the player information as one panel") fits.
# Without Kodi, from the files of the skin and of the Kodi tree:
#  - DialogPlayerProcessInfo.xml and the skin's include, variable and map files are well-formed; every include of the
#    dialog is expanded as Kodi expands it (the <param> values of the call, the defaults of the definition, nested
#    includes);
#  - the panel is there: the pages that Window(Home).Property(PlayerInfoDialogFocus) shows and, on them, the rows
#    (the includes HadesPanelRow, HadesPanelRowFlash, HadesPanelRowWide and HadesPanelRowText); no page, or no row, is
#    a failure;
#  - the width: every label and button of the panel (rows, headings, notes, the sentence at the top, the strip, the
#    tabs, the speakers), in each of the four languages, with every text it can show (each value of each $VAR it
#    reads, each string, the samples below), is not wider than its label: Kodi cuts a wider text and ends it with an
#    ellipsis (CGUILabel, OVER_FLOW_TRUNCATE). A label that wraps takes at most ceil(height / line height) lines
#    (CGUITextLayout::WrapText) and no word wider than itself; a button keeps its textoffsetx on both sides, and its
#    label and label2 fit it together (7,
#    Estuary's Defaults.xml); an automatic width is limited by its max; a label that scrolls (<scroll>true</scroll>)
#    is read in full, as Kodi moves its text while it is wider than the label (CGUILabel::Process()), and is listed
#    (a button scrolls only while it has the focus, CGUIButtonControl::ProcessText(): it is measured);
#  - the height: each column of rows (a vertical grouplist) holds the rows that can be shown together, in every
#    combination of the values its conditions test (each value they compare with, empty and another; booleans true
#    and false; a value that only shows a row when it is there is taken as there); a grouplist does not scroll here,
#    and what is below its end is cut. Each horizontal list holds its items at their widest, in each language;
#  - a button of one page under the columns of that page (the upscaler's on the Video page): no column above it
#    reaches its top in a state where it is shown, it ends within the frame of the columns, and it lies on no other
#    button shown on that page (the one of the output of this video, in the bar of every page);
#  - every value the panel shows ($INFO; and the infolabel of a $MAP, which Kodi looks up, or shows raw where the map
#    has no entry) has a sample below, at the widest that it really takes, read from the code that makes it; a value
#    without one is a failure (a new row is measured, never passed empty);
#  - every string the panel uses is in en_gb and translated in es_es, es_ar and es_mx (the skin's strings), or is one
#    of Kodi's core strings; no string in the arguments of an $INFO, $VAR or $MAP has a comma (Kodi splits them at
#    commas after it puts the strings in); every include, $VAR and $MAP is defined; every hades.* property that the
#    panel reads is published by the Kodi tree.
# The width is measured as Kodi measures it (GUIFontTTF.cpp), with the font file and size of each font of Estuary's
# Font.xml (fontset Default, the one of these four languages) and FreeType through ctypes (see measure()). An earlier
# checker measured it with PIL and HarfBuzz's kerning, which is not what Kodi draws: Kodi adds no kerning, hints
# lightly, thins "lighten" and emboldens "bold", and rounds each glyph's advance; measured on 1.0, PIL gave 356.9 px
# for "Ordinary, without Dolby Vision" in font12 (Kodi 349) and 173.2 px for "Dolby Vision" in font30_title (Kodi
# 187). Kodi's own translations are not in its source tree: in the Spanish labels Kodi's core words are measured in
# English.
# A variable shows the first of its values whose condition holds (GUIInfoManager, CGUIInfoLabel): a value that an
# outer variable reaches only after earlier conditions of its own were false, and whose condition implies one of those
# (all its terms are among the "+" terms of the value's condition), is never shown and is not measured (e.g.
# HadesOvAudioOutVar says "PCM, decoded" first, and the PCM values with a reason of the $VAR[HadesAudioOutVar] after
# it are never reached through it).
# Usage: skin_check.py <Kodi tree>
import copy
import ctypes
import ctypes.util
import itertools
import math
import os
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

if len(sys.argv) != 2:
    print("usage: skin_check.py <Kodi tree>")
    sys.exit(2)
ROOT = pathlib.Path(sys.argv[1])
SKIN = ROOT / "addons/skin.estuary"
XML = SKIN / "xml"
DIALOG = "DialogPlayerProcessInfo.xml"
LANGS = ("en_gb", "es_es", "es_ar", "es_mx")
ROWS = ("HadesPanelRow", "HadesPanelRowFlash", "HadesPanelRowWide", "HadesPanelRowText")
KEEP = 4  # the widest texts kept of each part of a label at each step (widths add up: the widest whole is among them)
fails = []


def fail(msg):
    if msg in fails:  # (the two value labels of a Flash row say the same)
        return
    fails.append(msg)
    print("FAIL:", msg)


def finish():
    print("SKIN-CHECK " + ("ALL PASSED" if not fails else "FAILED (%d)" % len(fails)))
    sys.exit(1 if fails else 0)


# ------------------------------------------------------------------ the samples
# Every value that the panel shows, at the widest that it really takes, and where its form comes from; a list is every
# real form of a value with few. W = xbmc/windowing/gbm/WinSystemGbmGLESContext.cpp, G = xbmc/windowing/gbm/
# WinSystemGbm.cpp, H = xbmc/utils/HadesPlayerInfo.h, P = xbmc/guilib/guiinfo/PlayerGUIInfo.cpp. A sample may hold
# $LOCALIZE[n] where the code puts one of Kodi's strings in the value.
D = ", "
X = "x"
SAMPLES = {
    # the output (W PublishPlayerInfo)
    "hades.out.display": "SONY TV  *00",  # G DisplayModel(): the monitor name of the EDID, up to 13 characters
    "hades.out.connector": ["HDMI-A-1", "DP-1", "eDP-1"],
    "hades.out.driver": ["nvidia-drm", "amdgpu", "i915", "xe"],
    "hades.out.mode": ["3840x2160 @ 119.880 Hz", "1920x1080i @ 59.940 Hz",  # "{}x{}{} @ {:.3f} Hz"
                       "3840x2160 @ 119.880 Hz (VRR)",  # (fixV) the rate of the flips with VRR: VrrModeText()
                       # (playA) a frame-packed 3D output in its raster (W FramePackedModeTextOf(): 1080p24 and
                       # 720p50/60 only); behind it, on the overview, HadesOvStereoVar
                       "1920x2205 @ 23.976 Hz", "1280x1470 @ 59.940 Hz"],
    # (playA) the mode where there is a 3D output (W hades.out.mode3d; 3D never goes with VRR): frame packing in its
    # raster, a half layout told of up to 1080p60 or 2160p30, and the eyes drawn split and not told in any mode
    "hades.out.mode3d": ["1920x2205 @ 23.976 Hz", "1280x1470 @ 59.940 Hz", "3840x2160 @ 119.880 Hz",
                         "1920x1080i @ 59.940 Hz"],
    # (fixV) the same without the mark of VRR, for the System page, whose VRR row says it
    "hades.out.modeplain": ["3840x2160 @ 119.880 Hz", "1920x1080i @ 59.940 Hz", "1920x2205 @ 23.976 Hz"],
    # (fixV) W the VRR row, each picture more than once below the display's VRR minimum (Estuary #54307, {0} how many
    # times): at the widest, 16 (a video at 6 fps on a display whose VRR starts at 90 Hz)
    "hades.out.vrrtext": [("$ADDON[skin.estuary 54307]", "16")],
    "hades.out.colorspace": ["Default", "BT2020_RGB", "BT2020_YCC", "DCI-P3_RGB_D65"],  # what Hades sets
    "hades.out.depth": ["8", "10", "12"],
    "hades.out.linkformat": ["RGB", "YCbCr 4:4:4", "YCbCr 4:2:2", "YCbCr 4:2:0"],  # DVBridgeLink.h DisplayName()
    "hades.out.fbformat": ["XR24", "AR30", "AB4H", "XB4H"],  # FourccName()
    "hades.out.fbdepth": ["8", "10", "16f"],
    "hades.out.mastering": "10000 / 0.0050",  # "{} / {:.4f}"
    # H LightLevelsText(): "{} / {} nits", a 0 not given in the words of the interface (W EstuaryWord(51834)), "none"
    "hades.out.cll": ["10000 / 10000 nits", "10000 nits / $ADDON[skin.estuary 51834]", "none"],
    "hades.out.adaptpeaks": "10000 > 1500",  # "{:.0f} > {:.0f}": the source's peak and the display's
    "hades.out.hdr10plus.average": "10000",
    "hades.out.hdr10plus.p99": "10000",
    "hades.out.hdr10plus.target": ["976+", "0"],
    "hades.out.hdr10plus.anchors": ["15", "0"],
    # the Dolby Vision output (W: the presented detail and PublishLevelValues)
    "hades.dv.l1": "0.0050 / 1000 / 10000",  # "{:.4f} / {:.0f} / {:.0f}"
    "hades.dv.l1pq": "4095 / 4095 / 4095",
    "hades.dv.l2": "5",  # the L2 trims (the targets below are five); 0 is the variable's own "none"
    "hades.dv.l2targets": "100" + D + "600" + D + "1000" + D + "2000" + D + "4000",  # "{}{:.0f}" with ", "
    "hades.dv.l3": "-2048 / -2048 / -2048",  # "{:+d} / {:+d} / {:+d}"
    "hades.dv.l4": "10000 / 4095",  # "{:.0f} / {}"
    "hades.dv.l5": "480 / 480 / 280 / 280",  # the bars, left / right / top / bottom: a 4:3 film in a 2.39:1 frame
    "hades.dv.l5sent": "480 / 480 / 280 / 280",
    "hades.dv.l6": ["10000 / 4000 nits", "10000 nits / $ADDON[skin.estuary 51834]", "none"],  # LightLevelsText()
    "hades.dv.l8": "1" + D + "16" + D + "18" + D + "21" + D + "48",
    "hades.dv.l9": ["P3-D65", "custom"],
    "hades.dv.l10": "1: 4000" + D + "5: 1000",
    "hades.dv.l11": "1 / 3 / 0",
    "hades.dv.l254": "0 / 2",
    "hades.dv.l255": "0 / 1",
    "hades.dv.source": "0.0050 / 10000",
    "hades.dv.blocks": "L1 L2" + X + "5 L3 L4 L5 L6 L8" + X + "5 L9 L10" + X + "2 L11 L254",  # DescribeLevels()
    "hades.dv.sent": "L1 L2" + X + "5 L3 L4 L5 L8" + X + "5 L9 L10" + X + "2 L11 L254",
    "hades.dv.notsent": ["L6 L255", "none"],
    # W the composer: the bits "BL {} + EL {}" (or "BL {}"), and those of the result, which the skin says after them
    # ("BL 10 + EL 10, out 12"); the maps of "-", "poly", "MMR", "poly+MMR" ("Y {}, Cb {}, Cr {}" where all three
    # differ, "Y {}, Cb/Cr {}" or "Y/Cb/Cr {}") and ", NLQ"
    "hades.dv.composebits": ["BL 10 + EL 10", "BL 10"],
    "hades.dv.composeout": "12",
    # W PublishPlayerInfo, the connection to the TV: FrlLanesAndGbps() (lanes 3 or 4, 3 to 12 Gb/s), TmdsKHz() / 1000
    # (up to 600 MHz on HDMI 2.0; a 4K 4:4:4 12-bit mode is counted 891)
    "hades.out.frllanes": "4",
    "hades.out.frlgbps": "12",
    "hades.out.tmdsmhz": "891",
    # the versions: BUILDER_VERSION (or a builder's own name and version), VERSION of os-release (a nightly's is the
    # longest), the kernel (uname), the driver's version (/sys/module/nvidia/version, or the DRM interface's)
    "hades.sys.hades": ["1.0", "1.0-rc8", "CustomBuild 12.3.4"],
    "hades.sys.build": ["devel-20261117211015-00f60f1", "nightly-20261117-00f60f1"],
    "hades.sys.kernel": ["Linux 7.3.4", "Linux 6.19.0-rc5"],
    "hades.sys.driverversion": ["620.70.06", "580.196.07", "3.64.0"],
    # W DeclaresOf(), what the TV declares in its EDID: the HDR formats it lists (all three, HDR10+ of version 0 the
    # longest), the version of its Dolby Vision block (0 to 2), VRRmin (6 bits) and VRRmax (10 bits, up to 1023;
    # displays of 500 Hz are sold), FRL as FrlLanesAndGbps() says it, Max_TMDS_Character_Rate (at most 600 MHz in HDMI
    # 2.1, up to 1275 in the field: libdisplay-info gives the byte times 5)
    "hades.tv.hdr": ["HDR10, HLG, HDR10+ (v0)", "none"],
    "hades.tv.dvversion": "2",
    "hades.tv.vrrmin": ["48", "none"],
    "hades.tv.vrrmax": ["500", "1023"],
    "hades.tv.frllanes": "4",
    "hades.tv.frlgbps": "12",
    "hades.tv.tmdsmhz": ["600", "1275"],
    # (W DeclaresOf(), EdidHdmiPath.h: the HDMI address, four digits of the inputs of each device (8 is more than any TV
    # or receiver has), and the sound formats of the EDID, all of them the longest)
    "hades.tv.address": ["2.2.0.0", "8.8.8.8"],
    "hades.tv.audio": ["TrueHD, DD+, DTS-HD, DTS, PCM 8 ch", "none"],
    "hades.tv.features": ["ALLM, QMS, FreeSync", "none"],
    "hades.dv.composemaps": ["Y poly+MMR, Cb MMR, Cr poly+MMR, NLQ", "Y poly, Cb/Cr MMR, NLQ", "Y/Cb/Cr poly"],
    "hades.dv.shown": "600",  # the frames of the last 10 s: Dolby Vision goes out at the video's rate, 60 Hz at most
    "hades.dv.notshown": "600",
    "hades.dv.gap": "9999",  # "{:.0f}" ms, the longest gap of the last 10 s
    "hades.dv.l1changes": "600",
    "hades.dv.fel": "600 / 600",
    "hades.dv.gpu": "45.3",  # "{:.1f}" ms (a GPU that cannot keep up at all, 3 digits, is not measured)
    "hades.dv.headroom": "-28.6",
    "hades.dv.step": ["0", "3"],
    "hades.dv.guistep": "3",
    "hades.dv.passes": "horizontal scaling 12.3" + D + "output 9.8 ms",  # DVPassTimes.h Compact(): at most 36 + " ms"
    "hades.dv.lvhead": "L1   L2   L3   L4   L5   L6   L8   L9   L10  L11  L254 L255 ",  # H LevelTable()
    "hades.dv.lvfile": "+    +    +    +    +    +    +    +    +    +    +    +    ",
    "hades.dv.lvused": "+    +    +    -    +    +    -    -    -    -    -    -    ",
    "hades.dv.lvsent": "+    +    +    +    +    -    +    +    +    +    +    -    ",
    "hades.dv.cm": ["2.9", "4.0"],
    # the cells of the table (H LevelCells(); W LEVEL_CELLS): a sign each
    **{"hades.dv.lv%s.%d" % (row, lv): ["+", "-"] for row in ("file", "used", "sent")
       for lv in (1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 254, 255)},
    # the sound
    "hades.audio.channels": "8",
    "hades.audio.rate": ["192", "44.1"],  # KiloHertz(): "{:.1f}" or whole
    "hades.audio.srcrate": ["192", "88.2"],  # W KiloHertz()
    "hades.audio.srcbits": "32",
    "hades.audio.bitrate": "18432",
    "hades.audio.bits": ["24", "float"],
    "hades.audio.format": ["truehd", "dtshd_ma", "eac3"],  # BitstreamName()
    # the plan of the speakers and its rows (G SpeakerPlanInfo(), utils/SpeakerPlan.h): the layout declared (the most
    # places of the plan: 12 floor, 1 LFE, 9 heights), the name of the display (DisplayModel(): the monitor name of the
    # EDID, up to 13 characters), the layout of the channels sent,
    # what a downmix folds (7.1 to stereo, the most folds) and what it leaves out
    "hades.audio.decl": ["12.1.9", "5.1.2"],
    "hades.audio.declby": "SONY TV  *00",
    "hades.audio.sent": "7.1",
    "hades.audio.fold": "FC > FL+FR; BL+BR > FL+FR; SL+SR > FL+FR",
    "hades.audio.folddrop": "LFE",
    # the upscaler of the videos of this kind, on the button of the Video page (GUIDialogVideoSettings.cpp
    # UpscalerScope(), UpscalerValueName())
    "hades.video.upscalerscope": ["$ADDON[skin.estuary 54076]", "$ADDON[skin.estuary 54077]", "$ADDON[skin.estuary 54078]"],
    "hades.video.upscaler": ["$ADDON[skin.estuary 54066]", "$ADDON[skin.estuary 54067]", "$ADDON[skin.estuary 54068]",
                             "$ADDON[skin.estuary 54069]", "$ADDON[skin.estuary 54070]"],
    # the system
    "hades.sys.cpu": "100",
    "hades.sys.gpu": "100",
    "hades.sys.gputemp": "194 °F",  # GetTemperatureAsString(): degrees Fahrenheit take three digits
    "hades.sys.memory": "65536 / 65536",
    "hades.sys.memorypct": "99",
    "hades.play.dropped": "9999",  # frames of the whole playback: four digits in a long one that stutters
    "hades.play.skipped": "9999",
    # the video
    "hades.video.size": "3840x2160",
    "hades.video.bitrate": "130.5",  # "{:.1f}" Mb/s
    "hades.video.area": "3840 " + X + " 1608 (2.39:1)",  # H PictureArea()
    # W ActiveAreaZoomText(): the zoom to the active area, on (Estuary #54281, {0} the area as PictureArea() writes it,
    # {1} the factor "{:.2f}", at most 2.50: an area of 40 % of the picture) or why not (#54282 to #54291); a tuple is
    # a string whose {0}, {1} the code fills
    "hades.video.zoom": [("$ADDON[skin.estuary 54281]", "4096" + X + "1714 (2.39:1)", "2.50")] +
                        ["$ADDON[skin.estuary %d]" % n for n in range(54282, 54292)],
    # W LutLine(): the 3D LUT of the display (DisplayLut.h Line()): the name of its file where it is applied (at most
    # LINE_NAME, 32 characters: a longer one is cut in the middle; wide letters), or why not (Estuary #54814 to #54819)
    "hades.video.lut": ["LG C5 #1 2026-...XYZLUT+MTX.cube", "JVC NZ8 HDR10 B...MTX 65P HGIG.CUBE"[:32]] +
                       ["$ADDON[skin.estuary %d]" % n for n in range(54814, 54820)],
    "hades.video.api": ["VAAPI", "DRM PRIME", "VDPAU", "NVDEC"],  # H DecoderApi()
    # xbmc/network/ProjectorControl.cpp PlayerInfoText(): the protocol, then its parts joined by ", " (a list of
    # parts: Estuary #54781 the lens memory, its picture and where its shape comes from, #54782 to #54784; #54785 the
    # picture mode by its name on the projector without what the settings add in brackets; #54786 to #54793 what
    # stands in the way, alone)
    "hades.video.projector": [
        [("JVC: $ADDON[skin.estuary 54781]", "10", "2.39", "$ADDON[skin.estuary 54784]"),
         ("$ADDON[skin.estuary 54785]", "User 4")],
        [("Sony (ADCP): $ADDON[skin.estuary 54781]", "7", "2.39", "$ADDON[skin.estuary 54782]"),
         ("$ADDON[skin.estuary 54785]", "Cinema Film 1")],
        [("Sony (SDCP): $ADDON[skin.estuary 54794]", "5"), ("$ADDON[skin.estuary 54785]", "Bright Cinema")],
        [("Epson: $ADDON[skin.estuary 54781]", "10", "2.76", "$ADDON[skin.estuary 54783]"),
         ("$ADDON[skin.estuary 54785]", "Theatre Black 2")]] +
        ["Sony (ADCP): $ADDON[skin.estuary %d]" % n for n in range(54786, 54794) if n != 54790] +
        ["Sony (ADCP): $ADDON[skin.estuary 54790]"],
    # PictureDefaults.h Line(): the settings that change the picture from the film, the words of the first three
    # (Estuary #54420 to #54443) and how many more ("+N": at most 20 more, the 23 words less the three): every three
    # of the eight widest words in the four languages, with " +20"; and how many words they are (hades.pic.count)
    "hades.pic.changed": [D.join("$ADDON[skin.estuary %d]" % n for n in four) + " +20"
                          for four in itertools.combinations((54430, 54431, 54435, 54432, 54424, 54429, 54441, 54439), 3)],
    "hades.pic.count": ["1", "23"],
    # H ColourText(), in the words of the interface (W ColourWordsOf(): Estuary #51834, #52007-#52010)
    "hades.video.color": ["BT.601 (NTSC) ($ADDON[skin.estuary 52007])" + D + "BT.709/BT.1886" + D +
                          "$ADDON[skin.estuary 52009]",
                          "$ADDON[skin.estuary 51834]" + D + "$ADDON[skin.estuary 52010]" + D + "$ADDON[skin.estuary 52008]",
                          "BT.2020" + D + "DCI gamma 2.6" + D + "$ADDON[skin.estuary 52009]", "IPT-PQ-c2 (Dolby Vision)"],
    "hades.video.matrix": ["BT.2020 NCL", "SMPTE 240M"],  # H MatrixText()
    "hades.src.mastering": "10000 / 0.0050",  # G: "{} / {:.4f}"
    "hades.src.cll": ["10000 / 10000 nits", "10000 nits / $ADDON[skin.estuary 51834]", "none"],  # LightLevelsText()
    "hades.sum.arg": ["passthroughoff", "syncdisplay", "notqueued"],  # H Summarize(): the code of the first note
    "hades.sum.more": "14",  # H: the notes but the first
    "hades.sum.vbadge": ["Dolby Vision" + D + "P8.4", "Dolby Vision" + D + "FEL"],  # H VideoBadge()
    "hades.sum.abadge": ["Dolby Digital+ 7.1.4", "Dolby TrueHD 7.1.4", "TrueHD Atmos 7.1.4 + objects",
                         "DD+ 7.1, no Atmos", "TrueHD 7.1, no Atmos"],  # H AudioBadge()
    # Kodi's own labels (P, unless said)
    "Player.CacheLevel": "100",
    "Player.Process(videodecoder)": ["ff-mpeg2video-vaapi", "hevc-drm_prime", "ff-hevc"],
    "Player.Process(pixformat)": ["yuv420p10le", "vaapi", "drm_prime", "p010le"],
    "Player.Process(videodar)": "2.39",  # "{:.2f}"
    "Player.Process(videofps)": "119.880",  # "{:.3f}"
    "Player.Process(videoscantype)": ["p", "i"],
    "Player.Process(deintmethod)": ["vaapi-mcdi", "vaapi-madi", "bwdif", "unknown", "none"],  # VAAPI.cpp
    "Player.Process(videolivebitrate)": "130.5 $LOCALIZE[25020]",  # "{:.1f}" + string 25020
    "Player.Process(videoqueuelevel)": "100",
    "Player.Process(videoqueuedatalevel)": "100",
    "Player.Process(videooutputeotf)": ["pq", "hlg", "sdr", "hdrgamma"],
    # (GUIDialogVideoSettings.cpp: #59824/#51766 with the peak of videoscreen.hadesdvhdr10peak, at most 4000)
    "Player.Process(videodvoutputchoice)": [("$ADDON[skin.estuary 51766]", "4000")],
    "Player.Process(audiochannels)": ["FL, FR, FC, LFE, BL, BR, SL, SR", "FL, FR, FC, LFE, SL, SR"],  # CAEChannelInfo: ", "
    "Player.Process(audiodecoder)": ["ff-truehd", "PT_DTSHD_MA", "ff-pcm_bluray"],
    "Player.Process(audiosamplerate)": "192,000",  # FormatNumber()
    "Player.Process(audiobitspersample)": "32",
    "Player.Process(audiolivebitrate)": "18,432 $LOCALIZE[25019]",  # FormatNumber(kb) + string 25019
    "Player.Process(audioqueuelevel)": "100",
    "Player.Process(audioqueuedatalevel)": "100",
    "Player.Process(audiobedlayout)": "7.1.4",
    "Player.Process(audio.object.count)": "118",
    "Player.Process(audiodialnormgain)": "-31 dB",  # AudioPresentation.h Dialnorm::Text()
    "Player.Process(audiodialnormsent)": "-31 dB",
    "Player.Process(audiodialoguelevel)": "-31 dBFS",
    "Player.Process(subtitledecoder)": ["hdmv_pgs_subtitle", "dvb_subtitle"],
    # SystemGUIInfo.cpp GetSystemHeatInfo(): a temperature, or Kodi's "Not available" on a PC without a sensor
    "System.CPUTemperature": ["194 °F", "$LOCALIZE[10005]"],
    "System.FPS": "119.88",
    "System.ScreenResolution": ["3840x2160 @ 119.88 Hz - $LOCALIZE[244]", "1920x1080 - $LOCALIZE[242]"],  # SystemGUIInfo.cpp
    "VideoPlayer.AudioBitrate": "18432",
    "VideoPlayer.AudioChannels": "8",
    "VideoPlayer.AudioLanguage": "Portuguese (Brazil)",
    "VideoPlayer.AudioCodec": ["truehd_atmos", "eac3_ddp_atmos", "dtshd_ma_x_imax", "dtshd_ma", "pcm_bluray", "aac_latm"],
    # the source's detail and, with Dolby Vision presented, the output's (DVPlaybackInfo.h Detail())
    "VideoPlayer.HdrDetail": ["7FEL, CM2.9, HDMI DV", "8.4, CM2.9, HDMI DV", "7MEL, HDR10", "5"],  # VideoPlayer.cpp 8052-8064
    "VideoPlayer.HdrType": ["dolbyvision", "hdr10plus", "hdr10", "hlg"],
    "VideoPlayer.SubtitlesLanguage": "Portuguese (Brazil)",
    "VideoPlayer.SubtitleCodec": ["hdmv_pgs_subtitle", "subrip", "ass", "dvd_subtitle", "dvb_subtitle"],
    "VideoPlayer.VideoAspect": "2.39",
    "VideoPlayer.VideoBitrate": "130500",
    "VideoPlayer.VideoCodec": ["hevc", "h264", "av1", "vc1", "mpeg2video", "vp9"],
    "VideoPlayer.VideoResolution": ["4K", "1080", "576"],
    "ListItem.AudioCodec": "truehd_atmos",
    "ListItem.VideoCodec": "hevc",
    "ListItem.VideoResolution": "4K",
    "MusicPlayer.Codec": "flac",
    "PVR.ActStreamBer": "1.0e-07",
    "PVR.ActStreamClient": "Tvheadend HTSP Client",
    "PVR.ActStreamDevice": "Silicon Labs Si2168 #0 : DVB-T #0",
    "PVR.ActStreamEncryptionName": "Free to air",
    "PVR.ActStreamMux": "618MHz",
    "PVR.ActStreamProviderName": "Tvheadend",
    "PVR.ActStreamSNR": "100 %",
    "PVR.ActStreamServiceName": "BBC ONE Lon HD",
    "PVR.ActStreamSignal": "100 %",
    "PVR.ActStreamStatus": "Locked",
    "PVR.ActStreamUnc": "99999",
}
missing_samples = set()


def samples_of(info, lang):
    info = info.strip()
    m = re.fullmatch(r"Window\(Home\)\.Property\(([^)]*)\)", info)
    key = m.group(1) if m else info
    if key not in SAMPLES:
        missing_samples.add(key)
        return [""]
    v = SAMPLES[key]
    out = []

    def one(x):
        if isinstance(x, tuple):
            # (a string of the interface whose {0}, {1}... the code fills: the string in the language, then the values,
            # which may be strings of the interface too)
            text = localize(x[0], lang)
            for i, a in enumerate(x[1:]):
                a = localize(a, lang)
                text = text.replace("{%d}" % i, a)
                text = text.replace("{}", a, 1)  # (fmt's automatic numbering)
            return text
        return localize(x, lang)
    for x in v if isinstance(v, list) else [v]:
        # (a list in the list: a value made of parts that the code joins with ", ")
        out.append(D.join(one(p) for p in x) if isinstance(x, list) else one(x))
    return out


# ------------------------------------------------------------------ the files
def parse(path, required=True):
    try:
        return ET.parse(path).getroot()
    except (ET.ParseError, OSError) as e:
        if required:
            fail("%s: %s" % (path.relative_to(ROOT), e))
        return None


dialog = parse(XML / DIALOG)
if dialog is None:
    finish()
print("ok: %s is well-formed" % DIALOG)
INCLUDES, VARS, MAPS, CONSTS = {}, {}, {}, {}
for f in sorted(XML.glob("*.xml")):
    if f.name == DIALOG:
        continue
    r = parse(f, required=f.name in ("Includes_HadesPlayerInfo.xml", "Variables.xml", "Includes_Maps.xml", "Font.xml", "Defaults.xml"))
    if r is None or r.tag != "includes":
        continue
    for e in r:
        n = e.get("name")
        if e.tag == "include" and n:
            INCLUDES[n] = e
        elif e.tag == "variable" and n:
            VARS[n] = [(v.get("condition"), v.text or "") for v in e.findall("value")]
        elif e.tag == "map" and n:
            MAPS[n] = e
        elif e.tag == "constant" and n:
            CONSTS[n] = (e.text or "").strip()
print("skin: %d includes, %d variables, %d maps, %d constants" % (len(INCLUDES), len(VARS), len(MAPS), len(CONSTS)))
DEFAULTS = {}
d = parse(XML / "Defaults.xml")
for e in (d.iter("default") if d is not None else []):
    DEFAULTS[e.get("type")] = e
FONTS = {}
fx = parse(XML / "Font.xml")
fs = next((s for s in fx.findall("fontset") if s.get("id") == "Default"), None) if fx is not None else None
if fs is None:
    fail("no fontset Default in Font.xml")
    finish()
for f in fs.findall("font"):
    FONTS[f.findtext("name")] = (f.findtext("filename"), int(f.findtext("size")), (f.findtext("style") or "").split())

# The width as Kodi takes it (xbmc/guilib/GUIFontTTF.cpp): FreeType with the size that CFreeTypeLibrary::GetFont sets
# (FT_Set_Char_Size, size * 64 + 0.5, 72 dpi), each glyph loaded with FT_LOAD_TARGET_LIGHT, then emboldened for "bold"
# (GLYPH_STRENGTH_BOLD 24) or thinned for "light" (GLYPH_STRENGTH_LIGHT -48) as SetGlyphStrength() does, its advance
# moved by what that moves the right edge of the outline, and rounded to whole pixels (CacheCharacter(): round_int,
# ties up); the width of a line is the sum of those advances, the last glyph's render width where that is larger
# (GetTextWidthInternal()). Kodi shapes with HarfBuzz to choose the glyphs, but adds no kerning: the glyphs here are
# those of the characters (a ligature of HarfBuzz is not taken).
class _Vec(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


class _BBox(ctypes.Structure):
    _fields_ = [("xMin", ctypes.c_long), ("yMin", ctypes.c_long), ("xMax", ctypes.c_long), ("yMax", ctypes.c_long)]


class _Generic(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("finalizer", ctypes.c_void_p)]


class _Bitmap(ctypes.Structure):
    _fields_ = [("rows", ctypes.c_uint), ("width", ctypes.c_uint), ("pitch", ctypes.c_int), ("buffer", ctypes.c_void_p),
                ("num_grays", ctypes.c_ushort), ("pixel_mode", ctypes.c_ubyte), ("palette_mode", ctypes.c_ubyte),
                ("palette", ctypes.c_void_p)]


class _Outline(ctypes.Structure):
    _fields_ = [("n_contours", ctypes.c_short), ("n_points", ctypes.c_short), ("points", ctypes.c_void_p),
                ("tags", ctypes.c_void_p), ("contours", ctypes.c_void_p), ("flags", ctypes.c_int)]


class _Slot(ctypes.Structure):
    _fields_ = [("library", ctypes.c_void_p), ("face", ctypes.c_void_p), ("next", ctypes.c_void_p),
                ("glyph_index", ctypes.c_uint), ("generic", _Generic)] + \
               [(n, ctypes.c_long) for n in ("width", "height", "horiBearingX", "horiBearingY", "horiAdvance",
                                             "vertBearingX", "vertBearingY", "vertAdvance", "linearHoriAdvance",
                                             "linearVertAdvance")] + \
               [("advance", _Vec), ("format", ctypes.c_int), ("bitmap", _Bitmap), ("bitmap_left", ctypes.c_int),
                ("bitmap_top", ctypes.c_int), ("outline", _Outline)]


class _SizeMetrics(ctypes.Structure):
    _fields_ = [("x_ppem", ctypes.c_ushort), ("y_ppem", ctypes.c_ushort), ("x_scale", ctypes.c_long),
                ("y_scale", ctypes.c_long), ("ascender", ctypes.c_long), ("descender", ctypes.c_long),
                ("height", ctypes.c_long), ("max_advance", ctypes.c_long)]


class _Size(ctypes.Structure):
    _fields_ = [("face", ctypes.c_void_p), ("generic", _Generic), ("metrics", _SizeMetrics)]


class _Face(ctypes.Structure):
    _fields_ = [("num_faces", ctypes.c_long), ("face_index", ctypes.c_long), ("face_flags", ctypes.c_long),
                ("style_flags", ctypes.c_long), ("num_glyphs", ctypes.c_long), ("family_name", ctypes.c_char_p),
                ("style_name", ctypes.c_char_p), ("num_fixed_sizes", ctypes.c_int), ("available_sizes", ctypes.c_void_p),
                ("num_charmaps", ctypes.c_int), ("charmaps", ctypes.c_void_p), ("generic", _Generic), ("bbox", _BBox),
                ("units_per_EM", ctypes.c_ushort), ("ascender", ctypes.c_short), ("descender", ctypes.c_short),
                ("height", ctypes.c_short), ("max_advance_width", ctypes.c_short), ("max_advance_height", ctypes.c_short),
                ("underline_position", ctypes.c_short), ("underline_thickness", ctypes.c_short),
                ("glyph", ctypes.POINTER(_Slot)), ("size", ctypes.POINTER(_Size))]


FT = ctypes.CDLL(ctypes.util.find_library("freetype") or "libfreetype.so.6")
FT.FT_MulFix.restype = ctypes.c_long
FT.FT_MulFix.argtypes = [ctypes.c_long, ctypes.c_long]
FT.FT_Get_Char_Index.restype = ctypes.c_uint
FT.FT_Get_Char_Index.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
FT.FT_Load_Glyph.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int32]
FT.FT_Set_Char_Size.argtypes = [ctypes.c_void_p, ctypes.c_long, ctypes.c_long, ctypes.c_uint, ctypes.c_uint]
FT.FT_Outline_Embolden.argtypes = [ctypes.c_void_p, ctypes.c_long]
FT.FT_Outline_Get_CBox.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
FT.FT_New_Face.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_void_p]
FT_LOAD_TARGET_LIGHT = 1 << 16
FT_GLYPH_FORMAT_OUTLINE = 0x6F75746C  # 'outl'
_lib = ctypes.c_void_p()
if FT.FT_Init_FreeType(ctypes.byref(_lib)):
    fail("FreeType did not start")
    finish()
_maj, _min, _pat = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
FT.FT_Library_Version(_lib, ctypes.byref(_maj), ctypes.byref(_min), ctypes.byref(_pat))
print("text measured as Kodi measures it, with FreeType %d.%d.%d" % (_maj.value, _min.value, _pat.value))
_faces, _adv, _widths = {}, {}, {}


def face(name):
    if name not in FONTS:
        fail("font %s is not in the fontset Default of Font.xml" % name)
        FONTS[name] = FONTS["font13"]
    fn, size, style = FONTS[name]
    if (fn, size) not in _faces:
        fp = ctypes.c_void_p()
        if FT.FT_New_Face(_lib, str(SKIN / "fonts" / fn).encode(), 0, ctypes.byref(fp)) or \
                FT.FT_Set_Char_Size(fp, 0, int(size * 64 + 0.5), 72, 72):
            fail("FreeType cannot open %s at %d" % (fn, size))
            finish()
        _faces[(fn, size)] = fp
    return _faces[(fn, size)], size, style


def glyph(fp, ch, strength):
    """Kodi's advance of the glyph of ch (whole pixels) and the right edge of what it draws"""
    key = (fp.value, ch, strength)
    if key not in _adv:
        f = ctypes.cast(fp, ctypes.POINTER(_Face)).contents
        if FT.FT_Load_Glyph(fp, FT.FT_Get_Char_Index(fp, ord(ch)), FT_LOAD_TARGET_LIGHT):
            _adv[key] = (0, 0)
            return _adv[key]
        slot = f.glyph.contents
        adv = slot.advance.x
        before, after = _BBox(), _BBox()
        FT.FT_Outline_Get_CBox(ctypes.byref(slot.outline), ctypes.byref(before))
        after = before
        if strength and slot.format == FT_GLYPH_FORMAT_OUTLINE:
            s = FT.FT_MulFix(f.units_per_EM, f.size.contents.metrics.y_scale)
            s = int(s / strength)  # C: toward zero
            after = _BBox()
            FT.FT_Outline_Embolden(ctypes.byref(slot.outline), s)
            FT.FT_Outline_Get_CBox(ctypes.byref(slot.outline), ctypes.byref(after))
            if adv:
                adv += after.xMax - before.xMax
        _adv[key] = (math.floor(adv / 64.0 + 0.5), math.ceil(after.xMax / 64.0) if after.xMax > after.xMin else 0)
    return _adv[key]


def measure(text, font):
    if (text, font) not in _widths:
        fp, size, style = face(font)
        t = text.upper() if "uppercase" in style else text
        strength = 24 if "bold" in style else (-48 if ("light" in style or "lighten" in style) else 0)
        w = 0
        for i, ch in enumerate(t):
            a, right = glyph(fp, ch, strength)
            w += max(a, right) if i == len(t) - 1 else a
        _widths[(text, font)] = float(w)
    return _widths[(text, font)]


def line_height(font):
    fp, size, style = face(font)
    return ctypes.cast(fp, ctypes.POINTER(_Face)).contents.size.contents.metrics.height / 64.0


# ------------------------------------------------------------------ the strings
def unesc(s):
    return s.replace('\\"', '"').replace("\\\\", "\\")


def load_po(path):
    if not path.exists():
        return None
    out, cur, field, entry = {}, None, None, {}

    def close_entry():
        if cur is not None:
            out[cur] = (unesc(entry.get("msgid", "")), unesc(entry.get("msgstr", "")))

    for line in path.read_text(encoding="utf-8").split("\n"):
        line = line.strip()
        m = re.match(r'msgctxt "#(\d+)"$', line)
        if m:
            close_entry()
            cur, entry, field = int(m.group(1)), {}, None
            continue
        m = re.match(r'(msgid|msgstr) "(.*)"$', line)
        if m and cur is not None:
            field = m.group(1)
            entry[field] = m.group(2)
            continue
        m = re.match(r'"(.*)"$', line)
        if m and cur is not None and field:
            entry[field] += m.group(1)
    close_entry()
    return out


CORE = load_po(ROOT / "addons/resource.language.en_gb/resources/strings.po") or {}
SKINPO = {l: load_po(SKIN / ("language/resource.language.%s/strings.po" % l)) or {} for l in LANGS}
print("strings: core %d, the skin's %s" % (len(CORE), ", ".join("%s %d" % (l, len(SKINPO[l])) for l in LANGS)))
used_strings = set()


def skin_string(n, lang):
    used_strings.add(("skin", n))
    msgid, msgstr = SKINPO[lang].get(n, SKINPO["en_gb"].get(n, ("", "")))
    return SKINPO["en_gb"].get(n, ("", ""))[0] if lang == "en_gb" else (msgstr or msgid)


def core_string(n):
    used_strings.add(("core", n))
    return CORE.get(n, ("", ""))[0]


def localize(text, lang):
    text = re.sub(r"\$LOCALIZE\[(\d+)\]", lambda m: skin_string(int(m.group(1)), lang) if 31000 <= int(m.group(1)) <= 31999
                  else core_string(int(m.group(1))), text)
    return re.sub(r"\$ADDON\[skin\.estuary (\d+)\]", lambda m: skin_string(int(m.group(1)), lang), text)


# ------------------------------------------------------------------ the includes
PARAM = re.compile(r"\$PARAM\[(\w+)\]")


def subst(text, params):
    return PARAM.sub(lambda m: params.get(m.group(1), ""), text) if text else text


def instantiate(name, call):
    d = INCLUDES.get(name)
    if d is None:
        fail("include %s is not defined in the skin" % name)
        return []
    params = {p.get("name"): (p.text if p.text is not None else (p.get("value") or "")) for p in d.findall("param")}
    if call is not None:
        for p in call.findall("param"):
            params[p.get("name")] = p.text if p.text is not None else (p.get("value") or "")
    body = d.find("definition")
    src = list(body) if body is not None else [c for c in d if c.tag != "param"]
    out = []
    for c in src:
        c2 = copy.deepcopy(c)
        for e in c2.iter():
            e.text = subst(e.text, params)
            for k, v in list(e.attrib.items()):
                e.attrib[k] = subst(v, params)
        out.append(c2)
    return out


def expand(parent, depth=0):
    if depth > 40:
        fail("includes nested more than 40 deep")
        return
    new = []
    for c in list(parent):
        if c.tag == "include":
            name = c.get("content") or (c.text or "").strip()
            parts = instantiate(name, c if c.get("content") else None)
            for p in parts:
                if name in ROWS and p.tag == "control":
                    p.set("_row", name)
                expand(p, depth + 1)
            new.extend(parts)
        else:
            expand(c, depth + 1)
            new.append(c)
    parent[:] = new


rows_in_source = sum(len(re.findall(r'<include content="%s"' % r, (XML / DIALOG).read_text(encoding="utf-8"))) for r in ROWS)
expand(dialog)
PARENT = {c: p for p in dialog.iter() for c in p}

# ------------------------------------------------------------------ the panel
PAGE = re.compile(r"String\.IsEqual\(Window\(Home\)\.Property\(PlayerInfoDialogFocus\),(\d+)\)")
pages = []
for g in dialog.iter("control"):
    if g.get("type") == "group":
        for v in g.findall("visible"):
            m = PAGE.fullmatch((v.text or "").strip())
            if m:
                pages.append((m.group(1), g))
rows = [(pid, r) for pid, g in pages for r in g.iter("control") if r.get("_row")]
print("the panel: %d pages (%s), %d rows (%s; the dialog calls %d)" % (
    len(pages), " ".join(p for p, _ in pages), len(rows),
    ", ".join("%s %d" % (n, sum(1 for _, r in rows if r.get("_row") == n)) for n in ROWS), rows_in_source))
if not pages:
    fail("the panel is not in %s: no page shown by Window(Home).Property(PlayerInfoDialogFocus)" % DIALOG)
    finish()
if not rows:
    fail("the panel has no rows: no %s on its pages" % ", ".join(ROWS))
    finish()
if len(rows) != rows_in_source:
    fail("the dialog calls %d row includes, %d are on the pages" % (rows_in_source, len(rows)))


# ------------------------------------------------------------------ texts
def close(s, i):
    depth = 0
    for j in range(i, len(s)):
        depth += {"[": 1, "]": -1}.get(s[j], 0)
        if depth == 0:
            return j
    return len(s) - 1


def split_args(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        depth += {"[": 1, "]": -1}.get(ch, 0)
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    return out + [cur]


BLOCK = re.compile(r"\$(INFO|ESCINFO|VAR|ESCVAR|MAP|ESCMAP)\[")


def segments(text):
    out, i = [], 0
    while i < len(text):
        m = BLOCK.search(text, i)
        if not m:
            out.append(text[i:])
            break
        if m.start() > i:
            out.append(text[i:m.start()])
        j = close(text, m.end() - 1)
        out.append((m.group(1).replace("ESC", ""), split_args(text[m.end():j])))
        i = j + 1
    return out


def clean(t):
    t = t.replace("$COMMA", ",")
    t = re.sub(r"\[/?(COLOR[^\]]*|B|I|LIGHT)\]", "", t)
    return re.sub(r"\[UPPERCASE\](.*?)\[/UPPERCASE\]", lambda m: m.group(1).upper(), t, flags=re.S)


def widest(cands, font):
    return sorted(set(cands), key=lambda c: (-measure(clean(c), font), c))[:KEEP]


def map_lookup(name, key):
    m = MAPS.get(name)
    while m is not None:
        for e in m.findall("entry"):
            if e.get("key") == key:
                return e.text or ""
        m = MAPS.get(m.get("ref")) if m.get("ref") else None
    return key  # Kodi: the raw value where the map has no entry for it (SkinMapManager.h)


def terms(cond):
    """The terms of a condition that is one conjunction ("a + b"), or None (an "or", or nothing)"""
    if not cond or "|" in cond:
        return None
    return frozenset(x.strip() for x in cond.split("+") if x.strip())


def never(cond, false):
    """A condition that cannot hold where every condition of `false` is false: it implies one of them"""
    mine = terms(cond)
    if mine is None:
        return False
    return any(f is not None and f <= mine for f in (terms(x) for x in false))


def not_equal(info, true):
    """The values that `info` cannot have where every term of `true` holds: the v of each !String.IsEqual(info,v)"""
    out = set()
    for x in true:
        m = re.fullmatch(r"!String\.IsEqual\((.*),(.*)\)", x)
        if m and m.group(1).strip() == info.strip():
            out.add(m.group(2).strip())
    return out


def texts(text, lang, font, depth=0, false=frozenset(), true=frozenset()):
    """The texts that a label's text can show, the widest first (KEEP of them); `false`: the conditions that are false
    where this text is reached (earlier values of the variables it is in); `true`: the terms that hold there (of the
    conditions of those values, where each is one conjunction), so that a sample that a !String.IsEqual of them rules
    out is not measured there (e.g. HadesBarCpuVar shows the temperature only where it is not "Not available")"""
    if depth > 20:
        fail("variables nested more than 20 deep")
        return [""]
    text = localize(text, lang)
    acc = [""]
    for s in segments(text):
        if isinstance(s, str):
            opts = [s]
        else:
            kind, args = s
            if kind == "MAP":
                name = args[0].strip()
                if name not in MAPS:
                    fail("map %s is not defined in the skin" % name)
                    MAPS[name] = ET.Element("map")
                vals = [localize(map_lookup(name, k), lang) for k in samples_of(args[1] if len(args) > 1 else "", lang)]
                opts = [""] + widest([v for v in vals if v], font)
            else:
                pre = texts(args[1], lang, font, depth + 1, false, true) if len(args) > 1 else [""]
                post = texts(args[2], lang, font, depth + 1, false, true) if len(args) > 2 else [""]
                if kind == "INFO":
                    ruled_out = {localize(v, lang) for v in not_equal(args[0], true)}
                    vals = [v for v in samples_of(args[0], lang) if v not in ruled_out]
                else:
                    name = args[0].strip()
                    if name not in VARS:
                        fail("variable %s is not defined in the skin" % name)
                        VARS[name] = []
                    vals = []
                    earlier = set(false)
                    for cond, val in VARS[name]:
                        if never(cond, earlier):
                            continue  # (an earlier value is shown wherever this one's condition holds)
                        vals += texts(val, lang, font, depth + 1, frozenset(earlier), true | (terms(cond) or frozenset()))
                        if cond:
                            earlier.add(cond)
                opts = [""] + [p + v + q for v in widest([v for v in vals if v], font) for p in pre for q in post]
        acc = widest([a + o for a in acc for o in opts], font)
    return acc


def args_strings(text):
    out = set()
    for m in BLOCK.finditer(text):
        j = close(text, m.end() - 1)
        for a in split_args(text[m.end():j])[1:]:
            out |= set(int(x) for x in re.findall(r"\$LOCALIZE\[(\d+)\]", a))
            out |= set(int(x) for x in re.findall(r"\$ADDON\[skin\.estuary (\d+)\]", a))
    return out


# ------------------------------------------------------------------ geometry
def num(t, parent=None):
    t = (t or "").strip()
    t = CONSTS.get(t, t)
    if not t or t == "auto":
        return None
    try:
        if t.endswith("%"):
            return float(t[:-1]) * (parent or 0) / 100.0
        return float(t.rstrip("r"))
    except ValueError:
        return None


def control_parent(c):
    p = PARENT.get(c)
    while p is not None and p.tag != "control":
        p = PARENT.get(p)
    return p


def width_of(c):
    wt = (c.findtext("width") or "").strip()
    if wt == "auto":
        return None
    p = control_parent(c)
    pw = width_of(p) if p is not None else 1920
    if wt:
        return num(wt, pw)
    if pw is None:
        return None
    return pw - (num(c.findtext("left")) or 0) - (num(c.findtext("right")) or 0)


def height_of(c):
    return num(c.findtext("height")) or 0


def default(c, tag, fallback):
    v = c.findtext(tag)
    if v is None and c.get("type") in DEFAULTS:
        v = DEFAULTS[c.get("type")].findtext(tag)
    return v if v is not None else fallback


def where(c):
    p, path = c, []
    while p is not None:
        if p.tag == "control":
            if p.get("_row"):
                path.append("%s %s" % (p.get("_row"), next((x.findtext("label") or "" for x in p.iter("control") if x.get("type") == "label"), "")[:40]))
            for v in p.findall("visible"):
                m = PAGE.fullmatch((v.text or "").strip())
                if m:
                    path.append("page " + m.group(1))
        p = PARENT.get(p)
    return " in ".join(path) or "the panel"


# ------------------------------------------------------------------ 1. the width of every label
NEED = {}  # the lines that each text box takes at most (its texts in the four languages)


def kodi_cell(font):
    """Kodi's cell of a font: the height of a text of one line (GUIFontTTF::Load(): the ascender and the descender
    of the font and its bounding box, rounded out)"""
    fp, size, style = face(font)
    f = ctypes.cast(fp, ctypes.POINTER(_Face)).contents
    sc = size / float(f.units_per_EM)
    rnd = lambda x: math.floor(x + 0.5) if x > 0 else math.ceil(x - 0.5)
    return rnd(max(f.bbox.yMax, f.ascender) * sc + 0.5) - rnd(min(f.bbox.yMin, f.descender) * sc - 0.5)


labels = [c for c in dialog.iter("control") if c.get("type") in ("label", "button", "fadelabel", "textbox")]
measured = 0
over_msgs = set()
closest = (0, 0.0, 0.0, "", "")
skipped = []
scrolling = set()
used_text = ""
for c in labels:
    t = c.get("type")
    text = c.findtext("label") or ""
    used_text += text + "\n"
    if t == "fadelabel":
        skipped.append(t)
        continue
    if not text.strip():
        continue
    font = default(c, "font", "font13")
    off = float(num(default(c, "textoffsetx", "0")) or 0)
    wnode = c.find("width")
    limit = width_of(c)
    if wnode is not None and (wnode.text or "").strip() == "auto":
        limit = float(wnode.get("max")) if wnode.get("max") else None
    if limit is None:
        continue
    limit -= 2 * off
    wrap = (default(c, "wrapmultiline", "false") or "").strip() == "true" or t == "textbox"
    scrolls = t == "label" and not wrap and (default(c, "scroll", "false") or "").strip() == "true"
    measured += 1
    bad = {}
    for lang in LANGS:
        for s in texts(text, lang, font):
            for line in clean(s).split("[CR]"):
                if wrap:
                    words, lines, cur = line.split(" "), [], ""
                    for wd in words:
                        cand = (cur + " " + wd) if cur else wd
                        if measure(cand, font) > limit and cur:
                            lines.append(cur)
                            cur = wd
                        else:
                            cur = cand
                    lines.append(cur)
                    lh = line_height(font)
                    maxl = int(-(-height_of(c) // lh)) if height_of(c) else len(lines)
                    if t == "textbox":
                        # (a text box draws the lines that start within its height: a cell, and a line for each
                        # line after the first, CGUIFontTTF::GetTextHeight(); one of automatic height is as high as
                        # its lines up to its max, CGUITextBox::UpdateInfo())
                        hn = c.find("height")
                        mx = (float(hn.get("max")) if hn.get("max") else None) if (hn.text or "").strip() == "auto" \
                            else height_of(c)
                        cellh = kodi_cell(font)
                        maxl = len(lines) if mx is None else (1 + int((mx - cellh) // lh + 1e-6) if mx >= cellh else 1)
                        NEED[id(c)] = max(NEED.get(id(c), 1), min(len(lines), maxl))
                    wmax = max(measure(x, font) for x in lines)
                    if len(lines) > maxl or wmax > limit + 0.01:
                        bad.setdefault(lang, "%d lines of at most %d (height %g, a line %d px), the widest %.1f of %.1f px: %s" % (
                            len(lines), maxl, height_of(c), lh, wmax, limit, line[:170]))
                    continue
                w = measure(line, font)
                if limit - w < closest[1] or not closest[3]:
                    closest = (0, limit - w, w, lang, line)
                if w > limit + 0.01 and scrolls:
                    scrolling.add("%s: %.1f px in %.1f (%s): %s" % (where(c), w, limit, font, line[:90]))
                elif w > limit + 0.01:
                    bad.setdefault(lang, "%.1f px in a label of %.1f px (%s): %s" % (w, limit, font, line[:170]))
    if bad:
        langs = sorted(bad)
        msg = "%s, %s: %s" % (where(c), " ".join(langs), bad[langs[0]] if all(bad[l] == bad[langs[0]] for l in langs)
                              else "; ".join("%s %s" % (l, bad[l]) for l in langs))
        over_msgs.add(msg)
        fail(msg)
print("labels measured: %d (4 languages, every text each can show); wider than their label: %d; not measured: %s" % (
    measured, len(over_msgs), ", ".join("%d %s" % (skipped.count(k), k) for k in sorted(set(skipped))) or "none"))
print("labels that scroll where their text is wider (read in full): %d" % len(scrolling))
# a button with a second label (label2, drawn right-aligned in the same box, CGUIButtonControl::ProcessText()): where
# the two meet, Kodi cuts both (CGUILabel::CheckAndCorrectOverlap()), so the widest text of each, together, must fit
# the width less textoffsetx on both sides, in each language
pairs = 0
for c in dialog.iter("control"):
    l2 = c.findtext("label2") or ""
    if c.get("type") != "button" or not l2.strip() or not (c.findtext("label") or "").strip():
        continue
    w = width_of(c)
    if w is None:
        continue
    pairs += 1
    font = default(c, "font", "font13")
    limit = w - 2 * float(num(default(c, "textoffsetx", "0")) or 0)
    for lang in LANGS:
        a = max(texts(c.findtext("label"), lang, font), key=lambda s: measure(clean(s), font))
        b = max(texts(l2, lang, font), key=lambda s: measure(clean(s), font))
        wa, wb = measure(clean(a), font), measure(clean(b), font)
        if wa + wb > limit + 0.01:
            fail("button %s, %s: its label and label2 take %.1f px together in %.1f (%s): %s | %s" % (
                c.get("id"), lang, wa + wb, limit, font, clean(a)[:80], clean(b)[:80]))
print("buttons with a second label, both measured together: %d" % pairs)
for s in sorted(scrolling):
    print("  scrolls:", s)
if closest[3]:
    print("the label closest to its width: %.1f px, %.1f px to spare (%s): %s" % (closest[2], closest[1], closest[3], closest[4]))

# ------------------------------------------------------------------ 2. conditions, for the lists
ATOM = re.compile(r"(String\.IsEqual|String\.IsEmpty|String\.Contains|String\.StartsWith|Integer\.IsGreater)\(((?:[^()]|\([^()]*\))*)\)"
                  r"|(VideoPlayer\.Content|Control\.IsVisible|Window\.IsActive)\(([^()]*)\)"
                  r"|(Player\.HasVideo|VideoPlayer\.HasSubtitles|VideoPlayer\.SubtitlesEnabled|true|false)\b"
                  r"|(Player\.Process\(videohwdecoder\))")


def info_key(s):
    s = s.strip()
    m = re.fullmatch(r"Window\(Home\)\.Property\(([^)]*)\)", s)
    return m.group(1) if m else s


def compile_cond(expr):
    """A Python expression of the condition over a state s (dict); and what it tests: {key: set of literals}"""
    tested = {}
    out, i = "", 0
    expr = (expr or "true").strip()
    while i < len(expr):
        m = ATOM.match(expr, i)
        if m:
            if m.group(1):
                fn, args = m.group(1), split_args(m.group(2).replace("(", "[").replace(")", "]"))
                args = [a.replace("[", "(").replace("]", ")") for a in args]
                k = info_key(args[0])
                tested.setdefault(k, set())
                if fn == "String.IsEmpty":
                    out += "(s.get(%r, '') == '')" % k
                elif fn == "String.IsEqual":
                    lit = args[1].strip().strip('"')
                    tested[k].add(lit)
                    out += "(s.get(%r, '') == %r)" % (k, lit)
                elif fn == "String.Contains":
                    lit = args[1].strip().strip('"')
                    tested[k].add("contains:" + lit)
                    out += "(%r in s.get(%r, ''))" % (lit, k)
                elif fn == "String.StartsWith":
                    lit = args[1].strip().strip('"')
                    tested[k].add("starts:" + lit)
                    out += "(s.get(%r, '').startswith(%r))" % (k, lit)
                else:
                    tested[k].add("gt:" + args[1].strip())
                    out += "(s.get(%r, '').isdigit() and int(s.get(%r, '')) > %d)" % (k, k, int(args[1]))
            elif m.group(3):
                k = "%s(%s)" % (m.group(3), m.group(4))
                tested.setdefault(k, {"bool"})
                out += "s.get(%r, False)" % k
            elif m.group(5) in ("true", "false"):
                out += "True" if m.group(5) == "true" else "False"
            else:
                k = m.group(5) or m.group(6)
                tested.setdefault(k, {"bool"})
                out += "s.get(%r, False)" % k
            i = m.end()
            continue
        ch = expr[i]
        out += {"!": " not ", "+": " and ", "|": " or ", "[": "(", "]": ")"}.get(ch, ch if ch.isspace() else "?")
        if out.endswith("?"):
            return None, tested
        i += 1
    return out, tested


def visible_expr(c):
    exprs, tested = [], {}
    for v in c.findall("visible"):
        e, t = compile_cond(v.text)
        if e is None:
            fail("%s: a condition this check cannot read: %s" % (where(c), v.text))
            e = "True"
        exprs.append("(%s)" % e)
        for k, lits in t.items():
            tested.setdefault(k, set()).update(lits)
    return (" and ".join(exprs) or "True"), tested


def domain(k, lits, uses_empty):
    if "bool" in lits:
        return [True, False]
    vals = sorted(l for l in lits if not l.startswith(("contains:", "starts:", "gt:")))
    vals += ["x" + l[9:] + "x" for l in lits if l.startswith("contains:")]
    vals += [l[7:] + "x" for l in lits if l.startswith("starts:")]
    vals += [str(int(l[3:]) + 1) for l in lits if l.startswith("gt:")]
    return vals + ["", "<another>"]


# combinations that the code cannot make, and where it says so (each is taken out of the lists' combinations)
FACTS = [
    ("hades.dv.vsif is hdmi14b only while the output is native Dolby Vision (W PublishPlayerInfo: set in the branch of "
     "native output, from OutputHandover::HeldHdmi14b())",
     lambda s: s.get("hades.dv.vsif", "") != "hdmi14b" or s.get("hades.dv.state", "<a value>") in ("native", "<a value>")),
]
def is_auto(gl):
    return (gl.findtext("height") or "").strip() == "auto"


def row_height(k):
    """A row of automatic height (a grouplist): as high as what it shows, at least its min; its text boxes (of which
    one is shown at a time) as high as their lines at most"""
    if not (k.get("type") == "grouplist" and is_auto(k)):
        return height_of(k)
    lo = float(k.find("height").get("min") or 0)
    rest, tb = 0.0, 0.0
    for kid in k.findall("control"):
        top = num(kid.findtext("top")) or 0
        if kid.get("type") == "textbox":
            font = default(kid, "font", "font13")
            tb = max(tb, top + kodi_cell(font) + (NEED.get(id(kid), 1) - 1) * line_height(font))
        else:
            rest += top + height_of(kid)
    return max(lo, rest + tb)


lists = 0
for gl in dialog.iter("control"):
    if gl.get("type") != "grouplist" or is_auto(gl):
        continue
    kids = gl.findall("control")
    if not kids:
        continue
    lists += 1
    vertical = (gl.findtext("orientation") or "vertical").strip() == "vertical"
    gap = num(gl.findtext("itemgap")) or 0
    room = height_of(gl) if vertical else (width_of(gl) or 0)
    compiled, tested = [], {}
    for k in kids:
        e, t = visible_expr(k)
        compiled.append(compile(e, "<visible>", "eval"))
        for key, lits in t.items():
            tested.setdefault(key, set()).update(lits)
    # a key that only shows a row while it holds a value, and is tested in no other way: taken as holding one
    texts_of_conds = " ".join(v.text or "" for k in kids for v in k.findall("visible"))
    positive = {info_key(m.group(2)) for m in re.finditer(r"(!?)\s*String\.IsEmpty\(((?:[^()]|\([^()]*\))*)\)", texts_of_conds)
                if not m.group(1)}
    if re.search(r"!\s*\[", texts_of_conds):  # a negated group: every key in the column is tried both ways
        positive |= set(tested)
    enum = {}
    for key, lits in tested.items():
        if lits or key in positive:
            enum[key] = domain(key, lits, key in positive)
    fixed = {key: "<a value>" for key in tested if key not in enum}
    sizes = []
    for k in kids:
        if vertical:
            sizes.append(row_height(k))
        else:
            wn = k.find("width")
            if wn is not None and (wn.text or "").strip() == "auto":
                font = default(k, "font", "font13")
                most = {lang: max([measure(clean(s), font) for s in texts(k.findtext("label") or "", lang, font)] or [0]) for lang in LANGS}
                lo = float(wn.get("min") or 0)
                sizes.append({lang: max(lo, min(float(wn.get("max")) if wn.get("max") else most[lang], most[lang])) for lang in LANGS})
            else:
                sizes.append(width_of(k) or 0)
    keys = sorted(enum)
    combos = 1
    for key in keys:
        combos *= len(enum[key])
    if combos > 2000000:
        fail("%s: %d combinations of the values its conditions test, too many for this check" % (where(gl), combos))
        continue
    worst, worst_state = -1, None
    for vals in itertools.product(*(enum[k] for k in keys)):
        s = dict(fixed)
        s.update(zip(keys, vals))
        if not all(f(s) for _, f in FACTS):
            continue
        shown =[i for i, ce in enumerate(compiled) if eval(ce, {"__builtins__": {}}, {"s": s})]
        if vertical:
            total = sum(sizes[i] for i in shown) + gap * max(0, len(shown) - 1)
        else:
            total = max(sum(sizes[i][lang] if isinstance(sizes[i], dict) else sizes[i] for i in shown) for lang in LANGS) + gap * max(0, len(shown) - 1)
        if total > worst:
            worst, worst_state = total, (s, len(shown))
    ok = worst <= room + 0.01
    name = "%s list at %s,%s in %s" % ("vertical" if vertical else "horizontal", gl.findtext("left") or ("right " + (gl.findtext("right") or "")), gl.findtext("top"), where(gl))
    print("  %s: %d %s, at most %d at once taking %.0f of %.0f px (%d combinations of %s)%s" % (
        name, len(kids), "rows" if vertical else "items", worst_state[1], worst, room, combos, " ".join(keys) or "nothing",
        "" if ok else "  OVER"))
    if os.environ.get("HADES_ROWS") and vertical:
        # (HADES_ROWS=1, a diagnostic: each row of the list, its height, and "*" where the worst state shows it)
        s = worst_state[0]
        for i, k in enumerate(kids):
            label = ""
            for lab in k.iter("control"):
                if lab.get("type") in ("label", "textbox") and (lab.findtext("label") or "").strip():
                    label = lab.findtext("label").strip()
                    break
            on = eval(compiled[i], {"__builtins__": {}}, {"s": s})
            print("    %s %4.0f  %s" % ("*" if on else " ", sizes[i], label[:110]))
    if not ok:
        s = worst_state[0]
        fail("%s: %d %s take %.0f of its %.0f px where %s" % (name, worst_state[1], "rows" if vertical else "items", worst, room,
                                                             ", ".join("%s=%r" % (k, s[k]) for k in keys) or "all are shown"))
print("lists: %d" % lists)


def worst_of(gl, extra):
    """The most that the rows of a vertical list take where the condition extra holds as well"""
    kids = gl.findall("control")
    gap = num(gl.findtext("itemgap")) or 0
    e2, t2 = compile_cond(extra)
    compiled, tested = [], {}
    for k in kids:
        e, tk = visible_expr(k)
        compiled.append(compile("(%s) and (%s)" % (e, e2), "<visible>", "eval"))
        for key, lits in list(tk.items()) + list(t2.items()):
            tested.setdefault(key, set()).update(lits)
    conds = " ".join(v.text or "" for k in kids for v in k.findall("visible")) + " " + extra
    positive = {info_key(m.group(2)) for m in re.finditer(r"(!?)\s*String\.IsEmpty\(((?:[^()]|\([^()]*\))*)\)", conds)
                if not m.group(1)}
    if re.search(r"!\s*\[", conds):
        positive |= set(tested)
    enum = {key: domain(key, lits, key in positive) for key, lits in tested.items() if lits or key in positive}
    fixed = {key: "<a value>" for key in tested if key not in enum}
    sizes = [row_height(k) for k in kids]
    keys = sorted(enum)
    worst = 0
    for vals in itertools.product(*(enum[k] for k in keys)):
        s = dict(fixed)
        s.update(zip(keys, vals))
        if not all(f(s) for _, f in FACTS):
            continue
        shown = [i for i, ce in enumerate(compiled) if eval(ce, {"__builtins__": {}}, {"s": s})]
        worst = max(worst, sum(sizes[i] for i in shown) + gap * max(0, len(shown) - 1))
    return worst


def page_of(c):
    p = c
    while p is not None:
        if p.tag == "control":
            for v in p.findall("visible"):
                m = PAGE.fullmatch((v.text or "").strip())
                if m:
                    return m.group(1)
        p = PARENT.get(p)
    return None


# HADES_STATES=<file.json>, a diagnostic for placing what goes below the columns of a page: {"<page id>": [<condition>,
# ...]}; for each condition, how low the vertical lists of that page reach at most where it holds ("" for any state)
if os.environ.get("HADES_STATES"):
    import json
    states = json.load(open(os.environ["HADES_STATES"], encoding="utf-8"))
    for pid, conds in sorted(states.items()):
        cols = [g for g in dialog.iter("control") if g.get("type") == "grouplist" and not is_auto(g)
                and (g.findtext("orientation") or "vertical").strip() == "vertical" and page_of(g) == pid]
        for i, cond in enumerate(conds):
            each = [(num(g.findtext("top")) or 0) + worst_of(g, cond or "true") for g in cols]
            low = max(each)
            print("STATE %s %d %g  (columns: %s)" % (pid, i, low, " ".join("%g" % e for e in each)))

# ------------------------------------------------------------------ a button of one page under its columns
def abs_box(c):
    """The box of a control on the screen, where it and the controls it is in are plain groups (no list places it)"""
    x = y = 0.0
    p = c
    while p is not None:
        if p.tag == "control":
            if p is not c and p.get("type") != "group":
                return None
            x += num(p.findtext("left")) or 0
            y += num(p.findtext("top")) or 0
        p = PARENT.get(p)
    w = width_of(c)
    return (x, y, x + w, y + height_of(c)) if w else None


def named_pages(c):
    """The pages that the conditions of a control name outside a negation ("![...]"), and its conditions"""
    vis = " ".join((v.text or "") for v in c.findall("visible"))
    return set(PAGE.findall(re.sub(r"!\s*\[[^\]]*\]", "", vis))), vis


page_buttons = 0
buttons = [c for c in dialog.iter("control") if c.get("type") == "button"]
for b in buttons:
    pids, vis = named_pages(b)
    if len(pids) != 1 or "|" in vis:  # (a button shown on one page only)
        continue
    pid = next(iter(pids))
    cols = [g for g in dialog.iter("control") if g.get("type") == "grouplist" and not is_auto(g)
            and (g.findtext("orientation") or "vertical").strip() == "vertical" and page_of(g) == pid]
    if not cols:
        continue
    page_buttons += 1
    bb = abs_box(b)
    if bb is None:
        fail("button %s of page %s: its place cannot be read" % (b.get("id"), pid))
        continue
    frame = max(abs_box(g)[1] + height_of(g) for g in cols if abs_box(g))
    if bb[3] > frame:
        fail("button %s of page %s ends at %g, below the frame of the columns of its page (%g)" % (b.get("id"), pid, bb[3], frame))
    for g in cols:
        gb = abs_box(g)
        if gb and gb[0] < bb[2] and bb[0] < gb[2]:
            low = gb[1] + worst_of(g, vis)
            if low > bb[1]:
                fail("button %s of page %s at %g,%g: the column at %g,%g reaches %g where the button is shown" % (
                    b.get("id"), pid, bb[0], bb[1], gb[0], gb[1], low))
            else:
                print("button %s of page %s: top %g, the column at %g,%g above it reaches %g at most" % (
                    b.get("id"), pid, bb[1], gb[0], gb[1], low))
    for o in buttons:
        if o is b:
            continue
        opids, _ = named_pages(o)
        if opids and pid not in opids:
            continue
        ob = abs_box(o)
        if ob and ob[0] < bb[2] and bb[0] < ob[2] and ob[1] < bb[3] and bb[1] < ob[3]:
            fail("button %s of page %s lies on button %s" % (b.get("id"), pid, o.get("id")))
print("buttons of one page under its columns: %d" % page_buttons)

# ------------------------------------------------------------------ 3. strings, properties, samples
for kind, n in sorted(used_strings):
    if kind == "skin":
        for l in LANGS:
            if n not in SKINPO[l]:
                fail("#%d is not in the skin strings of %s" % (n, l))
            elif l != "en_gb" and not SKINPO[l][n][1]:
                fail("#%d is not translated in %s" % (n, l))
    elif n not in CORE:
        fail("#%d is not in Kodi's core strings" % n)
in_args = args_strings(used_text)
for name in VARS:
    for _, v in VARS[name]:
        in_args |= args_strings(v)
for n in sorted(in_args):
    ts = [SKINPO[l].get(n, ("", ""))[1] or SKINPO[l].get(n, ("", ""))[0] for l in LANGS] if 31000 <= n else [CORE.get(n, ("", ""))[0]]
    if any("," in t for t in ts):
        fail("#%d has a comma and is in the arguments of an $INFO, $VAR or $MAP: %s" % (n, ts))
print("strings used: %d of the skin, %d of Kodi's core (in Spanish, the core ones measured in English); in arguments: %d" % (
    sum(1 for k, _ in used_strings if k == "skin"), sum(1 for k, _ in used_strings if k == "core"), len(in_args)))
for k in sorted(missing_samples):
    fail("no sample of %s: its widest real value is not known to this check" % k)
print("values shown: %d samples here, %d values without one" % (len(SAMPLES), len(missing_samples)))
src = ""
for d, _, files in os.walk(ROOT / "xbmc"):
    for f in files:
        if f.endswith((".cpp", ".h")):
            t = open(os.path.join(d, f), encoding="utf-8", errors="replace").read()
            if '"hades.' in t:
                src += t
panel_text = ET.tostring(dialog, encoding="unicode")
reached, todo = set(), set(re.findall(r"\$(?:ESC)?VAR\[(\w+)", panel_text))
while todo:
    n = todo.pop()
    if n in reached or n not in VARS:
        continue
    reached.add(n)
    for cond, v in VARS[n]:
        todo |= set(re.findall(r"\$(?:ESC)?VAR\[(\w+)", (cond or "") + v)) - reached
read_text = panel_text + "".join((cond or "") + v for n in reached for cond, v in VARS[n])
props = sorted(set(re.findall(r"Property\((hades\.[\w.]+)\)", read_text)))
unpublished = [p for p in props if '"%s"' % p not in src and not (p.startswith("hades.f.") and '"hades.f."' in src and '"hades.%s"' % p[8:] in src)]
print("hades properties read: %d; not published by the Kodi tree: %s" % (len(props), " ".join(unpublished) or "none"))
for p in unpublished:
    fail("%s is read by the panel and published nowhere in xbmc/" % p)
finish()
