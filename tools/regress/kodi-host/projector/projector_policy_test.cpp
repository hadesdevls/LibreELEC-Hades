// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// projector_policy_test: what the projector control of Hades asks of a projector (xbmc/network/ProjectorPolicy.h), the
// shape of a picture from its frame and bar (ProjectorFeed.cpp) and which addresses are of a home network
// (ReadAddress(), ProjectorLink.cpp), with the files of the tree as they are. Prints "ok: ..." or "FAIL: ..." per
// check and "PROJECTOR-POLICY PASSED" or "PROJECTOR-POLICY FAILED (n)".
#include "network/ProjectorFeed.h"
#include "network/ProjectorLink.h"
#include "network/ProjectorPolicy.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace PROJECTOR;

namespace
{
int g_fails = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what)
{
  g_checks++;
  if (!ok)
    g_fails++;
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
}

Shape Known(double ratio, unsigned int video = 1)
{
  Shape s;
  s.valid = true;
  s.ratio = ratio;
  s.known = true;
  s.video = video;
  return s;
}

Shape Unknown(double ratio, unsigned int video = 1)
{
  Shape s = Known(ratio, video);
  s.known = false;
  return s;
}

//! the lens fed a shape every 0.1 s from t0 to t1 (playing), the memory at the end and the times it changed
struct Run
{
  int memory = 0;
  int changes = 0;
  double lastChange = -1.0;
};
Run Feed(CLensFollow& f, double t0, double t1, const Shape& shape, const LensSettings& s, bool playing = true)
{
  Run r;
  for (double t = t0; t < t1 - 1e-9; t += 0.1)
  {
    const LensWanted w = f.Update(t, playing, shape, s);
    if (w.changed)
    {
      r.changes++;
      r.lastChange = t;
    }
    r.memory = w.memory;
  }
  return r;
}
} // namespace

