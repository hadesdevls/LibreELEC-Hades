// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Hades, host test: a drain of the video decoder keeps the picture that the renderer had no room for
// (xbmc/cores/VideoPlayer/DecoderDrain.h, CVideoPlayerVideo::DrainDecoder()). A decoder that holds numbered pictures
// and a renderer that takes or refuses each offer are simulated; asking the decoder for a picture while one is still
// kept loses it (CDVDVideoCodec::GetPicture() releases the picture it is given to fill), as in Kodi. And in the source,
// CVideoPlayerVideo drains only through DrainDecoder(), at its three places.
#include "DecoderDrain.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace KODI::VIDEOPLAYER::DECODER_DRAIN;

namespace
{
int g_checks = 0, g_failures = 0;
void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

struct Sim
{
  std::deque<int> decoder; // the pictures the decoder still holds
  std::deque<bool> renderer; // each offer in turn: taken (true) or no room (false); none left: taken
  int kept = 0; // the picture in the thread's hands
  bool waiting = false; // ... still waiting for the renderer
  bool paused = false;
  std::vector<int> shown;
  int lost = 0, offers = 0, asks = 0, mayWaitCalls = 0;
  bool runaway = false;

  void Offer()
  {
    ++offers;
    bool taken = true;
    if (!renderer.empty())
    {
      taken = renderer.front();
      renderer.pop_front();
    }
    waiting = !taken;
    if (taken)
      shown.push_back(kept);
  }
  bool TakeNext()
  {
    ++asks;
    if (decoder.empty())
      return false;
    if (waiting)
      ++lost; // GetPicture() releases what it is given
    kept = decoder.front();
    decoder.pop_front();
    waiting = false;
    Offer();
    return true;
  }
  Result Run()
  {
    return Drain([this] { return TakeNext(); }, [this] { return waiting; }, [this] { Offer(); },
                 [this]
                 {
                   if (++mayWaitCalls > 1000)
                   {
                     runaway = true;
                     return false;
                   }
                   return !paused;
                 });
  }
};

std::string Shown(const Sim& s)
{
  std::string t;
  for (int p : s.shown)
    t += (t.empty() ? "" : ",") + std::to_string(p);
  return t;
}

void Pictures(Sim& s, int n)
{
  for (int i = 1; i <= n; ++i)
    s.decoder.push_back(i);
}

size_t Count(const std::string& text, const std::string& what)
{
  size_t n = 0;
  for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
    ++n;
  return n;
}
} // namespace

int main()
{
  {
    Sim s;
    Pictures(s, 5);
    const Result r = s.Run();
    Check(r == Result::EMPTY && Shown(s) == "1,2,3,4,5" && s.lost == 0,
          "every picture taken: the five are shown in order (" + Shown(s) + ")");
  }
  {
    Sim s;
    Pictures(s, 5);
    s.renderer = {true, false, true};
    const Result r = s.Run();
    Check(r == Result::EMPTY && Shown(s) == "1,2,3,4,5" && s.lost == 0 && s.offers == 6,
          "no room once for picture 2: it is offered again and shown, none lost (shown " + Shown(s) + ", lost " +
              std::to_string(s.lost) + ", offers " + std::to_string(s.offers) + ")");
  }
  {
    Sim s;
    Pictures(s, 5);
    s.paused = true;
    s.renderer = {false};
    const Result r = s.Run();
    Check(r == Result::WAITING && s.waiting && s.kept == 1 && s.decoder.size() == 4 && s.lost == 0 &&
              s.offers == 1 && s.asks == 1,
          "paused, no room for picture 1: the drain stops with it kept and the decoder not asked again (offers " +
              std::to_string(s.offers) + ", asks " + std::to_string(s.asks) + ", lost " + std::to_string(s.lost) +
              ")");
  }
  {
    Sim s;
    Pictures(s, 5);
    s.renderer = std::deque<bool>(50, false);
    const Result r = s.Run();
    Check(r == Result::WAITING && !s.runaway && s.offers == 1 + static_cast<int>(MAX_OFFERS_AGAIN) &&
              s.lost == 0 && s.decoder.size() == 4,
          "a renderer that never takes it while playing: offered again a bounded number of times, then kept (offers " +
              std::to_string(s.offers) + ")");
  }
  {
    Sim s;
    Pictures(s, 3);
    s.renderer = {false, false, true, false, false, true, false, false, true};
    const Result r = s.Run();
    Check(r == Result::EMPTY && Shown(s) == "1,2,3" && s.lost == 0,
          "every picture taken at its third offer: the bound is per picture, all three shown (" + Shown(s) + ")");
  }
  {
    Sim s;
    Pictures(s, 2);
    s.kept = 7;
    s.waiting = true;
    const Result r = s.Run();
    Check(r == Result::EMPTY && Shown(s) == "7,1,2" && s.lost == 0,
          "a picture already waiting when the drain starts goes out first (" + Shown(s) + ")");
  }
  {
    Sim s;
    const Result r = s.Run();
    Check(r == Result::EMPTY && s.offers == 0 && s.asks == 1, "an empty decoder: nothing offered");
  }
  {
    std::ifstream in(std::string(KODI_TREE) + "/xbmc/cores/VideoPlayer/VideoPlayerVideo.cpp");
    std::stringstream text;
    text << in.rdbuf();
    const std::string t = text.str();
    Check(!t.empty(), "VideoPlayerVideo.cpp is read");
    Check(Count(t, "SetCodecControl(DVD_CODEC_CTRL_DRAIN)") == 1 && Count(t, "DrainDecoder(frametime, pts)") == 3,
          "CVideoPlayerVideo drains only through DrainDecoder(), at its three places (drain controls " +
              std::to_string(Count(t, "SetCodecControl(DVD_CODEC_CTRL_DRAIN)")) + ", calls " +
              std::to_string(Count(t, "DrainDecoder(frametime, pts)")) + ")");
  }
  std::printf("DECODER-DRAIN %d checks, %d failed\n", g_checks, g_failures);
  if (!g_failures)
    std::printf("ALL PASSED\n");
  return g_failures ? 1 : 0;
}
