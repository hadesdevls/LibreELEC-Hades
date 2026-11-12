/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of the hang at the start of a video from a settings window
 * (settings/windows/SettingsWindowResize.h, CGUIWindowSettingsCategory::OnMessage()).
 *
 * A model of the threads and messages that take part, each as Kodi does it:
 * - the interface thread runs passes of its loop (CApplication::Process()): first the messages of the
 *   window manager (DispatchThreadMessages(): "playback started", "the player shows its video"), then
 *   those of the application messenger until its queue is empty (ProcessMessages(): the built-in
 *   PlayMedia, the player's TMSG_SETVIDEORESOLUTION);
 * - PlayMedia opens the player: its thread starts, and "playback started" is queued for the window
 *   manager (CVideoPlayer::OpenFile(), CApplicationPlayerCallback::OnPlayBackStarted());
 * - "playback started" waits for the player with the busy dialog: 100 ms, then a loop of passes of its
 *   own until the player shows its video (CGUIDialogBusy::WaitOnEvent(m_playerEvent), which
 *   GUI_MSG_PLAYBACK_AVSTARTED sets);
 * - the player thread asks for the mode of its video and waits until the interface has set it
 *   (CGraphicContext::SetVideoResolution(): SendMsg(TMSG_SETVIDEORESOLUTION)), then opens its video
 *   stream (it has a video from then on: m_HasVideo) and, once it shows it, says so;
 * - the change of mode is told to every window (GUI_MSG_WINDOW_RESIZE); the settings window, active,
 *   does what CSettingsWindowResize::Of() says. SAVE changes videoscreen.screenmode, which asks "Keep
 *   display mode?" in a modal dialog: a loop of passes of its own until its time runs out (15 s in Kodi),
 *   the answer "no" putting the mode of before back (CDisplaySettings::OnSettingChanging()).
 * A loop inside a pass is a call on the same stack: the loop below goes on only once the one above it
 * has returned.
 * The two orders of a start: the player's request is handled in the pass that ran PlayMedia, before
 * "playback started" (the failed runs of the GUI smoke test: there were other messages in the queue, or
 * the interface was slow), or inside the busy dialog that "playback started" opened (the runs that
 * passed). A run ends by itself after DEADLINE (every loop and the wait of the player give up then) and
 * says what happened.
 * Built with -DOLD_RULE, the window asks what it asked before (IsPlayingVideo(): the player has a video),
 * and the test must fail: the start hangs in the first order, and asks and waits in the second.
 *
 * The rule keeps a mode where the player set it for its video (KODI::WINDOWING::CVideoModeChange,
 * the real header: the player thread asks for the mode under its scope, and CRenderManager sets one
 * under it on the interface thread), and while a video plays; anything else is saved as Kodi does,
 * also while music plays. Cases added: music and a mode that the window system sets by itself (a
 * display plugged in again) is saved and asked about; a video that plays and a mode of the window
 * system, or one that the player sets during playback, is not. Mutants that must fail:
 * -DIS_PLAYING_RULE (the rule of before: any active player keeps the mode, music too),
 * -DNO_SIGNAL (the window does not read the signal), -DNO_VIDEO_PLAYS (it does not look at a video
 * that plays).
 */
#include "settings/windows/SettingsWindowResize.h"
#include "windowing/VideoModeChange.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono;
using namespace std::chrono_literals;

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

// The times of Kodi, shortened (the question waits 15 s there, the busy dialog 100 ms)
constexpr auto QUESTION = 600ms;
constexpr auto BUSY_FIRST = 20ms;
constexpr auto FRAME = 2ms;
constexpr auto DEADLINE = 3s;

struct Event
{
  std::mutex m;
  std::condition_variable cv;
  bool set = false;
  void Set()
  {
    {
      std::lock_guard l(m);
      set = true;
    }
    cv.notify_all();
  }
  bool IsSet()
  {
    std::lock_guard l(m);
    return set;
  }
  bool WaitFor(milliseconds d)
  {
    std::unique_lock l(m);
    return cv.wait_for(l, d, [this] { return set; });
  }
};

enum class Order
{
  REQUEST_FIRST, //!< the player's request is handled in the pass of PlayMedia
  STARTED_FIRST, //!< ... inside the busy dialog of "playback started"
};

const char* Name(Order order)
{
  return order == Order::REQUEST_FIRST ? "request before \"playback started\""
                                       : "request inside the busy dialog";
}

constexpr int INTERFACE_MODE = 0;
constexpr int VIDEO_MODE = 1;

class CStart
{
public:
  CStart(Order order, bool windowActive) : m_order(order), m_windowActive(windowActive) {}

  // The interface thread is this one: PlayMedia is queued (python's xbmc.executebuiltin()), and the
  // main loop runs until the player shows its video, or DEADLINE
  void Run()
  {
    m_start = steady_clock::now();
    Post([this] { PlayMedia(); });
    Loop("main loop", [this] { return m_playerEvent.IsSet(); });
    m_giveUp = true;
    if (m_player.joinable())
      m_player.join();
  }

  // A change of mode outside playback (the window system set one by itself): Kodi's own case
  void ChangeModeWithoutPlayer()
  {
    m_start = steady_clock::now();
    SetVideoResolution(VIDEO_MODE);
  }

  // The player plays music (PAPlayer: active, no video), or a video, and the mode changes on
  // the interface thread: by the window system itself (a display plugged in again,
  // CWinSystemGbm::RefreshModes()), or by the player for its video (CRenderManager::UpdateResolution(),
  // under its scope)
  void ChangeModeWhilePlaying(bool video, bool byPlayer)
  {
    m_start = steady_clock::now();
    m_playerActive = true;
    m_playerHasVideo = video;
    if (byPlayer)
    {
      const KODI::WINDOWING::CVideoModeChange::CScope playerSetsMode;
      SetVideoResolution(VIDEO_MODE);
    }
    else
      SetVideoResolution(VIDEO_MODE);
  }

  bool Shown() const { return m_shownAfter >= 0ms; }
  milliseconds ShownAfter() const { return m_shownAfter; }
  bool PlayerWaitedForGood() const { return m_playerWaitedForGood; }
  int Questions() const { return m_questions; }
  int InterfaceMode() const { return m_interfaceMode; }
  int Mode() const { return m_mode; }
  CSettingsWindowResize::Step FirstStep() const { return m_firstStep; }
  const std::string& DeepestLoops() const { return m_deepest; }

private:
  void Post(std::function<void()> f)
  {
    std::lock_guard l(m_queueLock);
    m_appQueue.push_back(std::move(f));
  }

  bool Late()
  {
    if (steady_clock::now() - m_start > DEADLINE)
      m_giveUp = true;
    return m_giveUp;
  }

  // A loop of passes (the main loop, or one of a dialog: CGUIDialog::Open_Internal(),
  // CGUIDialogBusy::WaitOnEvent()), on the stack of the interface thread
  void Loop(const char* what, const std::function<bool()>& done)
  {
    m_loops.emplace_back(what);
    if (m_loops.size() > m_deepestSize)
    {
      m_deepestSize = m_loops.size();
      m_deepest.clear();
      for (const auto& loop : m_loops)
        m_deepest += (m_deepest.empty() ? "" : " > ") + loop;
    }
    while (!done() && !Late())
      Pass();
    m_loops.pop_back();
  }

  // CApplication::Process(), then a frame
  void Pass()
  {
    // DispatchThreadMessages(): one at a time, only those that were queued when it began; a loop that
    // a message opens takes the others in its own passes
    size_t count;
    {
      std::lock_guard l(m_queueLock);
      count = m_threadQueue.size();
    }
    for (size_t i = 0; i < count; ++i)
    {
      int message;
      {
        std::lock_guard l(m_queueLock);
        if (m_threadQueue.empty())
          break;
        message = m_threadQueue.front();
        m_threadQueue.pop_front();
      }
      if (message == PLAYBACK_STARTED)
        BusyWait();
      else if (message == PLAYBACK_AVSTARTED)
      {
        m_shownAfter = duration_cast<milliseconds>(steady_clock::now() - m_start);
        m_playerEvent.Set();
      }
    }
    // ProcessMessages(): until the queue is empty; a message waited for is answered once handled
    for (;;)
    {
      std::function<void()> message;
      {
        std::lock_guard l(m_queueLock);
        if (m_appQueue.empty())
          break;
        message = std::move(m_appQueue.front());
        m_appQueue.pop_front();
      }
      message();
    }
    std::this_thread::sleep_for(FRAME);
  }

  // CApplication::PlayFile(), CVideoPlayer::OpenFile()
  void PlayMedia()
  {
    m_playerActive = true;
    m_player = std::thread([this] { PlayerThread(); });
    {
      std::lock_guard l(m_queueLock);
      m_threadQueue.push_back(PLAYBACK_STARTED);
    }
    // (the rest of this pass: other messages in the queue, a slow interface; longer than the player
    // takes to ask for its mode)
    if (m_order == Order::REQUEST_FIRST)
      while (!m_requestQueued && !Late())
        std::this_thread::sleep_for(1ms);
  }

  // CGUIDialogBusy::WaitOnEvent(m_playerEvent)
  void BusyWait()
  {
    if (m_playerEvent.WaitFor(BUSY_FIRST))
      return;
    m_inBusyDialog = true;
    Loop("busy dialog", [this] { return m_playerEvent.IsSet(); });
  }

  // The player's thread (CVideoPlayer::OpenVideoStream())
  void PlayerThread()
  {
    if (m_order == Order::STARTED_FIRST)
      while (!m_inBusyDialog && !m_giveUp)
        std::this_thread::sleep_for(1ms);
    // SendMsg(TMSG_SETVIDEORESOLUTION): queued, and waited for until the interface has handled it;
    // under the scope of the player that sets the mode of its video, as
    // CVideoPlayer::OpenVideoStream() makes it around SetVideoResolution()
    auto handled = std::make_shared<Event>();
    {
      const KODI::WINDOWING::CVideoModeChange::CScope playerSetsMode;
      Post(
          [this, handled]
          {
            SetVideoResolution(VIDEO_MODE);
            handled->Set();
          });
      m_requestQueued = true;
      while (!handled->WaitFor(10ms) && !m_giveUp)
      {
      }
    }
    // (answered only once every loop gave up at DEADLINE, or not at all: in Kodi, never)
    if (m_giveUp)
    {
      m_playerWaitedForGood = true;
      return;
    }
    m_playerHasVideo = true;
    std::this_thread::sleep_for(5ms); // (its first picture)
    std::lock_guard l(m_queueLock);
    m_threadQueue.push_back(PLAYBACK_AVSTARTED);
  }

  // CGraphicContext::SetVideoResolutionInternal(), on the interface thread
  void SetVideoResolution(int mode)
  {
    if (mode == m_mode)
      return;
    m_mode = mode;
    OnResize();
  }

  // CGUIWindowSettingsCategory::OnMessage(GUI_MSG_WINDOW_RESIZE)
  void OnResize()
  {
    const bool differs = m_windowActive && m_interfaceMode != m_mode;
    // (as CGUIWindowSettingsCategory::OnMessage() asks: the signal of the player, IsPlayingVideo())
    const bool videoPlays = m_playerActive && m_playerHasVideo;
#if defined(OLD_RULE)
    const bool playerSetsMode = false; // before: IsPlayingVideo() alone
#elif defined(IS_PLAYING_RULE)
    const bool playerSetsMode = m_playerActive.load(); // IsPlaying(): music too
#elif defined(NO_SIGNAL)
    const bool playerSetsMode = false;
#else
    const bool playerSetsMode = differs && KODI::WINDOWING::CVideoModeChange::Now();
#endif
#if defined(NO_VIDEO_PLAYS)
    const bool keepWhileVideo = false;
#else
    const bool keepWhileVideo = videoPlays;
#endif
    const auto step =
        CSettingsWindowResize::Of(m_windowActive, differs, playerSetsMode, keepWhileVideo);
    if (!m_stepSeen)
    {
      m_stepSeen = true;
      m_firstStep = step;
    }
    if (step == CSettingsWindowResize::Step::SAVE)
      SaveScreenMode(m_mode);
  }

  // CDisplaySettings::SetCurrentResolution(mode, true) -> OnSettingChanging(videoscreen.screenmode)
  void SaveScreenMode(int mode)
  {
    const int before = m_interfaceMode;
    m_interfaceMode = mode;
    SetVideoResolution(mode);
    ++m_questions;
    // "Keep display mode?": nobody answers, and its time runs out (the answer is no)
    const auto until = steady_clock::now() + QUESTION;
    Loop("question", [until] { return steady_clock::now() >= until; });
    m_interfaceMode = before;
    SetVideoResolution(before);
  }

  static constexpr int PLAYBACK_STARTED = 1;
  static constexpr int PLAYBACK_AVSTARTED = 2;

  const Order m_order;
  const bool m_windowActive;
  steady_clock::time_point m_start;
  std::atomic<bool> m_giveUp{false};
  std::mutex m_queueLock;
  std::deque<std::function<void()>> m_appQueue;
  std::deque<int> m_threadQueue;
  Event m_playerEvent;
  std::atomic<bool> m_playerActive{false};
  std::atomic<bool> m_playerHasVideo{false};
  std::atomic<bool> m_inBusyDialog{false};
  std::atomic<bool> m_requestQueued{false};
  std::atomic<bool> m_playerWaitedForGood{false};
  std::thread m_player;
  // (the interface thread only)
  int m_interfaceMode = INTERFACE_MODE;
  int m_mode = INTERFACE_MODE;
  int m_questions = 0;
  bool m_stepSeen = false;
  CSettingsWindowResize::Step m_firstStep = CSettingsWindowResize::Step::NOTHING;
  milliseconds m_shownAfter{-1};
  std::vector<std::string> m_loops;
  size_t m_deepestSize = 0;
  std::string m_deepest;
};

void Describe(const char* what, const CStart& s)
{
  std::printf("%s: %s; %d question(s); interface mode %d, mode %d; deepest loops: %s\n", what,
              s.Shown() ? ("video shown after " + std::to_string(s.ShownAfter().count()) + " ms").c_str()
              : s.PlayerWaitedForGood()
                  ? ("HANG: the player still waited for its mode after " +
                     std::to_string(duration_cast<milliseconds>(DEADLINE).count()) + " ms")
                        .c_str()
                  : "no video",
              s.Questions(), s.InterfaceMode(), s.Mode(), s.DeepestLoops().c_str());
}
} // namespace

