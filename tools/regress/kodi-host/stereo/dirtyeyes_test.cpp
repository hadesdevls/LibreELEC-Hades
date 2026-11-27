// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// dirtyeyes_test.cpp (Ryudoadema, 1.2.1: a mouse pointer left on the screen in a 3D output): the dirty regions of the
// GUI age once a presented frame, also where CGUIWindowManager::Render() is called for each eye
// (DirtyRegionAging.h of the tree under test).
//  - a model of Render() as GUIWindowManager.cpp has it (the regions aged when the frame before was presented, then
//    the whole eye drawn where a region marked within the age of the buffer and one more says so: the default
//    algorithm, "fill the viewport on change") and of the window's buffers (each a picture of both eyes; an eye that
//    is not drawn keeps what that buffer had; the age of the buffer as EGL says it), called for each eye as
//    KODI::RENDERING::STEREO_VIEWS::ForEachView() calls it
//  - the pointer moved for some frames, then still: in every frame that goes to the screen both eyes show the same
//    GUI, and what stays on the screen is the GUI of the last move, for buffers of age 1 to 5, with and without 3D
//  - -DOLD_RULE (each call ages them, as before): a frame goes to the screen with the GUI of an older frame in the
//    right eye, and stays there (buffers of age 2 and 4), which must fail
//  - in the source: GUIWindowManager.cpp ages them only in a call that starts a frame (StartsFrame() of the view)
// Prints a FAIL line for each failed check and "<n> failed" last. usage: dirtyeyes_test (KODI_TREE compiled in)
#include "guilib/DirtyRegionAging.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace A = KODI::GUILIB::DIRTY_REGION_AGING;

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what)
{
  ++checks;
  if (!ok)
  {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

constexpr int MAX_BUFFER_AGE = 10; // (GUIWindowManager.cpp)

// CDirtyRegionTracker: the ages of the marked regions
struct Tracker
{
  std::vector<int> ages;
  void Mark() { ages.push_back(0); }
  void Clean(int bufferAge)
  {
    std::vector<int> kept;
    for (int a : ages)
      if (++a <= bufferAge)
        kept.push_back(a);
    ages = kept;
  }
  bool Dirty(int maxAge) const
  {
    for (int a : ages)
      if (a <= maxAge)
        return true;
    return false;
  }
};

struct Picture
{
  int left{-1};
  int right{-1};
};

struct Result
{
  bool asymmetric{false}; // a frame went to the screen with another GUI in each eye
  Picture screen;
  int version{0};
};

// frames of the pointer moved (a region marked and a new GUI in each), then frames of it still; buffers of a fixed
// age (a queue of that many, drawn in turn, one presented each frame that drew something)
Result Run(int buffers, bool stereo, int moving, int still)
{
  Tracker tracker;
  bool presentedFrame = false;
  std::vector<Picture> queue(buffers);
  int back = 0;
  Result r;
  for (int frame = 0; frame < moving + still; ++frame)
  {
    if (frame < moving)
    {
      ++r.version;
      tracker.Mark();
    }
    Picture& buffer = queue[back];
    bool hasRendered = false;
    const auto render = [&](RenderStereoView view)
    {
#if defined(OLD_RULE)
      const bool starts = true;
#else
      const bool starts = A::StartsFrame(view);
#endif
      if (starts)
      {
        if (presentedFrame)
          tracker.Clean(MAX_BUFFER_AGE + 1);
        presentedFrame = false;
      }
      const bool drawn = tracker.Dirty(buffers + 1);
      if (drawn)
      {
        if (view != RenderStereoView::RIGHT)
          buffer.left = r.version;
        if (view != RenderStereoView::LEFT)
          buffer.right = r.version;
      }
      presentedFrame |= drawn;
      hasRendered |= drawn;
    };
    if (stereo)
    {
      render(RenderStereoView::LEFT);
      render(RenderStereoView::RIGHT);
    }
    else
      render(RenderStereoView::OFF);
    if (hasRendered)
    {
      r.screen = buffer;
      if (buffer.left != buffer.right)
        r.asymmetric = true;
      back = (back + 1) % buffers;
    }
  }
  return r;
}

std::string Read(const std::string& path)
{
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
} // namespace

int main()
{
  check(A::StartsFrame(RenderStereoView::OFF) && A::StartsFrame(RenderStereoView::LEFT) &&
            !A::StartsFrame(RenderStereoView::RIGHT),
        "StartsFrame(): without 3D and the left eye start a frame, the right eye does not");
  for (int buffers = 1; buffers <= 5; ++buffers)
    for (bool stereo : {false, true})
      for (int moving : {1, 3, 7})
      {
        const Result r = Run(buffers, stereo, moving, 30);
        const std::string name = std::string(stereo ? "3D" : "2D") + ", buffers of age " + std::to_string(buffers) +
                                 ", the pointer moved in " + std::to_string(moving) + " frames";
        check(!r.asymmetric, name + ": every frame on the screen shows the same GUI in both eyes");
        check(r.screen.left == r.version && r.screen.right == r.version,
              name + ": the screen keeps the GUI of the last move (left " + std::to_string(r.screen.left) +
                  ", right " + std::to_string(r.screen.right) + ", last " + std::to_string(r.version) + ")");
      }
  const std::string src = Read(std::string(KODI_TREE) + "/xbmc/guilib/GUIWindowManager.cpp");
  const size_t at = src.find("bool CGUIWindowManager::Render()");
  const std::string body = at == std::string::npos ? "" : src.substr(at, 2500);
  const size_t starts = body.find("if (KODI::GUILIB::DIRTY_REGION_AGING::StartsFrame(\n"
                                  "          CServiceBroker::GetWinSystem()->GetGfxContext().GetStereoView()))\n  {");
  const size_t clean = body.find("      m_tracker.CleanMarkedRegions(");
  const size_t reset = body.find("    m_presentedFrame = false;\n  }");
  const size_t dirty = body.find("m_tracker.GetDirtyRegions(bufferAge + 1)");
  check(starts != std::string::npos && clean != std::string::npos && reset != std::string::npos &&
            starts < clean && clean < reset && reset < dirty && body.find("\n  m_presentedFrame = false;\n") == std::string::npos,
        "GUIWindowManager.cpp: Render() ages the regions and forgets the presented frame only in a call that starts a "
        "frame, before it asks for them");
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
