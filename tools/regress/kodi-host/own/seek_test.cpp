// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Host test of CSeekTrace (SeekTrace.h) and CSeekPending (SeekPending.h), and of what the player measures
// and shows of a slow source while it buffers (SourceRate.h, and where VideoPlayer.cpp calls it).
// Build: g++ -std=c++20 -O1 -Wall -Wextra -I<dir with the headers> seek_test.cpp -o seek_test -lpthread
#include "ModeChangeTime.h"
#include "SeekPending.h"
#include "SeekTrace.h"
#include "SourceRate.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                \
  do                                                                               \
  {                                                                                \
    if (cond)                                                                      \
      ++g_pass;                                                                    \
    else                                                                           \
    {                                                                              \
      ++g_fail;                                                                    \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                              \
  } while (0)

static bool Has(const std::string& s, const char* part)
{
  return s.find(part) != std::string::npos;
}

static CSeekTrace::Seek MakeSeek(double from, double to, double landed, int64_t asked = 1000000000LL)
{
  CSeekTrace::Seek s;
  s.askedNs = asked;
  s.handledNs = asked + 2000000; // 2 ms
  s.doneNs = asked + 644000000; // 642 ms later
  s.fromMs = from;
  s.toMs = to;
  s.landedMs = landed;
  return s;
}

static void TestTime()
{
  CHECK(CSeekTrace::Time(0) == "00:00:00.000");
  CHECK(CSeekTrace::Time(3723004) == "01:02:03.004");
  CHECK(CSeekTrace::Time(59999.6) == "00:01:00.000");
  CHECK(CSeekTrace::Time(-1500) == "-00:00:01.500");
  CHECK(CSeekTrace::Time(NAN) == "?");
  CHECK(CSeekTrace::Time(36000000.0 * 3) == "30:00:00.000");
}

static void TestTrace()
{
  CSeekTrace t;
  CHECK(!t.Waiting());
  CHECK(t.Shown(5, 1).empty()); // no seek: nothing
  CHECK(t.Ended().empty());

  // a seek handled when 10 frames were added: frames 1..10 are of before it
  CHECK(t.Handled(MakeSeek(2472345, 2700000, 2698120), 10).empty());
  CHECK(t.Waiting());
  CHECK(t.Shown(9, 2000000000).empty());
  CHECK(t.Shown(10, 2000000000).empty()); // the last frame of before the seek
  CHECK(t.Waiting());
  int64_t asked = 0;
  const std::string line = t.Shown(11, 1000000000LL + 1210000000, &asked);
  CHECK(!line.empty());
  CHECK(asked == 1000000000);
  CHECK(Has(line.c_str(), "Seek: from 00:41:12.345 to 00:45:00.000"));
  CHECK(Has(line.c_str(), "the player began 2 ms after it was asked for"));
  CHECK(Has(line.c_str(), "had the stream at 00:44:58.120 642 ms later"));
  CHECK(Has(line.c_str(), "the first frame after it went to the screen 1210 ms after it was asked for"));
  CHECK(!t.Waiting());
  CHECK(t.Shown(12, 3000000000).empty()); // the seek ended once
  CHECK(t.Ended().empty());

  // two seeks, no frame of the first: the first is told when the second is handled
  CHECK(t.Handled(MakeSeek(1000, 61000, 60500), 20).empty());
  const std::string first = t.Handled(MakeSeek(61000, 121000, 120000), 20);
  CHECK(Has(first.c_str(), "from 00:00:01.000 to 00:01:01.000"));
  CHECK(Has(first.c_str(), "no frame after it went to the screen before the next seek"));
  CHECK(t.Waiting());
  const std::string second = t.Shown(21, 1000000000LL + 300000000);
  CHECK(Has(second.c_str(), "from 00:01:01.000 to 00:02:01.000"));
  CHECK(Has(second.c_str(), "went to the screen 300 ms after"));

  // the playback ends first
  CHECK(t.Handled(MakeSeek(5000, 1000, 0), 30).empty());
  const std::string ended = t.Ended();
  CHECK(Has(ended.c_str(), "before the playback ended"));
  CHECK(t.Ended().empty());
  CHECK(t.Shown(31, 1).empty());

  // a stream that could not seek, a relative seek, paused
  CSeekTrace::Seek s = MakeSeek(10000, 40000, NAN);
  s.relative = true;
  s.paused = true;
  CHECK(t.Handled(s, 40).empty());
  const std::string rel = t.Shown(41, 1000000000LL + 50000000);
  CHECK(Has(rel.c_str(), "(a step of +30.0 s), paused"));
  CHECK(Has(rel.c_str(), "could not seek there"));
  s.toMs = 4000;
  CHECK(t.Handled(s, 41).empty());
  CHECK(Has(t.Ended().c_str(), "(a step of -6.0 s)"));
}

static void TestPending()
{
  auto p = std::make_shared<CSeekPending>();
  CHECK(!p->Pending());
  CHECK(p->Time(5) == 5);
  {
    auto t1 = p->Ticket(100);
    CHECK(p->Pending());
    CHECK(p->Time(5) == 100);
    auto t2 = p->Ticket(200);
    CHECK(p->Time(5) == 200);
    t1.reset(); // the first left out for the second
    CHECK(p->Pending());
    CHECK(p->Time(5) == 200);
    t2.reset();
    CHECK(!p->Pending());
    CHECK(p->Time(7) == 7);
  }
  {
    // the later message gone first: nothing is on its way, whatever the earlier one does
    auto t1 = p->Ticket(300);
    auto t2 = p->Ticket(400);
    t2.reset();
    CHECK(!p->Pending());
    t1.reset();
    CHECK(!p->Pending());
    CHECK(p->Time(9) == 9);
  }
  {
    // a ticket copied (a message shared by two holders) ends with its last copy
    auto t1 = p->Ticket(500);
    auto copy = t1;
    t1.reset();
    CHECK(p->Pending());
    copy.reset();
    CHECK(!p->Pending());
  }
  {
    auto t1 = p->Ticket(600);
    p->Clear();
    CHECK(!p->Pending());
    CHECK(p->Time(1) == 1);
    auto t2 = p->Ticket(700); // a seek after the clear
    CHECK(p->Time(1) == 700);
    t1.reset(); // the old ticket ends nothing of the new seek
    CHECK(p->Pending());
    t2.reset();
    CHECK(!p->Pending());
  }
  {
    // the ticket outlives the player
    auto other = std::make_shared<CSeekPending>();
    auto t = other->Ticket(1);
    other.reset();
    t.reset(); // must not touch freed memory (run under the address sanitizer)
    CHECK(true);
  }
  {
    // threads: tickets made and dropped from several threads at once
    auto q = std::make_shared<CSeekPending>();
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
      threads.emplace_back(
          [q, i]
          {
            for (int j = 0; j < 2000; ++j)
            {
              auto t = q->Ticket(i * 10000 + j);
              (void)q->Time(0);
            }
          });
    for (auto& th : threads)
      th.join();
    CHECK(!q->Pending());
  }
}

static void TestModeChange()
{
  using namespace std::chrono;
  CHECK(CModeChangeTime::TakeMs() == 0);
  CModeChangeTime::Add(steady_clock::now() - milliseconds(450));
  CModeChangeTime::Add(steady_clock::now() - milliseconds(250));
  const long long ms = CModeChangeTime::TakeMs();
  CHECK(ms >= 700 && ms < 800);
  CHECK(CModeChangeTime::TakeMs() == 0); // taken once
  CModeChangeTime::Add(steady_clock::now() + milliseconds(100)); // a start in the future adds nothing
  CHECK(CModeChangeTime::TakeMs() == 0);
}

// A model of the player's queue of seeks (CVideoPlayer::HandleMessages()): a seek message with a
// newer one queued behind it is left out; a relative step goes from the player's clock. The asker
// is CVideoPlayer::SeekTime() and SeekTimeRelative() as Hades has them.
struct SeekMsg
{
  bool relative;
  double ms;
  std::shared_ptr<void> ticket;
};
struct PlayerModel
{
  std::shared_ptr<CSeekPending> pending = std::make_shared<CSeekPending>();
  double clock = 0;
  std::deque<SeekMsg> queue;
  void SeekTime(double ms) { queue.push_back({false, ms, pending->Ticket(ms)}); }
  void SeekTimeRelative(double deltaMs)
  {
    if (const auto target = pending->RelativeTarget(deltaMs))
    {
      SeekTime(*target);
      return;
    }
    queue.push_back({true, deltaMs, pending->Ticket(clock + deltaMs)});
  }
  void Handle()
  {
    while (!queue.empty())
    {
      SeekMsg m = queue.front();
      queue.pop_front();
      if (!queue.empty())
        continue; // left out: a newer seek is queued
      clock = m.relative ? clock + m.ms : m.ms;
    }
  }
};

static void TestRelativeWhilePending()
{
  {
    // the bar dragged to 1:00:00, then +30 s before the player took the first seek
    PlayerModel p;
    p.clock = 600000;
    p.SeekTime(3600000);
    CHECK(p.pending->Time(p.clock) == 3600000);
    p.SeekTimeRelative(30000);
    CHECK(p.pending->Time(p.clock) == 3630000);
    p.Handle();
    CHECK(p.clock == 3630000); // not 630000, the old time plus 30 s
    CHECK(!p.pending->Pending());
  }
  {
    // two steps after it
    PlayerModel p;
    p.clock = 600000;
    p.SeekTime(3600000);
    p.SeekTimeRelative(30000);
    p.SeekTimeRelative(-10000);
    p.Handle();
    CHECK(p.clock == 3620000);
  }
  {
    // steps alone: the first relative as Kodi has it, the next ones from its target
    PlayerModel p;
    p.clock = 600000;
    p.SeekTimeRelative(30000);
    CHECK(p.queue.size() == 1 && p.queue.front().relative);
    p.SeekTimeRelative(30000);
    p.Handle();
    CHECK(p.clock == 660000);
    p.SeekTimeRelative(-60000);
    p.Handle();
    CHECK(p.clock == 600000);
    CHECK(!p.pending->Pending());
    CHECK(!p.pending->RelativeTarget(1000).has_value());
  }
}

static std::string ReadTree(const char* rel)
{
  std::ifstream in(std::string(KODI_TREE) + "/" + rel);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

static std::string BodyOf(const std::string& text, const char* head)
{
  const size_t at = text.find(head);
  if (at == std::string::npos)
    return {};
  const size_t end = text.find("\n}\n", at);
  return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

static void TestSourceRate()
{
  namespace R = SOURCE_RATE;
  // a film of 3840x2160 at 48 frames a second, two views: about 6 MB/s (48 Mbit/s) on average
  const uint32_t film = 6000000;
  CHECK(std::fabs(R::Mbits(6000000, 1000) - 48.0) < 1e-9);
  CHECK(std::fabs(R::Mbits(27000000, 14600) - 14.794520547945206) < 1e-9);
  CHECK(R::Mbits(1000, 0) == 0.0);
  // 14.6 s of a buffering after a seek, a third of what the film takes: slower, told
  CHECK(R::Slower(14600, 29200000, film));
  // the same source for 2.9 s: not told yet
  CHECK(!R::Slower(2900, 5800000, film));
  // as fast as the film, or faster: never told
  CHECK(!R::Slower(5000, 30000000, film));
  CHECK(!R::Slower(5000, 60000000, film));
  // nothing known of the video, or of what came: not told
  CHECK(!R::Slower(5000, 1000, 0));
  CHECK(!R::Slower(5000, -1, film));
  // just below and just at what the film takes, after 3 s
  CHECK(R::Slower(3000, 17999999, film));
  CHECK(!R::Slower(3000, 18000000, film));

  // the level shown while buffering: how far the player got to playing on (4 s of queue)
  CHECK(R::Progress(0.0, 2.0, 4.0) == 0.5);
  CHECK(R::Progress(0.0, 5.0, 4.0) == 1.0);
  CHECK(R::Progress(0.3, 0.4, 4.0) == 0.3);
  CHECK(R::Progress(-1.0, 1.0, 4.0) == 0.25);
  CHECK(R::Progress(-1.0, 0.0, 4.0) == 0.0);
  CHECK(R::Progress(0.0, 2.0, 0.0) == 0.0);

  const std::string player = ReadTree("xbmc/cores/VideoPlayer/VideoPlayer.cpp");
  CHECK(Has(player, "    state.cache_level = state.caching\n"
                    "                            ? SOURCE_RATE::Progress(cache.level, cache.time, "
                    "m_messageQueueTimeSize)\n"));
  const std::string caching = BodyOf(player, "void CVideoPlayer::SetCaching(ECacheState state)");
  CHECK(Has(caching, "  if (state == CACHESTATE_FLUSH || (m_caching == CACHESTATE_FULL && state != "
                     "CACHESTATE_FULL))\n    WatchBuffering(true);"));
  const std::string speed = BodyOf(player, "void CVideoPlayer::HandlePlaySpeed()");
  CHECK(Has(speed, "  if (m_caching == CACHESTATE_FULL)\n  {\n    // Hades: what the source sends while "
                   "the player buffers (SourceRate.h)\n    WatchBuffering(false);"));
  const std::string watch = BodyOf(player, "void CVideoPlayer::WatchBuffering(bool ends)");
  CHECK(Has(watch, "  if (m_buffering.told || !SOURCE_RATE::Slower(ms, sent, status.maxrate))\n    return;\n"
                   "  m_buffering.told = true;"));
  CHECK(Has(watch, "position + static_cast<int64_t>(status.forward)"));
  const std::string open = BodyOf(player, "bool CVideoPlayer::OpenInputStream()");
  CHECK(Has(open, "  m_buffering = BufferingState{};"));
}

int main()
{
  TestTime();
  TestTrace();
  TestPending();
  TestModeChange();
  TestRelativeWhilePending();
  TestSourceRate();
  std::printf("%s: %d passed, %d failed\n", g_fail ? "FAILED" : "PASSED", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
