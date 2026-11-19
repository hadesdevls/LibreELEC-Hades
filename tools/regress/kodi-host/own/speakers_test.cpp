// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// speakers_test.cpp: the plane of the speakers on the Audio page of the player information (hades.audio.spk, from
// KODI::UTILS::PLAYERINFO::Speakers() and SpeakersOrUsual() of utils/HadesPlayerInfo.h), on the way that its value
// takes in Kodi: the layout that CVideoPlayerAudio tells, as CAEChannelInfo writes it, into Kodi's own data cache
// (cores/DataCacheCore.cpp, built here as it is); the audio engine's clear of that cache when its format changes
// (CDataCacheCore::ResetAudioCache(), which ActiveAE::Configure() calls), which left the plane at "not known" for the
// rest of a video; the player telling it again (StreamInfoCleared(), as CVideoPlayerAudio::KeepStreamInfo() asks it);
// and the usual layout of the stream's number of channels where nothing is said, against Kodi's own standard layouts
// (AEChannelInfo.cpp). One line FAIL: per check that fails; the last line says the count.
#include "ServiceBroker.h"
#include "cores/AudioEngine/Utils/AEChannelInfo.h"
#include "cores/DataCacheCore.h"
#include "utils/HadesPlayerInfo.h"

#include <cstdio>
#include <string>

CDataCacheCore& CServiceBroker::GetDataCacheCore()
{
  static CDataCacheCore cache;
  return cache;
}

namespace
{
int checks = 0;
int failed = 0;

void Check(bool ok, const std::string& what, const std::string& got = "")
{
  ++checks;
  if (!ok)
  {
    ++failed;
    std::printf("FAIL: %s%s%s\n", what.c_str(), got.empty() ? "" : ": got ", got.c_str());
  }
}

// a layout as CAEChannelInfo writes it ("FL, FR, FC"), as UsualLayout() writes it ("FL,FR,FC")
std::string Plain(const std::string& names)
{
  std::string out;
  for (const char c : names)
    if (c != ' ')
      out += c;
  return out;
}
} // namespace

int main()
{
  using namespace KODI::UTILS::PLAYERINFO;
  const CAEChannelInfo l71(AE_CH_LAYOUT_7_1);
  const std::string told = l71; // what the player tells of a 7.1 stream
  const std::string lit71 = ";BL;BR;FC;FL;FR;LFE;SL;SR;";
  const std::string lit712 = ";BL;BR;FC;FL;FR;LFE;SL;SR;TFL;TFR;";

  // 1. what the player tells, and a bed of Dolby Atmos
  Check(Speakers(told, "") == lit71, "a 7.1 layout lights its eight speakers", Speakers(told, ""));
  Check(Speakers(told, "7.1.2") == lit712, "a bed of 7.1.2 lights its own", Speakers(told, "7.1.2"));
  Check(Speakers(told, "LFE") == lit71, "a bed of the LFE alone leaves it to the layout", Speakers(told, "LFE"));
  for (const char* none : {"unknown", "RAW, RAW, RAW, RAW, RAW, RAW, RAW, RAW", "NULL", ""})
    Check(Speakers(none, "").empty(), std::string("\"") + none + "\" names no speaker", Speakers(none, ""));

  // 2. the usual layout of a number of channels is Kodi's standard one
  const AEStdChLayout standard[] = {AE_CH_LAYOUT_1_0, AE_CH_LAYOUT_2_0, AE_CH_LAYOUT_3_0,
                                    AE_CH_LAYOUT_4_0, AE_CH_LAYOUT_5_0, AE_CH_LAYOUT_5_1,
                                    AE_CH_LAYOUT_7_0, AE_CH_LAYOUT_7_1};
  for (int n = 1; n <= 8; ++n)
  {
    const std::string kodi = Plain(CAEChannelInfo(standard[n - 1]));
    Check(UsualLayout(n) == kodi, "the usual layout of " + std::to_string(n) + " channel(s) is Kodi's " + kodi,
          UsualLayout(n));
  }
  Check(UsualLayout(0).empty() && UsualLayout(9).empty() && UsualLayout(-1).empty(),
        "no usual layout for 0, 9 or -1 channels");

  // 3. the plane: what the player told first; where it names nothing, the usual layout, marked as such
  {
    const SpeakersShown s = SpeakersOrUsual(told, "", 6);
    Check(s.lit == lit71 && !s.fromCount, "what the player told wins over the stream's count", s.lit);
  }
  {
    const SpeakersShown s = SpeakersOrUsual("unknown", "", 8);
    Check(s.lit == lit71 && s.fromCount, "\"unknown\" with 8 channels: the usual 7.1, said as such", s.lit);
  }
  {
    const SpeakersShown s = SpeakersOrUsual("RAW, RAW, RAW, RAW, RAW, RAW", "", 6);
    Check(s.lit == ";BL;BR;FC;FL;FR;LFE;" && s.fromCount, "a passthrough carrier with 6 channels: the usual 5.1", s.lit);
  }
  {
    const SpeakersShown s = SpeakersOrUsual("unknown", "7.1.2", 8);
    Check(s.lit == lit712 && !s.fromCount, "a bed is what the stream says, not a guess", s.lit);
  }
  {
    const SpeakersShown s = SpeakersOrUsual("", "", 0);
    Check(s.lit.empty() && !s.fromCount, "nothing told and no count: not known", s.lit);
  }

  // 4. Kodi's data cache: the engine's clear, and the layout told again
  CDataCacheCore& cache = CServiceBroker::GetDataCacheCore();
  cache.SetAudioChannels(told);
  CDataCacheCore::SAudioPresentation presentation;
  presentation.objects = true;
  presentation.bedName = "7.1.2";
  cache.SetAudioPresentation(presentation);
  Check(Speakers(cache.GetAudioChannels(), cache.GetAudioPresentation().bedName) == lit712,
        "the cache gives the plane what the player told");
  Check(!StreamInfoCleared(true, cache.GetAudioChannels()), "a layout in the cache is not told again");
  cache.ResetAudioCache(); // ActiveAE::Configure(), where the engine's own format changes
  Check(cache.GetAudioChannels().empty() && !cache.GetAudioPresentation().objects,
        "the engine's clear leaves no layout and no bed (Kodi's own behaviour, what made the plane \"not known\")");
  Check(Speakers(cache.GetAudioChannels(), "").empty(), "after the clear, the layout names no speaker");
  Check(!StreamInfoCleared(false, cache.GetAudioChannels()), "nothing told for this stream: nothing to tell again");
  Check(StreamInfoCleared(true, cache.GetAudioChannels()), "a layout told and cleared is told again");
  // as CVideoPlayerAudio::KeepStreamInfo() does: its own copies, told again
  if (StreamInfoCleared(true, cache.GetAudioChannels()))
  {
    cache.SetAudioChannels(told);
    cache.SetAudioPresentation(presentation);
  }
  Check(Speakers(cache.GetAudioChannels(), cache.GetAudioPresentation().bedName) == lit712,
        "told again, the plane lights the stream's speakers",
        Speakers(cache.GetAudioChannels(), cache.GetAudioPresentation().bedName));

  std::printf("SPEAKERS-TEST: %d checks, %d failed\n", checks, failed);
  std::printf("%s\n", failed ? "SPEAKERS-TEST FAILED" : "SPEAKERS-TEST PASSED");
  return failed ? 1 : 0;
}
