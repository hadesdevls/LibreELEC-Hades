// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: Kodi's reading of when NVKMS names a 4K mode by an
// HDMI VIC (KODI::UTILS::NvkmsNamesByHdmiVic() of xbmc/utils/EdidHdmiVic.h, from the raw EDID) against what NVKMS 615
// itself answers for every 4K timing of 593 real EDIDs (vicrule.txt: what a harness built with NVKMS's own EDID
// parser, the integer-rate twins of Hades' NVIDIA patches and nvFindEdidNVT_TIMING(), printed for them).
// usage: nvkms_vic_cross_test <vicrule.txt>
#include "utils/EdidHdmiVic.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>

using namespace KODI::UTILS;

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::fprintf(stderr, "usage: %s <vicrule.txt>\n", argv[0]);
    return 2;
  }
  std::ifstream in(argv[1]);
  const std::regex re(R"(MODE (\S+) uhd=(\d) scdc=(\d) h10p=(\d) hdr=(\d) (\d+)x2160 rr=(\d+) pclk=\d+ -> (\S+) (\d+))");
  std::map<std::string, HdmiVicSink> cache;
  std::map<std::string, int> rows;
  int lines = 0, compared = 0, mismatches = 0, substituted = 0, parseMismatch = 0;
  std::string line;
  while (std::getline(in, line))
  {
    std::smatch m;
    if (!std::regex_search(line, m, re))
      continue;
    ++lines;
    const std::string file = m[1];
    const int w = std::stoi(m[6]), rr = std::stoi(m[7]);
    const bool nvkmsHdmiVic = m[8] == "HDMI_VIC";
    if (!HdmiVicOfMode(w, 2160, rr) && !(w == 4096 && std::abs(rr - 23976) <= 10))
      continue;
    auto it = cache.find(file);
    if (it == cache.end())
    {
      std::ifstream f(file, std::ios::binary);
      std::vector<uint8_t> e((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      it = cache.emplace(file, HdmiVicSinkOf(e)).first;
    }
    const HdmiVicSink& s = it->second;
    // the flags of the EDID that NVKMS printed, against Kodi's reading of them
    if (s.uhdVic != (m[2] == "1") || s.scdc != (m[3] == "1") || s.hdr10Plus != (m[4] == "1") ||
        s.st2084 != (m[5] == "1"))
      ++parseMismatch;
    ++compared;
    const bool kodi = NvkmsNamesByHdmiVic(s, w, 2160, rr);
    rows[std::to_string(w) + "@" + std::to_string(rr) + " uhd" + (s.uhdVic ? "1" : "0") + " NVKMS " +
         (nvkmsHdmiVic ? "HDMI_VIC" : "CTA") + " Kodi " + (kodi ? "HDMI_VIC" : "CTA")]++;
    if (kodi != nvkmsHdmiVic)
    {
      if (++mismatches <= 10)
        std::printf("MISMATCH %s %dx2160 rr=%d: NVKMS %s, Kodi %s\n", file.c_str(), w, rr,
                    nvkmsHdmiVic ? "HDMI_VIC" : "CTA", kodi ? "HDMI_VIC" : "CTA");
    }
    substituted += NvkmsAviCtaVicWithHdr10Plus(s, w, 2160, rr);
  }
  for (const auto& [k, n] : rows)
    std::printf("  %-48s %d\n", k.c_str(), n);
  std::printf("lines %d, 4K modes compared %d, EDID flags read otherwise than NVKMS %d, modes where the AVI would "
              "carry the CTA VIC with HDR10+ %d\n",
              lines, compared, parseMismatch, substituted);
  const bool ok = compared > 1000 && mismatches == 0 && parseMismatch == 0;
  std::printf("%s: %d mismatches\n", ok ? "PASSED" : "FAILED", mismatches);
  return ok ? 0 : 1;
}
