// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades: the defaults of Kodi's file cache (xbmc/settings/FileCacheDefaults.h): the memory from the RAM of the machine,
// the <cache> of an advancedsettings.xml taken in the units Kodi up to 20 had it (Kodi 21 and later leave it out
// without a word: forum 30362 #813 gave one), each brought to a choice of its setting and the memory to a quarter of the
// RAM at most; that the lists of choices are those of the settings (ServicesSettings.cpp of the tree), and that
// AdvancedSettings.cpp reads <cache> and applies it once the settings are loaded. With -DOLD_RULE the test carries the
// rule of 1.1 (1 GB at most, <cache> left out), which must fail.
// usage: cachedefaults_test <Kodi tree>
#include "settings/FileCacheDefaults.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace F = KODI::SETTINGS::FILECACHE;

#ifdef OLD_RULE
namespace OLD
{
int DefaultMemorySize(uint64_t totalRam)
{
  if (totalRam >= 14 * F::GiB)
    return 1024;
  if (totalRam >= 7 * F::GiB)
    return 512;
  if (totalRam >= 7 * F::GiB / 2)
    return 256;
  return 64;
}
F::FromAdvanced TakeAdvanced(const char*, const char*, const char*, const char*, uint64_t)
{
  return {};
}
} // namespace OLD
#define DEFAULT_MEMORY OLD::DefaultMemorySize
#define TAKE OLD::TakeAdvanced
#else
#define DEFAULT_MEMORY F::DefaultMemorySize
#define TAKE F::TakeAdvanced
#endif

static int fails = 0;
static int checks = 0;

