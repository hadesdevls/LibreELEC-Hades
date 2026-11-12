# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
"""Verdict of autoshare_test.sh: reads what the image's testparm made of each smb.conf.

Usage: autoshare_verdict.py <outputs> <smb.conf template of the image>
For the samba-config under test ("test") every check must pass; for the reference ("ref"), when
there is one, the checks that fail are listed as what the change is for, and the smb.conf written
for ordinary names must be the same byte for byte.
"""
import pathlib
import re
import sys

OUT = pathlib.Path(sys.argv[1])
TEMPLATE = pathlib.Path(sys.argv[2])
SPECIAL = {"global", "globals", "homes", "printers", "ipc$"}
ALLOWED_ROOTS = ("/storage/", "/media/", "/run/samba/media/")


def testparm(path):
    """Sections of testparm's output, in order: [(name, {parameter: value})]."""
    sections, cur = [], None
    for line in path.read_text(encoding="utf-8", errors="surrogateescape").splitlines():
        m = re.match(r"^\[(.*)\]$", line)
        if m:
            cur = (m.group(1), {})
            sections.append(cur)
            continue
        m = re.match(r"^\t(.+?) = (.*)$", line)
        if m and cur is not None:
            cur[1][m.group(1)] = m.group(2)
    return sections


def template_paths():
    """Share -> path of the smb.conf template (the shares samba-config starts from)."""
    shares, cur = {}, None
    for line in TEMPLATE.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^\s*\[([^]]*)\]", line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^\s*path\s*=\s*(.*?)\s*$", line)
        if m and cur:
            shares[cur] = m.group(1)
    return shares


def lines(path):
    return [l for l in path.read_text(encoding="utf-8", errors="surrogateescape").split("\n") if l]


def check(variant, group):
    """The checks of one pass; returns the list of faults (empty: all pass)."""
    faults = []
    err = (OUT / f"{variant}-{group}.testparm-err").read_text(errors="replace")
    if "Loaded services file OK." not in err:
        return [f"{group}: Samba cannot load the smb.conf at all ({err.strip().splitlines()[-1]!r})"]
    secs = testparm(OUT / f"{variant}-{group}.testparm")
    names = [n for n, _ in secs]
    tmpl = template_paths()
    drives = lines(OUT / f"{variant}-{group}.drives")
    resolved = {}
    for l in lines(OUT / f"{variant}-{group}.paths"):
        p, r = l.split("\t")
        resolved[p] = r
    # template shares keep their paths, [global] gets none
    for share, path in tmpl.items():
        got = [p.get("path") for n, p in secs if n.lower() == share.lower()]
        if got != [path]:
            faults.append(f"{group}: the share [{share}] of the template has path {got}, not [{path!r}]")
    for n, p in secs:
        if n == "global" and "path" in p:
            faults.append(f"{group}: [global] has a path ({p['path']!r}), which every share without one takes")
    extra = [(n, p) for n, p in secs if n != "global" and n not in tmpl]
    for n, p in extra:
        path = p.get("path", "")
        # nothing but the path, and the path under the media folders
        if set(p) != {"path"}:
            faults.append(f"{group}: share [{n}] has parameters {sorted(set(p) - {'path'})} besides its path")
        if not path.startswith(ALLOWED_ROOTS[1:]):
            faults.append(f"{group}: share [{n}] has path {path!r}, outside the media folders")
        if "%" in n or "%" in path:
            faults.append(f"{group}: share [{n}] (path {path!r}) has a % that Samba expands as a macro")
        if any(c in n for c in "\\[]"):
            faults.append(f"{group}: share name [{n}] has a backslash or a bracket")
        if n.lower() in SPECIAL:
            faults.append(f"{group}: share [{n}] is a name Samba treats specially")
    # every drive reachable through exactly one share, by its own name of folder
    for d in drives:
        want = f"/var/media/{d}"
        hits = [n for n, p in extra if resolved.get(p.get("path", "")) == want]
        if len(hits) != 1:
            faults.append(f"{group}: drive {d!r} is reached by {len(hits)} shares ({hits})")
    if len(names) != len({n.lower() for n in names}):
        faults.append(f"{group}: two sections with the same name")
    return faults


def main():
    variants = [v for v in ("test", "ref") if (OUT / f"{v}-plain.conf").exists()]
    failed = False
    for v in variants:
        print(f"== {v}")
        for group in ("plain", "names", "break"):
            if not (OUT / f"{v}-{group}.testparm-err").exists():
                print(f"  {group}: MISSING OUTPUT")
                failed |= v == "test"
                continue
            faults = check(v, group)
            n = len(lines(OUT / f"{v}-{group}.drives"))
            if faults:
                for f in faults:
                    print(f"  FAIL {f}")
                failed |= v == "test"
            else:
                print(f"  ok   {group}: {n} drives, each reached through one share; no macro, no injected line")
    if "ref" in variants:
        a = (OUT / "test-plain.conf").read_bytes()
        b = (OUT / "ref-plain.conf").read_bytes()
        if a == b:
            print(f"== ordinary names: the smb.conf is the same byte for byte as the reference's ({len(a)} bytes)")
        else:
            print("== FAIL ordinary names: the smb.conf differs from the reference's")
            failed = True
    print("VERDICT:", "FAIL" if failed else "PASS")
    sys.exit(1 if failed else 0)


main()
