// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// info_refresh_test.cpp: how often the controls of the player information are processed while it is open
// (xbmc/dialogs/PlayerInfoRefresh.h of the Kodi tree): its first frame, at most four times a second while
// nothing is asked of it and nothing moves, at once for a key, in every frame while something of it moves, and
// what it saves in a film at 24 and at 60 frames a second whose panel changes in every frame (the frames whose GUI
// pass can be left out: CDVGuiReuse). Hades: and whether a control of the panel animates or is about to
// (CPlayerInfoRefresh::Animates(), on a tree of controls of the test's own): an animation of any type running at any
// depth, a condition changed (visibility, a conditional animation), the controls of a group that is not shown left
// alone, as Kodi processes them; and the frames of a fade of a skin's own panel processed every one, as before.
// Prints a line per failed check and PASSED or FAILED (n) last.
#include "dialogs/PlayerInfoRefresh.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

static int failed = 0;
static void expect(bool ok, const char* what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what);
    ++failed;
  }
}

// frames of a film at fps for seconds, from time t0 (ms); returns how many were processed
static unsigned int Film(CPlayerInfoRefresh& r, double fps, double seconds, unsigned int t0, bool busy = false)
{
  unsigned int n = 0;
  const unsigned int frames = static_cast<unsigned int>(fps * seconds + 0.5);
  for (unsigned int i = 0; i < frames; ++i)
    n += r.Due(t0 + static_cast<unsigned int>(i * 1000.0 / fps + 0.5), busy);
  return n;
}

static void expect(bool ok, const std::string& what)
{
  expect(ok, what.c_str());
}

// a control of the test's own: what Kodi's CGUIControl says to Animates()
enum Anim
{
  A_UNFOCUS = -3,
  A_HIDDEN,
  A_WINDOW_CLOSE,
  A_NONE,
  A_WINDOW_OPEN,
  A_VISIBLE,
  A_FOCUS,
  A_CONDITIONAL
};
constexpr std::array<Anim, 7> TYPES = {A_WINDOW_OPEN, A_WINDOW_CLOSE, A_VISIBLE, A_HIDDEN,
                                       A_FOCUS,       A_UNFOCUS,      A_CONDITIONAL};
struct Control
{
  Anim running = A_NONE;
  bool changed = false;
  bool visible = true;
  bool group = false;
  bool IsAnimating(Anim type) { return type != A_NONE && type == running; }
  bool ConditionsChanged() const { return changed; }
  bool IsVisible() const { return visible; }
  bool IsGroup() const { return group; }
};
struct Group : Control
{
  std::vector<Control*> children;
  Group() { group = true; }
  const std::vector<Control*>& Children() const { return children; }
};
static bool Busy(const Group& g)
{
  return CPlayerInfoRefresh::Animates<Group, Control>(g, TYPES);
}

static void TestAnimates()
{
  Group window, rows, inner;
  Control label, image, bar;
  window.children = {&rows, &label, nullptr};
  rows.children = {&image, &inner};
  inner.children = {&bar};
  expect(!Busy(window), "nothing animates and nothing changed: not busy (the panel is processed 4 times a second)");
  for (Anim a : TYPES)
  {
    bar.running = a;
    expect(Busy(window), "an animation of a control two groups deep runs: busy (every type)");
  }
  bar.running = A_NONE;
  label.running = A_HIDDEN;
  expect(Busy(window), "a control fading out: busy");
  label.running = A_NONE;
  image.changed = true;
  expect(Busy(window), "a condition of a control changed (it is to show, hide or animate): busy at once");
  image.changed = false;
  // a group that is not shown: its controls are not processed by Kodi, and are left alone; its own are not
  rows.visible = false;
  bar.changed = true;
  bar.running = A_VISIBLE;
  expect(!Busy(window), "the controls of a group that is not shown are left alone, as Kodi leaves them");
  rows.changed = true;
  expect(Busy(window), "... but the group's own condition counts (it is to show)");
  rows.changed = false;
  rows.running = A_VISIBLE;
  expect(Busy(window), "... and its own animation");
  rows.running = A_NONE;
  rows.visible = true;
  expect(Busy(window), "shown again: its controls count");
  bar.changed = false;
  bar.running = A_NONE;
  expect(!Busy(window), "all still again: not busy");

  // a fade of 400 ms of a row of a skin's own panel, at 60 frames a second: processed in every frame of it
  CPlayerInfoRefresh r;
  unsigned int t = 1000, processed = 0, frames = 0;
  r.Due(t, Busy(window));
  for (int i = 0; i < 60; ++i) // a second without anything moving
    r.Due(t += 17, Busy(window));
  label.changed = true; // its visibility changes: processed at once
  processed += r.Due(t += 17, Busy(window));
  ++frames;
  label.changed = false;
  label.running = A_VISIBLE;
  for (int i = 0; i < 24; ++i, ++frames) // the fade
    processed += r.Due(t += 17, Busy(window));
  expect(processed == frames, "every frame of the fade is processed (" + std::to_string(processed) + " of " +
                                  std::to_string(frames) + "; at 4 a second it would run at 4 frames a second)");
  label.running = A_NONE;
  unsigned int after = 0;
  for (int i = 0; i < 60; ++i)
    after += r.Due(t += 17, Busy(window));
  expect(after >= 3 && after <= 5, "the fade over: about 4 times a second again (" + std::to_string(after) + ")");
}

int main()
{
  TestAnimates();
  {
    CPlayerInfoRefresh r;
    expect(r.Due(5000, false), "the first frame is processed");
    expect(!r.Due(5042, false) && !r.Due(5200, false) && !r.Due(5249, false),
           "then not again before 250 ms, while nothing is asked and nothing moves");
    expect(r.Due(5250, false), "at 250 ms it is");
    expect(!r.Due(5260, false), "and not just after");
    r.Now();
    expect(r.Due(5270, false), "a key: processed in the next frame");
    expect(!r.Due(5280, false), "once");
    expect(r.Due(5290, true) && r.Due(5300, true) && r.Due(5310, true), "something moves: every frame");
    expect(!r.Due(5320, false), "it stopped moving: 250 ms again from the last frame processed");
    expect(r.Due(100, false), "the time went back (a new clock): processed");
    expect(r.Frames() == 13 && r.Processed() == 7, "the frames counted, and those processed");
    r.Reset();
    expect(r.Frames() == 0 && r.Processed() == 0 && r.Due(100, false), "opened again: counted again, processed at once");
  }
  {
    // a film at 24 fps, 10 s: 240 frames, the panel processed at most 4 times a second (and its first frame)
    CPlayerInfoRefresh r;
    const unsigned int n = Film(r, 24000.0 / 1001.0, 10.0, 1000);
    std::printf("24 fps, 10 s: processed in %u of 240 frames (before: 240)\n", n);
    expect(n >= 38 && n <= 41, "24 fps: about 4 times a second");
  }
  {
    CPlayerInfoRefresh r;
    const unsigned int n = Film(r, 60.0, 10.0, 1000);
    std::printf("60 fps, 10 s: processed in %u of 600 frames (before: 600)\n", n);
    expect(n >= 38 && n <= 41, "60 fps: about 4 times a second");
  }
  {
    CPlayerInfoRefresh r;
    const unsigned int n = Film(r, 24.0, 10.0, 1000, true);
    expect(n == 240, "a text that scrolls: every frame, as before");
  }
  expect(CPlayerInfoRefresh::PERIOD_MS == 250, "the period is 250 ms (the properties change at most twice a second)");
  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
