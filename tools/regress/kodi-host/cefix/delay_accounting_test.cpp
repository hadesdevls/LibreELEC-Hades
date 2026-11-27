// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// delay_accounting_test.cpp: what the audio engine counts of a passthrough stream that its sink holds, against what the
// IEC 61937 packing puts on the wire, for TrueHD (MAT), E-AC-3, AC-3, DTS (512, 1024 and 2048 samples), DTS-HD HR and
// DTS-HD MA, with Kodi's own CAEBitstreamPacker, CAEPackIEC61937 and CAEStreamInfo (the files of the tree, as a release
// build compiles them).
//
// The question of 1.2.3 (the owner's A/V: TrueHD -38 ms, near two MAT frames; E-AC-3 -62, near two bursts): does the
// delay that ActiveAE reports for passthrough leave out what the packing holds? The engine counts every packet that is
// not yet in the device as CAEStreamInfo::GetDuration() of its type (CEngineStats::GetDelay()), and the device as what
// ALSA says it holds (snd_pcm_delay); the sink packs each packet whole into one burst and writes it all before it reads
// that delay (CActiveAESink::OutputSamples()). So the delay misses nothing if, for every type, the burst of one packet
// lasts on the wire exactly GetDuration() at the rate and in the channels the sink is opened with
// (CAEBitstreamPacker::GetOutputRate(), GetOutputChannelMap(), 16 bits a sample), and a pause burst of N ms lasts N ms.
// Checked here, and in the source that the engine and the sink count as said and that the passthrough codec labels each
// packet with its own time. Prints a line per check that fails and DELAY-ACCOUNTING PASSED or FAILED (n) last.
#include "cores/AudioEngine/Utils/AEBitstreamPacker.h"
#include "cores/AudioEngine/Utils/AEStreamInfo.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
std::string Read(const std::string& path)
{
  std::ifstream f(path);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

struct Type
{
  const char* name;
  CAEStreamInfo::DataType type;
  unsigned int rate; // of the stream
  unsigned int dtsBlocks; // DTS: samples / 32
  unsigned int payload; // bytes of a packet as the parser hands it over (any size that fits its burst)
};

// the sink's frame: every channel of the output 16 bits (AE_FMT_S16NE of a RAW format)
unsigned int FrameBytes(const CAEStreamInfo& info)
{
  return CAEBitstreamPacker::GetOutputChannelMap(info).Count() * 2;
}

// how long a burst of that many bytes lasts on the wire, in ms
double WireMs(const CAEStreamInfo& info, unsigned int bytes)
{
  return static_cast<double>(bytes) / FrameBytes(info) / CAEBitstreamPacker::GetOutputRate(info) * 1000.0;
}
} // namespace

