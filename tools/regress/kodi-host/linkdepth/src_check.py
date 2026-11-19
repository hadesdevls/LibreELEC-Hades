#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades (1.0 RC4, written again for the transport of 1.0): the depth of the link never makes a commit of its own.
# In the Kodi tree:
#  S1 QueueLinkDepth(), QueuePictureLinkDepth(), ReadLinkDepthOutput(), CurrentLinkContent() and
#     LinkContentOfMode() (both) call nothing that commits (FlipPage*, DrmAtomicCommit,
#     drmModeAtomicCommit) or turns the output on or off (SetActive); QueueLinkDepth() allows a
#     modeset only where the depth goes up (step.raises), adds only "max bpc" (by the user's depth or
#     by the content), and neither way while native output has the output (but at its end);
#  S2 they are called from these places only: SetFullScreen() (with the commit of the mode),
#     SetHDR() (with its request), the staging of the end of native Dolby Vision (FlipDVBridgePage(),
#     restoring, with transportEnds), and the joined restorations of the GLES context (which set
#     m_linkNextContent around EndDVBridge(true, true): in place, and in a new window);
#  S3 RequeueOutputProperties() queues "max bpc" only from m_linkBpc and not while native output
#     has the output; every place in Kodi that gives "max bpc" a value is one of a fixed list: these
#     two, the TEST_ONLY probe of the GLES context (S3b), and the transport: its specs (8, required,
#     for the kernel's backends and for nvidia-drm), the defaults it stages where the record of the
#     previous session is gone (the top of the range), and its table that gives it back as a
#     setting. Anything else that writes it, in xbmc/ or tools/dvbridge, fails.
# Usage: src_check.py <Kodi tree>
import os
import re
import sys

root = sys.argv[1]
G = "xbmc/windowing/gbm/"
gbm = open(root + "/" + G + "WinSystemGbm.cpp", encoding="utf-8").read()
gles = open(root + "/" + G + "WinSystemGbmGLESContext.cpp", encoding="utf-8").read()
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    failures += 0 if ok else 1


def body(text, head):
    # a function that is not there gives an empty body: its checks fail, the script goes on
    if head not in text:
        print("     (no %s in this tree)" % head)
        return ""
    i = text.index(head)
    depth, j = 0, text.index("{", i)
    for k in range(j, len(text)):
        depth += text[k] == "{"
        depth -= text[k] == "}"
        if depth == 0:
            return text[i:k + 1]
    raise ValueError(head)


def before(text, *marks):
    # the marks are in the text, in this order (each searched after the one before it)
    pos = 0
    for m in marks:
        pos = text.find(m, pos)
        if pos < 0:
            return False
        pos += len(m)
    return True


