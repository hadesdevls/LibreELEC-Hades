// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY: Kodi and i915 read the same HDR10+ application version of every EDID
// at hand: KODI::UTILS::Hdr10PlusBlockVersion() (utils/EdidHdr10Plus.h of the Kodi tree) against
// intel_hdr10plus_edid_version() (intel_hdr10plus.h of the kernel tree with linux-9972, through kedid.c), on each EDID
// as it is and with the version bits of its HDR10+ blocks set to 0, 1, 2 and 3. And how many EDIDs declare which
// version. Files: raw EDIDs (.bin) or the "edid-decode (hex):" text of the linuxhw database (linuxhw/EDID).
// usage: edid_cross_test <dir>...
#include "utils/EdidHdr10Plus.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" int k_edid_version(const unsigned char* edid, size_t size);

static int g_failures, g_checks, g_files;
static std::map<int, int> g_versions;

static bool ReadEdid(const std::string& path, std::vector<uint8_t>& e)
{
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  e.clear();
  if (data.rfind("edid-decode (hex):", 0) == 0)
  {
    std::istringstream in(data.substr(18));
    std::string line;
    bool started = false;
    while (std::getline(in, line))
    {
      if (line.find_first_not_of(" \t\r") == std::string::npos)
      {
        if (started)
          break;
        continue;
      }
      started = true;
      std::istringstream bytes(line);
      std::string b;
      while (bytes >> b)
        e.push_back(static_cast<uint8_t>(std::stoul(b, nullptr, 16)));
    }
  }
  else
    e.assign(data.begin(), data.end());
  return e.size() >= 128 && e.size() % 128 == 0;
}

// the offsets of the version bytes of the HDR10+ blocks (the walk of both readers)
static std::vector<std::size_t> VersionBytes(const std::vector<uint8_t>& e)
{
  std::vector<std::size_t> at;
  for (std::size_t b = 128; b + 128 <= e.size(); b += 128)
  {
    const uint8_t* x = &e[b];
    const unsigned end = x[2];
    if (x[0] != 0x02 || end < 4 || end > 127)
      continue;
    for (unsigned i = 4; i < end;)
    {
      const unsigned tag = x[i] >> 5, len = x[i] & 31;
      if (i + 1 + len > end)
        break;
      if (tag == 7 && len >= 5 && x[i + 1] == 1 && x[i + 2] == 0x8b && x[i + 3] == 0x84 && x[i + 4] == 0x90)
        at.push_back(b + i + 5);
      i += 1 + len;
    }
  }
  return at;
}

static void One(const std::string& path)
{
  std::vector<uint8_t> e;
  if (!ReadEdid(path, e))
    return;
  ++g_files;
  const int kodi = KODI::UTILS::Hdr10PlusBlockVersion(e), kernel = k_edid_version(e.data(), e.size());
  ++g_checks;
  if (kodi != kernel)
  {
    ++g_failures;
    std::printf("FAIL %s: Kodi %d, i915 %d\n", path.c_str(), kodi, kernel);
  }
  ++g_versions[kodi];
  for (int v = 0; v < 4; ++v)
  {
    std::vector<uint8_t> x = e;
    const auto at = VersionBytes(x);
    if (at.empty())
      break;
    for (std::size_t i : at)
      x[i] = static_cast<uint8_t>((x[i] & ~3) | v);
    const int a = KODI::UTILS::Hdr10PlusBlockVersion(x), b = k_edid_version(x.data(), x.size());
    ++g_checks;
    if (a != v || b != v)
    {
      ++g_failures;
      std::printf("FAIL %s with version %d: Kodi %d, i915 %d\n", path.c_str(), v, a, b);
    }
  }
}

static void Walk(const std::string& dir)
{
  DIR* d = opendir(dir.c_str());
  if (!d)
    return;
  while (dirent* ent = readdir(d))
  {
    const std::string name = ent->d_name;
    if (name == "." || name == "..")
      continue;
    const std::string path = dir + "/" + name;
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode))
      Walk(path);
    else if (S_ISREG(st.st_mode) && st.st_size <= 4096)
      One(path);
  }
  closedir(d);
}

int main(int argc, char** argv)
{
  for (int i = 1; i < argc; ++i)
    Walk(argv[i]);
  std::printf("EDIDs read: %d; HDR10+ application version:", g_files);
  for (const auto& [v, n] : g_versions)
    std::printf(" %s %d", v < 0 ? "none" : std::to_string(v).c_str(), n);
  std::printf("\n%s: %d checks, %d failed\n", g_failures || !g_files ? "FAILED" : "PASSED", g_checks, g_failures);
  return g_failures != 0 || !g_files;
}
