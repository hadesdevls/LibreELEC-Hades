# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
# The places the scripts of tools/regress/mvc read from and write to (sourced by each of them). Every one can
# be given in the environment; those without a default are required.
#   MVC_WORK     the work folder: trees, streams, results (required; nothing is written anywhere else)
#   MVC_LE       the LibreELEC tree whose packages/multimedia/ffmpeg is tested (default: the tree that has this
#                folder); read only
#   MVC_SOURCES  where ffmpeg/ffmpeg-<version>.tar.xz is (checked against PKG_SHA256 of the recipe)
#   MVC_JM       JM 19.1, the reference decoder: bin/ldecod and src/cfg/decoder.cfg
#   MVC_ITU_ZIP  ITU-T_H.264.1(2016-02)_MVC_bitstreams.zip (SHA256 checked)
#   MVC_SD540    the 960x540 stream of two views of the reference encoder whose second view wraps frame_num
#   MVC_BD1080   the 1920x1080 stream of two views of the reference encoder (Blu-ray 3D size)
#   MVC_KODI     the Kodi tree of the host tests in kodi/ (read only)
TOOLS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=${MVC_WORK:?set MVC_WORK to a work folder}
LT=${MVC_LE:-$(cd "$TOOLS/../../.." && pwd)}
SRCD=${MVC_SOURCES:?set MVC_SOURCES to the folder with ffmpeg/ffmpeg-<version>.tar.xz}
J=${MVC_JM:?set MVC_JM to the folder of JM 19.1}
ITU_ZIP=${MVC_ITU_ZIP:?set MVC_ITU_ZIP to the ITU-T H.264.1 MVC bitstreams zip}
ITU_ZIP_SHA256=0631ace7163c0fd53483e755697ffb0fbc3399276974e738910491d6209b4e00
SD540=${MVC_SD540:?set MVC_SD540 to the 960x540 two-view stream}
SD540_SHA256=ed8c14b379ca3749d65bc39c24aeb1392463e4cfe71ff1e36f4cd9483631d3db
BD1080=${MVC_BD1080:?set MVC_BD1080 to the 1920x1080 two-view stream}
BD1080_SHA256=6804cd26ca0606a8f40b12109468b9b78624a33af7b79c59f5b4bb6d7774a275
ITU=$W/itu
KODI=${MVC_KODI:?set MVC_KODI to the Kodi tree of the host tests}
mkdir -p "$W"
