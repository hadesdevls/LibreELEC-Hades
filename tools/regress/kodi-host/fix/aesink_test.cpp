/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Hades (1.0 RC4): the list of the outputs of the audio engine
 * (CActiveAESink: EnumerateSinkList() writes it, about fifteen accessors read it), run on the host as the tree has
 * it: aesink_gen.py takes out of ActiveAESink.cpp the readers, EnumerateSinkList(), the helpers of its unnamed
 * namespace and SinkInfo() where the tree has it; the real AESinkFactory.h, AEDeviceInfo.h/.cpp,
 * AEChannelInfo.h/.cpp, ActiveAEFollowDisplay.h, AEPcmChannels.h and DisplayIdentity.h; and here the drivers
 * (CAESinkFactory::EnumerateEx(): the outputs of ALSA and PulseAudio, a display on one HDMI output or on the other,
 * one output at a time with a yield between them, as a driver that reads each card takes time), ParseDevice() as
 * the factory parses a device for those two drivers, and the members of the class that the readers use.
 *  same      every reader, for a set of devices, on each of two lists: the lines printed, which two trees compare
 *            (what each returns must not change);
 *  race <n>  an enumeration again and again on one thread (the engine's, on a hot-plug), the readers on three
 *            others (the settings and JSON-RPC, OutputOfTheDisplay() that looks for the output of the display, the
 *            thread of the sink), n rounds each. With ThreadSanitizer: the tree before reports the race, the
 *            snapshot none; with AddressSanitizer, no read of freed memory.
 */
#include "config.h"

#include "cores/AudioEngine/AESinkFactory.h"
#include "cores/AudioEngine/Engines/ActiveAE/ActiveAEFollowDisplay.h"
#include "cores/AudioEngine/Utils/AEPcmChannels.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "utils/DisplayIdentity.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

typedef std::pair<std::string, std::string> AEDevice;
typedef std::vector<AEDevice> AEDeviceList;

enum
{
  LOGDEBUG,
  LOGINFO,
  LOGWARNING,
  LOGERROR
};
struct CLog
{
  template<typename... A>
  static void Log(int, const char*, A&&...)
  {
  }
  template<typename... A>
  static void LogF(int, const char*, A&&...)
  {
  }
};
struct CThread
{
  template<typename D>
  static void Sleep(D)
  {
  }
};

// --- the drivers
namespace
{
std::atomic<int> g_variant{0}; // which HDMI output has the display
int g_enumerations = 0;

CAEDeviceInfo Output(const std::string& name, const std::string& display, AEDeviceType type, bool there,
                     unsigned pcm)
{
  CAEDeviceInfo d;
  d.m_deviceName = name;
  d.m_displayName = display;
  d.m_displayNameExtra = there ? "LG TV SSCR2, a display with a name long enough to be on the heap" : "";
  d.m_deviceType = type;
  d.m_sampleRates = {44100, 48000, 96000, 192000};
  d.m_dataFormats = {AE_FMT_FLOAT, AE_FMT_S32NE, AE_FMT_S16NE};
  if (type != AE_DEVTYPE_PCM)
    d.m_streamTypes = {CAEStreamInfo::STREAM_TYPE_AC3, CAEStreamInfo::STREAM_TYPE_EAC3,
                       CAEStreamInfo::STREAM_TYPE_TRUEHD};
  d.m_wantsIECPassthrough = true;
  d.m_display = type == AE_DEVTYPE_HDMI ? (there ? CAEDeviceInfo::Display::PRESENT : CAEDeviceInfo::Display::ABSENT)
                                        : CAEDeviceInfo::Display::UNKNOWN;
  d.m_maxPcmChannels = there ? pcm : 0;
  d.m_maxAc3Channels = there ? 6 : 0;
  d.m_maxEac3Channels = there ? 8 : 0;
  if (there)
  {
    d.m_displayIdentity.maker = 0x1E6D;
    d.m_displayIdentity.product = 0xC0DE;
    d.m_displayIdentity.model = "LG TV SSCR2";
  }
  return d;
}
} // namespace

bool AE::CAESinkFactory::HasSinks()
{
  return true;
}

// as the factory parses a device (AESinkFactory.cpp), for its two registered drivers
AE::AESinkDevice AE::CAESinkFactory::ParseDevice(const std::string& device)
{
  AE::AESinkDevice dev{};
  dev.name = device;
  size_t pos = dev.name.find_first_of(':');
  bool found = false;
  if (pos != std::string::npos)
  {
    dev.driver = device.substr(0, pos);
    if (dev.driver == "ALSA" || dev.driver == "PULSE")
    {
      dev.name = dev.name.substr(pos + 1, dev.name.length() - pos - 1);
      found = true;
    }
  }
  if (!found)
    dev.driver.clear();
  pos = dev.name.find_last_of('|');
  if (pos != std::string::npos)
  {
    dev.friendlyName = dev.name.substr(pos + 1);
    dev.name = dev.name.substr(0, pos);
  }
  return dev;
}

// one output after another, a yield between them, into the list given
void AE::CAESinkFactory::EnumerateEx(std::vector<AE::AESinkInfo>& list, bool, const std::string& driver)
{
  ++g_enumerations;
  const int v = g_variant.load();
  if (driver.empty() || driver == "ALSA")
  {
    list.push_back({"ALSA", {}});
    AE::AESinkInfo& alsa = list.back();
    const std::vector<CAEDeviceInfo> outputs = {
        Output("default", "Default (the system's default output, with a long name)", AE_DEVTYPE_PCM, false, 0),
        Output("hdmi:CARD=NVidia,DEV=0", "HDA NVidia, HDMI 0", AE_DEVTYPE_HDMI, v == 0, 8),
        Output("hdmi:CARD=NVidia,DEV=1", "HDA NVidia, HDMI 1", AE_DEVTYPE_HDMI, false, 0),
        Output("hdmi:CARD=Generic,DEV=0", "HD-Audio Generic, HDMI 0", AE_DEVTYPE_HDMI, v == 1, 6),
        Output("sysdefault:CARD=Generic", "HD-Audio Generic, ALC1220 Analog", AE_DEVTYPE_PCM, false, 0),
        Output("iec958:CARD=Generic,DEV=0", "HD-Audio Generic, ALC1220 Digital", AE_DEVTYPE_IEC958, false, 0)};
    for (const auto& o : outputs)
    {
      alsa.m_deviceInfoList.push_back(o);
      std::this_thread::yield();
    }
  }
  if (driver.empty() || driver == "PULSE")
  {
    list.push_back({"PULSE", {Output("Default", "Default output of PulseAudio", AE_DEVTYPE_PCM, false, 0)}});
    std::this_thread::yield();
  }
}

const char* CAEUtil::DataFormatToStr(const enum AEDataFormat dataFormat)
{
  return dataFormat == AE_FMT_FLOAT ? "float" : "other";
}
const char* CAEUtil::StreamTypeToStr(const enum CAEStreamInfo::DataType dataType)
{
  return dataType == CAEStreamInfo::STREAM_TYPE_AC3 ? "ac3" : "other";
}

namespace ActiveAE
{
// (what of CActiveAESink the readers use)
struct SinkStub
{
  std::string GetName() const { return "ALSA"; }
};
class CActiveAESink
{
public:
  void EnumerateSinkList(bool force, std::string driver);
  void EnumerateOutputDevices(AEDeviceList& devices, bool passthrough);
  std::string ValidateOuputDevice(const std::string& device, bool passthrough) const;
  std::string FollowDisplay(const std::string& device,
                            const std::string& followed,
                            bool passthrough,
                            bool* missing = nullptr,
                            bool* familyListed = nullptr,
                            const INTERNAL::SoundDisplays* displays = nullptr,
                            INTERNAL::DisplayInUseChoice* choice = nullptr) const;
  bool IsMissingBesideItsFamily(const std::string& device) const;
  std::string FallbackOutput(bool passthrough) const;
  std::string OutputOfTheDisplay(bool passthrough,
                                 const KODI::UTILS::DisplayIdentity& driven,
                                 INTERNAL::DisplayOutputFound& found) const;
  AEDeviceType GetDeviceType(const std::string& device);
  bool GetDisplayAudio(const std::string& device, AE::PCM::DisplayAudio& audio, bool& displayThere) const;
  bool HasPassthroughDevice();
  bool SupportsFormat(const std::string& device, AEAudioFormat& format);
  bool DeviceExist(std::string driver, const std::string& device);
  void PrintSinks(std::string& driver);
  void GetDeviceFriendlyName(const std::string& device);
  bool NeedIECPacking();

  std::string m_device;
  std::string m_deviceFriendlyName;
  bool m_extReserved{false};
  std::unique_ptr<SinkStub> m_sink;
#if HAVE_SNAPSHOT
  using SinkInfoList = std::vector<AE::AESinkInfo>;
  std::atomic<std::shared_ptr<const SinkInfoList>> m_sinkInfo{std::make_shared<const SinkInfoList>()};
  std::shared_ptr<const SinkInfoList> SinkInfo() const;
#else
  std::vector<AE::AESinkInfo> m_sinkInfoList;
#endif
};
} // namespace ActiveAE

using namespace AE;
using namespace ActiveAE;
using namespace std::chrono_literals;

#include "pieces.inc"

namespace
{
const std::vector<std::string> g_devices = {
    "ALSA:hdmi:CARD=NVidia,DEV=0",
    "ALSA:hdmi:CARD=NVidia,DEV=0|HDA NVidia, HDMI 0",
    "ALSA:hdmi:CARD=Generic,DEV=0",
    "ALSA:hdmi:CARD=Generic,DEV=1",
    "ALSA:hdmi:CARD=Intel,DEV=0",
    "ALSA:default",
    "ALSA:sysdefault:CARD=Generic",
    "ALSA:iec958:CARD=Generic,DEV=0",
    "PULSE:Default",
    "ALSA:nothing|HD-Audio Generic, ALC1220 Analog",
    "nothing",
    ""};

// every reader once for a device, its results in one line
std::string ReadAll(CActiveAESink& s, const std::string& d, bool printing)
{
  std::ostringstream o;
  for (const bool pt : {false, true})
  {
    bool missing = false, family = false;
    o << " validate(" << pt << ")=" << s.ValidateOuputDevice(d, pt);
    o << " follow(" << pt << ")=" << s.FollowDisplay(d, "ALSA:hdmi:CARD=NVidia,DEV=0", pt, &missing, &family)
      << "/" << missing << family;
    o << " fallback(" << pt << ")=" << s.FallbackOutput(pt);
    KODI::UTILS::DisplayIdentity tv;
    tv.maker = 0x1E6D;
    tv.product = 0xC0DE;
    tv.model = "LG TV SSCR2";
    INTERNAL::DisplayOutputFound found;
    o << " display(" << pt << ")=" << s.OutputOfTheDisplay(pt, tv, found) << "/" << static_cast<int>(found);
  }
  o << " missing=" << s.IsMissingBesideItsFamily(d);
  o << " type=" << s.GetDeviceType(d);
  AE::PCM::DisplayAudio audio;
  bool there = false;
  const bool listed = s.GetDisplayAudio(d, audio, there);
  o << " audio=" << listed << "/" << audio.pcm << "," << audio.ac3 << "," << audio.eac3 << "/" << there;
  o << " passthrough=" << s.HasPassthroughDevice();
  AEAudioFormat format;
  format.m_dataFormat = AE_FMT_RAW;
  format.m_streamInfo.m_type = CAEStreamInfo::STREAM_TYPE_TRUEHD;
  format.m_streamInfo.m_sampleRate = 48000;
  o << " truehd=" << s.SupportsFormat(d, format);
  format.m_dataFormat = AE_FMT_FLOAT;
  format.m_sampleRate = 96000;
  o << " float96=" << s.SupportsFormat(d, format);
  const AE::AESinkDevice dev = CAESinkFactory::ParseDevice(d);
  o << " exist=" << s.DeviceExist(dev.driver.empty() ? "ALSA" : dev.driver, dev.name);
  if (printing)
  {
    s.m_device = d;
    o << " iec=" << s.NeedIECPacking();
    s.GetDeviceFriendlyName(dev.name);
    o << " friendly=" << s.m_deviceFriendlyName;
  }
  return o.str();
}

std::string Listed(CActiveAESink& s)
{
  std::ostringstream o;
  for (const bool pt : {false, true})
  {
    AEDeviceList devices;
    s.EnumerateOutputDevices(devices, pt);
    o << " devices(" << pt << "):";
    for (const auto& d : devices)
      o << " [" << d.first << " = " << d.second << "]";
  }
  return o.str();
}
} // namespace

int main(int argc, char** argv)
{
  const std::string what = argc > 1 ? argv[1] : "";
  CActiveAESink sink;
  sink.m_sink = std::make_unique<SinkStub>();
  if (what == "same")
  {
    std::printf("empty:%s\n", Listed(sink).c_str()); // (EnumerateOutputDevices() enumerates a list that is empty)
    for (int v = 0; v < 2; ++v)
    {
      g_variant = v;
      sink.EnumerateSinkList(true, "");
      std::printf("list %d:%s\n", v, Listed(sink).c_str());
      for (const auto& d : g_devices)
        std::printf("list %d, %s:%s\n", v, d.c_str(), ReadAll(sink, d, true).c_str());
      // the outputs of one driver enumerated again, the others kept
      g_variant = 1 - v;
      sink.EnumerateSinkList(true, "ALSA");
      std::printf("list %d, ALSA again:%s\n", v, Listed(sink).c_str());
      sink.EnumerateSinkList(false, "");
      std::printf("list %d, not forced:%s enumerations %d\n", v, Listed(sink).c_str(), g_enumerations);
    }
    std::printf("SAME DONE\n");
    return 0;
  }
  if (what == "race")
  {
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 200;
    sink.EnumerateSinkList(true, "");
    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};
    std::thread writer(
        [&]
        {
          for (int i = 0; !stop; ++i)
          {
            g_variant = i % 2;
            sink.EnumerateSinkList(true, i % 3 == 2 ? "ALSA" : "");
          }
        });
    auto reader = [&](int which)
    {
      for (int r = 0; r < rounds; ++r)
      {
        for (const auto& d : g_devices)
        {
          // (the thread of the sink alone reads with NeedIECPacking() and GetDeviceFriendlyName(), which write
          // members of its own)
          const std::string line = ReadAll(sink, d, which == 2);
          if (line.empty())
            std::abort();
          ++reads;
        }
        if (which == 0)
          Listed(sink);
      }
    };
    std::thread a(reader, 0), b(reader, 1), c(reader, 2);
    a.join();
    b.join();
    c.join();
    stop = true;
    writer.join();
    std::printf("race: %d rounds on 3 readers, %ld reads, %d enumerations\n", rounds, reads.load(),
                g_enumerations);
    return 0;
  }
  std::printf("usage: aesink_test same | race <rounds>\n");
  return 2;
}
