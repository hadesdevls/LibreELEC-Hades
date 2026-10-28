#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026-present Hades

"""jar-same-bytes.py <jar> <output> <YYYY-MM-DD> <highest class file version>

Write a jar again, entry by entry, so that two builds of the same classes give the same bytes, on
any build host. What changes, and only that:

- the date of every entry (ant writes the time of the build): the given one, at 00:00:00;
- the order of the entries (ant writes them in the order the file system lists them): the one of
  jar files, META-INF/ and META-INF/MANIFEST.MF first, then by name;
- how they are stored: every file uncompressed (ZIP_STORED), so that the bytes do not depend on the
  version of zlib that compresses them; the image's file system compresses the jar anyway;
- their attributes: Unix, 0644 for a file and 0755 for a folder, no extra fields, no comments.

The contents of every entry, the manifest included, are copied as they are. Before the copy is
kept, it is read back and compared with the original, entry by entry (the same names, and the same
bytes in each); and every class file is checked to be of a version up to the given one (48 is Java
1.4, 49 Java 5, 52 Java 8), so that a jar compiled for a newer Java than the one that runs it never
goes into the image.
"""

import os
import sys
import zipfile


def order(info):
    name = info.filename
    if name == "META-INF/":
        return (0, name)
    if name == "META-INF/MANIFEST.MF":
        return (1, name)
    return (2, name)


def class_version(name, data):
    if len(data) < 8 or data[:4] != b"\xca\xfe\xba\xbe":
        sys.exit(f"{name}: not a class file")
    return int.from_bytes(data[6:8], "big")


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__.strip().splitlines()[0])
    src, dst, date, highest = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    year, month, day = (int(part) for part in date.split("-"))
    stamp = (year, month, day, 0, 0, 0)

    with zipfile.ZipFile(src) as zin:
        infos = zin.infolist()
        names = [info.filename for info in infos]
        if len(set(names)) != len(names):
            sys.exit(f"{src}: an entry is there twice")
        bad = zin.testzip()
        if bad is not None:
            sys.exit(f"{src}: {bad} is damaged")
        contents = {info.filename: zin.read(info.filename) for info in infos}

    versions = {}
    for name, data in contents.items():
        if name.endswith(".class"):
            version = class_version(name, data)
            versions[version] = versions.get(version, 0) + 1
            if version > highest:
                sys.exit(f"{src}: {name} is a class file of version {version}, newer than {highest}")
    if not versions:
        sys.exit(f"{src}: no class files")

    tmp = dst + ".tmp"
    with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_STORED) as zout:
        for info in sorted(infos, key=order):
            out = zipfile.ZipInfo(info.filename, date_time=stamp)
            out.create_system = 3  # Unix, for the permissions below
            out.compress_type = zipfile.ZIP_STORED
            out.external_attr = ((0o40755 << 16) | 0x10) if info.is_dir() else (0o100644 << 16)
            zout.writestr(out, contents[info.filename])

    with zipfile.ZipFile(tmp) as check:
        if check.testzip() is not None:
            sys.exit(f"{tmp}: written damaged")
        if check.namelist() != [info.filename for info in sorted(infos, key=order)]:
            sys.exit(f"{tmp}: not the entries of {src}")
        for name in names:
            if check.read(name) != contents[name]:
                sys.exit(f"{tmp}: {name} differs from {src}")
    os.replace(tmp, dst)
    print(f"{os.path.basename(dst)}: {len(names)} entries, class file versions "
          + ", ".join(f"{v} ({n})" for v, n in sorted(versions.items())))


if __name__ == "__main__":
    main()
