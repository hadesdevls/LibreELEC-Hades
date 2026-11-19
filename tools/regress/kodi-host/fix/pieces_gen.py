#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# takes out of a Kodi tree, as they are written there, the bodies that pieces_test.cpp compiles and runs
# (the build has no compile commands of Kodi now: the other changed functions are compiled here, on the host):
#   setmode.inc  CDRMUtils::SetMode() (DRMUtils.cpp)
#   take.inc     CDRMUtils::TakeConnectorModes()
#   replace.inc  CDisplaySettings::ReplaceCustomResolutions() (DisplaySettings.cpp)
# Usage: pieces_gen.py <tree> <out dir>
import pathlib
import sys

tree, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)


def body(text, head):
    i = text.index(head)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        depth += text[k] == "{"
        depth -= text[k] == "}"
        if depth == 0:
            return text[j + 1:k]


drm = (tree / "xbmc/windowing/gbm/drm/DRMUtils.cpp").read_text()
(out / "setmode.inc").write_text(body(drm, "bool CDRMUtils::SetMode(const RESOLUTION_INFO& res)"))
(out / "take.inc").write_text(body(drm, "void CDRMUtils::TakeConnectorModes(drmModeConnector* probed, int screenPlace)"))
ds = (tree / "xbmc/settings/DisplaySettings.cpp").read_text()
(out / "replace.inc").write_text(body(ds, "void CDisplaySettings::ReplaceCustomResolutions("))
print("pieces: setmode, take, replace taken out")
