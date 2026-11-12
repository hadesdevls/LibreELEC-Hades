# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hades
"""Host tests of the picture clips of "Check this setup" (packages/mediacenter/hades-check/clips).

usage: check_clips_tests.py --clips <clips dir> --x265 <x265> --ffmpeg <ffmpeg> --ffprobe <ffprobe>
                            --dovi-compare <program> --kodi <Kodi tree> --work <dir>
                            --gen1 <dir> --gen2 <dir> [--mutants] [--only <check,...>]
gen1 and gen2 are two generations of the same clips, made apart (make_clips.sh --video-only, or with the
sounds); the checks look at gen1 and compare the two. Every check prints PASS or FAIL with what it
measured. With --mutants, each check is then run against clips made wrong on purpose, one fault each,
and must fail there (and pass on a control made the same way without the fault): "CAUGHT" or "MISSED".
The last line is the verdict; the exit status is 0 only when every check passed and every mutant was
caught.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyze  # noqa: E402

A = None
RESULTS = []
MUTANTS = []
FORMATS = ("sdr", "hdr10", "hlg", "dv81", "dv5")
SIZE_LIMIT = 4 * 1024 * 1024  # all the clips of the check, pictures and sounds


def say(*a):
    print(*a, flush=True)


def record(name, ok, detail=""):
    RESULTS.append((name, ok))
    say("%s %s%s" % ("PASS" if ok else "FAIL", name, (": " + detail) if detail else ""))
    return ok


def import_clips(clips_dir, extra=()):
    """fresh modules of the generator (card, dovi, hevc, mkv and the extra ones), not shared with any
    earlier import, so that a mutant's changes stay in its own modules"""
    names = ("card", "dovi", "hevc", "mkv") + tuple(extra)
    for m in names + ("gen_video",):
        sys.modules.pop(m, None)
    sys.path.insert(0, clips_dir)
    try:
        mods = {m: __import__(m) for m in names}
    finally:
        sys.path.remove(clips_dir)
    for m in names + ("gen_video",):
        sys.modules.pop(m, None)
    return mods


def run(args, **kw):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)