static void Check(bool ok, const std::string& what)
{
  ++checks;
  if (!ok)
  {
    ++fails;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

static std::string Read(const std::string& path)
{
  std::ifstream f(path);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// the values that a filler of ServicesSettings.cpp gives, in its order
static std::vector<long long> Filler(const std::string& source, const std::string& name)
{
  std::vector<long long> values;
  const size_t at = source.find("void CServicesSettings::" + name + "(");
  if (at == std::string::npos)
    return values;
  const size_t end = source.find("\n}\n", at);
  const std::string body = source.substr(at, end - at);
  // list.emplace_back(<label>, <value>); the value, after the last comma of the call: a number, or "a * 1024",
  // "1024 * 1024", "2 * 1024"
  for (size_t call = body.find("emplace_back("); call != std::string::npos;
       call = body.find("emplace_back(", call + 1))
  {
    const size_t close = body.find(");", call);
    const size_t comma = body.rfind(',', close);
    if (close == std::string::npos || comma == std::string::npos || comma < call)
      continue;
    std::string text = body.substr(comma + 1, close - comma - 1);
    long long v = 1;
    bool number = false;
    size_t from = 0;
    while (from < text.size())
    {
      const size_t star = text.find('*', from);
      const std::string factor = text.substr(from, star == std::string::npos ? std::string::npos : star - from);
      if (factor.find_first_not_of(" 0123456789") != std::string::npos ||
          factor.find_first_of("0123456789") == std::string::npos)
      {
        number = false;
        break;
      }
      v *= std::stoll(factor);
      number = true;
      from = star == std::string::npos ? text.size() : star + 1;
    }
    if (number)
      values.push_back(v);
  }
  return values;
}

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::printf("usage: cachedefaults_test <Kodi tree>\n");
    return 2;
  }
  const std::string tree = argv[1];
  const uint64_t GiB = F::GiB;
  const uint64_t MiB = 1024 * 1024;

  // ---- the default from the RAM the kernel reports (an "8 GB" machine: 7.3 to 7.8 GiB; "32 GB": about 30.5)
  struct Ram
  {
    uint64_t bytes;
    int mb;
  };
  const Ram rams[] = {{0, 64},           {2 * GiB, 64},           {7 * GiB / 2 - 1, 64}, {7 * GiB / 2, 256},
                      {3900 * MiB, 256}, {7 * GiB - 1, 256},      {7 * GiB, 512},        {7600 * MiB, 512},
                      {14 * GiB - 1, 512}, {15 * GiB, 1024},      {28 * GiB - 1, 1024},  {28 * GiB, 2048},
                      {31200 * MiB, 2048}, {62 * GiB, 2048},      {256 * GiB, 2048}};
  for (const Ram& r : rams)
    Check(DEFAULT_MEMORY(r.bytes) == r.mb, "default for " + std::to_string(r.bytes / MiB) + " MiB of RAM: " +
                                                std::to_string(DEFAULT_MEMORY(r.bytes)) + " MB, " +
                                                std::to_string(r.mb) + " expected");
  // every default is a choice, at most 1/7 of the RAM where the RAM is known, and the largest at most 2 GB
  for (uint64_t mib = 512; mib <= 300 * 1024; mib += 64)
  {
    const int d = F::DefaultMemorySize(mib * MiB);
    bool choice = false;
    for (int c : F::MEMORY_SIZES)
      choice |= c == d;
    Check(choice && (d == 64 || static_cast<uint64_t>(d) * 7 <= mib) && d <= 2048,
          "default for " + std::to_string(mib) + " MiB is a choice, of at most 1/7 of the RAM: " + std::to_string(d));
  }
  // what a file of several streams holds ahead (3/8 of the setting) at the 130 Mbit/s of a dense FEL remux, in s
  for (const int mb : {64, 512, 1024, 2048})
    std::printf("ahead at 130 Mbit/s with %d MB: %.1f s\n", mb, mb * 3.0 / 8.0 * 8.0 * 1.048576 / 130.0);

  // ---- the <cache> of advancedsettings.xml
  const uint64_t ram32 = 31200 * MiB;
  {
    // ht2tweak's (forum 30362 #813): 256 MB, buffer every filesystem, adaptive (0: not a factor), 128 KB
    const auto t = TAKE("268435456", "1", "0", "131072", ram32);
    Check(t.memorySize && *t.memorySize == 256, "memorysize 268435456 bytes: 256 MB");
    Check(t.bufferMode && *t.bufferMode == 1, "buffermode 1 (all filesystems)");
    Check(!t.readFactor, "readfactor 0 is not a factor: left to the setting");
    Check(t.chunkSize && *t.chunkSize == 131072, "chunksize 131072");
  }
  {
    const auto t = TAKE(" 536870912 ", "4", "4.0", "200000", ram32);
    Check(t.memorySize && *t.memorySize == 512, "memorysize with spaces: 512 MB");
    Check(t.bufferMode && *t.bufferMode == 4, "buffermode 4 (network)");
    Check(t.readFactor && *t.readFactor == 400, "readfactor 4.0: 4x (400)");
    Check(t.chunkSize && *t.chunkSize == 128 * 1024, "chunksize 200000: the choice below it, 128 KB");
  }
  {
    // a guide's 1.5x and 20x, a value between two choices, and the first of each list
    Check(TAKE(nullptr, nullptr, "1.5", nullptr, ram32).readFactor.value_or(-1) == 150, "readfactor 1.5: 150");
    Check(TAKE(nullptr, nullptr, "20", nullptr, ram32).readFactor.value_or(-1) == 2000, "readfactor 20: 2000");
    Check(TAKE(nullptr, nullptr, "1", nullptr, ram32).readFactor.value_or(-1) == 110, "readfactor 1: the nearest, 1.1x");
    Check(TAKE(nullptr, nullptr, "8", nullptr, ram32).readFactor.value_or(-1) == 700, "readfactor 8: the nearest, 7x");
    Check(TAKE("1000000", nullptr, nullptr, nullptr, ram32).memorySize.value_or(-1) == 16, "memorysize under 16 MB: 16 MB");
    Check(TAKE("0", nullptr, nullptr, nullptr, ram32).memorySize.value_or(-1) == 0, "memorysize 0: the cache on disk");
    Check(TAKE("104857600", nullptr, nullptr, nullptr, ram32).memorySize.value_or(-1) == 96, "100 MiB: the choice below, 96 MB");
    Check(TAKE(nullptr, nullptr, nullptr, "100", ram32).chunkSize.value_or(-1) == 256, "chunksize 100: the smallest, 256");
  }
  {
    // the bound: a quarter of the RAM, 2 GB at most
    const auto big = TAKE("4294967296", nullptr, nullptr, nullptr, ram32);
    Check(big.memorySize && *big.memorySize == 2048 && big.memoryBounded, "4 GB on 30.5 GiB: 2 GB, bounded");
    const auto eight = TAKE("2147483648", nullptr, nullptr, nullptr, 7600 * MiB);
    Check(eight.memorySize && *eight.memorySize == 1024 && eight.memoryBounded, "2 GB on 7.4 GiB: 1 GB (a quarter)");
    const auto small = TAKE("1073741824", nullptr, nullptr, nullptr, 3 * GiB);
    Check(small.memorySize && *small.memorySize == 768 && small.memoryBounded, "1 GB on 3 GiB: 768 MB");
    const auto fits = TAKE("536870912", nullptr, nullptr, nullptr, 3 * GiB);
    Check(fits.memorySize && *fits.memorySize == 512 && !fits.memoryBounded, "512 MB on 3 GiB: taken as it is");
    const auto unknown = TAKE("4294967296", nullptr, nullptr, nullptr, 0);
    Check(unknown.memorySize && *unknown.memorySize == 2048, "RAM not known: 2 GB at most");
  }
  {
    // what is not a value of the setting is left out
    const auto t = TAKE("256MB", "7", "-1", "abc", ram32);
    Check(!t.memorySize && !t.bufferMode && !t.readFactor && !t.chunkSize && !t.Any(),
          "256MB, buffermode 7, readfactor -1, chunksize abc: all left out");
    Check(!TAKE(nullptr, nullptr, nullptr, nullptr, ram32).Any(), "no <cache>: nothing");
    Check(!TAKE("", " ", "nan", "", ram32).Any(), "empty texts and nan: nothing");
    Check(!TAKE("-5", "-1", "0", "0", ram32).Any(), "negative and zero chunk: nothing");
  }

  // ---- the lists are those of the settings
  const std::string services = Read(tree + "/xbmc/settings/ServicesSettings.cpp");
  const auto same = [&](const char* filler, const auto& list, bool zero)
  {
    std::vector<long long> want(std::begin(list), std::end(list));
    std::vector<long long> got = Filler(services, filler);
    if (zero)
    {
      // (0, the cache on disk or adaptive, besides the choices)
      std::erase(got, 0);
    }
    std::string text;
    for (long long v : got)
      text += std::to_string(v) + " ";
    Check(got == want, std::string(filler) + " lists what FileCacheDefaults.h has: " + text);
  };
  same("SettingOptionsMemorySizesFiller", F::MEMORY_SIZES, true);
  same("SettingOptionsReadFactorsFiller", F::READ_FACTORS, true);
  same("SettingOptionsCacheChunkSizesFiller", F::CHUNK_SIZES, false);
  Check(services.find("return KODI::SETTINGS::FILECACHE::DefaultMemorySize(totalRam);") != std::string::npos,
        "CServicesSettings::DefaultMemorySize() is the rule of FileCacheDefaults.h");
  // ---- and the callers: <cache> is read, applied once the settings are loaded, the RAM default set at the start
  const std::string advanced = Read(tree + "/xbmc/settings/AdvancedSettings.cpp");
  Check(advanced.find("FirstChildElement(\"cache\")") != std::string::npos, "AdvancedSettings.cpp reads <cache>");
  const size_t loaded = advanced.find("void CAdvancedSettings::OnSettingsLoaded()");
  const size_t apply = advanced.find("  ApplyFileCacheDefaults();", loaded);
  const size_t next = advanced.find("\nvoid CAdvancedSettings::", loaded + 1);
  Check(loaded != std::string::npos && apply != std::string::npos && apply < next,
        "OnSettingsLoaded() applies <cache> to the settings");
  Check(advanced.find("TakeAdvanced(") != std::string::npos && advanced.find("SetDefault(*value)") != std::string::npos,
        "ApplyFileCacheDefaults() sets each value as the default of its setting");
  const std::string settings = Read(tree + "/xbmc/settings/Settings.cpp");
  Check(settings.find("CServicesSettings::DefaultMemorySize(memory.totalPhys)") != std::string::npos,
        "Settings.cpp sets the default from the RAM");

  std::printf("CACHEDEFAULTS: %d checks, %d failed\n", checks, fails);
  if (fails)
    return 1;
  std::printf("CACHEDEFAULTS-TEST PASSED\n");
  return 0;
}
