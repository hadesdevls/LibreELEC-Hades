/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): utils/RetiredKept.h, how long what a replacement retires is
 * kept, and with settings/ResolutionListReplace.h as CDisplaySettings::ReplaceCustomResolutions() uses them, on a
 * clock of the test: a reference into a list stays readable (AddressSanitizer would stop the test) until the list
 * goes, which is never before sixteen lists after it and a minute; and the lists kept are bounded.
 * Build: g++ -std=c++20 -fsanitize=address,undefined -I <tree>/xbmc retiredkept_test.cpp
 */
#include "settings/ResolutionListReplace.h"
#include "utils/RetiredKept.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;
void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

std::vector<int64_t> Since(std::size_t n, int64_t at)
{
  return std::vector<int64_t>(n, at);
}
const auto none = [](std::size_t) { return false; };

struct Info // what matters of RESOLUTION_INFO here: strings on the heap
{
  std::string strMode;
  std::string strId;
};

// One list made again every stepMs, as ReplaceCustomResolutions() does it, for steps; the reference into the
// first list is read after every replacement until that list goes. Gives the most lists kept at once.
std::size_t Run(int64_t stepMs, int steps, int& readsOfFirst, int& stepGone, int64_t& msGone)
{
  constexpr std::size_t desktopAt = 0, firstCustom = 1;
  std::vector<Info> list(firstCustom);
  std::vector<std::vector<Info>> retired;
  std::vector<int64_t> since;
  std::vector<Info> resolutions;
  for (int i = 0; i < 40; ++i)
    resolutions.push_back({"3840x2160 @ 23.976 Hz, mode number " + std::to_string(i), std::to_string(i)});
  const auto correct = [](const Info& i) { return i; };
  list.insert(list.end(), resolutions.begin(), resolutions.end());
  // a reader holds a reference into the list that is current now, which the first replacement retires
  const Info& first = list[5];
  const std::string expected = first.strMode;
  std::size_t most = 0;
  bool firstThere = true;
  readsOfFirst = 0;
  stepGone = -1;
  msGone = -1;
  for (int step = 1; step <= steps; ++step)
  {
    const int64_t now = step * stepMs;
    resolutions[0].strMode = "the list of step " + std::to_string(step);
    RESOLUTION_LIST::Replace(list, resolutions, std::optional<std::size_t>(0), desktopAt, firstCustom, correct,
                             retired);
    since.push_back(now);
    // retired[0] is the list that was current at the start (it was retired by the first replacement)
    const auto go = RETIRED_KEPT::ThatGo(since, now, none);
    if (firstThere && !go.empty() && go.front() == 0)
    {
      firstThere = false;
      stepGone = step;
      msGone = now - since[0];
    }
    if (firstThere)
    {
      if (first.strMode != expected)
        return 0;
      ++readsOfFirst;
    }
    RETIRED_KEPT::LetGo(retired, since, go);
    most = std::max(most, retired.size());
    if (retired.size() != since.size())
      return 0;
  }
  return most;
}
} // namespace

int main()
{
  using RETIRED_KEPT::KEPT;
  using RETIRED_KEPT::KEPT_MS;
  Check(RETIRED_KEPT::ThatGo(Since(KEPT, 0), 10 * KEPT_MS, none).empty(), "the 16 newest are kept, however old");
  {
    const auto go = RETIRED_KEPT::ThatGo(Since(KEPT + 4, 0), KEPT_MS, none);
    Check(go == std::vector<std::size_t>({0, 1, 2, 3}), "20 retired a minute ago: the 4 oldest go");
  }
  {
    const auto go = RETIRED_KEPT::ThatGo(Since(KEPT + 4, 0), KEPT_MS, [](std::size_t i) { return i == 1; });
    Check(go == std::vector<std::size_t>({0, 2, 3}), "... but not one that something stored points into");
  }
  Check(RETIRED_KEPT::ThatGo(Since(KEPT + 4, 1), KEPT_MS, none).empty(),
        "20 retired a minute less a millisecond ago: none goes");
  {
    std::vector<int64_t> since = Since(KEPT + 3, 0);
    since[1] = 500;
    const auto go = RETIRED_KEPT::ThatGo(since, KEPT_MS, none);
    Check(go == std::vector<std::size_t>({0, 2}), "each by its own time");
  }
  {
    std::vector<std::vector<int>> retired = {{0}, {1}, {2}, {3}, {4}};
    std::vector<int64_t> since = {0, 1, 2, 3, 4};
    const int* inside = retired[3].data(); // (a reference into a list that stays)
    RETIRED_KEPT::LetGo(retired, since, {0, 2});
    Check(retired.size() == 3 && since == std::vector<int64_t>({1, 3, 4}) && retired[0][0] == 1 &&
              retired[1][0] == 3 && retired[2][0] == 4 && retired[1].data() == inside && *inside == 3,
          "what goes goes with its time; what stays stays where it is");
  }
  int reads = 0, gone = 0;
  int64_t ms = 0;
  // As Kodi makes its resolutions again at most (a hot-plug event, then 3 s without one)
  std::size_t most = Run(3000, 1000, reads, gone, ms);
  Check(most > 0, "every 3 s, 1000 times: every read of the reference read what it read first");
  Check(gone > static_cast<int>(KEPT) && ms >= KEPT_MS && reads == gone - 1,
        "every 3 s: the first list went only after " + std::to_string(gone - 1) + " lists after it and " +
            std::to_string(ms) + " ms (at least 16 and 60000)");
  Check(most <= KEPT + KEPT_MS / 3000 + 1,
        "every 3 s: at most " + std::to_string(most) + " lists kept at once (bound " +
            std::to_string(KEPT + KEPT_MS / 3000 + 1) + ")");
  // Faster than Kodi makes them: bounded by the time
  most = Run(100, 3000, reads, gone, ms);
  Check(most > 0 && ms >= KEPT_MS && most <= KEPT + KEPT_MS / 100 + 1,
        "every 100 ms: the first list kept " + std::to_string(ms) + " ms, at most " + std::to_string(most) +
            " lists kept at once");
  // Slower: bounded by the count
  most = Run(120000, 200, reads, gone, ms);
  Check(most > 0 && gone == static_cast<int>(KEPT) + 1 && reads == static_cast<int>(KEPT) && most <= KEPT + 1,
        "every 2 minutes: the first list went after 16 lists after it, at most " + std::to_string(most) +
            " kept at once");
  std::printf("%s (%d failed)\n", g_failures ? "RETIRED-KEPT FAILED" : "RETIRED-KEPT PASSED", g_failures);
  return g_failures != 0;
}
