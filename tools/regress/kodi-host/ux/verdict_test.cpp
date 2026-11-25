// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// verdict_test.cpp: the sentence of the overview page of the player information and the frames that Kodi loses
// (xbmc/utils/HadesPlayerInfo.h of the Kodi tree: CLostFrames, Notes(), Decide()), with the counters that Kodi keeps
// (dropped by the video player, skipped by the renderer) sampled twice a second as the window system does:
//  - sustained loss in steady playback changes the sentence, named by what lost the frames (the case of the audit: 4
//    dropped and 8 skipped every 10 s at 59.94 Hz), or by the graphics where they are busy;
//  - a start, a seek, a pause, a new mode, a one-off burst, losses a minute apart, a gap while the page was closed, and
//    a new video never change it; what is lost while the stream buffers is a note of its own, not a fault;
//  - a display rate that does not fit the video keeps its own sentence (its cadence skips frames);
//  - with no frame lost, the sentence is what it was (the other causes, in their order).
// Prints a line per failed check and PASSED or FAILED (n) last.
#include "utils/HadesPlayerInfo.h"

#include <cstdio>
#include <string>

using namespace KODI::UTILS::PLAYERINFO;

static int failed = 0;
static void expect(bool ok, const std::string& what)
{
  if (!ok)
  {
    std::printf("FAIL: %s\n", what.c_str());
    ++failed;
  }
}

namespace
{
// a player that loses `dropped` and `skipped` frames every 10 s, evenly, sampled twice a second from `from` to `to`
struct Player
{
  CLostFrames lost;
  int dropped = 0;
  int skipped = 0;
  double fracD = 0, fracS = 0;
  void Play(double from, double to, double dropPer10, double skipPer10, bool buffering = false)
  {
    for (double t = from; t < to - 1e-9; t += 0.5)
    {
      fracD += dropPer10 / 20.0;
      fracS += skipPer10 / 20.0;
      while (fracD >= 1.0)
      {
        ++dropped;
        fracD -= 1.0;
      }
      while (fracS >= 1.0)
      {
        ++skipped;
        fracS -= 1.0;
      }
      lost.Sample(t, dropped, skipped, buffering);
    }
  }
};

SummaryInput Input(const CLostFrames::Lost& l)
{
  SummaryInput in;
  in.video = true;
  in.source = "hdr10";
  in.signal = "pq";
  in.rateFit = "exact";
  in.dropped = l.dropped;
  in.skipped = l.skipped;
  in.lostSustained = l.sustained;
  in.bufferingLost = l.bufferingSustained;
  return in;
}
} // namespace

