// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// sync_landing_test.cpp: where a correction of the sync of a passthrough stream lands
// (xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAESyncLanding.h of the Kodi tree), by a model of the correction of
// CActiveAE::SyncStream() in passthrough: ahead, a pause as long as the error, whole milliseconds (pause_burst_ms), at
// most a packet each; behind, a packet left out while the stream is behind and not landed (RawLeaveOut()), the pause
// then taking what that left ahead. For every start from 1 s behind to 1 s ahead, in steps of 0.37 ms, for TrueHD
// (20 ms, its errors scaled by TRUEHD_SYNC_ERROR_SCALE as SyncStream() has them), E-AC-3 (32 ms), AC-3 (32 ms), DTS
// (10.7 ms) and DTS-HD (10.7 ms): the correction ends, and lands within a millisecond either way (up to 1.2.3 anywhere
// within 30 ms: the owner's TrueHD kept -26, -15 or -13 ms for a whole film; the 1.2.3 test builds within half a
// packet behind: his E-AC-3 kept -15 ms); and where ActiveAE.cpp takes it (in the source). Prints a line per failed
// check and PASSED or FAILED (n) last.
#include "cores/AudioEngine/Engines/ActiveAE/ActiveAESyncLanding.h"
#include "cores/AudioEngine/Utils/TrueHDSyncScale.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using AE::TRUEHD_SYNC_ERROR_SCALE;

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
struct Landing
{
  bool ended = false;
  double residual = 0.0; // unscaled, ms
  int steps = 0;
  int dropped = 0; // packets left out
};

