# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Makes the picture clips of "Check this setup": the Hades check card (card.py) in SDR, HDR10, HLG,
Dolby Vision 8.1 and Dolby Vision 5, as Matroska files, with clips.json (what each clip is and what it
must show); the clip of the A/B comparison of native Dolby Vision (abclip.py: clips.json "ab", apart
from the card's list); the 3D clip of the frame packing card (fpclip.py: clips.json "fp"); and the HDR 3D clip of
the card "3D: HDR" (hdr3dclip.py: clips.json "hdr3d"). Runs at build time on the build host: x265 encodes, everything
else is this directory's
Python (standard library only). Deterministic: the same bytes on every run.

usage: gen_video.py --x265 <x265> --ffprobe <ffprobe> --out <dir> [--only <format,...>] [--frames <n>]
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abclip  # noqa: E402
import card  # noqa: E402
import dovi  # noqa: E402
import fpclip  # noqa: E402
import hdr3dclip  # noqa: E402
import hevc  # noqa: E402
import mkv  # noqa: E402

# x265's options, the same for every clip but the colour and the quantiser: the tools of the medium
# preset, made exact (every picture decodes to the very codes of the card, measured by
# tests/check_clips_tests.py):
#  - one key frame (an IDR) and then P pictures that predict all of it (the card does not move); no
#    B pictures, so that decode order is presentation order;
#  - 10 bits (HDR10, HLG, Dolby Vision): a constant QP of 8, adaptive quantisation off (the picture is
#    flat). At QP 8 every picture of the three decodes exactly; at QP 12 a chroma code near the edges is
#    off by one;
#  - the HDR 3D clip (10 bits, each eye squeezed to half its width): lossless, as the SDR clips. At QP 8
#    luma codes near the edges of its halved regions decode off by up to 2, and chroma by 1 (measured);
#  - 8 bits (SDR): lossless. Lossy 8-bit coding leaves luma codes off by one near the edges of the
#    regions (within 8 pixels; without SAO, inside them too), and its P pictures change them again,
#    whatever the quantiser (QP 8 down to 0), sign data hiding, RDOQ, psy-rd, the largest transform or
#    edges on a grid of 16 pixels; lossless (the bypass of transform and quantisation of HEVC version 1,
#    Main profile) is exact by construction, and its P pictures are as small as the others';
#  - determinism: one frame thread, a fixed pool of threads, no lookahead slices; no version SEI
#    (--no-info), so that nothing of the encoder or the machine is in the stream; C code only (--no-asm),
#    so that the CPU of the build host plays no part (the same bytes with any number of threads in the
#    pool, measured).
X265_COMMON = ["--preset", "medium", "--aq-mode", "0", "--bframes", "0", "--no-scenecut",
               "--no-open-gop", "--frame-threads", "1", "--pools", "4", "--lookahead-slices", "0",
               "--no-info", "--sar", "1:1", "--no-asm", "--log-level", "error"]
QUANTISER = {"sdr": ["--lossless"], "hdr10": ["--qp", "8"], "hlg": ["--qp", "8"], "dv81": ["--qp", "8"],
             "dv5": ["--qp", "8"], abclip.FORMAT: ["--qp", "8"], fpclip.FORMAT: ["--lossless"],
             hdr3dclip.FORMAT: ["--lossless"]}

P3_D65 = ((0.680, 0.320), (0.265, 0.690), (0.150, 0.060))


def x265_colour(f):
    if f["signal"] == "sdr":
        return ["--range", "limited", "--colorprim", "bt709", "--transfer", "bt709", "--colormatrix", "bt709"]
    if f["signal"] == "pq":
        return ["--range", "limited", "--colorprim", "bt2020", "--transfer", "smpte2084",
                "--colormatrix", "bt2020nc"]
    if f["signal"] == "hlg":
        return ["--range", "limited", "--colorprim", "bt2020", "--transfer", "arib-std-b67",
                "--colormatrix", "bt2020nc"]
    # IPT-PQ-c2, full range, as x265 4.2 signals profile 5 (its own Dolby Vision presets): BT.2020,
    # PQ, IPT-C2 (ITU-T H.273 MatrixCoefficients 15)
    return ["--range", "full", "--colorprim", "bt2020", "--transfer", "smpte2084", "--colormatrix", "15"]


def static_metadata(f):
    """(master-display option, MaxCLL, MaxFALL) of a clip of the card (HDR10, the base layer of profile
    8.1, and level 6 of the Dolby Vision clips): the light of its own picture, its label included"""
    peak, mean = card.light_stats(f)
    max_cll = card.rnd(peak, "MaxCLL")
    max_fall = card.rnd(mean, "MaxFALL")
    m = card.MASTERING
    # x265: G, B, R and the white point in units of 0.00002; luminance in units of 0.0001 cd/m2
    xy = lambda p: "(%d,%d)" % (card.rnd(p[0] / 0.00002, "x"), card.rnd(p[1] / 0.00002, "y"))
    md = "G%sB%sR%sWP%sL(%d,%d)" % (xy(P3_D65[1]), xy(P3_D65[2]), xy(P3_D65[0]), xy(card.D65),
                                    card.rnd(m["max_nits"] * 10000, "Lmax"), card.rnd(m["min_nits"] * 10000, "Lmin"))
    return md, max_cll, max_fall


def encode(x265, f, path, log, quantiser, planes=None, static=None):
    """(planes: the picture, the card's where not given; static: its HDR10 static metadata, the card's where
    not given, as static_metadata() gives them)"""
    if planes is None:
        planes = card.planes(f)
    frame = b"".join(planes)
    args = [x265, "--input", "-", "--input-res", "%dx%d" % (f["width"], f["height"]),
            "--fps", "%d/%d" % f["fps"], "--input-depth", str(f["bits"]), "--output-depth", str(f["bits"]),
            "--input-csp", "i420", "--frames", str(f["frames"]),
            "--keyint", str(f["frames"]), "--min-keyint", str(f["frames"])]
    args += X265_COMMON + quantiser + x265_colour(f)
    if f["signal"] == "pq":
        md, max_cll, max_fall = static_metadata(f) if static is None else static
        args += ["--master-display", md, "--max-cll", "%d,%d" % (max_cll, max_fall)]
    args += ["-o", path]
    env = {"LC_ALL": "C", "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
    with open(log, "wb") as err:
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=err, env=env)
        try:
            for _ in range(f["frames"]):
                p.stdin.write(frame)
            p.stdin.close()
        except BrokenPipeError:
            pass
        rc = p.wait()
    if rc:
        with open(log, "rb") as e:
            sys.stderr.write(e.read().decode("utf-8", "replace"))
        raise SystemExit("x265 failed for %s (%d)" % (f["id"], rc))
    return args[1:-2]  # (the options: without the program and the output, whose name is temporary)


def rounded(x, places=3, what=""):
    s = 10 ** places
    return card.rnd(x * s, what) / s


def make_clip(f, stream, out_dir):
    """the Matroska file of a format from its HEVC stream; returns its entry of clips.json"""
    nals = hevc.split_annexb(stream)
    aus = hevc.access_units(nals)
    if len(aus) != f["frames"]:
        raise SystemExit("%s: %d access units, %d pictures expected" % (f["id"], len(aus), f["frames"]))
    if any(hevc.nal_type(n) == hevc.UNSPEC62 for n in nals):
        raise SystemExit("%s: the encoder wrote a NAL unit of type 62" % f["id"])
    hv, sps = hevc.hvcc(nals)
    if (sps["width"], sps["height"], sps["bit_depth_luma"], sps["chroma_format_idc"]) != \
            (f["width"], f["height"], f["bits"], 1):
        raise SystemExit("%s: the stream is not what was asked for: %r" % (f["id"], sps))

    entry = {"file": f["file"], "format": f["id"], "label": f["label"], "width": f["width"],
             "height": f["height"], "fps": list(f["fps"]), "frames": f["frames"],
             "duration": rounded(f["frames"] * f["fps"][1] / f["fps"][0], 3, "duration"),
             "bit_depth": f["bits"], "range": f["range"],
             "colour": {"primaries": f["primaries"], "transfer": f["transfer"], "matrix": f["matrix"]}}
    mastering = cll = fall = dv = None
    if f["signal"] == "pq":
        _, cll, fall = static_metadata(f)
        m = card.MASTERING
        mastering = (P3_D65, card.D65, m["max_nits"], m["min_nits"])
        entry["mastering"] = {"primaries": [list(p) for p in P3_D65], "white": list(card.D65),
                              "max_nits": m["max_nits"], "min_nits": m["min_nits"]}
        entry["max_cll"], entry["max_fall"] = cll, fall
    profile = f.get("dovi_profile")
    if profile:
        l1_measured, l1 = card.level1(f)
        _, cll6, fall6 = static_metadata(f)
        level = dovi.dv_level(f["width"], f["height"], f["fps"])
        compat = 0 if profile == 5 else 1
        first = dovi.rpu_nal(dovi.metadata(profile, l1, card.MASTERING, cll6, fall6, True))
        rest = dovi.rpu_nal(dovi.metadata(profile, l1, card.MASTERING, cll6, fall6, False))
        # the RPU last in each access unit, as dovi_tool puts it there
        aus = [au + [first if k == 0 else rest] for k, au in enumerate(aus)]
        dv = (dovi.config_box_type(profile), dovi.config_record(profile, level, compat))
        entry["dovi"] = {"profile": profile, "level": level, "compatibility": compat,
                         "config_box": dv[0].decode(), "l1_measured": list(l1_measured), "l1": list(l1),
                         "l6": {"max_luminance": card.rnd(card.MASTERING["max_nits"], "L6"),
                                "min_luminance": card.rnd(card.MASTERING["min_nits"] * 10000, "L6"),
                                "max_cll": cll6, "max_fall": fall6},
                         "rpu_bytes": [len(first), len(rest)]}
    colour = (f["matrix"], 2 if f["range"] == "full" else 1, f["transfer"], f["primaries"])
    path = os.path.join(out_dir, f["file"])
    mkv.write(path, f, hv, [hevc.length_prefixed(au) for au in aus], colour, mastering, cll, fall, dv)

    # what the card is in this clip: the pixels of every region (in pixels of the clip; the label as
    # its bounding box) and its codes, and in HDR the light it means
    values = card.region_values(f)
    s = f["width"] // card.GRID_W
    rects = {n: [c * s for c in r] for n, r in card.regions()}
    dots = card.label_dots(f["label"])
    rects["label"] = [min(d[0] for d in dots) * s, min(d[1] for d in dots) * s,
                      max(d[2] for d in dots) * s, max(d[3] for d in dots) * s]
    rects["background"] = [0, 0, f["width"], f["height"]]
    regions = {}
    for n, v in values.items():
        r = {"rect": rects[n], "codes": list(v.codes)}
        if v.light is not None:
            r["light"] = [rounded(c, 3, n) for c in v.light]
        regions[n] = r
    entry["regions"] = regions
    entry["bytes"] = os.path.getsize(path)
    with open(path, "rb") as fh:
        entry["sha256"] = hashlib.sha256(fh.read()).hexdigest()
    return entry


def ab_static(f):
    """the HDR10 static metadata of the A/B clip: the mastering display of the card, its own light"""
    md, _, _ = static_metadata(card.fmt("hdr10"))
    peak, mean = abclip.light_stats(f)
    return md, card.rnd(peak, "MaxCLL"), card.rnd(mean, "MaxFALL")


def make_ab_clip(x265, ffprobe, out_dir, tmp, frames=0):
    """the clip of the A/B comparison (abclip.py) and its entry of clips.json ("ab")"""
    f = abclip.fmt()
    if frames:
        f["frames"] = frames
    values = abclip.region_values()
    static = ab_static(f)
    hevc_path = os.path.join(tmp, f["id"] + ".hevc")
    args = encode(x265, f, hevc_path, os.path.join(tmp, f["id"] + ".log"), QUANTISER[f["id"]],
                  planes=card.planes(f, values), static=static)
    with open(hevc_path, "rb") as fh:
        nals = hevc.split_annexb(fh.read())
    aus = hevc.access_units(nals)
    if len(aus) != f["frames"]:
        raise SystemExit("%s: %d access units, %d pictures expected" % (f["id"], len(aus), f["frames"]))
    hv, sps = hevc.hvcc(nals)
    if (sps["width"], sps["height"], sps["bit_depth_luma"], sps["chroma_format_idc"]) != (f["width"], f["height"], 10, 1):
        raise SystemExit("%s: the stream is not what was asked for: %r" % (f["id"], sps))
    _, cll, fall = static
    l1_measured, l1 = abclip.level1(f)
    l2, l5 = abclip.level2(), abclip.level5(f)
    first = dovi.rpu_nal(dovi.metadata(8, l1, card.MASTERING, cll, fall, True, l2, l5))
    rest = dovi.rpu_nal(dovi.metadata(8, l1, card.MASTERING, cll, fall, False, l2, l5))
    aus = [au + [first if k == 0 else rest] for k, au in enumerate(aus)]
    level = dovi.dv_level(f["width"], f["height"], f["fps"])
    dv = (dovi.config_box_type(8), dovi.config_record(8, level, 1))
    m = card.MASTERING
    mastering = (P3_D65, card.D65, m["max_nits"], m["min_nits"])
    path = os.path.join(out_dir, f["file"])
    mkv.write(path, f, hv, [hevc.length_prefixed(au) for au in aus], (9, 1, 16, 9), mastering, cll, fall, dv)
    if ffprobe:
        self_check(ffprobe, path, f)
    s = f["width"] // card.GRID_W
    regions = {n: {"rect": [c * s for c in r], "codes": list(values[n].codes),
                   "light": [rounded(c, 3, n) for c in values[n].light]} for n, r in abclip.regions()}
    with open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    return {"file": f["file"], "format": f["id"], "label": f["label"], "width": f["width"], "height": f["height"],
            "fps": list(f["fps"]), "frames": f["frames"],
            "duration": rounded(f["frames"] * f["fps"][1] / f["fps"][0], 3, "duration"), "bit_depth": 10,
            "range": "limited", "colour": {"primaries": 9, "transfer": 16, "matrix": 9},
            "max_cll": cll, "max_fall": fall,
            "dovi": {"profile": 8, "level": level, "compatibility": 1, "l1_measured": list(l1_measured),
                     "l1": list(l1), "l2": l2, "l5": l5, "rpu_bytes": [len(first), len(rest)]},
            "regions": regions, "x265": args, "bytes": os.path.getsize(path), "sha256": digest}


def make_fp_clip(x265, ffprobe, out_dir, tmp, frames=0):
    """the 3D clip of the frame packing card (fpclip.py) and its entry of clips.json ("fp")"""
    f = fpclip.fmt()
    if frames:
        f["frames"] = frames
    hevc_path = os.path.join(tmp, f["id"] + ".hevc")
    args = encode(x265, f, hevc_path, os.path.join(tmp, f["id"] + ".log"), QUANTISER[f["id"]],
                  planes=fpclip.planes())
    with open(hevc_path, "rb") as fh:
        nals = hevc.split_annexb(fh.read())
    aus = hevc.access_units(nals)
    if len(aus) != f["frames"]:
        raise SystemExit("%s: %d access units, %d pictures expected" % (f["id"], len(aus), f["frames"]))
    hv, sps = hevc.hvcc(nals)
    if (sps["width"], sps["height"], sps["bit_depth_luma"], sps["chroma_format_idc"]) != (f["width"], f["height"], 8, 1):
        raise SystemExit("%s: the stream is not what was asked for: %r" % (f["id"], sps))
    path = os.path.join(out_dir, f["file"])
    mkv.write(path, f, hv, [hevc.length_prefixed(au) for au in aus], (1, 1, 1, 1), stereo=fpclip.STEREO_MODE)
    if ffprobe:
        self_check(ffprobe, path, f)
    with open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    return {"file": f["file"], "format": f["id"], "label": f["label"], "width": f["width"], "height": f["height"],
            "fps": list(f["fps"]), "frames": f["frames"],
            "duration": rounded(f["frames"] * f["fps"][1] / f["fps"][0], 3, "duration"), "bit_depth": 8,
            "range": "limited", "colour": {"primaries": 1, "transfer": 1, "matrix": 1},
            "stereo_mode": fpclip.STEREO_NAME, "eyes": list(fpclip.LABELS), "x265": args,
            "bytes": os.path.getsize(path), "sha256": digest}


def hdr3d_static():
    """the HDR10 static metadata of the HDR 3D clip: the mastering display of the card; MaxCLL the peak of the card,
    MaxFALL the mean of its two eyes (each half of the frame is one eye, squeezed on whole chroma samples, its label
    included)"""
    md, _, _ = static_metadata(card.fmt("hdr10"))
    stats = [card.light_stats(dict(hdr3dclip.base(), label=label)) for label in hdr3dclip.LABELS]
    return md, card.rnd(max(p for p, _ in stats), "MaxCLL"), card.rnd(sum(m for _, m in stats) / 2, "MaxFALL")


def make_hdr3d_clip(x265, ffprobe, out_dir, tmp, frames=0):
    """the HDR 3D clip of the card "3D: HDR" (hdr3dclip.py) and its entry of clips.json ("hdr3d")"""
    f = hdr3dclip.fmt()
    if frames:
        f["frames"] = frames
    static = hdr3d_static()
    hevc_path = os.path.join(tmp, f["id"] + ".hevc")
    args = encode(x265, f, hevc_path, os.path.join(tmp, f["id"] + ".log"), QUANTISER[f["id"]],
                  planes=hdr3dclip.planes(), static=static)
    with open(hevc_path, "rb") as fh:
        nals = hevc.split_annexb(fh.read())
    aus = hevc.access_units(nals)
    if len(aus) != f["frames"]:
        raise SystemExit("%s: %d access units, %d pictures expected" % (f["id"], len(aus), f["frames"]))
    hv, sps = hevc.hvcc(nals)
    if (sps["width"], sps["height"], sps["bit_depth_luma"], sps["chroma_format_idc"]) != (f["width"], f["height"], 10, 1):
        raise SystemExit("%s: the stream is not what was asked for: %r" % (f["id"], sps))
    _, cll, fall = static
    m = card.MASTERING
    mastering = (P3_D65, card.D65, m["max_nits"], m["min_nits"])
    path = os.path.join(out_dir, f["file"])
    mkv.write(path, f, hv, [hevc.length_prefixed(au) for au in aus], (9, 1, 16, 9), mastering, cll, fall,
              stereo=hdr3dclip.STEREO_MODE)
    if ffprobe:
        self_check(ffprobe, path, f)
    with open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    return {"file": f["file"], "format": f["id"], "label": f["label"], "width": f["width"], "height": f["height"],
            "fps": list(f["fps"]), "frames": f["frames"],
            "duration": rounded(f["frames"] * f["fps"][1] / f["fps"][0], 3, "duration"), "bit_depth": 10,
            "range": "limited", "colour": {"primaries": 9, "transfer": 16, "matrix": 9},
            "mastering": {"primaries": [list(p) for p in P3_D65], "white": list(card.D65),
                          "max_nits": m["max_nits"], "min_nits": m["min_nits"]},
            "max_cll": cll, "max_fall": fall,
            "stereo_mode": hdr3dclip.STEREO_NAME, "eyes": list(hdr3dclip.LABELS), "x265": args,
            "bytes": os.path.getsize(path), "sha256": digest}


def self_check(ffprobe, path, f):
    """a quick look with the host's ffprobe at what a decoder sees in the file (the full checks are the
    tests of this directory)"""
    out = subprocess.run([ffprobe, "-v", "error", "-show_streams", "-show_entries",
                          "stream=width,height,pix_fmt,color_range,color_space,color_transfer,color_primaries,"
                          "nb_read_frames:stream_side_data", "-count_frames", "-of", "json", path],
                         check=True, stdout=subprocess.PIPE).stdout
    st = json.loads(out.decode())["streams"][0]
    want_pix = "yuv420p" if f["bits"] == 8 else "yuv420p10le"
    problems = []
    if (st.get("width"), st.get("height"), st.get("pix_fmt")) != (f["width"], f["height"], want_pix):
        problems.append("size or pixel format %r" % ((st.get("width"), st.get("height"), st.get("pix_fmt")),))
    if int(st.get("nb_read_frames", 0)) != f["frames"]:
        problems.append("%s pictures" % st.get("nb_read_frames"))
    side = [d.get("side_data_type") for d in st.get("side_data_list", [])]
    has_dovi = any("DOVI" in (t or "") for t in side)
    if bool(f.get("dovi_profile")) != has_dovi:
        problems.append("Dolby Vision configuration %s" % ("missing" if not has_dovi else "unexpected"))
    if problems:
        raise SystemExit("%s: %s" % (f["file"], "; ".join(problems)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x265", required=True)
    ap.add_argument("--ffprobe")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--frames", type=int, default=0, help="(tests) fewer pictures a clip")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    only = [x for x in a.only.split(",") if x]
    entries = []
    with tempfile.TemporaryDirectory(prefix="hades-clips-") as tmp:
        for f0 in card.FORMATS:
            if only and f0["id"] not in only:
                continue
            f = dict(f0)
            if a.frames:
                f["frames"] = a.frames
            hevc_path = os.path.join(tmp, f["id"] + ".hevc")
            args = encode(a.x265, f, hevc_path, os.path.join(tmp, f["id"] + ".log"), QUANTISER[f["id"]])
            with open(hevc_path, "rb") as fh:
                stream = fh.read()
            entry = make_clip(f, stream, a.out)
            entry["x265"] = args
            if a.ffprobe:
                self_check(a.ffprobe, os.path.join(a.out, f["file"]), f)
            entries.append(entry)
            print("%s: %d bytes" % (f["file"], entry["bytes"]), flush=True)
        ab = None
        if not only or abclip.FORMAT in only:
            ab = make_ab_clip(a.x265, a.ffprobe, a.out, tmp, a.frames)
            print("%s: %d bytes" % (ab["file"], ab["bytes"]), flush=True)
        fp = None
        if not only or fpclip.FORMAT in only:
            fp = make_fp_clip(a.x265, a.ffprobe, a.out, tmp, a.frames)
            print("%s: %d bytes" % (fp["file"], fp["bytes"]), flush=True)
        hdr3d = None
        if not only or hdr3dclip.FORMAT in only:
            hdr3d = make_hdr3d_clip(a.x265, a.ffprobe, a.out, tmp, a.frames)
            print("%s: %d bytes" % (hdr3d["file"], hdr3d["bytes"]), flush=True)
    doc = {"version": 1, "card": {"grid": [card.GRID_W, card.GRID_H], "reference_white_nits": card.REF_WHITE},
           "clips": entries}
    if ab:
        doc["ab"] = ab
    if fp:
        doc["fp"] = fp
    if hdr3d:
        doc["hdr3d"] = hdr3d
    with open(os.path.join(a.out, "clips.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, indent=1, sort_keys=True)
        fh.write("\n")


if __name__ == "__main__":
    main()
