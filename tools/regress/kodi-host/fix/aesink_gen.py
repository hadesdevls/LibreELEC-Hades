#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# takes out of ActiveAESink.cpp of a Kodi tree, as they are written there, what aesink_test.cpp runs: the
# helpers of its unnamed namespace (IsUsableFor() ... NamesDisplayOutput()), the readers of the list of outputs and
# EnumerateSinkList(), and SinkInfo() where the tree has it (config.h says which).
# Usage: aesink_gen.py <tree> <out dir>
import pathlib
import sys

tree, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
c = (tree / "xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAESink.cpp").read_text()


def function(text, head):
    i = text.index(head)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        depth += text[k] == "{"
        depth -= text[k] == "}"
        if depth == 0:
            return text[i:k + 1]
    raise SystemExit("unbalanced: " + head)


a = c.index("namespace\n{\nbool IsUsableFor(")
b = c.index("} // unnamed namespace", a) + len("} // unnamed namespace")
pieces = [c[a:b]]
heads = [
    "AEDeviceType CActiveAESink::GetDeviceType(const std::string &device)",
    "bool CActiveAESink::GetDisplayAudio(const std::string& device,",
    "bool CActiveAESink::HasPassthroughDevice()",
    "bool CActiveAESink::SupportsFormat(const std::string &device, AEAudioFormat &format)",
    "bool CActiveAESink::NeedIECPacking()",
    "bool CActiveAESink::DeviceExist(std::string driver, const std::string& device)",
    "void CActiveAESink::EnumerateSinkList(bool force, std::string driver)",
    "void CActiveAESink::PrintSinks(std::string& driver)",
    "std::string CActiveAESink::FollowDisplay(const std::string& device,",
    "bool CActiveAESink::IsMissingBesideItsFamily(const std::string& device) const",
    "std::string CActiveAESink::FallbackOutput(bool passthrough) const",
    "std::string CActiveAESink::OutputOfTheDisplay(bool passthrough,",
    "std::string CActiveAESink::ValidateOuputDevice(const std::string& device, bool passthrough) const",
    "void CActiveAESink::EnumerateOutputDevices(AEDeviceList &devices, bool passthrough)",
    "void CActiveAESink::GetDeviceFriendlyName(const std::string& device)",
]
snapshot = "std::shared_ptr<const CActiveAESink::SinkInfoList> CActiveAESink::SinkInfo() const" in c
if snapshot:
    heads.insert(0, "std::shared_ptr<const CActiveAESink::SinkInfoList> CActiveAESink::SinkInfo() const")
pieces += [function(c, h) for h in heads]
(out / "pieces.inc").write_text("\n\n".join(pieces) + "\n")
(out / "config.h").write_text("#define HAVE_SNAPSHOT %d\n" % snapshot)
print("aesink: %d functions and the helpers; the snapshot: %s" % (len(heads), "yes" if snapshot else "no"))
