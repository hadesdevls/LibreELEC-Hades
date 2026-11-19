// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// LibreELEC Hades, TEST ONLY (section 4: the wait that counts from the display coming back): the decisions of CDVStartSignal (DVStartSignal.h), as the
// window system asks them (CWinSystemGbmGLESContext::DVStartSignal(), PresentRender(),
// SetVideoOutput(), EndDVBridge()).
// Build: g++ -std=c++17 -Wall -Wextra -Werror -I<tree>/xbmc/windowing/gbm dvstartsignal_test.cpp
#include "DVStartSignal.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using KODI::WINDOWING::GBM::CDVStartSignal;
using Refusal = CDVStartSignal::Refusal;
using Wait = CDVStartSignal::Wait;

static int g_checks, g_failed;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failed;
    std::printf("FAIL %s\n", what.c_str());
  }
}

// everything eligible: the RTX 4080 SUPER with the LG C5, a profile 7 stream, the GUI at 2160p120
static CDVStartSignal::Change Eligible()
{
  CDVStartSignal::Change c;
  c.switchedOn = true;
  c.nativeOpens = true;
  c.holds = true;
  c.transportMode = true;
  c.standard = true;
  c.disabled = false;
  c.videoOutput = false;
  c.profile = 7;
  c.compatibility = 6;
  c.busy = false;
  c.windowFits = true;
  return c;
}

