#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Hand-made Dolby Vision RPUs for the test of FFmpeg's RPU parser (rpu_parse_test.c): the RPU of the check
clips (packages/mediacenter/hades-check/clips/dovi.py: profiles 8.1 and 5, CM v2.9, levels 1 and 6), and the same
RPU with its 0x80 terminator missing or replaced, its CRC32 wrong, a zero padding byte before the CRC32, and
trailing zero bytes.

Every vector is the RBSP of the NAL unit after its two header bytes, as the HEVC decoder hands it to
ff_dovi_rpu_parse(): the prefix 0x19, the payload, the CRC32, the terminator.

usage: rpu_vectors.py --clips <hades-check/clips> --out <file>
Output, one vector a line: name profile unpatched patched patched_crc same_as hex
  unpatched, patched: acc or rej, what ff_dovi_rpu_parse() must do with err_recognition 0 (Kodi's) without and
  with ffmpeg-9935; patched_crc: the same with AV_EF_CRCCHECK | AV_EF_EXPLODE and ffmpeg-9935;
  same_as: the vector whose parsed metadata this one must equal, or -.
"""
import argparse
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clips", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    sys.path.insert(0, a.clips)
    import card  # noqa: E402
    import dovi  # noqa: E402

    ends = []
    orig_align = dovi.BitWriter.align

    def recording_align(self):
        ends.append(len(self.bits))
        orig_align(self)

    dovi.BitWriter.align = recording_align

    def body_of(profile, avg=None, blocks=True):
        """the payload without CRC32 and terminator, and whether it ends on a byte boundary. The extension
        blocks start on a byte boundary and each adds an odd number of bits (its ue(v) length; the rest are
        whole bytes): the three of the check clips (levels 1, 5 and 6) never end on one, none always does."""
        l1 = (0, 2081, 819 if avg is None else avg)
        m = dovi.metadata(profile, l1, card.MASTERING, 1000, 400, True)
        if not blocks:
            m["ext"] = []
        del ends[:]
        rpu = dovi.rpu_payload(m)
        body = rpu[:-5]
        if rpu[-1] != 0x80 or dovi.crc32_mpeg2(body).to_bytes(4, "big") != rpu[-5:-1]:
            raise SystemExit("dovi.rpu_payload() did not end in its CRC32 and 0x80")
        return body, ends[-1] % 8 == 0

    def crc(b):
        return dovi.crc32_mpeg2(b).to_bytes(4, "big")

    def bad(c):
        return bytes([c[0] ^ 0x01]) + c[1:]

    out = []

    def vec(name, profile, unpatched, patched, patched_crc, same_as, payload):
        out.append("%s %d %s %s %s %s %s" % (name, profile, unpatched, patched, patched_crc, same_as,
                                             (b"\x19" + payload).hex()))

    for p in (8, 5):
        body, aligned = body_of(p)
        if aligned:
            raise SystemExit("profile %d: the payload of the check clips ends on a byte boundary" % p)
        c = crc(body)
        n = "p%d" % p
        vec(n + "-ok", p, "acc", "acc", "acc", "-", body + c + b"\x80")
        vec(n + "-noterm", p, "rej", "acc", "acc", n + "-ok", body + c)
        vec(n + "-otherterm", p, "rej", "acc", "acc", n + "-ok", body + c + b"\x55")
        vec(n + "-noterm-zeros", p, "rej", "acc", "acc", n + "-ok", body + c + b"\x00\x00\x00")
        vec(n + "-noterm-badcrc", p, "rej", "rej", "rej", "-", body + bad(c))
        vec(n + "-otherterm-badcrc", p, "rej", "rej", "rej", "-", body + bad(c) + b"\x55")
        # (with the terminator a wrong CRC32 is seen only with AV_EF_CRCCHECK, as before)
        vec(n + "-term-badcrc", p, "acc", "acc", "rej", "-", body + bad(c) + b"\x80")

        # a CRC32 that ends in a zero byte: without the terminator, the parser strips that byte as padding
        for avg in range(819, 819 + 4096):
            b0, al0 = body_of(p, avg=avg)
            if not al0 and crc(b0)[3] == 0:
                break
        else:
            raise SystemExit("no level 1 gives a CRC32 that ends in 0x00")
        vec(n + "-crc0-ok", p, "acc", "acc", "acc", "-", b0 + crc(b0) + b"\x80")
        vec(n + "-crc0-noterm", p, "rej", "acc", "acc", n + "-crc0-ok", b0 + crc(b0))

        # a payload that ends on a byte boundary, so that a zero padding byte may come before the CRC32
        ba, al = body_of(p, blocks=False)
        if not al:
            raise SystemExit("profile %d: the payload without extension blocks does not end on a byte boundary" % p)
        ca, cp = crc(ba), crc(ba + b"\x00")
        if ca[0] == 0 or cp[3] == 0x80:
            raise SystemExit("profile %d: an ambiguous padding (CRC32 %s, padded %s)" % (p, ca.hex(), cp.hex()))
        vec(n + "-al-ok", p, "acc", "acc", "acc", "-", ba + ca + b"\x80")
        vec(n + "-al-pad", p, "acc", "acc", "acc", n + "-al-ok", ba + b"\x00" + cp + b"\x80")
        vec(n + "-al-pad-noterm", p, "rej", "acc", "acc", n + "-al-ok", ba + b"\x00" + cp)
        vec(n + "-al-pad-noterm-badcrc", p, "rej", "rej", "rej", "-", ba + b"\x00" + bad(cp))

    with open(a.out, "w", encoding="ascii", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")
    print("rpu_vectors.py: %d vectors" % len(out))


if __name__ == "__main__":
    main()