// the correction of SyncStream() in passthrough, one packet of the stream at a time
Landing Correct(double errorMs, double packetMs, double scale)
{
  Landing l;
  double error = errorMs * scale;
  const double packet = packetMs * scale;
  while (l.steps < 1000)
  {
    if (AE::SYNC::RawLanded(error, scale))
    {
      l.ended = true;
      break;
    }
    ++l.steps;
    if (error > 0)
    {
      int pause = static_cast<int>(error / scale); // pause_burst_ms
      if (error / scale > packetMs)
        pause = static_cast<int>(packetMs);
      error -= pause * scale;
    }
    else if (AE::SYNC::RawLeaveOut(error, scale))
    {
      error += packet;
      ++l.dropped;
    }
  }
  l.residual = error / scale;
  return l;
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
  struct Format
  {
    const char* name;
    double packet;
    double scale;
  };
  const Format formats[] = {{"TrueHD", 20.0, TRUEHD_SYNC_ERROR_SCALE},
                            {"E-AC-3", 32.0, 1.0},
                            {"AC-3", 32.0, 1.0},
                            {"DTS", 512.0 / 48.0, 1.0},
                            {"DTS-HD", 512.0 / 48.0, 1.0}};
  for (const Format& f : formats)
  {
    double worstAhead = 0.0, worstBehind = 0.0;
    int stuck = 0, extra = 0;
    for (double e = -1000.0; e <= 1000.0; e += 0.37)
    {
      const Landing l = Correct(e, f.packet, f.scale);
      if (!l.ended)
        ++stuck;
      worstAhead = std::max(worstAhead, l.residual);
      worstBehind = std::min(worstBehind, l.residual);
      // behind: never more packets left out than cover the error (the last one's remainder a pause
      // takes)
      if (e < 0 && l.dropped > static_cast<int>(std::ceil(-e / f.packet)))
        ++extra;
    }
    const std::string n = f.name;
    expect(stuck == 0, n + ": every correction ends (" + std::to_string(stuck) + " did not)");
    expect(worstAhead < 1.0, n + ": ahead it lands within a millisecond (" + std::to_string(worstAhead) + ")");
    expect(-worstBehind < 1.0, n + ": behind it lands within a millisecond (" + std::to_string(worstBehind) + ")");
    expect(extra == 0, n + ": behind, no packet left out beyond the error (" + std::to_string(extra) + " starts)");
    std::printf("%s: from 1 s behind to 1 s ahead, lands between %.2f and %.2f ms (up to 1.2.3: -30 to +30; the "
                "1.2.3 test builds: -%.2f to +1)\n",
                f.name, worstBehind, worstAhead, f.packet / 2);
  }
  // the owner's starts: TrueHD 426, 453 and 935 ms behind, 51 ahead; E-AC-3 495 behind (it kept -15 ms in the 1.2.3
  // test builds); DTS-HD 92 and 388 behind (it kept -4)
  expect(std::fabs(Correct(-426.0, 20.0, TRUEHD_SYNC_ERROR_SCALE).residual) < 1.0, "TrueHD 426 ms behind: within 1 ms");
  expect(std::fabs(Correct(-453.0, 20.0, TRUEHD_SYNC_ERROR_SCALE).residual) < 1.0, "TrueHD 453 ms behind: within 1 ms");
  expect(std::fabs(Correct(-935.0, 20.0, TRUEHD_SYNC_ERROR_SCALE).residual) < 1.0, "TrueHD 935 ms behind: within 1 ms");
  expect(Correct(51.0, 20.0, TRUEHD_SYNC_ERROR_SCALE).residual < 1.0, "TrueHD 51 ms ahead: within 1 ms, not +11");
  {
    const Landing l = Correct(-495.0, 32.0, 1.0);
    expect(std::fabs(l.residual) < 1.0 && l.dropped == 16,
           "E-AC-3 495 ms behind: 16 packets left out and a pause, within 1 ms, not -15 (" +
               std::to_string(l.residual) + ", " + std::to_string(l.dropped) + " left out)");
  }
  expect(std::fabs(Correct(-92.0, 512.0 / 48.0, 1.0).residual) < 1.0, "DTS-HD 92 ms behind: within 1 ms");
  expect(std::fabs(Correct(-388.0, 512.0 / 48.0, 1.0).residual) < 1.0, "DTS-HD 388 ms behind: within 1 ms, not -4");
  // a stream already within a millisecond is left alone: no packet left out, no pause
  expect(Correct(-0.9, 32.0, 1.0).steps == 0 && Correct(0.9, 32.0, 1.0).steps == 0,
         "within a millisecond: nothing done");
  // a resume is put back from 10 ms, ahead or behind (the noise of the average)
  expect(AE::SYNC::RAW_RESUME_CHECK_MS == 10.0, "a resume is put back from 10 ms");
  // where ActiveAE.cpp takes it
  const std::string ae = Read(std::string(KODI_TREE) + "/xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAE.cpp");
  expect(ae.find("    return AE::SYNC::RawLanded(e, scale);") != std::string::npos,
         "ActiveAE.cpp: a passthrough correction lands as ActiveAESyncLanding.h says");
  expect(ae.find("        if (AE::SYNC::RawLeaveOut(error, scale))") != std::string::npos &&
             ae.find("-error > stream->m_format.m_streamInfo.GetDuration() * scale / 2") == std::string::npos,
         "ActiveAE.cpp: a packet is left out while the stream is behind, not only beyond half a packet");
  expect(ae.find("    if (landed(error))") != std::string::npos &&
             ae.find("if (fabs(error) < 30 * scale)") == std::string::npos,
         "ActiveAE.cpp: the end of a correction asks landed(), not 30 ms");
  expect(ae.find("        stream->m_rawAdjusting = !landed(error);") != std::string::npos,
         "ActiveAE.cpp: a start out of where it lands is corrected and said");
  expect(ae.find("resumeOff = fabs(error) >= AE::SYNC::RAW_RESUME_CHECK_MS * scale;") != std::string::npos,
         "ActiveAE.cpp: a resume is checked from 10 ms");
  expect(ae.find("        stream->m_rawLandedAt = error / scale;") != std::string::npos &&
             ae.find("\"ActiveAE::SyncStream - the passthrough stream ({}) measured after its correction: \"") !=
                 std::string::npos,
         "ActiveAE.cpp: the first average after a correction is said against where it was reckoned");
  if (failed)
    std::printf("FAILED (%d)\n", failed);
  else
    std::printf("PASSED\n");
  return failed ? 1 : 0;
}
