/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.1): the modes of videos kept on HDMI FRL while the menus need it, on nvidia-drm
 * (windowing/gbm/FrlKeep.h, NV_HDMI_FRL_KEEP of nvidia-open-kernel-modules-9078): when Kodi asks for it
 * (MenusNeedFrl(), Wanted(), on the modes of the LG C5 as nvidia-drm gives them on the RTX 4080 SUPER of the
 * tests), in which commits it goes in the request (Stage()), the test made again without it (Without()) and the
 * end of the block of the way to a TMDS mode (Unblock()); (crash) no mode under MIN_KEPT_KHZ kept (ForMode(): 1080p
 * at 23.977 to 60 Hz goes over TMDS, 3840x2160 at 23.976 Hz and 1080p at 120 Hz stay on FRL), with the sequence of a
 * 1080p film and the way back to the menus. Built with the tree's header:
 *   g++ -std=c++20 -Wall -Wextra -Werror -I <kodi>/xbmc -I <kodi>/xbmc/windowing/gbm -o t frl_keep_test.cpp
 */
#include "windowing/gbm/FrlKeep.h"

#include <cstdio>
#include <string>

using namespace KODI::WINDOWING::GBM::FRL_KEEP;

static int g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
  if (!ok)
    ++g_failures;
}

// NV_OUTPUT_LINK (nvidia-open-kernel-modules-9057): bits 0-7 the kind (1 TMDS, 2 TMDS scrambled, 3 FRL, 4 DP),
// 8-15 the FRL rate, 16-23 the format
static uint64_t Link(unsigned kind, unsigned rate = 0, unsigned format = 0)
{
  return kind | rate << 8 | format << 16;
}

