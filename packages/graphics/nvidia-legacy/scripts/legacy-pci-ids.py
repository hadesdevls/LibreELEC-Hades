#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades
#
# The PCI device IDs of the cards that the image drives with NVIDIA's 580 driver
# (nvidia-legacy) instead of the open 615 one (nvidia), one lowercase 4-digit hex ID per line,
# from the supported-gpus.json of the two installers: the IDs that 615 lists with
# "legacybranch": "580.xx" (its own word that 580 is their last branch), that 615 does not
# support itself, and that 580 supports. Anything else (Turing and later, older cards, IDs of
# no list) is left to 615, whose open nvidia.ko refuses what it cannot drive.
#
# usage: legacy-pci-ids.py <supported-gpus.json of 615> <supported-gpus.json of 580>

import json
import sys


def chips(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)["chips"]


def devid(chip):
    value = int(chip["devid"], 16)
    if not 0 <= value <= 0xFFFF:
        sys.exit("legacy-pci-ids: device ID out of range: %r" % chip["devid"])
    return value


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: legacy-pci-ids.py <615 json> <580 json>")
    open_chips = chips(sys.argv[1])
    legacy_chips = chips(sys.argv[2])

    open_supported = {devid(c) for c in open_chips if "legacybranch" not in c}
    open_says_580 = {devid(c) for c in open_chips if c.get("legacybranch") == "580.xx"}
    legacy_supported = {devid(c) for c in legacy_chips if "legacybranch" not in c}

    ids = (open_says_580 - open_supported) & legacy_supported

    # What the list has to be, or the installers are not what this script was written for.
    if not open_says_580 or not ids:
        sys.exit("legacy-pci-ids: no ID for the 580 driver")
    if ids & open_supported:
        sys.exit("legacy-pci-ids: an ID of the list is supported by 615")
    for known in (0x13C2, 0x1401, 0x17C8, 0x1B06, 0x1B80, 0x1C03, 0x1C82, 0x1D01, 0x1D81):
        # GTX 970, GTX 960, GTX 980 Ti, GTX 1080 Ti, GTX 1080, GTX 1060 6GB, GTX 1050 Ti,
        # GT 1030, TITAN V
        if known not in ids:
            sys.exit("legacy-pci-ids: %04x is not in the list" % known)
    for known in (0x1E84, 0x1F82, 0x2204, 0x2702, 0x2B85):
        # RTX 2070 SUPER, GTX 1650, RTX 3090, RTX 4080 SUPER, RTX 5090
        if known in ids:
            sys.exit("legacy-pci-ids: %04x is in the list" % known)

    for value in sorted(ids):
        print("%04x" % value)


if __name__ == "__main__":
    main()