int main()
{
  // 1. eligible
  {
    CDVStartSignal s;
    Check(s.Refuses(Eligible()) == Refusal::NONE, "everything eligible: set up");
    Check(!s.Off() && !s.Pending(), "nothing pending, not off, at first");
    Check(s.Waiting(0, true) == Wait::NONE && s.Waiting(123456, false) == Wait::NONE,
          "nothing waits before it is sent");
  }
  // 2. each condition alone refuses, with its own reason
  {
    struct Case
    {
      const char* name;
      void (*apply)(CDVStartSignal::Change&);
      Refusal want;
    };
    const Case cases[] = {
        {"not native", [](CDVStartSignal::Change& c) { c.nativeOpens = false; }, Refusal::NOT_NATIVE},
        {"switched off", [](CDVStartSignal::Change& c) { c.switchedOn = false; }, Refusal::SWITCHED_OFF},
        {"no hold", [](CDVStartSignal::Change& c) { c.holds = false; }, Refusal::NO_HOLD},
        {"not a transport mode", [](CDVStartSignal::Change& c) { c.transportMode = false; },
         Refusal::NOT_TRANSPORT_MODE},
        {"not Standard", [](CDVStartSignal::Change& c) { c.standard = false; }, Refusal::NOT_STANDARD},
        {"disabled", [](CDVStartSignal::Change& c) { c.disabled = true; }, Refusal::DISABLED},
        {"another video's output", [](CDVStartSignal::Change& c) { c.videoOutput = true; },
         Refusal::VIDEO_OUTPUT},
        {"profile 8.4", [](CDVStartSignal::Change& c) { c.profile = 8, c.compatibility = 4; },
         Refusal::PROFILE},
        {"busy", [](CDVStartSignal::Change& c) { c.busy = true; }, Refusal::BUSY},
        {"window", [](CDVStartSignal::Change& c) { c.windowFits = false; }, Refusal::WINDOW},
    };
    for (const auto& k : cases)
    {
      CDVStartSignal s;
      CDVStartSignal::Change c = Eligible();
      k.apply(c);
      Check(s.Refuses(c) == k.want, std::string("refused alone: ") + k.name);
    }
    // a video that does not go out natively is never set up, whatever else
    CDVStartSignal s;
    CDVStartSignal::Change c;
    c.nativeOpens = false;
    Check(s.Refuses(c) == Refusal::NOT_NATIVE, "nothing set: not native first");
    c = Eligible();
    c.nativeOpens = false;
    c.switchedOn = false;
    Check(s.Refuses(c) == Refusal::NOT_NATIVE, "not native before switched off (nothing to log)");
    // every refusal has its own words
    std::set<std::string> words;
    for (int r = static_cast<int>(Refusal::NONE); r <= static_cast<int>(Refusal::WINDOW); ++r)
    {
      const char* w = CDVStartSignal::Why(static_cast<Refusal>(r));
      Check(w && *w && std::strcmp(w, "?") != 0, "every refusal says why");
      words.insert(w ? w : "");
    }
    Check(words.size() == static_cast<size_t>(Refusal::WINDOW) + 1, "every refusal says it differently");
  }
  // 3. the profiles
  {
    struct P
    {
      int profile, compatibility;
      bool taken;
    };
    const P profiles[] = {{5, 0, true},  {7, 6, true},   {8, 1, true},   {8, 6, true},
                          {8, 2, false}, {8, 4, false},  {8, 0, false},  {10, 0, false},
                          {10, 1, false}, {10, 4, false}, {4, 2, false}, {0, 0, false},
                          {9, 2, false}};
    for (const auto& p : profiles)
    {
      CDVStartSignal s;
      CDVStartSignal::Change c = Eligible();
      c.profile = p.profile;
      c.compatibility = p.compatibility;
      Check(CDVStartSignal::ProfileTaken(p.profile, p.compatibility) == p.taken &&
                (s.Refuses(c) == Refusal::NONE) == p.taken,
            "profile " + std::to_string(p.profile) + "." + std::to_string(p.compatibility) +
                (p.taken ? " taken" : " not taken"));
    }
  }
  // 4. the wait: while the player plays; WAIT_MS from the display coming back (DisplayBack()),
  //    WAIT_MS + DISPLAY_WAIT_MS from the commit while it has not; at once when the player stops
  {
    CDVStartSignal s;
    const int64_t t0 = 5000000;
    s.Sent(t0);
    Check(s.Pending() && s.SentMs() == t0 && !s.Back(), "sent: pending, the display not back yet");
    Check(s.Waiting(t0, true) == Wait::WAITING, "waits at once");
    Check(s.Waiting(t0 + 530, true) == Wait::WAITING, "waits at 530 ms (the renderer of the test)");
    Check(s.Waiting(t0 + CDVStartSignal::WAIT_MS, true) == Wait::WAITING,
          "the display not back: no deadline at WAIT_MS from the commit (Kodi's own wait for the display)");
    Check(s.Waiting(t0 + CDVStartSignal::WAIT_MS + CDVStartSignal::DISPLAY_WAIT_MS - 1, true) ==
              Wait::WAITING,
          "the display not back: waits up to WAIT_MS + DISPLAY_WAIT_MS");
    Check(s.Waiting(t0 + CDVStartSignal::WAIT_MS + CDVStartSignal::DISPLAY_WAIT_MS, true) ==
              Wait::DEADLINE,
          "the display never back: the deadline at WAIT_MS + DISPLAY_WAIT_MS");
    Check(s.Waiting(t0 + 1, false) == Wait::STOPPED, "the player stopped: at once");
    Check(s.Waiting(t0 + CDVStartSignal::WAIT_MS + CDVStartSignal::DISPLAY_WAIT_MS, false) == Wait::STOPPED,
          "the player stopped, the deadline passed too: stopped (the stop is seen first)");
    Check(CDVStartSignal::WAIT_MS >= 3000 && CDVStartSignal::WAIT_MS <= 10000,
          "the wait is bounded: no less than a held change waits (3 s), at most 10 s");
    Check(CDVStartSignal::DISPLAY_WAIT_MS >= 20000 && CDVStartSignal::DISPLAY_WAIT_MS <= 30000,
          "the wait for the display covers the longest delay that can be set (20 s), and is bounded");
  }
  for (const int64_t delay : {int64_t{0}, int64_t{5000}, int64_t{15000}, int64_t{20000}})
  {
    // Kodi's wait after the change of the refresh rate (0, 5, 15, 20 s), then the video opens
    CDVStartSignal s;
    const int64_t t0 = 777000;
    s.Sent(t0);
    s.DisplayBack(t0 + delay);
    const std::string d = " (delay " + std::to_string(delay) + " ms)";
    Check(s.Back() && s.BackMs() == t0 + delay, "the display back: noted" + d);
    Check(s.Waiting(t0 + delay + CDVStartSignal::WAIT_MS - 1, true) == Wait::WAITING,
          "waits WAIT_MS from the display coming back" + d);
    Check(s.Waiting(t0 + delay + CDVStartSignal::WAIT_MS, true) == Wait::DEADLINE,
          "the deadline at WAIT_MS from the display coming back" + d);
    Check(s.Waiting(t0 + delay + 20000, true) == (20000 >= CDVStartSignal::WAIT_MS ? Wait::DEADLINE : Wait::WAITING),
          "an opening of 20 s after the display came back passes the deadline" + d);
    s.DisplayBack(t0 + delay + 3000);
    Check(s.BackMs() == t0 + delay, "told again: the first time counts" + d);
    Check(s.Waiting(t0 + delay + 1, false) == Wait::STOPPED, "the player stopped: at once" + d);
    Check(s.Waiting(t0 + delay + CDVStartSignal::WAIT_MS, false) == Wait::STOPPED,
          "the player stopped, the deadline passed too: stopped (the stop is seen first)" + d);
    s.Ended();
    s.DisplayBack(t0 + delay + 4000);
    Check(s.Waiting(t0 + delay + 4001, true) == Wait::NONE, "nothing waits after the end" + d);
    s.Sent(t0 + 100000);
    Check(!s.Back() && s.Waiting(t0 + 100000 + CDVStartSignal::WAIT_MS, true) == Wait::WAITING,
          "a new signal: the display not back for it" + d);
  }
  {
    CDVStartSignal s;
    s.DisplayBack(10);
    Check(!s.Back(), "the display back with nothing pending: not noted");
  }
  {
    // the deadline is ended (Ended()) by the window system, not counted: ten in a row stay set up
    CDVStartSignal s;
    for (int i = 0; i < 10; ++i)
    {
      s.Sent(i * 100000);
      s.DisplayBack(i * 100000 + 1000);
      if (s.Waiting(i * 100000 + 1000 + CDVStartSignal::WAIT_MS, true) == Wait::DEADLINE)
        s.Ended();
    }
    Check(!s.Off() && s.Refuses(Eligible()) == Refusal::NONE,
          "ten videos that were still opening at the deadline: still set up");
  }
  // 5. what ends the wait, and what it counts
  {
    CDVStartSignal s;
    s.Sent(0);
    s.Taken();
    Check(!s.Pending() && s.Waiting(1, true) == Wait::NONE && !s.Off(), "taken: nothing waits");
    s.Sent(0);
    s.Ended();
    Check(!s.Pending() && !s.Off(), "ended: nothing waits, nothing counted");
    for (int i = 0; i < 10; ++i)
    {
      s.Sent(i);
      s.Ended();
    }
    Check(!s.Off() && s.Refuses(Eligible()) == Refusal::NONE, "ended ten times: still set up");
  }
  {
    CDVStartSignal s;
    s.Sent(0);
    s.NotTaken();
    Check(!s.Pending() && !s.Off(), "not taken once: still set up");
    Check(s.Refuses(Eligible()) == Refusal::NONE, "... for the next video");
    s.Sent(1);
    s.NotTaken();
    Check(s.Off(), "not taken twice in a row: off");
    Check(s.Refuses(Eligible()) == Refusal::NOT_TAKEN_BEFORE, "... refused for the next videos");
    CDVStartSignal::Change c = Eligible();
    c.nativeOpens = false;
    Check(s.Refuses(c) == Refusal::NOT_NATIVE, "... and a video that is not native says only that");
    c = Eligible();
    c.switchedOn = false;
    Check(s.Refuses(c) == Refusal::SWITCHED_OFF, "... switched off says that first");
  }
  {
    CDVStartSignal s;
    s.Sent(0);
    s.NotTaken();
    s.Sent(1);
    s.Taken();
    s.Sent(2);
    s.NotTaken();
    Check(!s.Off(), "not taken, taken, not taken: not in a row, still set up");
    s.Sent(3);
    s.Ended();
    s.Sent(4);
    s.NotTaken();
    Check(s.Off(), "an end between two that were not taken does not break the row");
  }
  {
    CDVStartSignal s;
    s.Sent(0);
    s.Refused();
    Check(!s.Pending() && s.Off(), "refused: off at once");
    Check(s.Refuses(Eligible()) == Refusal::NOT_TAKEN_BEFORE, "... refused for the next videos");
    s.Taken();
    Check(s.Off(), "a refusal is not undone by anything after it (until Kodi starts again)");
  }
  {
    CDVStartSignal s;
    s.Refused();
    Check(s.Off(), "refused before it was sent (native output could not begin, not drawn): off");
  }
  std::printf("%s: %d checks, %d failed\n", g_failed ? "DVSTARTSIGNAL-FAILED" : "DVSTARTSIGNAL-OK",
              g_checks, g_failed);
  return g_failed ? 1 : 0;
}
