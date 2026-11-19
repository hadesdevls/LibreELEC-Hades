/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * LibreELEC Hades: host test of a HEVC stream whose VUI has frame_field_info_present_flag, taken as progressive
 * where the picture timing of its first access units says so (utils/ProgressiveVideo.cpp: Check()'s
 * byPictureTiming, HevcPictureTiming(), HevcLengthSize(), CPictureTimingProof):
 *  - the parameter sets: frame_field_info_present_flag alone leaves it to the picture timing; field_seq_flag, or a
 *    second set with it, does not; a set without either is progressive as before;
 *  - the SEI: pic_struct and source_scan_type read from a prefix SEI of the base layer, after other messages,
 *    through emulation prevention bytes, from length fields of 1 to 4 bytes and from Annex B; another layer, a
 *    suffix SEI, a cut message are not read;
 *  - the proof: 24 access units with whole progressive pictures (pic_struct 0, 7, 8; source_scan_type not 0);
 *    one field (1, 2, 9 to 12), a frame of two fields (3 to 6) or an interlaced source denies it at once; access
 *    units without timing count for nothing;
 *  - when it is read (PictureTimingNotNeeded()): only where its verdict can change a choice of a mode ("Video at
 *    its own rate with VRR" not off; whole-number multiples with a whitelist of the user; without one, a frame
 *    rate of 30 Hz or less but 24 Hz, as twice it or 2.5 times it can be only then), never with "Adjust display refresh
 *    rate" off: a film at 23.976 with the VRR of videos off takes no time; read, as before, where the settings or
 *    the frame rate are not known;
 *  - how far it is read (PictureTimingReadStops()): 4800 packets of all the streams, 48 MiB, 1.5 s; the 1200 packets
 *    of before stopped a film with TrueHD at 19 access units of its video.
 * The parameter sets and access units are made here bit by bit. Built with the headers of FFmpeg of the build
 * folder (codec_id.h, pixdesc.h) and three stubs of libavutil's names of colours.
 */
#include "utils/ProgressiveVideo.h"

extern "C"
{
#include <libavutil/pixdesc.h>
}

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// (the names of colours that the parse of the VUI asks libavutil for: every value has one here)
extern "C" const char* av_color_primaries_name(enum AVColorPrimaries)
{
  return "p";
}
extern "C" const char* av_color_transfer_name(enum AVColorTransferCharacteristic)
{
  return "t";
}
extern "C" const char* av_color_space_name(enum AVColorSpace)
{
  return "s";
}

