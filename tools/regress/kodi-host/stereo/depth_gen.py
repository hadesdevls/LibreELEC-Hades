#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 LibreELEC Hades
#
# Hades (1.0 RC5, Blu-ray 3D): the depth a subtitle gets on a Blu-ray 3D title, from the store of plane offsets
# (BlurayOffsetMetadata.cpp/.h, compiled with it) to the shift the overlay renderer draws it with: the body of
# CRenderer::SubtitleDepth(const COverlay&) taken word for word out of the tree's OverlayRenderer.cpp, compiled with
# stand-ins for the graphic context (the 3D mode and the eye being drawn) and for GetStereoscopicDepth() (the
# subtitles.stereoscopicdepth setting, as OverlayRendererUtil.cpp computes it: 0 without 3D output and in mono,
# signed by the eye otherwise). The answer of the store goes to the renderer as CVideoPlayer::GetSubtitlePlaneOffset()
# hands it on; a disc's menu (COverlay::m_isDiscMenu) is placed by the setting, not by the subtitles' sequence.
# usage: depth_gen.py <kodi tree> <output folder>   (writes <output folder>/depth_test.cpp)
import os
import sys

tree, out = sys.argv[1], sys.argv[2]
src = open(os.path.join(tree, 'xbmc/cores/VideoPlayer/VideoRenderers/OverlayRenderer.cpp'), encoding='utf-8').read()
sig = 'float CRenderer::SubtitleDepth(const COverlay& o) const'
if src.count(sig) != 1:
    sys.exit('FAIL: %r is in OverlayRenderer.cpp %d times' % (sig, src.count(sig)))
i = src.index(sig)
j = src.index('{', i)
depth, k = 0, j
while True:
    depth += {'{': 1, '}': -1}.get(src[k], 0)
    k += 1
    if depth == 0:
        break
body = src[j:k]
test = r'''
#include "BlurayOffsetMetadata.h"
#include <cstdio>
#include <optional>
enum class RenderStereoView { OFF, LEFT, RIGHT };
enum class RenderStereoMode { OFF, SPLIT_HORIZONTAL, SPLIT_VERTICAL, MONO };
struct CRect { float x1{0}, y1{0}, x2{0}, y2{0}; float Width() const { return x2 - x1; } };
struct CGraphicContext {
  RenderStereoMode mode{RenderStereoMode::SPLIT_VERTICAL};
  RenderStereoView view{RenderStereoView::LEFT};
  RenderStereoMode GetStereoMode() const { return mode; }
  RenderStereoView GetStereoView() const { return view; }
};
struct Win { CGraphicContext gfx; CGraphicContext& GetGfxContext() { return gfx; } };
struct CServiceBroker { static Win* GetWinSystem() { static Win w; return &w; } };
static int setting = 7;
static bool g_menu = false;
// as OverlayRendererUtil.cpp
static int GetStereoscopicDepth() {
  const CGraphicContext& c = CServiceBroker::GetWinSystem()->GetGfxContext();
  if (c.GetStereoMode() == RenderStereoMode::MONO || c.GetStereoMode() == RenderStereoMode::OFF)
    return 0;
  return setting * (c.GetStereoView() == RenderStereoView::LEFT ? 1 : -1);
}
struct COverlay { bool m_isDiscMenu{false}; };
struct CRenderer {
  std::optional<int> m_subtitlePlaneOffset;
  CRect m_rs{0, 0, 1920, 1080}, m_rd{0, 0, 3840, 2160};
  float SubtitleDepth(const COverlay& o) const;
};
float CRenderer::SubtitleDepth(const COverlay& o) const BODY
using namespace KODI::VIDEO::BLURAY;
static int failures = 0;
static void check(const char* what, float got, float want) {
  const bool ok = got == want;
  std::printf("%s: %s: depth %g (want %g)\n", ok ? "ok" : "FAIL", what, got, want);
  failures += !ok;
}
// one block: frames 0..2 of sequence 0 at the given offsets, starting at 1000, a frame of 1000
static OffsetMetadata Block(int a, int b, int c) {
  OffsetMetadata m; m.sequences = 1; m.frames = 3;
  m.offsets = {static_cast<int8_t>(a), static_cast<int8_t>(b), static_cast<int8_t>(c)};
  return m;
}
static float Depth(const COffsetMetadataStore& store, double pts, int sequence, RenderStereoMode mode,
                   RenderStereoView view) {
  CRenderer r;
  CServiceBroker::GetWinSystem()->GetGfxContext().mode = mode;
  CServiceBroker::GetWinSystem()->GetGfxContext().view = view;
  // as CVideoPlayer::GetSubtitlePlaneOffset(): no sequence, no answer; otherwise the store's
  if (sequence >= 0) r.m_subtitlePlaneOffset = store.GetOffset(pts, static_cast<unsigned int>(sequence));
  COverlay o;
  o.m_isDiscMenu = g_menu;
  return r.SubtitleDepth(o);
}
int main() {
  COffsetMetadataStore empty, store;
  store.Add(1000.0, 1000.0, Block(0, 5, -3));
  const auto L = RenderStereoView::LEFT, R = RenderStereoView::RIGHT, VOFF = RenderStereoView::OFF;
  const auto SBS = RenderStereoMode::SPLIT_VERTICAL, MONO = RenderStereoMode::MONO, MOFF = RenderStereoMode::OFF;
  // (the video is drawn at twice its size: an offset is drawn twice as far; the setting of 7 as it is)
  check("a stream that follows no sequence, left eye: the setting", Depth(store, 2000.0, -1, SBS, L), 7);
  check("a stream that follows no sequence, right eye: the setting", Depth(store, 2000.0, -1, SBS, R), -7);
  check("a stream that follows no sequence, no 3D output: nothing", Depth(store, 2000.0, -1, MOFF, VOFF), 0);
  check("a stream that follows no sequence, mono view: nothing", Depth(store, 2000.0, -1, MONO, L), 0);
  check("a frame the title gives 0 for: the screen", Depth(store, 1000.0, 0, SBS, L), 0);
  check("a frame the title gives 5 for, left eye", Depth(store, 2000.0, 0, SBS, L), 10);
  check("the same frame, right eye", Depth(store, 2000.0, 0, SBS, R), -10);
  check("a frame the title gives -3 for, left eye", Depth(store, 3000.0, 0, SBS, L), -6);
  check("no 3D output", Depth(store, 2000.0, 0, MOFF, VOFF), 0);
  check("the mono view of the 3D video (one eye drawn as a flat picture)", Depth(store, 2000.0, 0, MONO, L), 0);
  check("a frame after the block (no block describes it): the setting", Depth(store, 9000.0, 0, SBS, L), 7);
  check("a sequence the block does not carry: the setting", Depth(store, 2000.0, 4, SBS, L), 7);
  check("a store with nothing (a remux without offsets, or right after a seek): the setting", Depth(empty, 2000.0, 0, SBS, L), 7);
  g_menu = true;
  check("a disc menu over a frame the title gives 5 for: the setting, not the subtitles' sequence", Depth(store, 2000.0, 0, SBS, L), 7);
  g_menu = false;
  std::printf("%d failed\n", failures);
  return failures ? 1 : 0;
}
'''.replace('BODY', body)
os.makedirs(out, exist_ok=True)
open(os.path.join(out, 'depth_test.cpp'), 'w', encoding='utf-8').write(test)
print('depth_gen: the body of SubtitleDepth(const COverlay&) taken (%d lines)' % body.count('\n'))