int main()
{
  const Type types[] = {
      {"TrueHD (MAT)", CAEStreamInfo::STREAM_TYPE_TRUEHD, 48000, 0, 61440},
      {"E-AC-3", CAEStreamInfo::STREAM_TYPE_EAC3, 48000, 0, 1792},
      {"AC-3", CAEStreamInfo::STREAM_TYPE_AC3, 48000, 0, 1792},
      {"DTS 512", CAEStreamInfo::STREAM_TYPE_DTS_512, 48000, 16, 2012},
      {"DTS 1024", CAEStreamInfo::STREAM_TYPE_DTS_1024, 48000, 32, 2012},
      {"DTS 2048", CAEStreamInfo::STREAM_TYPE_DTS_2048, 48000, 64, 2012},
      {"DTS-HD HR", CAEStreamInfo::STREAM_TYPE_DTSHD, 48000, 16, 3000},
      {"DTS-HD MA", CAEStreamInfo::STREAM_TYPE_DTSHD_MA, 48000, 16, 6000},
  };
  for (const Type& t : types)
  {
    const std::string n = t.name;
    CAEStreamInfo info;
    info.m_type = t.type;
    info.m_sampleRate = t.rate;
    info.m_repeat = 1;
    info.m_dataIsLE = false;
    // (as CAEStreamParser::SyncDTS() sets it)
    if (t.type == CAEStreamInfo::STREAM_TYPE_DTSHD_MA)
      info.m_dtsPeriod = (192000 * (8 >> 1)) * (t.dtsBlocks << 5) / t.rate;
    else if (t.type == CAEStreamInfo::STREAM_TYPE_DTSHD)
      info.m_dtsPeriod = (192000 * (2 >> 1)) * (t.dtsBlocks << 5) / t.rate;
    std::vector<uint8_t> data(t.payload, 0x5a);
    if (t.type == CAEStreamInfo::STREAM_TYPE_AC3 || t.type == CAEStreamInfo::STREAM_TYPE_EAC3)
    {
      data[0] = 0x0b;
      data[1] = 0x77;
    }
    else if (t.type != CAEStreamInfo::STREAM_TYPE_TRUEHD)
    {
      // a DTS core sync word, big endian 16 bits
      data[0] = 0x7f;
      data[1] = 0xfe;
      data[2] = 0x80;
      data[3] = 0x01;
    }
    CAEBitstreamPacker packer;
    packer.Pack(info, data.data(), static_cast<int>(data.size()));
    const unsigned int bytes = packer.GetSize();
    const double engineMs = info.GetDuration();
    const double wireMs = WireMs(info, bytes);
    expect(bytes > 0, n + ": the packer makes a burst of the packet");
    expect(bytes % FrameBytes(info) == 0, n + ": the burst is whole frames of the sink");
    expect(std::fabs(wireMs - engineMs) < 1e-6,
           n + ": a packet counted by the engine as " + std::to_string(engineMs) + " ms is " +
               std::to_string(wireMs) + " ms on the wire");
    std::printf("%s: a packet is %.3f ms for the engine and %u bytes at %u Hz in %u channels, %.3f ms on the "
                "wire\n",
                t.name, engineMs, bytes, CAEBitstreamPacker::GetOutputRate(info),
                CAEBitstreamPacker::GetOutputChannelMap(info).Count(), wireMs);
    // a pause, as SyncStream() asks it: whole milliseconds, at most a packet
    if (t.type == CAEStreamInfo::STREAM_TYPE_DTS_1024 || t.type == CAEStreamInfo::STREAM_TYPE_DTS_2048)
      continue; // (types that no pause is asked for at another length: the same packer path as DTS 512)
    for (unsigned int ms = 1; ms <= static_cast<unsigned int>(engineMs); ++ms)
    {
      CAEBitstreamPacker pauser;
      pauser.PackPause(info, ms, true);
      const double pauseMs = WireMs(info, pauser.GetSize());
      if (std::fabs(pauseMs - ms) > 1e-6)
      {
        expect(false, n + ": a pause of " + std::to_string(ms) + " ms is " + std::to_string(pauseMs) +
                          " ms on the wire");
        break;
      }
    }
  }

  // in the source: the engine counts a packet not yet in the device as GetDuration() of its type, the sink writes a
  // packet whole before it reads the device's delay, and the passthrough codec labels each packet with its own time
  const std::string K = KODI_TREE;
  const std::string ae = Read(K + "/xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAE.cpp");
  expect(ae.find("        static_cast<double>(m_bufferedSamples) * m_sinkFormat.m_streamInfo.GetDuration() / 1000;") !=
             std::string::npos,
         "ActiveAE.cpp: a passthrough packet on its way to the device is counted as its duration");
  expect(ae.find("          delay += static_cast<float>(m_sinkFormat.m_streamInfo.GetDuration() / 1000.0);") !=
             std::string::npos,
         "ActiveAE.cpp: a passthrough packet the stream holds is counted as its duration");
  const std::string sink = Read(K + "/xbmc/cores/AudioEngine/Engines/ActiveAE/ActiveAESink.cpp");
  const size_t pack = sink.find("        m_packer->Pack(m_sinkFormat.m_streamInfo, buffer[0], frames);");
  const size_t write = sink.find("    written = m_sink->AddPackets(buffer, maxFrames, totalFrames - frames);");
  const size_t told = sink.find("    m_stats->UpdateSinkDelay(status, counted ? 1 : 0);");
  expect(pack != std::string::npos && write != std::string::npos && told != std::string::npos && pack < write &&
             write < told,
         "ActiveAESink.cpp: a packet is packed whole, written, and only then the device's delay told");
  expect(sink.find("  while (frames > 0)\n  {\n    maxFrames = std::min(frames, m_sinkFormat.m_frames);") !=
             std::string::npos,
         "ActiveAESink.cpp: the whole burst is written before the delay is read");
  const std::string pt = Read(K + "/xbmc/cores/VideoPlayer/DVDCodecs/Audio/DVDAudioCodecPassthrough.cpp");
  expect(pt.find("      if (m_nextPts != DVD_NOPTS_VALUE)\n      {\n        m_currentPts = m_nextPts;\n        "
                 "m_nextPts = packet.pts;\n      }") != std::string::npos &&
             pt.find("    else\n    {\n      m_nextPts = packet.pts;\n    }") != std::string::npos,
         "DVDAudioCodecPassthrough.cpp: a frame that the parser hands over a packet later takes its own packet's time");
  expect(pt.find("  frame.pts = m_currentPts;\n  m_currentPts = DVD_NOPTS_VALUE;") != std::string::npos,
         "DVDAudioCodecPassthrough.cpp: each frame given takes the time kept for it, once");

  if (failed)
    std::printf("DELAY-ACCOUNTING FAILED (%d)\n", failed);
  else
    std::printf("DELAY-ACCOUNTING PASSED\n");
  return failed ? 1 : 0;
}