int main()
{
  {
    // steady loss, as in the audit: 4 dropped and 8 skipped every 10 s; a video that started (Restart) at 0
    Player p;
    p.lost.Restart(0.0);
    p.Play(0.0, 10.0, 4, 8);
    CLostFrames::Lost l = p.lost.Window(10.0);
    expect(!l.sustained, "the first seconds after the start are left out, and one part of the window is not sustained");
    p.Play(10.0, 30.0, 4, 8);
    l = p.lost.Window(30.0);
    expect(l.sustained && l.dropped >= 8 && l.skipped >= 16, "steady loss over two parts: sustained (" +
                                                                    std::to_string(l.dropped) + " dropped, " +
                                                                    std::to_string(l.skipped) + " skipped)");
    const Summary s = Decide(Input(l));
    expect(s.state == Severity::WARN && s.code == "frames_skipped" && s.arg == std::to_string(l.skipped) &&
               s.todo == "system" && s.more == 1,
           "the sentence says the frames skipped first (the larger), the dropped ones as one more (" + s.code + ")");
    SummaryInput busy = Input(l);
    busy.gpuBusy = 97;
    const Summary g = Decide(busy);
    expect(g.code == "frames_gpu" && g.arg == "97" && g.more == 0, "with the graphics busy: they are named (" +
                                                                         g.code + " " + g.arg + ")");
    busy.gpuBusy = 60;
    expect(Decide(busy).code == "frames_skipped", "graphics that are not busy are not blamed");
    SummaryInput mismatch = Input(l);
    mismatch.rateFit = "mismatch";
    const Summary m = Decide(mismatch);
    expect(m.code == "rate_mismatch" && m.more == 0,
           "a display rate that does not fit the video keeps its own sentence (" + m.code + ")");
    // the loss stops: the window empties, and the sentence is OK again
    p.Play(30.0, 61.0, 0, 0);
    l = p.lost.Window(61.0);
    expect(!l.sustained && l.dropped + l.skipped == 0, "30 s without a loss: nothing left");
    expect(Decide(Input(l)).code == "ok", "... and the sentence is OK");
  }
  {
    // a seek in the middle of steady playback with no loss: the burst of the seek is left out
    Player p;
    p.lost.Restart(0.0);
    p.Play(0.0, 20.0, 0, 0);
    p.lost.Restart(20.0); // the renderer's queue emptied
    p.Play(20.0, 24.0, 30, 30); // Kodi drops and skips while it finds its pace
    p.Play(24.0, 40.0, 0, 0);
    expect(!p.lost.Window(40.0).sustained && p.lost.Window(40.0).dropped == 0,
           "the frames lost in the seconds after a seek are not counted");
  }
  {
    // a pause: every sample paused restarts the settling, and the resume is left out
    Player p;
    p.lost.Restart(0.0);
    p.Play(0.0, 10.0, 0, 0);
    for (double t = 10.0; t < 20.0; t += 0.5)
      p.lost.Restart(t); // paused
    p.Play(20.0, 23.0, 20, 20); // the resume
    p.Play(23.0, 40.0, 0, 0);
    expect(!p.lost.Window(40.0).sustained, "the frames lost as playback resumes are not counted");
  }
  {
    // a one-off burst in steady playback, and losses a minute apart: never sustained
    Player p;
    p.lost.Restart(0.0);
    p.Play(0.0, 20.0, 0, 0);
    p.Play(20.0, 20.5, 200, 0); // ten frames at once
    p.Play(20.5, 45.0, 0, 0);
    expect(!p.lost.Window(45.0).sustained && p.lost.Window(45.0).dropped == 10,
           "a burst of frames in one part of the window: counted, not sustained");
    expect(Decide(Input(p.lost.Window(45.0))).code == "ok", "... and the sentence stays OK");
    Player q;
    q.lost.Restart(0.0);
    for (int minute = 0; minute < 3; ++minute)
    {
      const double t0 = 10.0 + minute * 60.0;
      q.Play(t0, t0 + 0.5, 20, 20);
      q.Play(t0 + 0.5, t0 + 60.0, 0, 0);
      expect(!q.lost.Window(t0 + 60.0).sustained, "two frames lost a minute apart: never sustained");
    }
  }
  {
    // losses while the stream buffers: a note of their own, not the fault of the PC
    Player p;
    p.lost.Restart(0.0);
    p.Play(0.0, 10.0, 0, 0);
    p.Play(10.0, 40.0, 6, 6, true);
    const CLostFrames::Lost l = p.lost.Window(40.0);
    expect(!l.sustained && l.bufferingSustained && l.dropped == 0 && l.buffering > 0,
           "lost while buffering: counted apart, not as steady loss");
    const Summary s = Decide(Input(l));
    expect(s.state == Severity::NOTE && s.code == "frames_buffering",
           "the sentence says the stream was buffering, as a note (" + s.code + ")");
  }
  {
    // a gap (the page was closed) and another video (counters that went back): nothing across them
    Player p;
    p.lost.Restart(0.0);
    p.Play(0.0, 10.0, 0, 0);
    p.dropped += 500; // lost while the page was closed
    p.Play(70.0, 72.0, 0, 0);
    expect(p.lost.Window(72.0).dropped == 0, "what was lost while the page was closed is not of the last seconds");
    p.dropped = 0;
    p.skipped = 0;
    p.Play(72.0, 80.0, 0, 0);
    expect(p.lost.Window(80.0).dropped == 0 && p.lost.Window(80.0).skipped == 0,
           "counters that went back (another video): nothing negative, nothing counted");
  }
  {
    // with no frame lost, the sentence is what it was: the other causes, in their order
    SummaryInput in;
    in.video = true;
    in.source = "dolbyvision";
    in.dvState = "native";
    in.signal = "dv";
    in.rateFit = "exact";
    expect(Decide(in).code == "ok" && Decide(in).state == Severity::OK, "nothing lost: OK, as before");
    in.failed = 3;
    in.stepLevel = 1;
    const Summary s = Decide(in);
    expect(s.code == "frames_failed" && s.more == 1, "the frames of native Dolby Vision not shown: as before");
    in.failed = 0;
    in.stepLevel = 0;
    in.dropped = 3; // counted, but not sustained
    expect(Decide(in).code == "ok", "frames lost but not sustained: the sentence stays OK");
  }
  expect(CLostFrames::MIN_LOST == 6 && CLostFrames::WINDOW_S == 30.0 && CLostFrames::PART_S == 10.0 &&
             CLostFrames::SETTLE_S == 5.0 && LOST_GPU_BUSY == 90,
         "the thresholds: 6 frames over 30 s in two parts of 10 s, 5 s left out after a change, the graphics busy from 90 %");
  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