int main()
{
  // ---- MenusNeedFrl(): the pixel clocks of the C5's modes against the 600 MHz of TMDS in RGB at 8 bits
  Check(MenusNeedFrl(1188000), "3840x2160 at 120 Hz (1188 MHz): FRL");
  Check(MenusNeedFrl(1186813), "3840x2160 at 119.88 Hz: FRL");
  Check(!MenusNeedFrl(594000), "3840x2160 at 60 Hz (594 MHz): TMDS");
  Check(!MenusNeedFrl(600000), "600 MHz, the most TMDS carries: TMDS");
  Check(MenusNeedFrl(600001), "... and above it: FRL");
  Check(!MenusNeedFrl(497750), "2560x1440 at 120 Hz (497.75 MHz): TMDS");
  Check(!MenusNeedFrl(0), "a clock that is not known: not asked");

  // ---- Wanted(): the menus, and the block of the way to a TMDS mode after a hang
  Check(Wanted(true, false) == Why::MENUS_NEED_FRL, "menus over FRL: asked");
  Check(Wanted(false, false) == Why::MENUS_FIT_TMDS, "menus over TMDS: not asked");
  Check(Wanted(true, true) == Why::TMDS_FALLBACK, "menus going to a TMDS mode after a hang: not asked");
  Check(Wanted(false, true) == Why::TMDS_FALLBACK, "blocked says so whatever the menus");

  // ---- (crash) ForMode(): no mode under MIN_KEPT_KHZ kept on FRL; the clocks of the LG C5's modes
  Check(ForMode(Why::MENUS_NEED_FRL, 74180) == Why::UNDER_MIN, "1920x1080 at 23.977 Hz (74.18 MHz): not kept, TMDS");
  Check(ForMode(Why::MENUS_NEED_FRL, 148500) == Why::UNDER_MIN, "1920x1080 at 60 Hz (148.5 MHz): not kept");
  Check(ForMode(Why::MENUS_NEED_FRL, 295999) == Why::UNDER_MIN, "just under the minimum: not kept");
  Check(ForMode(Why::MENUS_NEED_FRL, 296000) == Why::MENUS_NEED_FRL, "the minimum: kept");
  Check(ForMode(Why::MENUS_NEED_FRL, 296700) == Why::MENUS_NEED_FRL,
        "3840x2160 at 23.976 Hz (296.7 MHz): kept, as from 1.1d");
  Check(ForMode(Why::MENUS_NEED_FRL, 297000) == Why::MENUS_NEED_FRL, "1920x1080 at 120 Hz (297 MHz): kept");
  Check(ForMode(Why::MENUS_NEED_FRL, 1188000) == Why::MENUS_NEED_FRL, "the menus at 4K120: asked");
  Check(ForMode(Why::MENUS_NEED_FRL, 0) == Why::MENUS_NEED_FRL, "a clock that is not known: as before");
  Check(ForMode(Why::MENUS_FIT_TMDS, 74180) == Why::MENUS_FIT_TMDS, "menus that fit TMDS: what Wanted() said");
  Check(ForMode(Why::TMDS_FALLBACK, 74180) == Why::TMDS_FALLBACK, "the way to a TMDS mode: what Wanted() said");

  // ---- Stage(): only with a mode set, with the property, without native Dolby Vision, and only a change
  Check(Stage(true, Why::MENUS_NEED_FRL, 0, false) == 1u, "a mode set with the menus over FRL: 1 staged");
  Check(!Stage(true, Why::MENUS_NEED_FRL, 1, false), "the kernel has 1 already: nothing staged");
  Check(Stage(true, Why::MENUS_FIT_TMDS, 1, false) == 0u, "menus that fit TMDS, the kernel has 1: 0 staged");
  Check(Stage(true, Why::TMDS_FALLBACK, 1, false) == 0u, "the way to a TMDS mode, the kernel has 1: 0 staged");
  Check(!Stage(true, Why::MENUS_FIT_TMDS, 0, false), "the kernel has 0 already: nothing staged");
  Check(!Stage(false, Why::MENUS_NEED_FRL, 0, false), "a commit without a mode set: nothing (no change alone)");
  Check(!Stage(true, Why::MENUS_NEED_FRL, std::nullopt, false), "a connector without the property: nothing");
  Check(!Stage(true, Why::MENUS_NEED_FRL, 0, true), "while native Dolby Vision has the output: nothing");
  Check(!Stage(true, Why::MENUS_FIT_TMDS, 1, true), "... in either direction");

  // ---- Without(): a test refused with 1 is made again with 0
  Check(Without(1) == 0u, "refused with 1: again with 0");
  Check(!Without(0), "a request with 0: nothing to take out");
  Check(!Without(std::nullopt), "a request without it: nothing to take out");

  // ---- Unblock(): the block ends once a mode set went out and the output is over TMDS
  Check(Unblock(true, true, Link(1)), "blocked, mode set, over TMDS now: unblocked");
  Check(Unblock(true, true, Link(2)), "... over TMDS scrambled: unblocked");
  Check(!Unblock(true, true, Link(3, 4)), "still over FRL (the TMDS mode did not go out): stays blocked");
  Check(!Unblock(true, false, Link(1)), "no mode set in this commit: stays blocked");
  Check(!Unblock(true, true, std::nullopt), "the link not said: stays blocked");
  Check(!Unblock(true, true, Link(0)), "nothing driven: stays blocked");
  Check(!Unblock(false, true, Link(1)), "not blocked: nothing to end");

  // ---- the sequence of the tests of 2026-11-18, with this: Kodi's start, a video, back, a hang and its way out
  {
    std::optional<uint64_t> kernel = 0; // nvidia-drm's default
    bool blocked = false;
    // Kodi's start from the console (4K60 over FRL or TMDS) to the menus at 4K120
    auto v = Stage(true, Wanted(MenusNeedFrl(1188000), blocked), kernel, false);
    Check(v == 1u, "sequence: the mode set of Kodi's start asks for it");
    kernel = v;
    // the video's mode set (Dolby Vision has the output from it): nothing staged, the kernel keeps 1
    Check(!Stage(true, Wanted(MenusNeedFrl(1188000), blocked), kernel, true),
          "sequence: the video's mode set leaves it at 1");
    // back to the menus: nothing to change
    Check(!Stage(true, Wanted(MenusNeedFrl(1188000), blocked), kernel, false), "sequence: the way back leaves it at 1");
    // a hang, the way to 4K60 over TMDS: 0 for that mode set
    blocked = true;
    v = Stage(true, Wanted(MenusNeedFrl(1188000), blocked), kernel, false);
    Check(v == 0u, "sequence: the way to a TMDS mode asks for 0");
    kernel = v;
    blocked = !Unblock(blocked, true, Link(2));
    Check(!blocked, "sequence: over TMDS, unblocked");
    // the next video goes back to the chosen 4K120: asked again (NVKMS keeps nothing while the head is over TMDS)
    Check(Stage(true, Wanted(MenusNeedFrl(1188000), blocked), kernel, false) == 1u,
          "sequence: asked again with the next mode set");
  }

  // ---- (crash) a 1080p film with "Adjust display resolution", the menus at 4K120 over FRL (the kernel has 1)
  {
    std::optional<uint64_t> kernel = 1;
    Check(Stage(true, ForMode(Wanted(MenusNeedFrl(1188000), false), 74180), kernel, false) == 0u,
          "1080p film: 0 in the mode set of the video, which the driver sends over TMDS");
    kernel = 0;
    Check(Stage(true, ForMode(Wanted(MenusNeedFrl(1188000), false), 1188000), kernel, false) == 1u,
          "back to the menus at 4K120: 1 again (the change from TMDS goes out with the output off first)");
  }

  std::printf(g_failures ? "FRL-KEEP-TEST FAILED (%d)\n" : "FRL-KEEP-TEST PASSED\n", g_failures);
  return g_failures ? 1 : 0;
}
