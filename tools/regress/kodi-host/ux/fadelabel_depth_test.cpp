// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of the depth that a scrolling fadelabel is drawn with: the real GUIFadeLabelControl.cpp
// of a tree (copied next to this file by run.sh) against the stand-ins of stubs.h. Kodi processes
// every control, then assigns the depths of this pass (CGUIWindowManager: AssignDepth() after
// Process()), then renders: the text must be drawn with the depth of the pass it is drawn in.
#include "GUIFadeLabelControl.h"

#include <cstdio>
#include <vector>

CWinSystemBase g_winSystem;
CWinSystemBase* CServiceBroker::GetWinSystem()
{
  return &g_winSystem;
}
void CGUIControl::AssignDepth()
{
  m_cachedTransform.depth = CServiceBroker::GetWinSystem()->GetGfxContext().GetDepth();
}

int main()
{
  CGUIFont font;
  CLabelInfo info;
  info.font = &font;
  CGUIFadeLabelControl label(1, 2, 0, 0, 400, 40, info, false, 0, false, false);
  label.SetInfo({KODI::GUILIB::GUIINFO::CGUIInfoLabel("A text far wider than its control, that scrolls")});

  CGraphicContext& gfx = g_winSystem.GetGfxContext();
  // the layers of a window change from pass to pass (a dialog opens, a list grows), up and down
  const std::vector<uint32_t> passes = {5, 7, 3, 3, 9, 1};
  int failures = 0;
  unsigned int time = 0;
  for (const uint32_t depth : passes)
  {
    CDirtyRegionList dirty;
    gfx.m_depth = depth;
    gfx.m_renderedDepths.clear();
    label.Process(time += 16, dirty);
    label.AssignDepth();
    label.Render();
    if (gfx.m_renderedDepths.size() != 1)
    {
      std::printf("FAIL pass of depth %u: %zu transforms set by Render(), not 1\n", depth,
                  gfx.m_renderedDepths.size());
      ++failures;
      continue;
    }
    const uint32_t drawn = gfx.m_renderedDepths.front();
    std::printf("%s pass of depth %u: drawn with depth %u\n", drawn == depth ? "ok  " : "FAIL",
                depth, drawn);
    failures += drawn == depth ? 0 : 1;
  }
  std::printf("%s: %d of %zu passes drawn with another pass's depth\n",
              failures ? "FAILED" : "PASSED", failures, passes.size());
  return failures ? 1 : 0;
}