using namespace PROGRESSIVE_VIDEO;
static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const std::string& what)
{
  ++g_checks;
  if (!ok)
  {
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

class CBits
{
public:
  void U(uint32_t value, int bits)
  {
    for (int i = bits - 1; i >= 0; --i)
      Bit((value >> i) & 1);
  }
  void UE(uint32_t value)
  {
    const uint32_t v = value + 1;
    int len = 0;
    while ((v >> len) > 1)
      ++len;
    U(0, len);
    U(v, len + 1);
  }
  // rbsp_trailing_bits
  std::vector<uint8_t> Finish()
  {
    Bit(1);
    while (m_n % 8)
      Bit(0);
    return m_bytes;
  }

private:
  void Bit(uint32_t b)
  {
    if (m_n % 8 == 0)
      m_bytes.push_back(0);
    if (b)
      m_bytes.back() |= static_cast<uint8_t>(0x80 >> (m_n % 8));
    ++m_n;
  }
  std::vector<uint8_t> m_bytes;
  size_t m_n = 0;
};

// emulation prevention: 00 00 then 00 to 03 gets an 03 between
static std::vector<uint8_t> Ebsp(const std::vector<uint8_t>& rbsp)
{
  std::vector<uint8_t> out;
  int zeros = 0;
  for (const uint8_t b : rbsp)
  {
    if (zeros >= 2 && b <= 3)
    {
      out.push_back(3);
      zeros = 0;
    }
    out.push_back(b);
    zeros = b == 0 ? zeros + 1 : 0;
  }
  return out;
}

// a HEVC SPS of 3840x2160, 10 bits, with a VUI that has field_seq_flag and frame_field_info_present_flag as asked
static std::vector<uint8_t> Sps(bool fieldSeq, bool frameFieldInfo)
{
  CBits b;
  b.U(0, 4); // sps_video_parameter_set_id
  b.U(0, 3); // sps_max_sub_layers_minus1
  b.U(1, 1); // sps_temporal_id_nesting_flag
  b.U(0x22000000, 32); // profile_tier_level: general profile space .. flags (Main 10)
  b.U(0, 32);
  b.U(0, 24);
  b.U(153, 8); // general_level_idc
  b.UE(0); // sps_seq_parameter_set_id
  b.UE(1); // chroma_format_idc
  b.UE(3840);
  b.UE(2160);
  b.U(0, 1); // conformance_window_flag
  b.UE(2); // bit_depth_luma_minus8
  b.UE(2); // bit_depth_chroma_minus8
  b.UE(4); // log2_max_pic_order_cnt_lsb_minus4
  b.U(1, 1); // sps_sub_layer_ordering_info_present_flag
  b.UE(4);
  b.UE(2);
  b.UE(0);
  b.UE(0); // log2_min_luma_coding_block_size_minus3
  b.UE(3);
  b.UE(0);
  b.UE(3);
  b.UE(0);
  b.UE(0);
  b.U(0, 1); // scaling_list_enabled_flag
  b.U(0, 1); // amp_enabled_flag
  b.U(1, 1); // sample_adaptive_offset_enabled_flag
  b.U(0, 1); // pcm_enabled_flag
  b.UE(0); // num_short_term_ref_pic_sets
  b.U(0, 1); // long_term_ref_pics_present_flag
  b.U(1, 1); // sps_temporal_mvp_enabled_flag
  b.U(1, 1); // strong_intra_smoothing_enabled_flag
  b.U(1, 1); // vui_parameters_present_flag
  b.U(0, 1); // aspect_ratio_info_present_flag
  b.U(0, 1); // overscan_info_present_flag
  b.U(1, 1); // video_signal_type_present_flag
  b.U(5, 3); // video_format
  b.U(0, 1); // video_full_range_flag
  b.U(1, 1); // colour_description_present_flag
  b.U(9, 8); // BT.2020
  b.U(16, 8); // PQ
  b.U(9, 8);
  b.U(0, 1); // chroma_loc_info_present_flag
  b.U(0, 1); // neutral_chroma_indication_flag
  b.U(fieldSeq ? 1 : 0, 1);
  b.U(frameFieldInfo ? 1 : 0, 1);
  b.U(0, 1); // default_display_window_flag
  b.U(0, 1); // vui_timing_info_present_flag
  b.U(0, 1); // bitstream_restriction_flag
  b.U(0, 1); // sps_extension_present_flag
  std::vector<uint8_t> nal = {0x42, 0x01};
  const std::vector<uint8_t> e = Ebsp(b.Finish());
  nal.insert(nal.end(), e.begin(), e.end());
  return nal;
}

static std::vector<uint8_t> AnnexB(const std::vector<std::vector<uint8_t>>& nals)
{
  std::vector<uint8_t> out;
  for (const auto& n : nals)
  {
    out.insert(out.end(), {0, 0, 0, 1});
    out.insert(out.end(), n.begin(), n.end());
  }
  return out;
}

static std::vector<uint8_t> Lengths(const std::vector<std::vector<uint8_t>>& nals, int size)
{
  std::vector<uint8_t> out;
  for (const auto& n : nals)
  {
    for (int i = size - 1; i >= 0; --i)
      out.push_back(static_cast<uint8_t>(n.size() >> (8 * i)));
    out.insert(out.end(), n.begin(), n.end());
  }
  return out;
}

// a SEI message: its type and size as ff_byte values, its payload
static void Message(std::vector<uint8_t>& rbsp, unsigned type, const std::vector<uint8_t>& payload)
{
  for (; type >= 255; type -= 255)
    rbsp.push_back(0xFF);
  rbsp.push_back(static_cast<uint8_t>(type));
  size_t size = payload.size();
  for (; size >= 255; size -= 255)
    rbsp.push_back(0xFF);
  rbsp.push_back(static_cast<uint8_t>(size));
  rbsp.insert(rbsp.end(), payload.begin(), payload.end());
}

// pic_timing with frame_field_info_present_flag: pic_struct u(4), source_scan_type u(2), duplicate_flag u(1), and
// the bits of the delays after them (au_cpb_removal_delay_minus1, pic_dpb_output_delay, as an HRD gives them)
static std::vector<uint8_t> PicTiming(unsigned picStruct, unsigned scan)
{
  return {static_cast<uint8_t>(picStruct << 4 | scan << 2), 0x00, 0x00, 0x01};
}

// a prefix SEI NAL unit (type 39) of layer 0, or of another layer, or a suffix one (40)
static std::vector<uint8_t> Sei(const std::vector<std::vector<uint8_t>>& messages, unsigned layer = 0,
                                unsigned type = 39)
{
  std::vector<uint8_t> rbsp;
  // a user data unregistered message first, with 00 00 00 in it (emulation prevention)
  Message(rbsp, 5, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0, 0, 0, 0, 1});
  for (const auto& m : messages)
    rbsp.insert(rbsp.end(), m.begin(), m.end());
  rbsp.push_back(0x80);
  std::vector<uint8_t> nal = {static_cast<uint8_t>(type << 1 | layer >> 5), static_cast<uint8_t>((layer & 31) << 3 | 1)};
  const std::vector<uint8_t> e = Ebsp(rbsp);
  nal.insert(nal.end(), e.begin(), e.end());
  return nal;
}

static std::vector<uint8_t> TimingMessage(unsigned picStruct, unsigned scan)
{
  std::vector<uint8_t> m;
  Message(m, 1, PicTiming(picStruct, scan));
  return m;
}

// an access unit: an access unit delimiter, the SEI, a slice
static std::vector<std::vector<uint8_t>> Unit(const std::vector<uint8_t>& sei)
{
  std::vector<std::vector<uint8_t>> nals = {{0x46, 0x01, 0x50}};
  if (!sei.empty())
    nals.push_back(sei);
  nals.push_back({0x02, 0x01, 0xD0, 0x12, 0x34});
  return nals;
}

int main()
{
  // ---- the parameter sets
  {
    const std::vector<uint8_t> flag = AnnexB({Sps(false, true)});
    const Result r = Check(AV_CODEC_ID_HEVC, flag.data(), flag.size());
    Check(!r.progressive && r.byPictureTiming, "frame_field_info_present_flag: left to the picture timing (" +
                                                    std::string(r.reason) + ")");
    const std::vector<uint8_t> plain = AnnexB({Sps(false, false)});
    const Result p = Check(AV_CODEC_ID_HEVC, plain.data(), plain.size());
    Check(p.progressive && !p.byPictureTiming, "neither flag: progressive, as before (" + std::string(p.reason) + ")");
    const std::vector<uint8_t> fields = AnnexB({Sps(true, true)});
    const Result f = Check(AV_CODEC_ID_HEVC, fields.data(), fields.size());
    Check(!f.progressive && !f.byPictureTiming, "field_seq_flag: fields, not left to the timing");
    const std::vector<uint8_t> two = AnnexB({Sps(false, true), Sps(true, false)});
    const Result t = Check(AV_CODEC_ID_HEVC, two.data(), two.size());
    Check(!t.progressive && !t.byPictureTiming, "a second set with field_seq_flag: not left to the timing");
    const std::vector<uint8_t> mixed = AnnexB({Sps(false, false), Sps(false, true)});
    const Result m = Check(AV_CODEC_ID_HEVC, mixed.data(), mixed.size());
    Check(!m.progressive && m.byPictureTiming, "a plain set and one with the flag: left to the timing");
    Check(!Check(AV_CODEC_ID_H264, flag.data(), flag.size()).byPictureTiming, "H.264: never by this");
  }
  // ---- the length fields
  {
    std::vector<uint8_t> hvcc(23, 0);
    hvcc[0] = 1;
    hvcc[21] = 0xFF;
    Check(HevcLengthSize(hvcc.data(), hvcc.size()) == 4, "hvcC lengthSizeMinusOne 3: 4");
    hvcc[21] = 0xFC | 1;
    Check(HevcLengthSize(hvcc.data(), hvcc.size()) == 2, "hvcC lengthSizeMinusOne 1: 2");
    const std::vector<uint8_t> annexB = AnnexB({Sps(false, true)});
    Check(HevcLengthSize(annexB.data(), annexB.size()) == 0, "Annex B extradata: 0");
    Check(HevcLengthSize(nullptr, 0) == 0, "no extradata: 0");
  }
  // ---- the SEI of an access unit
  {
    for (int size = 1; size <= 4; ++size)
    {
      const std::vector<uint8_t> au = Lengths(Unit(Sei({TimingMessage(0, 1)})), size);
      const PictureTiming t = HevcPictureTiming(au.data(), au.size(), size);
      Check(t.found && t.wholeFrames && t.picStruct == 0 && t.sourceScanType == 1,
            "a frame, progressive source, length fields of " + std::to_string(size) + " bytes");
    }
    const std::vector<uint8_t> annexB = AnnexB(Unit(Sei({TimingMessage(0, 2)})));
    const PictureTiming a = HevcPictureTiming(annexB.data(), annexB.size(), 0);
    Check(a.found && a.wholeFrames && a.sourceScanType == 2, "Annex B, a frame of a source not told");
    for (const unsigned ps : {1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u, 11u, 12u})
    {
      const std::vector<uint8_t> au = Lengths(Unit(Sei({TimingMessage(ps, 1)})), 4);
      const PictureTiming t = HevcPictureTiming(au.data(), au.size(), 4);
      Check(t.found && !t.wholeFrames && t.picStruct == ps, "pic_struct " + std::to_string(ps) + ": not a whole picture");
    }
    for (const unsigned ps : {7u, 8u})
    {
      const std::vector<uint8_t> au = Lengths(Unit(Sei({TimingMessage(ps, 1)})), 4);
      Check(HevcPictureTiming(au.data(), au.size(), 4).wholeFrames,
            "pic_struct " + std::to_string(ps) + ": a frame shown again, whole");
    }
    const std::vector<uint8_t> interlacedSource = Lengths(Unit(Sei({TimingMessage(0, 0)})), 4);
    Check(!HevcPictureTiming(interlacedSource.data(), interlacedSource.size(), 4).wholeFrames,
          "a frame of an interlaced source (source_scan_type 0): not taken");
    // two pic_timing messages, the second with a field: not whole
    const std::vector<uint8_t> twoMsgs = Lengths(Unit(Sei({TimingMessage(0, 1), TimingMessage(1, 1)})), 4);
    const PictureTiming tw = HevcPictureTiming(twoMsgs.data(), twoMsgs.size(), 4);
    Check(tw.found && !tw.wholeFrames && tw.picStruct == 1, "the second of two messages is a field: not whole");
    const std::vector<uint8_t> none = Lengths(Unit({}), 4);
    Check(!HevcPictureTiming(none.data(), none.size(), 4).found, "no SEI: nothing found");
    const std::vector<uint8_t> layer1 = Lengths(Unit(Sei({TimingMessage(1, 1)}, 1)), 4);
    Check(!HevcPictureTiming(layer1.data(), layer1.size(), 4).found, "a SEI of another layer: not read");
    const std::vector<uint8_t> suffix = Lengths(Unit(Sei({TimingMessage(1, 1)}, 0, 40)), 4);
    Check(!HevcPictureTiming(suffix.data(), suffix.size(), 4).found, "a suffix SEI: not read");
    // a message cut short: its size is past the NAL unit
    std::vector<uint8_t> cut = Sei({TimingMessage(0, 1)});
    cut.resize(cut.size() - 4);
    const std::vector<uint8_t> cutAu = Lengths(Unit(cut), 4);
    Check(!HevcPictureTiming(cutAu.data(), cutAu.size(), 4).found, "a pic_timing cut short: not read");
    // a length field that runs past the access unit: what comes before it is read, nothing after
    std::vector<uint8_t> broken = Lengths(Unit(Sei({TimingMessage(0, 1)})), 4);
    broken.insert(broken.end(), {0x7F, 0xFF, 0xFF, 0xFF, 0x4E});
    Check(HevcPictureTiming(broken.data(), broken.size(), 4).found, "a length past the end: the units before it read");
    Check(!HevcPictureTiming(nullptr, 0, 4).found && !HevcPictureTiming(broken.data(), broken.size(), 5).found,
          "no data, or a length field of 5 bytes: nothing");
  }
  // ---- the proof
  {
    const PictureTiming frame{true, true, 0, 1};
    const PictureTiming field{true, false, 1, 1};
    const PictureTiming untimed{};
    CPictureTimingProof p;
    for (unsigned i = 0; i + 1 < CPictureTimingProof::MIN_TIMED; ++i)
      p.Add(frame);
    Check(p.Get() == CPictureTimingProof::Verdict::UNKNOWN && !p.Done(), "23 timed whole pictures: not yet");
    p.Add(untimed);
    Check(p.Get() == CPictureTimingProof::Verdict::UNKNOWN, "an access unit without timing counts for nothing");
    p.Add(frame);
    Check(p.Get() == CPictureTimingProof::Verdict::PROGRESSIVE && p.Done() && p.Timed() == 24 && p.Units() == 25,
          "24 timed whole pictures: progressive");
    CPictureTimingProof q;
    for (int i = 0; i < 9; ++i)
      q.Add(frame);
    q.Add(field);
    Check(q.Get() == CPictureTimingProof::Verdict::NOT_PROGRESSIVE && q.Done() && q.DeniedAt() == 10 &&
              q.PicStruct() == 1,
          "a field in the tenth access unit: not progressive, at once");
    for (int i = 0; i < 40; ++i)
      q.Add(frame);
    Check(q.Get() == CPictureTimingProof::Verdict::NOT_PROGRESSIVE, "... and whole pictures after it change nothing");
    CPictureTimingProof r;
    for (int i = 0; i < 100; ++i)
      r.Add(untimed);
    Check(r.Get() == CPictureTimingProof::Verdict::UNKNOWN, "no timing at all: not known, as before");
    // the whole chain: access units made here, read and judged
    CPictureTimingProof s;
    for (unsigned i = 0; i < CPictureTimingProof::MIN_TIMED; ++i)
    {
      const std::vector<uint8_t> au = Lengths(Unit(Sei({TimingMessage(i % 3 ? 0 : 7, 1)})), 4);
      s.Add(HevcPictureTiming(au.data(), au.size(), 4));
    }
    Check(s.Get() == CPictureTimingProof::Verdict::PROGRESSIVE, "24 access units of frames (and doublings): progressive");
    CPictureTimingProof u;
    for (unsigned i = 0; i < CPictureTimingProof::MIN_TIMED; ++i)
    {
      const std::vector<uint8_t> au = Lengths(Unit(Sei({TimingMessage(i % 2 ? 2 : 1, 1)})), 4);
      u.Add(HevcPictureTiming(au.data(), au.size(), 4));
    }
    Check(u.Get() == CPictureTimingProof::Verdict::NOT_PROGRESSIVE && u.DeniedAt() == 1,
          "access units of fields: not progressive at the first");
  }
  // ---- when it is read (PictureTimingNotNeeded())
  {
    const auto read = [](bool adjust, bool vrr, bool whitelist, bool multiple, double fps)
    {
      PictureTimingNeed n;
      n.settingsRead = true;
      n.adjustRefreshRate = adjust;
      n.vrrVideo = vrr;
      n.userWhitelist = whitelist;
      n.wholeMultiple = multiple;
      n.fps = fps;
      return PictureTimingNotNeeded(n) == nullptr;
    };
    const double film = 24000.0 / 1001;
    Check(!read(true, false, false, false, film), "a film at 23.976, the VRR of videos off, no whitelist: not read");
    Check(!read(true, false, false, false, 24.0), "24 fps, the VRR of videos off: not read");
    Check(!read(true, false, false, false, 50.0), "50 fps: not read (a mode above 30 Hz is taken for any video)");
    Check(!read(true, false, false, false, 60000.0 / 1001), "59.94 fps: not read");
    Check(!read(true, false, false, false, 48000.0 / 1001), "47.952 fps: not read");
    Check(read(true, false, false, false, 25.0), "25 fps without a whitelist: read (25 Hz only for a progressive video)");
    Check(read(true, false, false, false, 30000.0 / 1001), "29.97 fps: read");
    Check(read(true, false, false, false, 30.0), "30 fps: read");
    Check(read(true, false, false, false, 12.5), "12.5 fps: read (12.5 Hz, and twice it, 25 Hz, only for a progressive video)");
    Check(read(true, false, false, false, 12.0), "12 fps: read (12 Hz, and 2.5 times it, 30 Hz, only for a progressive video)");
    Check(read(true, false, false, false, 0.0), "no frame rate: read, as before");
    Check(read(true, true, false, false, film), "a film at 23.976 with the VRR of videos not off: read");
    Check(read(true, true, true, false, 50.0), "the VRR of videos not off, with a whitelist: read");
    Check(!read(false, true, false, false, 25.0), "\"Adjust display refresh rate\" off: not read, even 25 fps with VRR");
    Check(!read(true, false, true, false, 25.0), "a whitelist of the user, multiples off, VRR off: not read, even 25 fps");
    Check(read(true, false, true, true, film), "a whitelist of the user with whole-number multiples: read, even 23.976");
    PictureTimingNeed unknown;
    unknown.vrrVideo = false;
    unknown.fps = film;
    Check(PictureTimingNotNeeded(unknown) == nullptr, "the settings not read: read, as before");
    Check(HalfRefreshRate(25.0) && HalfRefreshRate(30.0) && HalfRefreshRate(30000.0 / 1001) &&
              !HalfRefreshRate(film) && !HalfRefreshRate(24.0) && !HalfRefreshRate(48000.0 / 1001) &&
              !HalfRefreshRate(30.5) && !HalfRefreshRate(0.0),
          "the refresh rates that Kodi's default whitelist takes only for a progressive video");
  }
  // ---- how far it is read (PictureTimingReadStops()): the Dolby Vision profile 7 film of the test of 2026-11-19
  // (TrueHD and two subtitle tracks) stopped at 1200 packets, 497 KiB and 0 ms with 19 of the 24 access units of its
  // video timed, and was not known to be progressive: those packets go on now
  {
    Check(PictureTimingReadStops(1200, 497 * 1024, 0) == nullptr,
          "1200 packets, 497 KiB, 0 ms (the profile 7 film with TrueHD): the read goes on");
    Check(PictureTimingReadStops(1520, 2 * 1024 * 1024, 300) == nullptr,
          "about 1520 packets for 24 access units of such a film: still read");
    Check(PictureTimingReadStops(4799, 0, 0) == nullptr, "4799 packets: read");
    Check(std::string(PictureTimingReadStops(4800, 0, 0) ? PictureTimingReadStops(4800, 0, 0) : "") ==
              "the packets held",
          "4800 packets held: stops, and says why");
    Check(std::string(PictureTimingReadStops(10, 48 * 1024 * 1024, 0) ? PictureTimingReadStops(10, 48 * 1024 * 1024, 0)
                                                                      : "") == "the bytes held",
          "48 MiB held: stops (the memory held, as before)");
    Check(std::string(PictureTimingReadStops(10, 0, 1500) ? PictureTimingReadStops(10, 0, 1500) : "") ==
              "the time to read",
          "1.5 s of reading: stops (the delay at the start, as before)");
    Check(PictureTimingReadStops(10, 48 * 1024 * 1024 - 1, 1499) == nullptr, "just under both: read");
  }
  std::printf("%d checks, %d failures\n%s\n", g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
  return g_failures ? 1 : 0;
}