int main()
{
  using Step = CSettingsWindowResize::Step;
  // the rule, every case (window active, another mode, the player sets the mode of its video, a video
  // plays)
  for (int i = 0; i < 16; ++i)
  {
    const bool active = i & 1, differs = i & 2, playerSets = i & 4, video = i & 8;
    const Step want = !active || !differs ? Step::NOTHING
                      : playerSets        ? Step::KEEP_PLAYER_MODE
                      : video             ? Step::KEEP_VIDEO_PLAYS
                                          : Step::SAVE;
    Check(CSettingsWindowResize::Of(active, differs, playerSets, video) == want,
          "the rule, case " + std::to_string(i));
  }
  static_assert(CSettingsWindowResize::Of(true, true, false, false) == Step::SAVE);
  static_assert(CSettingsWindowResize::Of(true, true, true, false) == Step::KEEP_PLAYER_MODE);
  static_assert(CSettingsWindowResize::Of(true, true, false, true) == Step::KEEP_VIDEO_PLAYS);
  // the signal of the player, the real header: none, one, two at once, none again
  {
    using KODI::WINDOWING::CVideoModeChange;
    const bool before = CVideoModeChange::Now();
    bool inside = false, both = false, afterOne = false;
    {
      const CVideoModeChange::CScope a;
      inside = CVideoModeChange::Now();
      {
        const CVideoModeChange::CScope b;
        both = CVideoModeChange::Now();
      }
      afterOne = CVideoModeChange::Now();
    }
    Check(!before && inside && both && afterOne && !CVideoModeChange::Now(),
          "the signal: on while a scope of the player lives, off after the last");
  }

  // the start of a video from the settings window, in both orders
  for (Order order : {Order::REQUEST_FIRST, Order::STARTED_FIRST})
  {
    CStart start(order, true);
    start.Run();
    Describe(Name(order), start);
    const std::string where = std::string("settings window, ") + Name(order) + ": ";
    Check(start.Shown(), where + "the video is shown (no hang)");
    Check(!start.PlayerWaitedForGood(), where + "the player's request was answered");
    Check(start.Questions() == 0, where + "nobody is asked to keep the mode of the video");
    Check(start.Shown() && start.ShownAfter() < QUESTION,
          where + "the video is shown sooner than a question would have run out");
    Check(start.InterfaceMode() == INTERFACE_MODE,
          where + "the resolution of the interface stays");
    Check(start.Mode() == VIDEO_MODE, where + "the mode is the video's");
    Check(start.FirstStep() == Step::KEEP_PLAYER_MODE,
          where + "the mode is kept as the one that the player set for its video");
  }

  // the same from another window: nothing to do with the settings window, in both orders
  for (Order order : {Order::REQUEST_FIRST, Order::STARTED_FIRST})
  {
    CStart start(order, false);
    start.Run();
    const std::string where = std::string("another window, ") + Name(order) + ": ";
    Check(start.Shown() && start.Questions() == 0 && start.Mode() == VIDEO_MODE,
          where + "the video is shown in its mode, nobody asked");
  }

  // outside playback, Kodi's behaviour stays: a mode that the window system set by itself under the
  // active settings window becomes the resolution of the interface, and the user is asked to keep it
  {
    CStart start(Order::STARTED_FIRST, true);
    start.ChangeModeWithoutPlayer();
    Check(start.Questions() == 1, "no player: the user is asked to keep the mode, as before");
    Check(start.FirstStep() == Step::SAVE, "no player: the mode is saved, as Kodi does");
  }

  // music plays (an active player with no video) and the window system sets a mode by itself
  // (a display plugged in again): Kodi's case too, the mode is saved and the user asked
  {
    CStart start(Order::STARTED_FIRST, true);
    start.ChangeModeWhilePlaying(false, false);
    Check(start.FirstStep() == Step::SAVE, "music and a mode of the window system: saved, as Kodi does");
    Check(start.Questions() == 1, "music and a mode of the window system: the user is asked");
  }
  // a video plays and the window system sets a mode by itself: not saved, nobody asked
  {
    CStart start(Order::STARTED_FIRST, true);
    start.ChangeModeWhilePlaying(true, false);
    Check(start.FirstStep() == Step::KEEP_VIDEO_PLAYS && start.Questions() == 0 &&
              start.InterfaceMode() == INTERFACE_MODE,
          "a video plays and a mode of the window system: not saved, nobody asked");
  }
  // a video plays and the player sets its mode again (CRenderManager::UpdateResolution()):
  // kept as the player's
  {
    CStart start(Order::STARTED_FIRST, true);
    start.ChangeModeWhilePlaying(true, true);
    Check(start.FirstStep() == Step::KEEP_PLAYER_MODE && start.Questions() == 0 &&
              start.InterfaceMode() == INTERFACE_MODE,
          "a video plays and the player sets its mode: not saved, nobody asked");
  }
  // the scope of a start is gone once it has its mode: nothing of it stays on
  Check(!KODI::WINDOWING::CVideoModeChange::Now(), "no scope of the player is left on");

  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
