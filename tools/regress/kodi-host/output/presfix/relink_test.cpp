// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: what the display's SCDC says after a hot-plug event, and the verdict of the output turned off
// and on again on a scrambled HDMI link of nvidia-drm (HotplugRelink.h: SinkOf(), Decide()).
//
// Issue #24: the "No Signal" after the TV was turned off and on came back with 1.0. A display whose TMDS_Config
// has the setup (Scrambling_Enable and TMDS_Bit_Clock_Ratio on) while its Status_Flags_0 says no clock, or a
// channel not locked, does not read the link: it is NOT_LOCKED, and the output is set up again as for a display
// that lost the setup. A display that shows the picture (clock and three channels locked), one whose flags could
// not be read, a link that the driver says is not scrambled, and an FRL link are as before.
//
// g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -I<kodi>/xbmc/windowing/gbm relink_test.cpp
#include "HotplugRelink.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

using namespace HOTPLUG_RELINK;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

const char* SinkName(Sink s)
{
  switch (s)
  {
    case Sink::KEPT:
      return "KEPT";
    case Sink::LOST:
      return "LOST";
    case Sink::NOT_LOCKED:
      return "NOT_LOCKED";
    default:
      return "UNKNOWN";
  }
}

void Sinks(Link link,
           std::optional<uint8_t> config,
           std::optional<uint8_t> frl,
           std::optional<uint8_t> flags,
           Sink want,
           const std::string& what)
{
  const Sink got = SinkOf(link, config, frl, flags);
  Check(got == want, what + ": " + SinkName(got) + ", not " + SinkName(want));
}

// A hot-plug event after which the display in use is back on a scrambled link, with the output on
Facts Back(Sink sink)
{
  Facts f;
  f.due = true;
  f.sinceHotplugMs = 2500;
  f.nvidia = true;
  f.drivenAsHdmi = true;
  f.connected = true;
  f.on = true;
  f.link = Link::TMDS_SCRAMBLED;
  f.tmdsKHz = 594000;
  f.sink = sink;
  return f;
}
} // namespace

int main()
{
  // ---- SinkOf(): what the display's SCDC says of a scrambled TMDS link
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, 0x0f, Sink::KEPT, "setup and the clock and three channels locked");
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, 0x00, Sink::NOT_LOCKED, "#24: setup, no clock and nothing locked");
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, 0x01, Sink::NOT_LOCKED, "setup, the clock, no channel locked");
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, 0x0b, Sink::NOT_LOCKED, "setup, the clock, channel 1 not locked");
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, 0x0e, Sink::NOT_LOCKED, "setup, three channels, no clock");
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, 0xff, Sink::KEPT, "setup, the flags with bits above 3 set as well");
  Sinks(Link::TMDS_SCRAMBLED, 0x03, 0x00, std::nullopt, Sink::KEPT, "setup, the flags not read: as before");
  Sinks(Link::TMDS_SCRAMBLED, 0x00, 0x00, 0x0f, Sink::LOST, "no setup (a display switched off and on)");
  Sinks(Link::TMDS_SCRAMBLED, 0x01, 0x00, 0x00, Sink::LOST, "scrambling without the 1/40 ratio");
  Sinks(Link::TMDS_SCRAMBLED, std::nullopt, 0x00, 0x0f, Sink::UNKNOWN, "TMDS_Config not read");
  // ... where the driver does not say the link, a mode above 340 MHz is read as a scrambled one
  Sinks(Link::UNKNOWN, 0x03, 0x00, 0x00, Sink::NOT_LOCKED, "link not said, setup, nothing locked");
  Sinks(Link::UNKNOWN, 0x03, 0x00, 0x0f, Sink::KEPT, "link not said, setup, locked");
  Sinks(Link::UNKNOWN, 0x00, 0x05, 0x00, Sink::KEPT, "link not said, an FRL rate: FRL kept, as before");
  // ... a link that the driver says is not scrambled: the flags are not asked
  Sinks(Link::TMDS, 0x03, 0x00, 0x00, Sink::KEPT, "TMDS not scrambled: the flags not asked");
  // ... FRL: as before, FRL_Rate alone
  Sinks(Link::FRL, 0x00, 0x05, 0x00, Sink::KEPT, "FRL at rate 5, whatever the flags");
  Sinks(Link::FRL, 0x03, 0x00, 0x0f, Sink::LOST, "FRL with FRL_Rate 0");
  Sinks(Link::FRL, 0x03, std::nullopt, 0x0f, Sink::UNKNOWN, "FRL, Config_1 not read");

  // ---- Decide(): the verdict with what the SCDC said
  Check(Decide(Back(Sink::KEPT)) == Verdict::SINK_KEPT, "a display that kept the setup and locks: nothing done");
  Check(Decide(Back(Sink::NOT_LOCKED)) == Verdict::RELINK, "#24: a display that does not lock: set up again");
  Check(Decide(Back(Sink::LOST)) == Verdict::RELINK, "a display that lost the setup: set up again");
  Check(Decide(Back(Sink::UNKNOWN)) == Verdict::RELINK, "the SCDC not read: set up again, as before");
  {
    Facts f = Back(Sink::NOT_LOCKED);
    f.otherOutputChanged = true;
    Check(Decide(f) == Verdict::RELINK, "another output changed, the one in use does not lock: set up again");
    f.sink = Sink::UNKNOWN;
    Check(Decide(f) == Verdict::OTHER_OUTPUT, "another output changed, nothing known of this one: as before");
  }
  {
    Facts f = Back(Sink::NOT_LOCKED);
    f.sinceRelinkMs = 4000;
    Check(Decide(f) == Verdict::ECHO, "within 10 s of the last time: the display's answer, as before");
    f.sinceRelinkMs = 20000;
    Check(Decide(f) == Verdict::TOO_SOON, "within 30 s of the last time: not again, as before");
    f.sinceRelinkMs = 31000;
    Check(Decide(f) == Verdict::RELINK, "30 s after: set up again");
  }
  {
    Facts f = Back(Sink::NOT_LOCKED);
    f.nativeDV = true;
    Check(Decide(f) == Verdict::NATIVE_DV, "native Dolby Vision on: its end sets the link up, as before");
  }
  Check(SinkLost(Sink::LOST) && SinkLost(Sink::NOT_LOCKED) && !SinkLost(Sink::KEPT) && !SinkLost(Sink::UNKNOWN),
        "SinkLost(): LOST and NOT_LOCKED only");
  Check(ScdcWanted(Decide(Back(Sink::UNKNOWN))), "the SCDC is read where it can change the verdict");

  if (g_failures)
    std::printf("RELINK-TEST: %d checks, %d failed\n", g_checks, g_failures);
  else
    std::printf("RELINK-TEST PASSED: %d checks\n", g_checks);
  return g_failures ? 1 : 0;
}