int main()
{
  LensSettings s; // memory 1 narrow, 2.10 -> memory 2, hold 10 s, back at stop

  // ---- the classes of shapes
  Check(ClassOf(1.78, s, -1) == 0 && ClassOf(1.85, s, -1) == 0 && ClassOf(2.00, s, -1) == 0,
        "16:9, 1.85:1 and 2.00:1 are narrower pictures");
  Check(ClassOf(2.20, s, -1) == 1 && ClassOf(2.39, s, -1) == 1 && ClassOf(2.76, s, -1) == 1,
        "2.20:1, 2.39:1 and 2.76:1 are wide pictures without a third memory");
  Check(ClassOf(2.10, s, 0) == 1, "a picture of exactly the threshold is wide");
  Check(ClassOf(2.09, s, 1) == 1 && ClassOf(2.08, s, 1) == 1,
        "wide: 0.03 under the threshold is not yet narrower (no back and forth at the edge)");
  Check(ClassOf(2.06, s, 1) == 0, "wide: more than 0.03 under the threshold is narrower");
  Check(ClassOf(2.09, s, 0) == 0, "narrower: under the threshold stays narrower");
  {
    LensSettings t = s;
    t.widestMemory = 3;
    t.widestFrom = 2.50;
    Check(ClassOf(2.76, t, -1) == 2 && MemoryOfClass(2, t) == 3, "with a third memory, 2.76:1 is the widest");
    Check(ClassOf(2.39, t, -1) == 1, "with a third memory, 2.39:1 stays wide");
    Check(ClassOf(2.48, t, 2) == 2 && ClassOf(2.45, t, 2) == 1, "the widest: the same band under its threshold");
  }

  // ---- the lens through a video
  {
    CLensFollow f;
    Run r = Feed(f, 0.0, 1.95, Known(2.39), s);
    Check(r.memory == 0 && r.changes == 0, "a Dolby Vision film with level 5: nothing before 2 s");
    r = Feed(f, 1.95, 2.5, Known(2.39), s);
    Check(r.memory == 2 && r.changes == 1 && f.Now().why == WHY_START, "... memory 2 at 2 s, at the start");
    r = Feed(f, 2.5, 30.0, Known(2.39), s);
    Check(r.changes == 0 && r.memory == 2, "... and it stays");
    // IMAX scenes: 1.90:1 for 5 s does not move the lens; for 12 s it does, after 10 s
    r = Feed(f, 30.0, 35.0, Known(1.90), s);
    Run back = Feed(f, 35.0, 40.0, Known(2.39), s);
    Check(r.changes == 0 && back.changes == 0 && back.memory == 2, "an IMAX scene of 5 s does not move the lens");
    r = Feed(f, 40.0, 52.0, Known(1.90), s);
    Check(r.changes == 1 && r.memory == 1 && std::abs(r.lastChange - 50.0) < 0.15 && f.Now().why == WHY_SHAPE,
          "an IMAX scene of 12 s moves it after the hold of 10 s");
    // bars that come and go every second (a dark scene, a fade): nothing moves
    int changes = 0;
    for (int i = 0; i < 60; i++)
      changes += Feed(f, 52.0 + i, 53.0 + i, i % 2 ? Known(2.39) : Known(1.78), s).changes;
    Check(changes == 0 && f.Now().memory == 1, "shapes that change every second for a minute move nothing");
  }
  {
    // a remux that opens dark: the frame 16:9, no bar seen yet; then the bars are measured
    CLensFollow f;
    Run r = Feed(f, 0.0, 30.0, Unknown(16.0 / 9.0), s);
    Check(r.memory == 1 && r.changes == 1 && std::abs(r.lastChange - 10.0) < 0.15,
          "a 16:9 frame without bars: the memory of narrower pictures, once, after the hold");
    r = Feed(f, 30.0, 33.0, Known(2.40), s);
    Check(r.memory == 2 && std::abs(r.lastChange - 32.0) < 0.15,
          "the first bar measured is taken after 2 s, not after the hold (the film just started)");
    r = Feed(f, 33.0, 40.0, Known(1.78), s);
    Check(r.changes == 0, "after it, a change waits for the hold");
  }
  {
    // a remux that opens bright: its bars are measured after 2.5 s; the lens moves once, to the wide memory
    CLensFollow f;
    Run r = Feed(f, 0.0, 2.5, Unknown(16.0 / 9.0), s);
    Run w = Feed(f, 2.5, 10.0, Known(2.40), s);
    Check(r.changes == 0 && w.changes == 1 && w.memory == 2 && std::abs(w.lastChange - 4.5) < 0.15,
          "a remux that opens bright: one move, to memory 2, 2 s after its bars are measured");
  }
  {
    // the end of a video, a playlist, and the setting that keeps the lens where it is
    CLensFollow f;
    Feed(f, 0.0, 5.0, Known(2.39), s);
    Run r = Feed(f, 5.0, 7.9, Shape{}, s, false);
    Check(r.memory == 2 && r.changes == 0, "stopped for under 3 s: nothing yet (the next of a playlist may come)");
    r = Feed(f, 7.9, 8.5, Shape{}, s, false);
    Check(r.memory == 1 && r.changes == 1 && f.Now().why == WHY_STOP, "stopped for 3 s: back to memory 1");
    r = Feed(f, 8.5, 20.0, Shape{}, s, false);
    Check(r.changes == 0, "... once");
    // the next video, scope again: the start rule
    r = Feed(f, 20.0, 22.5, Known(2.39, 2), s);
    Check(r.memory == 2 && std::abs(r.lastChange - 22.0) < 0.15, "the next video starts with its own 2 s");

    LensSettings keep = s;
    keep.backAtStop = false;
    CLensFollow g;
    Feed(g, 0.0, 5.0, Known(2.39), keep);
    r = Feed(g, 5.0, 20.0, Shape{}, keep, false);
    Check(r.memory == 2 && r.changes == 0, "with the setting off, the lens stays at the end");

    CLensFollow h;
    Feed(h, 0.0, 5.0, Known(2.39, 1), s);
    Feed(h, 5.0, 6.0, Shape{}, s, false); // (1 s between two videos of a playlist)
    r = Feed(h, 6.0, 8.5, Known(1.78, 2), s);
    Check(r.memory == 1 && std::abs(r.lastChange - 8.0) < 0.15,
          "a playlist: the next video is taken with the start rule (another video)");
  }
  {
    CLensFollow f;
    Run r = Feed(f, 0.0, 10.0, Shape{}, s);
    Check(r.memory == 0 && r.changes == 0, "no picture yet: nothing");
    CLensFollow g;
    r = Feed(g, 0.0, 10.0, Known(2.39), s, false);
    Check(r.memory == 0 && r.changes == 0, "nothing is ever wanted before a video plays");
  }

  // ---- the kind of signal
  Check(SignalOf("sdr", "sdr", false) == Signal::SDR, "SDR out is SDR");
  Check(SignalOf("hdr10", "hdr10", false) == Signal::HDR10 && SignalOf("hdr10plus", "hdr10plus", false) == Signal::HDR10,
        "HDR10 and HDR10+ out are HDR10");
  Check(SignalOf("hlg", "hlg", false) == Signal::HLG, "HLG out is HLG");
  Check(SignalOf("dolbyvision", "dolbyvision", false) == Signal::DOLBY_VISION, "native Dolby Vision is Dolby Vision");
  Check(SignalOf("dolbyvision", "hdr10", false) == Signal::DOLBY_VISION,
        "Dolby Vision sent as HDR10 (a projector without it) is Dolby Vision");
  Check(SignalOf("dolbyvision", "sdr", false) == Signal::SDR, "Dolby Vision mapped to SDR is SDR");
  Check(SignalOf("hdr10", "hdr10", true) == Signal::THREE_D, "a 3D output is 3D");
  Check(SignalOf("", "", false) == Signal::NONE, "a driver that does not say: nothing");
  {
    CSignalFollow f;
    Signal sent = Signal::NONE;
    int sends = 0;
    for (double t = 0.0; t < 10.0; t += 0.5)
    {
      const Signal k = f.Update(t, Signal::SDR);
      if (k != Signal::NONE)
      {
        sent = k;
        sends++;
      }
    }
    Check(sent == Signal::SDR && sends == 1, "SDR is taken once, after 3 s");
    sends = 0;
    for (double t = 10.0; t < 12.0; t += 0.5)
      sends += f.Update(t, Signal::HDR10) != Signal::NONE;
    for (double t = 12.0; t < 20.0; t += 0.5)
      sends += f.Update(t, Signal::SDR) != Signal::NONE;
    Check(sends == 0, "HDR10 for 2 s, then SDR again: nothing is sent");
    Signal at4 = Signal::NONE;
    for (double t = 20.0; t < 30.0; t += 0.5)
      if (const Signal k = f.Update(t, Signal::DOLBY_VISION); k != Signal::NONE)
        at4 = k;
    Check(at4 == Signal::DOLBY_VISION && f.Taken() == Signal::DOLBY_VISION, "Dolby Vision that lasts is taken");
    Check(f.Update(31.0, Signal::NONE) == Signal::NONE && f.Taken() == Signal::DOLBY_VISION,
          "no signal known: nothing changes");
  }

  // ---- Hades: the wait of the thread of the control: none while it is off (it sleeps until a setting or the test
  // wakes it), 250 ms while it is on
  Check(ControlWaitMs(false) < 0, "while the control is off its thread waits until it is woken, not 4 times a second");
  Check(ControlWaitMs(true) == 250 && POLL_MS == 250, "while it is on, 250 ms as before");

  // ---- Hades: how long the test of the connection is waited for: a talk that waits for a lens, then the test
  {
    const Timeouts t;
    const int wait = TestWaitMs(t);
    Check(wait == 106000, "the test is waited for 106 s at most with the timeouts of the protocols (" +
                              std::to_string(wait) + " ms)");
    Check(wait > t.lensMs + t.modeMs + t.connectMs + 2 * t.answerMs,
          "longer than a talk that waits for a lens that moves, so that the test after it is seen");
    Timeouts quick;
    quick.lensMs = 1000;
    Check(TestWaitMs(quick) == wait - 59000, "the wait follows the timeout of a lens");
  }

  // ---- the waits after failures
  Check(RetryAfter(1) == 5.0 && RetryAfter(2) == 10.0 && RetryAfter(3) == 20.0 && RetryAfter(7) == 300.0 &&
            RetryAfter(50) == 300.0,
        "the next try after 5 s, then twice as long, up to 5 minutes");

  // ---- the shape of a picture
  {
    const Shape a = ShapeOf(16.0 / 9.0, 2160, 280, true);
    Check(a.valid && a.known && std::abs(a.ratio - 2.40) < 0.005, "a 16:9 frame with bars of 280 rows is 2.40:1");
    const Shape b = ShapeOf(16.0 / 9.0, 2160, 0, false);
    Check(b.valid && !b.known && std::abs(b.ratio - 16.0 / 9.0) < 1e-9, "a 16:9 frame without bars is not known");
    const Shape c = ShapeOf(2.40, 1600, 0, false);
    Check(c.valid && c.known && std::abs(c.ratio - 2.40) < 1e-9, "a frame of 3840x1600 is known by itself");
    const Shape d = ShapeOf(16.0 / 9.0, 2160, 1100, true);
    Check(d.valid && std::abs(d.ratio - 16.0 / 9.0) < 1e-9, "a bar of more than half the picture is no bar");
    Check(!ShapeOf(0.0, 2160, 0, false).valid && !ShapeOf(1.78, 0, 0, false).valid, "no frame: no shape");
  }

  // ---- the addresses
  const auto home = [](const char* text) { return ReadAddress(text).valid && ReadAddress(text).home; };
  const auto away = [](const char* text) { return ReadAddress(text).valid && !ReadAddress(text).home; };
  Check(home("192.168.1.20") && home("10.0.0.5") && home("172.16.0.9") && home("172.31.255.1") &&
            home("169.254.10.2"),
        "RFC 1918 and link-local IPv4 addresses are of a home network");
  Check(away("172.32.0.1") && away("172.15.0.1") && away("8.8.8.8") && away("100.64.0.1") && away("127.0.0.1"),
        "other IPv4 addresses (and the host itself) are not");
  Check(home("fd12:3456::7") && home("fe80::1") && home("::ffff:192.168.1.2"),
        "IPv6 unique local, link-local and a home IPv4 in IPv6 are of a home network");
  Check(away("2001:db8::1") && away("::1"), "other IPv6 addresses are not");
  Check(!ReadAddress("projector.local").valid && !ReadAddress("").valid && !ReadAddress("192.168.1.256").valid &&
            !ReadAddress("192.168.1.20:20554").valid,
        "a name, an empty text, a wrong number and an address with a port are no address");

  std::printf("%d checks\n", g_checks);
  std::printf(g_fails ? "PROJECTOR-POLICY FAILED (%d)\n" : "PROJECTOR-POLICY PASSED\n", g_fails);
  return g_fails ? 1 : 0;
}