def code(text):
    # without comments and string literals
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(\\.|[^"\\])*"', '""', text)


def nocomm(text):
    # without comments (// and /* */), string literals kept
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


COMMITS = re.compile(r"\b(FlipPage\w*|DrmAtomicCommit|drmModeAtomicCommit|SetActive|SetVideoMode)\s*\(")
heads = {
    "QueueLinkDepth": (gbm, "void CWinSystemGbm::QueueLinkDepth("),
    "QueuePictureLinkDepth": (gbm, "void CWinSystemGbm::QueuePictureLinkDepth("),
    "ReadLinkDepthOutput": (gbm, "void CWinSystemGbm::ReadLinkDepthOutput("),
    "CurrentLinkContent": (gbm, "LinkContent CWinSystemGbm::CurrentLinkContent()"),
    "LinkContentOfMode (GLES)": (gles, "LinkContent CWinSystemGbmGLESContext::LinkContentOfMode("),
}
for name, (text, head) in heads.items():
    b = code(body(text, head))
    found = COMMITS.findall(b)
    check(not found, "S1 %s commits nothing (%s)" % (name, ", ".join(found) or "none"))
q = code(body(gbm, "void CWinSystemGbm::QueueLinkDepth("))
check(len(re.findall(r"AllowModeset\s*\(", q)) == 1 and
      re.search(r"if \(step\.raises\)\s*drm->AllowModeset\(\);", q) is not None,
      "S1 QueueLinkDepth allows a modeset only where the depth goes up")
raw_q = nocomm(body(gbm, "void CWinSystemGbm::QueueLinkDepth("))
adds = re.findall(r"\b(AddProperty|AddSoftProperty)\s*\(([^;]*)", raw_q)
check(len(adds) == 2 and all(re.match(r'\s*connector,\s*"max bpc",', a[1]) for a in adds)
      and sorted(a[0] for a in adds) == ["AddProperty", "AddSoftProperty"],
      'S1 QueueLinkDepth adds "max bpc" and nothing else, once by the user\'s depth (AddSoftProperty) and '
      'once by the content (AddProperty): %s' % [a[0] + "(" + a[1].strip()[:30] for a in adds])
check('AddProperty(connector, "max bpc", *step.maxBpc)' in raw_q and
      'AddSoftProperty(connector, "max bpc", *step.maxBpc, current)' in raw_q, 'S1 ... with the step that it decided')
guards = re.findall(r"if \(NativeDVOutputInUse\(\) && !transportEnds\)\s*return;", q)
check(len(guards) == 2,
      "S1 QueueLinkDepth does nothing while native output has the output, but at its end, in both ways (%d of 2)"
      % len(guards))
# (each way returns before its own "max bpc": the guard of the user's depth inside its "if (wanted)", the other
#  before the content's step)
check(before(q, "if (wanted)", "if (NativeDVOutputInUse() && !transportEnds)", "AddSoftProperty(",
             "if (NativeDVOutputInUse() && !transportEnds)", "LinkDepthFor(", "AddProperty(connector"),
      "S1 ... each guard before the \"max bpc\" of its own way")

# S2: the call sites
calls_gbm = [m.start() for m in re.finditer(r"\bQueueLinkDepth\(", code(gbm))]
calls_pic = [m.start() for m in re.finditer(r"\bQueuePictureLinkDepth\(", code(gbm))]
cg = code(gbm)


def owner(pos, text):
    # the function a position is in: the last definition head before it at column 0
    heads = [m for m in re.finditer(r"^[A-Za-z][^\n;]*CWinSystemGbm\w*::(\w+)\(", text, re.M)
             if m.start() < pos]
    return heads[-1].group(1) if heads else "?"


owners = sorted(owner(p, cg) for p in calls_gbm)
check(owners == ["FlipDVBridgePage", "QueueLinkDepth", "QueuePictureLinkDepth", "SetFullScreen"],
      "S2 QueueLinkDepth is called from SetFullScreen, QueuePictureLinkDepth and FlipDVBridgePage "
      "(and its definition): %s" % owners)
pic_owners = sorted(owner(p, cg) for p in calls_pic)
check(pic_owners == ["QueuePictureLinkDepth", "SetHDR", "SetHDR"],
      "S2 QueuePictureLinkDepth is called from SetHDR, twice (and its definition): %s" % pic_owners)
fs = code(body(gbm, "bool CWinSystemGbm::SetFullScreen("))
i_mode, i_q, i_video = fs.index("m_DRM->SetMode(res)"), fs.index("QueueLinkDepth("), fs.index("SetVideoMode(res, bo)")
check(i_mode < i_q < i_video, "S2 SetFullScreen: after the mode is selected, before its commit")
fl = code(body(gbm, "bool CWinSystemGbm::FlipDVBridgePage("))
stage = fl[fl.index("const auto stage"):fl.index("return staged;")]
check(re.search(r"if \(staged && restoring\)\s*QueueLinkDepth\([^;]*, true,\s*\"\", true\);", stage)
      is not None, "S2 FlipDVBridgePage: in the staging of the restoration only, after PutInRequest(), "
                   "setting the head, at the end of the transport")
check(before(stage, "state.PutInRequest(", "QueueLinkDepth("),
      "S2 ... after the transport (COutputHandover::PutInRequest) has staged what it gives back")
# a call of QueueLinkDepth() or QueuePictureLinkDepth() in the code of the GLES context, not the
# word: since 1.0 RC6 a comment of ProbeHdr10DepthAhead() names QueueLinkDepth(), and the first form of this check
# looked for the word in the whole text, comments too (QueuePictureLinkDepth() it did not look for)
check(re.search(r"\bQueue(Picture)?LinkDepth\s*\(", code(gles)) is None,
      "S2 the GLES context does not queue the depth itself")
# the joined restorations: in place (SetFullScreen) and in a new window (RestoreDVInNewWindow);
# each EndDVBridge(true, true) carries the depth of its mode, and m_linkNextContent is set nowhere else
cgl = code(gles)
joined = re.findall(r"m_linkNextContent = LinkContentOfMode\(res\);\s*const bool (\w+) = EndDVBridge\(true, true\);"
                    r"\s*m_linkNextContent\.reset\(\);", cgl)
ends = re.findall(r"\bEndDVBridge\(true, true\)", cgl)
sets = re.findall(r"\bm_linkNextContent\s*=", cgl)
check(len(joined) == 2 and len(ends) == 2 and len(sets) == 2,
      "S2 the joined restorations (in place and in a new window) carry the depth of their mode, and only they "
      "(%d wrapped, %d EndDVBridge(true, true), %d sets of m_linkNextContent)" % (len(joined), len(ends), len(sets)))
check(re.search(r"m_linkNextContent = LinkContentOfMode\(res\);\s*const bool restored = EndDVBridge",
                code(body(gles, "bool CWinSystemGbmGLESContext::SetFullScreen("))) is not None and
      re.search(r"m_linkNextContent = LinkContentOfMode\(res\);\s*const bool joined = EndDVBridge",
                code(body(gles, "bool CWinSystemGbmGLESContext::RestoreDVInNewWindow("))) is not None,
      "S2 ... one in SetFullScreen(), one in RestoreDVInNewWindow()")

# S3
rq = code(body(gbm, "void CWinSystemGbm::RequeueOutputProperties()"))
check(re.search(r'if \(m_linkBpc && !NativeDVOutputInUse\(\)\)\s*drm->AddProperty\(connector, "", \*m_linkBpc\);',
                rq) is not None, "S3 RequeueOutputProperties: m_linkBpc, not under native output")

# every "max bpc" written in the code of xbmc/ and tools/dvbridge (comments out, the string itself kept): the call
# it is an argument of, or the table row it is in
READS = {"GetRangePropertyLimits", "GetPropertyValue", "ReadPropertyValue", "RequestValue", "value",
         "SupportsProperty", "GetPropertyEnumValue", "compare", "names"}
WRITES = {"AddProperty", "AddSoftProperty", "put", "number", "emplace_back", "push_back", "save", "row"}


def callee(text, i):
    # a comparison (name == "max bpc") reads it
    if re.search(r"[=!]=\s*$", text[:i]):
        return "compare"
    depth, j = 0, i - 1
    while j >= 0:
        c = text[j]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                m = re.search(r"([A-Za-z_]\w*)\s*$", text[:j])
                return m.group(1) if m else "?("
            depth -= 1
        elif c == "{" and depth == 0:
            # a row of a table of the output state ({Role::CONNECTOR, "max bpc", Part::...}), or a constant list
            # of the names a driver must have (NVIDIA_NEEDS[] = {..., "max bpc", ...}: read)
            if re.match(r"\s*(Role::)?(CONNECTOR|CRTC|PLANE)\s*,\s*$", text[j + 1:i]):
                return "row"
            return "names" if re.search(r"\[\w*\]\s*=\s*$", text[:j]) else "{"
        elif c == ";" and depth == 0:
            return ";"
        j -= 1
    return "?"


def func_of(text, i):
    # the function a position is in: the last line before it that starts at column 0 with a name, has a "(" and does
    # not end with ";" (a definition's head); its name is the identifier before the first "("
    name = "?"
    for line in text[:i].split("\n")[:-1]:
        if re.match(r"[A-Za-z_]", line) and "(" in line and not line.rstrip().endswith(";"):
            m = re.search(r"([A-Za-z_]\w*)\s*\(", line)
            if m and m.group(1) not in ("if", "for", "while", "switch", "static_assert"):
                name = m.group(1)
    return name


writes, unknown = [], []
for base in ("xbmc", "tools/dvbridge"):
    for d, _, files in os.walk(os.path.join(root, base)):
        for f in files:
            if not f.endswith((".cpp", ".h", ".c", ".hpp", ".in")):
                continue
            p = os.path.join(d, f)
            try:
                t = open(p, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if '"max bpc"' not in t:
                continue
            c = nocomm(t)
            rel = os.path.relpath(p, root)
            for m in re.finditer(r'(?<!\\)"max bpc"', c):
                who = callee(c, m.start())
                if who in READS:
                    continue
                if who in WRITES:
                    writes.append((rel, func_of(c, m.start()), who))
                else:
                    unknown.append((rel, func_of(c, m.start()), who))
EXPECTED = sorted([
    (G + "WinSystemGbm.cpp", "QueueLinkDepth", "AddSoftProperty"),           # the user's depth
    (G + "WinSystemGbm.cpp", "QueueLinkDepth", "AddProperty"),               # the depth of the content (sdr8)
    (G + "WinSystemGbm.cpp", "RequeueOutputProperties", "AddProperty"),      # m_linkBpc again (sdr8)
    (G + "WinSystemGbmGLESContext.cpp", "ProbeHdr10DepthAhead", "emplace_back"),  # TEST_ONLY (S3b)
    (G + "drm/TransportSpecs.h", "TransportSpecs", "number"),               # the transport, kernel's backends
    (G + "drm/TransportSpecs.h", "TransportSpecs", "number"),               # the transport, nvidia-drm
    (G + "drm/OutputHandover.cpp", "StageDefaults", "put"),                 # the defaults after a lost record
    (G + "drm/OutputState.h", "PartOf", "row"),                             # given back as a setting
])
check(sorted(writes) == EXPECTED and not unknown,
      'S3 the places that give "max bpc" a value in xbmc/ and tools/dvbridge are the depth\'s (QueueLinkDepth twice, '
      'RequeueOutputProperties), the TEST_ONLY probe and the transport\'s (its two specs, its defaults, its table) '
      'and no other: %s%s' % (
          "as expected" if sorted(writes) == EXPECTED else
          "more: %s; missing: %s" % ([w for w in writes if w not in EXPECTED or writes.count(w) > EXPECTED.count(w)],
                                     [e for e in EXPECTED if writes.count(e) < EXPECTED.count(e)]),
          "; not read nor written by a known call: %s" % unknown if unknown else ""))
# the transport: its "max bpc" is 8 and required (both kinds), it is given back as a setting, its defaults are the top
# of the range, and its specs are taken by COutputHandover only
ts_path = root + "/" + G + "drm/TransportSpecs.h"
ts = nocomm(open(ts_path, encoding="utf-8").read()) if os.path.exists(ts_path) else ""
check(len(re.findall(r'\bnumber\(CONNECTOR, "max bpc", 8, true\);', ts)) == 2 and
      len(re.findall(r'"max bpc"', ts)) == 2,
      'S3t the transport sets "max bpc" 8, required, for the kernel\'s backends and for nvidia-drm (TransportSpecs)')
os_path = root + "/" + G + "drm/OutputState.h"
ost = nocomm(open(os_path, encoding="utf-8").read()) if os.path.exists(os_path) else ""
check(re.search(r'\{Role::CONNECTOR, "max bpc", Part::SETTING\}', ost) is not None,
      'S3t ... and gives it back with its end as a setting (OutputState.h, PartOf)')
oh_path = root + "/" + G + "drm/OutputHandover.cpp"
oh = nocomm(open(oh_path, encoding="utf-8").read()) if os.path.exists(oh_path) else ""
check(re.search(r'if \(const auto limits = connector->GetRangePropertyLimits\("max bpc"\)\)\s*'
                r'put\(connector, "max bpc", limits->second\);', oh) is not None,
      'S3t ... and where the record of the previous session is gone, stages the top of the range (StageDefaults)')
takers = []
for d, _, files in os.walk(os.path.join(root, "xbmc")):
    for f in files:
        if f.endswith((".cpp", ".h")):
            p = os.path.join(d, f)
            t = open(p, encoding="utf-8", errors="replace").read()
            if "TransportSpecs(" in t:
                for m in re.finditer(r"\bTransportSpecs\s*\(", nocomm(t)):
                    takers.append(os.path.relpath(p, root))
check(sorted(takers) == sorted([G + "drm/OutputHandover.cpp", G + "drm/TransportSpecs.h"]),
      "S3t ... and its specs are taken by COutputHandover only (and defined in TransportSpecs.h): %s" % sorted(takers))
# S3b: since 1.0 RC6 the GLES context gives "max bpc" a value in ProbeHdr10DepthAhead(), a
# TEST_ONLY commit of what HDR10 would have in the mode on the output, asked before a video. It is the only place
# there that gives it one (the other is read: value("max bpc")); the list it is in goes to TestConnectorProperties()
# and nowhere else; the probe commits nothing and adds nothing to the pending request; and TestConnectorProperties()
# commits a request of its own, TEST_ONLY, and nothing of the pending one (m_req). (S3 looks for AddProperty only.)
drmat = open(root + "/" + G + "drm/DRMAtomic.cpp", encoding="utf-8").read()
nocomm1 = lambda t: re.sub(r"//[^\n]*", "", t)
given = re.findall(r'"max bpc"', nocomm1(gles))
read_ = re.findall(r'\bvalue\("max bpc"\)', nocomm1(gles))
probe_text = body(gles, "void CWinSystemGbmGLESContext::ProbeHdr10DepthAhead()")
in_probe = re.findall(r'\bproperties\.emplace_back\("max bpc",', nocomm1(probe_text))
check(len(in_probe) == 1 and len(given) == len(read_) + 1,
      'S3b the GLES context gives "max bpc" a value only in ProbeHdr10DepthAhead() (%d there, %d read, %d in all)'
      % (len(in_probe), len(read_), len(given)))
probe = code(probe_text)
uses = len(re.findall(r"\bproperties\b", probe))
tested = len(re.findall(r"\bTestConnectorProperties\(properties\)", probe))
known = len(re.findall(r"\bproperties\{", probe)) + len(re.findall(r"\bproperties\.emplace_back\(", probe)) + tested
check(uses == known and tested == 1,
      "S3b ... its list goes to TestConnectorProperties() once and nowhere else (%d uses, %d known)" % (uses, known))
check(not COMMITS.findall(probe) and "AddProperty(" not in probe,
      "S3b ... the probe commits nothing and adds nothing to the pending request (%s)"
      % (", ".join(COMMITS.findall(probe)) or "none"))
tcp = code(body(drmat, "CDRMAtomic::PropertyTest CDRMAtomic::TestConnectorProperties("))
check("CDRMAtomicRequest request;" in tcp and "m_req" not in tcp and len(re.findall(r"drmModeAtomicCommit\(", tcp)) == 1
      and re.search(r"drmModeAtomicCommit\(m_fd, request\.Get\(\),\s*DRM_MODE_ATOMIC_TEST_ONLY\b", tcp) is not None,
      "S3b TestConnectorProperties() commits a request of its own, TEST_ONLY, and nothing of the pending one")
print("%s: %d failed" % ("SRC-CHECK PASSED" if not failures else "SRC-CHECK FAILED", failures))
sys.exit(1 if failures else 0)