def sha256(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def ffprobe_json(path, *extra):
    out = run([A.ffprobe, "-v", "error", "-of", "json"] + list(extra) + [path]).stdout
    return json.loads(out.decode())


def clips_doc(gen):
    with open(os.path.join(gen, "clips.json"), encoding="utf-8") as fh:
        return json.load(fh)


def frac(x):
    if x is None:
        return None
    x = str(x)
    if "/" in x:
        n, d = x.split("/")
        return float(n) / float(d)
    return float(x)


def raw_stream(path, name):
    """the HEVC stream of a clip with start codes, as FFmpeg takes it out of the file (its own reading
    of the container: nothing of the generator's reader)"""
    out = os.path.join(A.work, "raw-%s.hevc" % name)
    run([A.ffmpeg, "-v", "error", "-nostdin", "-y", "-i", path, "-c", "copy", "-bsf:v", "hevc_mp4toannexb",
         "-f", "hevc", out])
    with open(out, "rb") as fh:
        return out, fh.read()


def sei_types(nal, unescape):
    """the payload types of a SEI NAL unit (ITU-T H.265 7.3.5)"""
    rbsp = unescape(nal)[2:]
    types, i = [], 0
    while i < len(rbsp) and not (rbsp[i] == 0x80 and i == len(rbsp) - 1):
        t = 0
        while rbsp[i] == 0xFF:
            t += 255
            i += 1
        t += rbsp[i]
        i += 1
        s = 0
        while rbsp[i] == 0xFF:
            s += 255
            i += 1
        s += rbsp[i]
        i += 1
        types.append(t)
        i += s
    return types


# ---------------------------------------------------------------------------------------------------
# the checks: each takes a generated folder and the reference modules, and records PASS or FAIL


def check_determinism(gen1, gen2, prefix=""):
    f1, f2 = sorted(os.listdir(gen1)), sorted(os.listdir(gen2))
    differ = [f for f in f1 if f not in f2 or sha256(os.path.join(gen1, f)) != sha256(os.path.join(gen2, f))]
    differ += [f for f in f2 if f not in f1]
    detail = ("%d files, the same bytes (clips.json and SHA256SUMS included)" % len(f1) if not differ else
              "the two generations differ in %s" % differ)
    return record(prefix + "determinism: two generations", not differ, detail)


def check_sums(gen, mods, prefix=""):
    """SHA256SUMS lists every other file of the folder with its digest, and clips.json gives each clip's
    digest and size"""
    path = os.path.join(gen, "SHA256SUMS")
    bad = []
    listed = {}
    if not os.path.exists(path):
        bad.append("no SHA256SUMS")
    else:
        for line in open(path, encoding="utf-8").read().splitlines():
            m = re.fullmatch(r"([0-9a-f]{64})  (\S+)", line)
            if not m:
                bad.append("line %r" % line)
                continue
            listed[m.group(2)] = m.group(1)
    files = sorted(f for f in os.listdir(gen) if f != "SHA256SUMS")
    if sorted(listed) != files:
        bad.append("listed %s, in the folder %s" % (sorted(listed), files))
    bad += ["%s: digest" % f for f in files if f in listed and listed[f] != sha256(os.path.join(gen, f))]
    for e in clips_doc(gen)["clips"]:
        p = os.path.join(gen, e["file"])
        if not os.path.exists(p) or e.get("sha256") != sha256(p) or e.get("bytes") != os.path.getsize(p):
            bad.append("clips.json: digest or size of %s" % e["file"])
    return record(prefix + "SHA256SUMS and the digests of clips.json", not bad,
                  "%d files listed and right" % len(listed) if not bad else "; ".join(bad))


def check_clips_json(gen, mods, prefix=""):
    """what the settings add-on reads (file, format, duration) and what the clip is, against the table of
    formats of the generator"""
    card = mods["card"]
    doc = clips_doc(gen)
    bad = []
    if doc.get("version") != 1:
        bad.append("version %r" % doc.get("version"))
    clips = doc.get("clips", [])
    if [e.get("format") for e in clips] != list(FORMATS):
        bad.append("formats %r" % [e.get("format") for e in clips])
    for e in clips:
        f = card.fmt(e["format"])
        if not os.path.isfile(os.path.join(gen, e.get("file", ""))) or e["file"] != "hades-check-%s.mkv" % f["id"]:
            bad.append("%s: file %r" % (f["id"], e.get("file")))
        want = {"label": f["label"], "width": f["width"], "height": f["height"], "fps": list(f["fps"]),
                "frames": f["frames"], "bit_depth": f["bits"], "range": f["range"],
                "colour": {"primaries": f["primaries"], "transfer": f["transfer"], "matrix": f["matrix"]}}
        for k, v in want.items():
            if e.get(k) != v:
                bad.append("%s: %s %r, want %r" % (f["id"], k, e.get(k), v))
        dur = e.get("duration")
        exact = e["frames"] * e["fps"][1] / e["fps"][0]
        if not isinstance(dur, (int, float)) or abs(dur - exact) > 0.0005 or not 14.0 <= dur <= 16.0:
            bad.append("%s: duration %r (%d pictures at %d/%d: %.4f s; about 15 s wanted)" %
                       (f["id"], dur, e["frames"], e["fps"][0], e["fps"][1], exact))
        if (f["signal"] == "pq") != ("mastering" in e and "max_cll" in e and "max_fall" in e):
            bad.append("%s: HDR10 static metadata %s" % (f["id"], "missing" if f["signal"] == "pq" else "where none belongs"))
        if bool(f.get("dovi_profile")) != ("dovi" in e):
            bad.append("%s: Dolby Vision entry" % f["id"])
    return record(prefix + "clips.json", not bad,
                  "%d clips: %s" % (len(clips), ", ".join("%s %s s" % (e["format"], e["duration"]) for e in clips))
                  if not bad else "; ".join(bad))


def check_streams(gen, mods, prefix=""):
    """each clip as FFmpeg reads it: the track (container colour, HDR10 static metadata, Dolby Vision
    record), its packets (count, timing, one key frame), and the stream itself without the container
    (the VUI of the SPS, the SEI of the first picture)"""
    card, dovi = mods["card"], mods["dovi"]
    ok_all = True
    for e in clips_doc(gen)["clips"]:
        f = dict(card.fmt(e["format"]), frames=e["frames"])
        p = os.path.join(gen, e["file"])
        bad = {}
        st = ffprobe_json(p, "-show_streams", "-count_frames")["streams"]
        if len(st) != 1:
            bad["streams"] = (len(st), 1)
        st = st[0]
        want = {"codec_name": "hevc", "profile": "Main" if f["bits"] == 8 else "Main 10",
                "width": f["width"], "height": f["height"],
                "pix_fmt": "yuv420p" if f["bits"] == 8 else "yuv420p10le",
                "r_frame_rate": "%d/%d" % f["fps"], "nb_read_frames": str(f["frames"]),
                "field_order": "progressive"}
        for k, v in want.items():
            if str(st.get(k)) != str(v):
                bad["track " + k] = (st.get(k), v)
        side = {d.get("side_data_type"): d for d in st.get("side_data_list", [])}
        dv = side.get("DOVI configuration record")
        if f.get("dovi_profile"):
            want_dv = {"dv_version_major": 1, "dv_version_minor": 0, "dv_profile": f["dovi_profile"],
                       "dv_level": dovi.dv_level(f["width"], f["height"], f["fps"]), "rpu_present_flag": 1,
                       "el_present_flag": 0, "bl_present_flag": 1,
                       "dv_bl_signal_compatibility_id": 0 if f["dovi_profile"] == 5 else 1}
            for k, v in want_dv.items():
                if (dv or {}).get(k) != v:
                    bad["Dolby Vision record " + k] = ((dv or {}).get(k), v)
        elif dv:
            bad["a Dolby Vision record where none belongs"] = (dv, None)
        want_md = {"red_x": "34000/50000", "red_y": "16000/50000", "green_x": "13250/50000",
                   "green_y": "34500/50000", "blue_x": "7500/50000", "blue_y": "3000/50000",
                   "white_point_x": "15635/50000", "white_point_y": "16450/50000",
                   "min_luminance": "50/10000", "max_luminance": "10000000/10000"}
        # MaxCLL and MaxFALL of the clip's own picture (the reference card), and clips.json says the same
        if f["signal"] == "pq":
            peak, mean = card.light_stats(f)
            want_cll = (card.rnd(peak, "MaxCLL"), card.rnd(mean, "MaxFALL"))
            if (e.get("max_cll"), e.get("max_fall")) != want_cll:
                bad["clips.json MaxCLL, MaxFALL"] = ((e.get("max_cll"), e.get("max_fall")), want_cll)

        def static(sd, where):
            mdm, cll = sd.get("Mastering display metadata"), sd.get("Content light level metadata")
            if f["signal"] == "pq":
                for k, v in want_md.items():
                    if mdm is None or abs(frac(mdm.get(k)) - frac(v)) > 1e-9:
                        bad["mastering display (%s) %s" % (where, k)] = ((mdm or {}).get(k), v)
                got = (cll or {}).get("max_content"), (cll or {}).get("max_average")
                if got != want_cll:
                    bad["content light (%s)" % where] = (got, want_cll)
            elif mdm or cll:
                bad["HDR10 static metadata (%s) where none belongs" % where] = (sorted(sd), None)
        static(side, "container")

        # the packets: one a picture, at the clip's rate, the first the only key frame
        pk = ffprobe_json(p, "-show_packets", "-show_entries", "packet=pts_time,flags")["packets"]
        times = [float(x["pts_time"]) for x in pk]
        keys = [i for i, x in enumerate(pk) if "K" in x.get("flags", "")]
        steps = [b - a for a, b in zip(times, times[1:])]
        period = f["fps"][1] / f["fps"][0]
        if len(pk) != f["frames"] or keys != [0] or any(abs(s - period) > 0.0011 for s in steps):
            bad["packets"] = ((len(pk), keys[:3], (min(steps), max(steps)) if steps else None),
                              (f["frames"], [0], period))

        # the stream without its container: the VUI and the SEI are the encoder's
        raw, _ = raw_stream(p, f["id"])
        fr = ffprobe_json(raw, "-show_frames", "-read_intervals", "%+#1")["frames"][0]
        vui = {"color_range": "pc" if f["range"] == "full" else "tv",
               "color_primaries": {1: "bt709", 9: "bt2020"}[f["primaries"]],
               "color_transfer": {1: "bt709", 16: "smpte2084", 18: "arib-std-b67"}[f["transfer"]],
               "color_space": {1: "bt709", 9: "bt2020nc", 15: "ipt-c2"}[f["matrix"]]}
        for k, v in vui.items():
            if fr.get(k) != v:
                bad["VUI " + k] = (fr.get(k), v)
        fside = {d.get("side_data_type"): d for d in fr.get("side_data_list", [])}
        static(fside, "SEI of the first picture")
        if bool(f.get("dovi_profile")) != ("Dolby Vision Metadata" in fside):
            bad["RPU of the first picture"] = (sorted(fside), f.get("dovi_profile"))
        ok_all &= record("%sstream %s" % (prefix, e["file"]), not bad,
                         "as declared (track, %d packets, VUI, SEI)" % len(pk) if not bad else
                         "; ".join("%s: got %r want %r" % (k, g, w) for k, (g, w) in bad.items()))
    return ok_all


def check_units(gen, mods, prefix=""):
    """every access unit, as FFmpeg takes the stream out of the file: Dolby Vision clips one RPU (a NAL
    unit of type 62) in each, the last unit; the others none; no SEI that names the encoder or its
    options (user data unregistered); in HDR10 and profile 8.1 the mastering display and content light
    SEI in the first access unit"""
    card, hevc = mods["card"], mods["hevc"]
    ok_all = True
    for e in clips_doc(gen)["clips"]:
        f = card.fmt(e["format"])
        _, data = raw_stream(os.path.join(gen, e["file"]), f["id"])
        aus = hevc.access_units(hevc.split_annexb(data))
        bad = []
        if len(aus) != e["frames"]:
            bad.append("%d access units" % len(aus))
        rpus = [[i for i, n in enumerate(au) if hevc.nal_type(n) == hevc.UNSPEC62] for au in aus]
        if f.get("dovi_profile"):
            wrong = [k for k, (au, r) in enumerate(zip(aus, rpus)) if r != [len(au) - 1]]
            if wrong:
                bad.append("access units without one RPU last: %s" % wrong[:5])
        elif any(rpus):
            bad.append("RPUs in access units %s" % [k for k, r in enumerate(rpus) if r][:5])
        seis = [sei_types(n, hevc.unescape) for au in aus for n in au if hevc.nal_type(n) in (hevc.SEI_PREFIX, hevc.SEI_SUFFIX)]
        if any(5 in s for s in seis):
            bad.append("a SEI of user data unregistered (the encoder's own)")
        first = [t for n in aus[0] if hevc.nal_type(n) == hevc.SEI_PREFIX for t in sei_types(n, hevc.unescape)]
        if f["signal"] == "pq" and not (137 in first and 144 in first):
            bad.append("the first access unit's SEI: %s" % first)
        if f["signal"] != "pq" and (137 in first or 144 in first):
            bad.append("HDR10 SEI where none belongs: %s" % first)
        ok_all &= record("%sunits %s" % (prefix, e["file"]), not bad,
                         "%d access units, %s" % (len(aus), "one RPU last in each" if f.get("dovi_profile") else "no RPU")
                         if not bad else "; ".join(bad))
    return ok_all


def check_rpu(gen, mods, prefix=""):
    """the Dolby Vision clips through dovi_compare (tests/dovi_compare.c: FFmpeg's reading and Kodi's,
    against dvbridge_profile81_metadata() and dovi_tool's profile 5)"""
    card = mods["card"]
    ok_all = True
    for e in clips_doc(gen)["clips"]:
        if "dovi" not in e:
            continue
        f = card.fmt(e["format"])
        # the reference values, from the reference card: level 1 and the light of the clip's own picture
        l1 = card.level1(f)[1]
        peak, mean = card.light_stats(f)
        args = [A.dovi_compare, os.path.join(gen, e["file"]), str(f["dovi_profile"]),
                str(mods["dovi"].dv_level(f["width"], f["height"], f["fps"])), str(0 if f["dovi_profile"] == 5 else 1),
                str(l1[0]), str(l1[1]), str(l1[2]), str(card.rnd(card.MASTERING["max_nits"], "max")),
                str(card.rnd(card.MASTERING["min_nits"] * 10000, "min")), str(card.rnd(peak, "MaxCLL")),
                str(card.rnd(mean, "MaxFALL")), str(e["frames"])]
        p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode(errors="replace").strip().splitlines()
        last = out[-1] if out else "(no output)"
        fails = [l for l in out if l.startswith("FAIL ")][:4]
        ok_all &= record("%sRPU and record of %s, as FFmpeg and Kodi read them" % (prefix, e["file"]),
                         p.returncode == 0 and last.startswith("PASS"), last + ("" if not fails else " | " + " | ".join(fails)))
    return ok_all


def check_pixels(gen, mods, prefix=""):
    """every picture of every clip, decoded, against the card: every code exact (the first and the last
    are measured by distance from the edges of the regions, for the report)"""
    card = mods["card"]
    analyze.card = card
    ok_all, report = True, {}
    for e in clips_doc(gen)["clips"]:
        f = dict(card.fmt(e["format"]), frames=e["frames"])
        exp = analyze.expected_planes(f)
        n = exact = 0
        first = last = None
        for planes in analyze.decoded_pictures(A.ffmpeg, os.path.join(gen, e["file"]), f):
            if all((planes[c] == exp[c]).all() for c in range(3)):
                exact += 1
            if n == 0:
                first = planes
            last = planes
            n += 1
        ids, names = analyze.region_ids(f)
        prof = [analyze.error_profile(exp, pic, ids, names, 24) for pic in (first, last)] if n else []
        report[e["file"]] = {"pictures": n, "exact": exact,
                             "first": {"exact_from": [r["exact_from"] for r in prof[0]], "max": [r["max"] for r in prof[0]]} if prof else None,
                             "last": {"exact_from": [r["exact_from"] for r in prof[1]], "max": [r["max"] for r in prof[1]]} if prof else None}
        ok_all &= record("%spixels %s" % (prefix, e["file"]), n == f["frames"] and exact == n,
                         "%d of %d pictures exact; first picture: largest error Y/Cb/Cr %s, exact from distance %s; last: %s, %s" %
                         (exact, n, report[e["file"]]["first"]["max"] if prof else None,
                          report[e["file"]]["first"]["exact_from"] if prof else None,
                          report[e["file"]]["last"]["max"] if prof else None,
                          report[e["file"]]["last"]["exact_from"] if prof else None))
    return ok_all, report


def ipt_header_matrices():
    """the matrices of dvbridge_ipt.h, read from the Kodi tree (not from the generator)"""
    text = open(os.path.join(A.kodi, "tools/dvbridge/dvbridge_ipt.h")).read()

    def nums(name):
        m = re.search(r"#define %s \{([^}]*)\}" % name, text)
        return [int(x.strip().rstrip("u")) for x in m.group(1).split(",")]
    return nums("DVBRIDGE_IPT_YCC"), nums("DVBRIDGE_IPT_OFFSETS"), nums("DVBRIDGE_IPT_LMS"), nums("DVBRIDGE_LMS_2020")


PQ = dict(m1=2610 / 16384, m2=2523 / 4096 * 128, c1=3424 / 4096, c2=2413 / 4096 * 32, c3=2392 / 4096 * 32)


def pq_eotf(e):
    e = np.clip(e, 0, 1)
    p = e ** (1 / PQ["m2"])
    return (np.maximum(p - PQ["c1"], 0) / (PQ["c2"] - PQ["c3"] * p)) ** (1 / PQ["m1"])


def pq_oetf(y):
    y = np.clip(y, 0, 1)
    p = y ** PQ["m1"]
    return ((PQ["c1"] + PQ["c2"] * p) / (1 + PQ["c3"] * p)) ** PQ["m2"]


def check_p5_light(gen, mods, prefix=""):
    """profile 5: what the base layer carries, against the light the card means. With the matrices of
    Kodi's dvbridge_ipt.h (not the generator's), the exact I, P and T of each region's light (BT.2020 RGB
    -> LMS -> the crosstalk of profile 5 undone -> PQ -> IPT, codes as code / 1023); the decoded codes
    must be those values rounded (within half a code), but for greys, whose P and T are 512 by the
    generator's rule (the zero of the DM header, code / 1024; the exact value is a hair below 511.5).
    Reported: the light that the decoded codes give back (IPT -> L'M'S' -> LMS -> RGB, as a display does),
    in codes of 10-bit PQ of limited range (876 a unit), read as code / 1023 and as code / 1024."""
    card = mods["card"]
    values = card.region_values(card.fmt("dv5"))
    analyze.card = card
    by = {e["format"]: e for e in clips_doc(gen)["clips"]}
    if "dv5" not in by:
        return record(prefix + "profile 5 light", False, "no dv5 clip"), []
    ycc, off, lms, lms2020 = ipt_header_matrices()
    m_ycc = np.array(ycc, float).reshape(3, 3) / 8192
    m_lms = np.array(lms, float).reshape(3, 3) / 16384
    m_2020 = np.array(lms2020, float).reshape(3, 3) / 16384
    to_rgb = np.linalg.inv(m_2020) @ m_lms
    offs = np.array(off, float) / (1 << 28)

    def light_of(ipt_norm):
        return 10000.0 * (to_rgb @ pq_eotf(m_ycc @ (ipt_norm - offs)))

    def pq10(nits):
        return pq_oetf(np.maximum(nits, 0) / 10000.0) * 876 + 64

    f5 = dict(card.fmt("dv5"), frames=1)
    d5 = next(analyze.decoded_pictures(A.ffmpeg, os.path.join(gen, by["dv5"]["file"]), f5, count=1))
    rows, worst, worst1024, worst_code, over = [], 0.0, 0.0, 0.0, []
    for name, r in sorted(by["dv5"]["regions"].items()):
        if name == "label":
            continue  # (its rectangle is the bounding box of the dots)
        x0, y0, x1, y1 = r["rect"] if name != "background" else (0, 0, 32, 32)
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        codes = np.array([d5[0][cy, cx], d5[1][cy // 2, cx // 2], d5[2][cy // 2, cx // 2]], float)
        light = np.array(values[name].light, float)
        neutral = light[0] == light[1] == light[2]
        # the exact IPT of the light the card means, with the header's matrices inverted
        lmsp = pq_oetf(np.linalg.inv(to_rgb) @ (light / 10000.0))
        exact = (np.linalg.inv(m_ycc) @ lmsp + offs) * 1023.0
        if neutral:
            off_by = [abs(codes[0] - exact[0])]
            ok_codes = off_by[0] <= 0.5 + 1e-9 and codes[1] == 512 and codes[2] == 512
        else:
            off_by = list(np.abs(codes - exact))
            ok_codes = max(off_by) <= 0.5 + 1e-9
        worst_code = max(worst_code, max(off_by))
        want = pq10(light)
        diff = float(np.max(np.abs(pq10(light_of(codes / 1023.0)) - want)))
        diff1024 = float(np.max(np.abs(pq10(light_of(codes / 1024.0)) - want)))
        worst, worst1024 = max(worst, diff), max(worst1024, diff1024)
        if not ok_codes:
            over.append("%s: codes %s, exact %s" % (name, codes.astype(int).tolist(), np.round(exact, 3).tolist()))
        rows.append("%s: codes %s, exact IPT %s%s; light %s cd/m2 = 10-bit PQ %s; given back %.3f codes off "
                    "(as code / 1024: %.3f)" %
                    (name, codes.astype(int).tolist(), np.round(exact, 3).tolist(), " (grey: P, T 512)" if neutral else "",
                     np.round(light, 3).tolist(), np.round(want, 2).tolist(), diff, diff1024))
    ok = record(prefix + "profile 5 light against the card", not over,
                "every region's I, P, T the rounding of its exact IPT (largest distance %.3f codes; greys P = T = 512); "
                "the light they give back within %.3f codes of 10-bit PQ (as code / 1024: %.3f)" %
                (worst_code, worst, worst1024) if not over else "; ".join(over))
    return ok, rows


def check_dv81_base_layer(gen, mods, prefix=""):
    """profile 8.1: its base layer is the HDR10 picture (the same codes, decoded, everywhere but the
    label, which names the clip), with the same parameter sets (VPS, SPS and PPS identical)"""
    card, hevc = mods["card"], mods["hevc"]
    analyze.card = card
    by = {e["format"]: e for e in clips_doc(gen)["clips"]}
    if "dv81" not in by or "hdr10" not in by:
        return record(prefix + "profile 8.1 base layer is HDR10's picture", False, "dv81 or hdr10 missing")
    fa, fb = card.fmt("hdr10"), card.fmt("dv81")
    a = next(analyze.decoded_pictures(A.ffmpeg, os.path.join(gen, by["hdr10"]["file"]), fa, count=1))
    b = next(analyze.decoded_pictures(A.ffmpeg, os.path.join(gen, by["dv81"]["file"]), fb, count=1))
    s = fa["width"] // card.GRID_W
    boxes = [d for lab in (fa["label"], fb["label"]) for d in card.label_dots(lab)]
    x0, y0 = min(d[0] for d in boxes) * s, min(d[1] for d in boxes) * s
    x1, y1 = max(d[2] for d in boxes) * s, max(d[3] for d in boxes) * s
    differ = 0
    for c in range(3):
        k = 1 if c == 0 else 2
        mask = np.ones(a[c].shape, bool)
        mask[y0 // k:y1 // k, x0 // k:x1 // k] = False
        differ += int(np.count_nonzero((a[c] != b[c]) & mask))
    ps = lambda raw: [n for n in hevc.split_annexb(raw) if hevc.nal_type(n) in (hevc.VPS, hevc.SPS, hevc.PPS)]
    same_ps = ps(raw_stream(os.path.join(gen, by["hdr10"]["file"]), "bl-hdr10")[1]) == \
        ps(raw_stream(os.path.join(gen, by["dv81"]["file"]), "bl-dv81")[1])
    return record(prefix + "profile 8.1 base layer is HDR10's picture", differ == 0 and same_ps,
                  "%d samples differ outside the labels (box %d,%d-%d,%d); VPS, SPS and PPS identical: %s" %
                  (differ, x0, y0, x1, y1, same_ps))


def track_of(path, mkv):
    data = open(path, "rb").read()
    seg = [x for x in mkv.elements(data) if x[0] == mkv.SEGMENT][0]
    for i, s, t in mkv.elements(data, seg[1], seg[2]):
        if i == mkv.TRACKS:
            # (FFmpeg's muxer puts a CRC-32 element first in each element of the top level)
            entry = [x for x in mkv.elements(data, s, t) if x[0] == mkv.TRACK_ENTRY][0]
            out = {}
            for j, a, b in mkv.elements(data, entry[1], entry[2]):
                out[j] = data[a:b]
                if j == mkv.VIDEO:
                    for k, c, d in mkv.elements(data, a, b):
                        if k == mkv.COLOUR:
                            out["colour"] = {m: int.from_bytes(data[x:y], "big") for m, x, y in mkv.elements(data, c, d)
                                             if m in (mkv.MATRIX, mkv.RANGE, mkv.TRANSFER, mkv.PRIMARIES)}
            return out
    return {}


def check_hvcc_against_ffmpeg(gen, mods, prefix=""):
    """the track's CodecPrivate (hvcC) and colour against what FFmpeg's Matroska muxer writes for the same
    HEVC stream"""
    mkv, hevc = mods["mkv"], mods["hevc"]
    ok_all = True
    for e in clips_doc(gen)["clips"]:
        p = os.path.join(gen, e["file"])
        raw, data = raw_stream(p, "hvcc-" + e["format"])
        # hevc_mp4toannexb puts the parameter sets of the track's record before the key frame, ahead of the
        # ones the stream carries itself: each once, as the encoder wrote the stream
        seen, units = set(), []
        for n in hevc.split_annexb(data):
            if hevc.nal_type(n) in (hevc.VPS, hevc.SPS, hevc.PPS):
                if n in seen:
                    continue
                seen.add(n)
            units.append(n)
        with open(raw, "wb") as fh:
            fh.write(hevc.annexb(units))
        tmp = os.path.join(A.work, "ffmpeg-" + e["file"])
        run([A.ffmpeg, "-v", "error", "-nostdin", "-y", "-fflags", "+bitexact", "-r", "%d/%d" % tuple(e["fps"]),
             "-i", raw, "-c", "copy", "-fflags", "+bitexact", tmp])
        mine, theirs = track_of(p, mkv), track_of(tmp, mkv)
        same_cp = mine.get(mkv.CODEC_PRIVATE) == theirs.get(mkv.CODEC_PRIVATE)
        same_col = mine.get("colour") == theirs.get("colour")
        ok_all &= record("%shvcC and colour of %s as FFmpeg's muxer writes them" % (prefix, e["file"]), same_cp and same_col,
                         "CodecPrivate %s; colour %s, FFmpeg %s" % ("identical" if same_cp else "DIFFERENT",
                                                                   mine.get("colour"), theirs.get("colour")))
    return ok_all


def check_sizes(gen, mods, prefix="", limit=SIZE_LIMIT):
    """everything the folder holds (the pictures, the sounds when they were made, the JSON files and
    SHA256SUMS) under the limit"""
    files = sorted(os.listdir(gen))
    sizes = {f: os.path.getsize(os.path.join(gen, f)) for f in files}
    total = sum(sizes.values())
    picture_files = {e["file"] for e in clips_doc(gen)["clips"]}
    pictures = sum(v for f, v in sizes.items() if f in picture_files)
    return record(prefix + "sizes", total < limit,
                  "%s; the picture clips %d bytes, everything %d bytes (%.2f MiB), the limit %d" %
                  (sizes, pictures, total, total / 1048576.0, limit))


def check_audio_guard(clips_dir, prefix="", script=None):
    """make_clips.sh stops when make_audio.py is missing and --video-only is not given: at once, before
    any picture is made, and says why"""
    out = os.path.join(A.work, "guard-out")
    tmp = os.path.join(A.work, "guard-clips")
    for d in (out, tmp):
        if os.path.exists(d):
            shutil.rmtree(d)
    shutil.copytree(clips_dir, tmp)
    if os.path.exists(os.path.join(tmp, "make_audio.py")):
        os.remove(os.path.join(tmp, "make_audio.py"))
    if script is not None:
        with open(os.path.join(tmp, "make_clips.sh"), "w") as fh:
            fh.write(script)
    # a stand-in for the picture generator, so that the stop does not wait for any encoding
    with open(os.path.join(tmp, "gen_video.py"), "w") as fh:
        fh.write("import sys\nout = sys.argv[sys.argv.index('--out') + 1]\nopen(out + '/clips.json', 'w').write('{}')\n")
    p = subprocess.run(["sh", os.path.join(tmp, "make_clips.sh"), "--x265", A.x265, "--ffmpeg", A.ffmpeg,
                        "--ffprobe", A.ffprobe, "--python", sys.executable, "--out", out],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    msg = p.stdout.decode().strip()
    started = os.path.exists(os.path.join(out, "clips.json"))
    return record(prefix + "make_clips.sh stops without make_audio.py",
                  p.returncode != 0 and "make_audio.py is missing" in msg and not started,
                  "exit %d, %s: %s" % (p.returncode, "after making the pictures" if started else "before any picture", msg))


# ---------------------------------------------------------------------------------------------------
# mutants: clips made wrong on purpose, one fault each


def generate(out, only, frames, patch=None):
    """the pictures of some formats with the generator's own code (gen_video.main()), in this process, with
    a fault put into its modules (patch) or none"""
    if os.path.exists(out):
        shutil.rmtree(out)
    mods = import_clips(A.clips, extra=("gen_video",))
    if patch:
        patch(mods)
    argv = sys.argv
    sys.argv = ["gen_video.py", "--x265", A.x265, "--out", out, "--only", only, "--frames", str(frames)]
    try:
        mods["gen_video"].main()
    finally:
        sys.argv = argv
    return out


def copy_of(gen, name, change):
    out = os.path.join(A.work, "mut-" + name)
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(gen, out)
    change(out)
    return out


def flip_byte(path, offset_from_end=1000):
    with open(path, "r+b") as fh:
        fh.seek(-offset_from_end, 2)
        b = fh.read(1)
        fh.seek(-offset_from_end, 2)
        fh.write(bytes([b[0] ^ 0x01]))


def edit_json(path, change):
    doc = json.load(open(path, encoding="utf-8"))
    change(doc)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(doc, fh, indent=1, sort_keys=True)
        fh.write("\n")


def mutant(name, check, *args, **kw):
    """runs a check on a mutant; it must FAIL there"""
    n = len(RESULTS)
    try:
        r = check(*args, **kw)
        ok = r[0] if isinstance(r, tuple) else r
    except Exception as ex:  # a check that cannot even run on the mutant has caught it too, but say so
        say("  (the check stopped on the mutant: %s)" % ex)
        ok = False
    del RESULTS[n:]  # (a mutant's records are not checks of the clips)
    MUTANTS.append((name, not ok))
    say("%s mutant %s" % ("CAUGHT" if not ok else "MISSED", name))


def run_mutants(gen, ref):
    say("--- mutants")
    w = lambda name: os.path.join(A.work, "mut-" + name)

    # on copies of the real generation
    mutant("a byte of one clip changed: determinism", check_determinism, gen,
           copy_of(gen, "byte", lambda d: flip_byte(os.path.join(d, "hades-check-hlg.mkv"))), "mutant: ")
    mutant("clips.json written otherwise: determinism", check_determinism, gen,
           copy_of(gen, "json-ws", lambda d: open(os.path.join(d, "clips.json"), "a").write(" ")), "mutant: ")
    mutant("a byte of one clip changed: SHA256SUMS", check_sums,
           copy_of(gen, "sums", lambda d: flip_byte(os.path.join(d, "hades-check-dv5.mkv"))), ref, "mutant: ")
    mutant("a file that SHA256SUMS does not list", check_sums,
           copy_of(gen, "extra", lambda d: open(os.path.join(d, "extra.txt"), "w").write("x")), ref, "mutant: ")
    mutant("a duration of clips.json", check_clips_json,
           copy_of(gen, "duration", lambda d: edit_json(os.path.join(d, "clips.json"),
                                                         lambda doc: doc["clips"][2].update(duration=15.5))), ref, "mutant: ")
    mutant("a format missing from clips.json", check_clips_json,
           copy_of(gen, "missing", lambda d: edit_json(os.path.join(d, "clips.json"),
                                                        lambda doc: doc["clips"].pop(1))), ref, "mutant: ")
    mutant("the size limit one byte below the clips", lambda g, m, p: check_sizes(
        g, m, p, limit=sum(os.path.getsize(os.path.join(g, f)) for f in os.listdir(g))), gen, ref, "mutant: ")
    guard = open(os.path.join(A.clips, "make_clips.sh")).read()
    unguarded = guard.replace("exit 1\nfi", "fi", 1)
    if unguarded == guard:
        raise SystemExit("the mutant of the audio guard did not change make_clips.sh")
    mutant("make_clips.sh without its stop", check_audio_guard, A.clips, "mutant: ", unguarded)

    # a control and mutants made by the generator itself, two pictures each
    ctl = generate(w("control"), "sdr,hdr10,hlg,dv81,dv5", 2)
    say("--- control (two pictures a clip, no fault): every check below must pass")
    control = [check_streams(ctl, ref, "control: "), check_units(ctl, ref, "control: "), check_rpu(ctl, ref, "control: "),
               check_pixels(ctl, ref, "control: ")[0], check_p5_light(ctl, ref, "control: ")[0],
               check_dv81_base_layer(ctl, ref, "control: "), check_hvcc_against_ffmpeg(ctl, ref, "control: ")]
    if not all(control):
        say("the control fails: the mutants below would prove nothing")

    def pq_wrong_transfer(m):
        g = m["gen_video"]
        orig = g.x265_colour
        g.x265_colour = lambda f: [("bt709" if a == "smpte2084" else a) for a in orig(f)]
    mutant("HDR10 coded with the transfer of BT.709 in its VUI", check_streams,
           generate(w("vui"), "hdr10", 2, pq_wrong_transfer), ref, "mutant: ")

    def no_master_display(m):
        g = m["gen_video"]
        orig = g.static_metadata
        g.static_metadata = lambda f: ("G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,10)",) + orig(f)[1:]
    mutant("HDR10 with a mastering display minimum of 0.001 cd/m2 in its SEI", check_streams,
           generate(w("sei"), "hdr10", 2, no_master_display), ref, "mutant: ")

    def compat0(m):
        d = m["dovi"]
        orig = d.config_record
        d.config_record = lambda profile, level, compat: orig(profile, level, 0)
    mutant("profile 8.1 with a record of compatibility 0", check_streams,
           generate(w("compat"), "dv81", 2, compat0), ref, "mutant: ")
    mutant("profile 8.1 with a record of compatibility 0 (dovi_compare)", check_rpu, w("compat"), ref, "mutant: ")

    def level5(m):
        m["dovi"].dv_level = lambda width, height, fps: 5
    mutant("profile 8.1 with a record of level 5", check_rpu, generate(w("level"), "dv81", 2, level5), ref, "mutant: ")

    def rpu_in_hdr10(m):
        g, d = m["gen_video"], m["dovi"]
        orig = g.mkv.write

        def write(path, f, hv, aus, *rest):
            if f["id"] == "hdr10":
                c = m["card"]
                nal = d.rpu_nal(d.metadata(8, c.level1(c.fmt("hdr10"))[1], c.MASTERING, 1000, 48, True))
                aus = [au + len(nal).to_bytes(4, "big") + nal for au in aus]
            return orig(path, f, hv, aus, *rest)
        g.mkv.write = write
    mutant("RPUs in the HDR10 clip", check_units, generate(w("rpu-hdr10"), "hdr10", 2, rpu_in_hdr10), ref, "mutant: ")

    def rpu_missing(m):
        g = m["gen_video"]
        orig = g.mkv.write

        def write(path, f, hv, aus, *rest):
            if f["id"] == "dv81":
                last = aus[-1]
                # drop the last unit (the RPU) of the last access unit
                pos, units = 0, []
                while pos < len(last):
                    n = int.from_bytes(last[pos:pos + 4], "big")
                    units.append(last[pos:pos + 4 + n])
                    pos += 4 + n
                aus = aus[:-1] + [b"".join(units[:-1])]
            return orig(path, f, hv, aus, *rest)
        g.mkv.write = write
    gen_missing = generate(w("rpu-missing"), "dv81", 2, rpu_missing)
    mutant("profile 8.1 without the RPU of its last picture", check_units, gen_missing, ref, "mutant: ")
    mutant("profile 8.1 without the RPU of its last picture (dovi_compare)", check_rpu, gen_missing, ref, "mutant: ")

    def with_info(m):
        g = m["gen_video"]
        g.X265_COMMON = [a for a in g.X265_COMMON if a != "--no-info"]
    mutant("x265's own SEI left in the stream", check_units, generate(w("info"), "hlg", 2, with_info), ref, "mutant: ")

    def l1_avg(m):
        c = m["card"]
        orig = c.level1
        c.level1 = lambda f: (orig(f)[0], orig(f)[1][:2] + (orig(f)[1][2] + 1,))
    mutant("profile 8.1 with a level 1 average one code higher", check_rpu,
           generate(w("l1"), "dv81", 2, l1_avg), ref, "mutant: ")

    def l6_fall(m):
        d = m["dovi"]
        orig = d.metadata
        d.metadata = lambda profile, l1, mastering, cll, fall, refresh: orig(profile, l1, mastering, cll, fall + 1, refresh)
    mutant("profile 5 with a level 6 MaxFALL one higher", check_rpu, generate(w("l6"), "dv5", 2, l6_fall), ref, "mutant: ")

    def refresh_always(m):
        d = m["dovi"]
        orig = d.metadata
        d.metadata = lambda profile, l1, mastering, cll, fall, refresh: orig(profile, l1, mastering, cll, fall, True)
    mutant("profile 8.1 with a scene refresh in every RPU", check_rpu,
           generate(w("refresh"), "dv81", 2, refresh_always), ref, "mutant: ")

    def p5_matrix(m):
        d = m["dovi"]
        orig = d.metadata

        def metadata(profile, *rest):
            out = orig(profile, *rest)
            if profile == 5:
                out["color"]["ycc_to_rgb_matrix"] = (8192, 800) + tuple(out["color"]["ycc_to_rgb_matrix"][2:])
            return out
        d.metadata = metadata
    mutant("profile 5 with one coefficient of its YCCtoRGB off by one", check_rpu,
           generate(w("p5-matrix"), "dv5", 2, p5_matrix), ref, "mutant: ")

    def bar_code(m):
        c = m["card"]
        orig = c.region_values

        def region_values(f):
            v = orig(f)
            if f["id"] == "hdr10":
                b = v["bar3"]
                v["bar3"] = c.Value((b.codes[0] + 1,) + b.codes[1:], b.light, b.nonlinear)
            return v
        c.region_values = region_values
    mutant("HDR10 with one bar a code brighter", check_pixels, generate(w("pixels"), "hdr10", 2, bar_code), ref, "mutant: ")

    def coarse(m):
        m["gen_video"].QUANTISER["hlg"] = ["--qp", "30"]
    mutant("HLG coded at QP 30", check_pixels, generate(w("qp30"), "hlg", 2, coarse), ref, "mutant: ")

    def sdr_lossy(m):
        m["gen_video"].QUANTISER["sdr"] = ["--qp", "8"]
    mutant("SDR coded lossy at QP 8", check_pixels, generate(w("sdr-lossy"), "sdr", 6, sdr_lossy), ref, "mutant: ")

    def no_crosstalk(m):
        c = m["card"]
        c._TO_LMS_C = c.LMS_2020_M  # (the crosstalk of profile 5 left out of the coding)
    mutant("profile 5 coded without its crosstalk", check_p5_light,
           generate(w("p5-light"), "dv5", 2, no_crosstalk), ref, "mutant: ")

    def bar_in_dv81(m):
        c = m["card"]
        orig = c.region_values

        def region_values(f):
            v = orig(f)
            if f["id"] == "dv81":
                b = v["bar6"]
                v["bar6"] = c.Value((b.codes[0] - 1,) + b.codes[1:], b.light, b.nonlinear)
            return v
        c.region_values = region_values
    mutant("profile 8.1 with one bar a code darker than in HDR10", check_dv81_base_layer,
           generate(w("bl"), "hdr10,dv81", 2, bar_in_dv81), ref, "mutant: ")

    def dv81_own_ps(m):
        m["gen_video"].QUANTISER["dv81"] = ["--qp", "8", "--no-wpp"]
    mutant("profile 8.1 coded with other parameter sets than HDR10", check_dv81_base_layer,
           generate(w("bl-ps"), "hdr10,dv81", 2, dv81_own_ps), ref, "mutant: ")

    def hvcc_parallel(m):
        h = m["hevc"]
        orig = h.hvcc

        def hvcc(nals):
            rec, sps = orig(nals)
            rec = bytearray(rec)
            rec[15] = 0xFD  # parallelismType 1
            return bytes(rec), sps
        h.hvcc = hvcc
    mutant("hvcC with another parallelismType", check_hvcc_against_ffmpeg,
           generate(w("hvcc"), "sdr", 2, hvcc_parallel), ref, "mutant: ")


def main():
    global A
    ap = argparse.ArgumentParser()
    for k in ("clips", "x265", "ffmpeg", "ffprobe", "dovi-compare", "kodi", "work", "gen1", "gen2"):
        ap.add_argument("--" + k, required=k not in ("gen2",))
    ap.add_argument("--only", default="")
    ap.add_argument("--mutants", action="store_true")
    A = ap.parse_args()
    for k in ("clips", "work", "gen1", "gen2", "kodi"):
        if getattr(A, k):
            setattr(A, k, os.path.abspath(getattr(A, k)))
    os.makedirs(A.work, exist_ok=True)
    only = set(x for x in A.only.split(",") if x)
    want = lambda k: not only or k in only
    ref = import_clips(A.clips)
    gen = A.gen1
    if want("determinism"):
        check_determinism(A.gen1, A.gen2)
    if want("sums"):
        check_sums(gen, ref)
    if want("json"):
        check_clips_json(gen, ref)
    if want("streams"):
        check_streams(gen, ref)
    if want("units"):
        check_units(gen, ref)
    if want("rpu"):
        check_rpu(gen, ref)
    if want("pixels"):
        _, report = check_pixels(gen, ref)
        with open(os.path.join(A.work, "pixels-report.json"), "w") as fh:
            json.dump(report, fh, indent=1, sort_keys=True)
    if want("p5light"):
        _, rows = check_p5_light(gen, ref)
        with open(os.path.join(A.work, "p5-light.txt"), "w") as fh:
            fh.write("\n".join(rows) + "\n")
    if want("dv81bl"):
        check_dv81_base_layer(gen, ref)
    if want("hvcc"):
        check_hvcc_against_ffmpeg(gen, ref)
    if want("sizes"):
        check_sizes(gen, ref)
    if want("guard"):
        check_audio_guard(A.clips)
    checks = list(RESULTS)
    if A.mutants:
        run_mutants(gen, ref)
    failed = [n for n, ok in checks if not ok]
    control_failed = [n for n, ok in RESULTS[len(checks):] if not ok]
    missed = [n for n, caught in MUTANTS if not caught]
    say("VERDICT: %s (%d checks, %d failed; %d control checks, %d failed; %d mutants, %d missed)" %
        ("PASS" if not (failed or control_failed or missed) else "FAIL", len(checks), len(failed),
         len(RESULTS) - len(checks), len(control_failed), len(MUTANTS), len(missed)))
    sys.exit(1 if failed or control_failed or missed else 0)


if __name__ == "__main__":
    main()
