// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of HDR10+ across pictures without metadata (KODI::UTILS::HDR10PLUS::CGap) and of the
// graphics overlay flag (WithGraphicsOverlay()), in HDR10PlusVsif.h.
//
// The runner proves the checks with mutants of HDR10PlusVsif.h: a picture without metadata gets no InfoFrame
// (none held), and PB27 as MakeVsif() leaves it.

#include "utils/HDR10PlusVsif.h"

#include <cstdio>
#include <optional>
#include <string>

using KODI::UTILS::HDR10PLUS::Payload;
using KODI::UTILS::HDR10PLUS::CGap;

static int g_failures = 0;
static int g_checks = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

struct Gap
{
  CGap g;
  std::optional<Payload> Picture(const std::optional<Payload>& own) { return g.Picture(own); }
  void Flush() { g.Flush(); }
  void Reset() { g.Reset(); }
  std::string Message() const { return g.Message(); }
};

static Payload P(uint8_t n)
{
  Payload p{};
  p[0] = 0x8B;
  p[1] = 0x84;
  p[2] = 0x90;
  p[4] = n; // average_maxrgb: one value per scene
  p[26] = 0x40;
  return p;
}

#ifndef GAP_ONLY
static Payload Overlay(const Payload& p, bool on)
{
  return KODI::UTILS::HDR10PLUS::WithGraphicsOverlay(p, on);
}
#endif

// Pictures without metadata until the InfoFrame goes; their number, MISSING_MAX expected
static unsigned int MissingUntilGone(Gap& g, const Payload& last, bool* heldAll)
{
  *heldAll = true;
  for (unsigned int i = 1; i <= 100; ++i)
  {
    const auto out = g.Picture(std::nullopt);
    if (!out)
      return i;
    *heldAll = *heldAll && *out == last;
  }
  return 0;
}

// Pictures with metadata until HDR10+ is back; their number
static unsigned int PresentUntilBack(Gap& g, const Payload& p)
{
  for (unsigned int i = 1; i <= 5000; ++i)
    if (g.Picture(p))
      return i;
  return 0;
}

int main()
{
  const Payload a = P(10), b = P(20), c = P(30);
  {
    Gap g;
    Check(g.Picture(a) == a && g.Picture(b) == b, "pictures with metadata: their own");
    // 11 in a row without: the last one held
    bool held = true;
    for (int i = 0; i < 11; ++i)
      held = held && g.Picture(std::nullopt) == b;
    Check(held, "11 pictures in a row without metadata: the InfoFrame of the last one");
    Check(g.Picture(c) == c, "metadata again after 11: its own at once");
    Check(g.Message().empty(), "a gap of 11: nothing to log");
  }
  {
    Gap g;
    g.Picture(a);
    bool held = false;
    const unsigned int gone = MissingUntilGone(g, a, &held);
    Check(gone == 12 && held, "12 in a row without: 11 held, then none (" +
                                  std::to_string(gone) + ")");
    Check(!g.Message().empty(), "leaving HDR10+ is logged");
    Check(!g.Picture(std::nullopt), "after that, none");
    Check(PresentUntilBack(g, b) == 48, "back after 48 pictures in a row with metadata");
    Check(!g.Message().empty(), "coming back is logged");
    Check(g.Picture(c) == c, "then each its own");
  }
  {
    // each time it leaves again, twice as many, up to 1440
    Gap g;
    g.Picture(a);
    const unsigned int expected[] = {48, 96, 192, 384, 768, 1440, 1440};
    std::string got;
    bool ok = true;
    for (unsigned int e : expected)
    {
      bool held = false;
      ok = ok && MissingUntilGone(g, a, &held) == 12;
      const unsigned int back = PresentUntilBack(g, a);
      got += std::to_string(back) + " ";
      ok = ok && back == e;
    }
    Check(ok, "back after 48, 96, 192, 384, 768, 1440, 1440: " + got);
  }
  {
    // a stream whose first pictures have none
    Gap g;
    bool none = true;
    for (int i = 0; i < 11; ++i)
      none = none && !g.Picture(std::nullopt);
    Check(none && g.Picture(a) == a, "11 at the start without: HDR10+ with the first that has it");
    Gap h;
    for (int i = 0; i < 12; ++i)
      h.Picture(std::nullopt);
    Check(h.Message().empty(), "12 at the start without: nothing was sent, nothing to log");
    Check(PresentUntilBack(h, a) == 48, "12 at the start without: HDR10+ after 48 with it");
  }
  {
    // a gap that a new scene ends: the held one is the last, not the first
    Gap g;
    g.Picture(a);
    g.Picture(b);
    Check(g.Picture(std::nullopt) == b, "the last InfoFrame is held, not an older one");
  }
  {
    // a flush: nothing from before is held; leaving persists over it
    Gap g;
    g.Picture(a);
    g.Flush();
    Check(!g.Picture(std::nullopt), "after a flush, nothing from before the seek");
    Check(g.Picture(b) == b, "after a flush, the first with metadata at once");
    Gap h;
    h.Picture(a);
    for (int i = 0; i < 12; ++i)
      h.Picture(std::nullopt);
    h.Flush();
    Check(PresentUntilBack(h, a) == 48, "a flush does not end the wait for the metadata");
    Gap k;
    k.Picture(a);
    for (int i = 0; i < 11; ++i)
      k.Picture(std::nullopt);
    k.Flush();
    k.Picture(std::nullopt);
    Check(k.Picture(b) == b, "a flush starts the count of missing pictures again");
  }
  {
    // a new stream: everything as at first
    Gap g;
    g.Picture(a);
    for (int r = 0; r < 3; ++r)
    {
      for (int i = 0; i < 12; ++i)
        g.Picture(std::nullopt);
      PresentUntilBack(g, a);
    }
    g.Reset();
    for (int i = 0; i < 12; ++i)
      g.Picture(std::nullopt);
    Check(PresentUntilBack(g, a) == 48, "a new stream: back after 48 again");
  }
#ifndef GAP_ONLY
  {
    // the graphics overlay flag: bit 7 of PB27, nothing else
    const Payload v = P(10);
    const Payload on = Overlay(v, true);
    Check(on[26] == 0xC0, "overlay: PB27 0xC0 (graphics overlay and no delay)");
    Payload rest = on;
    rest[26] = v[26];
    Check(rest == v, "overlay: no other byte changes");
    Check(Overlay(on, false)[26] == 0x40, "no overlay: PB27 0x40 again");
    Check(Overlay(on, true) == on, "overlay twice: the same");
    Check(Overlay(v, false) == v, "no overlay on a payload without it: the same");
  }
#endif

  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
