/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): the resolutions of the display replaced
 * while other threads read them (settings/ResolutionListReplace.h, used by
 * CDisplaySettings::ReplaceCustomResolutions()). The real header of the tree replaces; the
 * accessors of CDisplaySettings are transcribed from the tree:
 *  - GetResolutionInfo(): the lock of the list held to find the entry, a reference given, read by
 *    the caller after the lock is let go (a JSON-RPC request, an add-on, a setting's options);
 *  - ResolutionInfoSize(): under the lock (as the tree has it now);
 *  - the way a window system made the list before: ClearCustomResolutions() (without the lock, as
 *    it was), AddResolutionInfo() for each resolution (each under the lock), then RES_DESKTOP.
 * Usage: reslist_test same            the result is the one of the way before, entry by entry
 *        reslist_test race new|old <rounds>   readers on 4 threads, the list made again on one
 * Built with AddressSanitizer or ThreadSanitizer: "race new" must be clean, "race old" is not
 * (a reference that a reader holds names an entry that the rebuild destroyed or moved).
 */
#include "settings/ResolutionListReplace.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

constexpr std::size_t RES_DESKTOP = 16;
constexpr std::size_t RES_CUSTOM = 17;

struct Info // what matters of RESOLUTION_INFO here: a size and strings on the heap
{
  int w = 0;
  int blanking = 0;
  std::string strMode;
  std::string strId;
};

std::string NameOf(int w)
{
  // (longer than what a string keeps in itself: on the heap, as "3840x2160 @ 60.000000 Hz")
  return w ? "mode " + std::to_string(w) + " x something @ 60.000000 Hz" : std::string();
}

Info Corrected(const Info& in) // AddResolutionInfo()'s corrections: one field changes
{
  Info out(in);
  if (out.w == 1920)
    out.blanking = 45;
  return out;
}

struct Settings // CDisplaySettings, the parts that matter
{
  mutable std::recursive_mutex critical;
  std::vector<Info> resolutions = std::vector<Info>(RES_CUSTOM);
  std::vector<std::vector<Info>> retired;
  Info empty;

  const Info& Get(std::size_t index) const
  {
    std::unique_lock lock(critical);
    if (index >= resolutions.size())
      return empty;
    return resolutions[index];
  }
  std::size_t Size() const
  {
    std::unique_lock lock(critical);
    return resolutions.size();
  }
  // The way before: ClearCustomResolutions() (no lock), AddResolutionInfo() each, RES_DESKTOP
  void MakeOld(const std::vector<Info>& list, std::optional<std::size_t> desktop)
  {
    if (resolutions.size() > RES_CUSTOM)
      resolutions.erase(resolutions.begin() + RES_CUSTOM, resolutions.end());
    for (const Info& res : list)
    {
      std::unique_lock lock(critical);
      resolutions.push_back(Corrected(res));
    }
    if (desktop)
    {
      std::unique_lock lock(critical);
      resolutions[RES_DESKTOP] = list[*desktop];
    }
  }
  // The tree: CDisplaySettings::ReplaceCustomResolutions()
  void MakeNew(const std::vector<Info>& list, std::optional<std::size_t> desktop)
  {
    std::unique_lock lock(critical);
    RESOLUTION_LIST::Replace(resolutions, list, desktop, RES_DESKTOP, RES_CUSTOM, Corrected, retired);
  }
};

struct Lcg
{
  uint64_t s;
  uint32_t Next()
  {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(s >> 33);
  }
  uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
};

std::vector<Info> RandomList(Lcg& rng)
{
  std::vector<Info> list(1 + rng.Below(60));
  for (std::size_t i = 0; i < list.size(); ++i)
  {
    list[i].w = rng.Below(2) ? 1920 : 640 + static_cast<int>(rng.Below(4000));
    list[i].strMode = NameOf(list[i].w);
    list[i].strId = std::to_string(i);
  }
  return list;
}

bool SameInfo(const Info& a, const Info& b)
{
  return a.w == b.w && a.blanking == b.blanking && a.strMode == b.strMode && a.strId == b.strId;
}

int TestSame()
{
  int failures = 0, checks = 0;
  Lcg rng{7};
  for (int run = 0; run < 5000; ++run)
  {
    Settings before, now;
    // the same start: a list made the way before, with a desktop entry
    const std::vector<Info> first = RandomList(rng);
    before.MakeOld(first, 0);
    now.MakeOld(first, 0);
    const std::vector<Info> kept = now.resolutions;
    const std::vector<Info> list = RandomList(rng);
    std::optional<std::size_t> desktop;
    if (rng.Below(4) != 0)
      desktop = rng.Below(static_cast<uint32_t>(list.size()));
    before.MakeOld(list, desktop);
    now.MakeNew(list, desktop);
    bool same = before.resolutions.size() == now.resolutions.size();
    for (std::size_t i = 0; same && i < now.resolutions.size(); ++i)
      same = SameInfo(before.resolutions[i], now.resolutions[i]);
    ++checks;
    if (!same)
    {
      ++failures;
      std::printf("FAIL: run %d: the list is not the one of the way before\n", run);
    }
    bool retired = now.retired.size() == 1 && now.retired[0].size() == kept.size();
    for (std::size_t i = 0; retired && i < kept.size(); ++i)
      retired = SameInfo(now.retired[0][i], kept[i]);
    ++checks;
    if (!retired)
    {
      ++failures;
      std::printf("FAIL: run %d: the list before is not kept as it was\n", run);
    }
  }
  // the storage of the list before is the one that was referred to
  {
    Settings s;
    Lcg r{9};
    s.MakeNew(RandomList(r), 0);
    const Info* held = &s.Get(RES_CUSTOM);
    const std::string name = held->strMode;
    s.MakeNew(RandomList(r), 0);
    ++checks;
    if (held != &s.retired.back()[RES_CUSTOM] || held->strMode != name)
    {
      ++failures;
      std::printf("FAIL: a reference into the list before does not name its entry any more\n");
    }
  }
  std::printf("%s: same: %d of %d checks failed\n", failures ? "FAILED" : "PASSED", failures, checks);
  return failures ? 1 : 0;
}

int TestRace(bool useNew, int rounds)
{
  Settings s;
  Lcg rng{11};
  s.MakeOld(RandomList(rng), 0);
  std::atomic<bool> stop{false};
  std::atomic<long> reads{0}, wrong{0};
  std::vector<std::thread> readers;
  for (int t = 0; t < 4; ++t)
  {
    readers.emplace_back(
        [&s, &stop, &reads, &wrong, t]
        {
          Lcg r{static_cast<uint64_t>(100 + t)};
          while (!stop.load(std::memory_order_relaxed))
          {
            const std::size_t n = s.Size();
            const Info& held = s.Get(r.Below(static_cast<uint32_t>(n)));
            // what a reader does with it after the lock (CGraphicContext::ResInfo(), a filler)
            const Info copy = held;
            if (copy.strMode != NameOf(copy.w))
              wrong.fetch_add(1, std::memory_order_relaxed);
            reads.fetch_add(1, std::memory_order_relaxed);
          }
        });
  }
  for (int i = 0; i < rounds; ++i)
  {
    const std::vector<Info> list = RandomList(rng);
    if (useNew)
      s.MakeNew(list, 0);
    else
      s.MakeOld(list, 0);
  }
  stop = true;
  for (auto& reader : readers)
    reader.join();
  std::printf("race %s: %d lists made again, %ld reads, %ld entries read half made\n",
              useNew ? "new" : "old", rounds, reads.load(), wrong.load());
  return wrong.load() ? 1 : 0;
}

int main(int argc, char** argv)
{
  if (argc > 1 && std::strcmp(argv[1], "same") == 0)
    return TestSame();
  if (argc > 2 && std::strcmp(argv[1], "race") == 0)
    return TestRace(std::strcmp(argv[2], "new") == 0, argc > 3 ? std::atoi(argv[3]) : 20000);
  std::printf("usage: reslist_test same | race new|old [rounds]\n");
  return 2;
}
