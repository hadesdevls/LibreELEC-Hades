// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// libass_par_test.cpp (1.2.3): the subtitles of each eye of a 3D output, rendered by the libass of the build (its
// library from the sysroot, run with the sysroot's loader), with the pixel aspect ratio that the tree under test gives
// it (SubtitlesStyle.h, LibassPixelAspect()) for Kodi's pixel ratio of the eye (GraphicContext.cpp: 2 side by side,
// 0.5 top and bottom). The frame of each eye is what CRenderer::GetRenderOptions() gives libass: the view of the eye,
// 960x1080 side by side and 1920x540 top and bottom in a 1080p output, 1920x2160 and 3840x1080 in a 2160p one; no
// storage size (a text subtitle, as Kodi renders SRT and the like), no margins.
//
// What each eye must get: the subtitle of the 2D picture, squeezed by exactly the half that the layout halves. So, as
// the display shows it (each pixel of the eye 'ratio' times as wide as tall): a glyph of the aspect it has in 2D, a
// line as wide as in 2D, as many lines as in 2D, and the text as tall as in 2D, against the 2D frame of the same
// output (1920x1080 or 3840x2160).
// Prints a FAIL line for each failed check, then "<n> checks" and "<n> failed".
#include "cores/VideoPlayer/DVDSubtitles/SubtitlesStyle.h"

extern "C" {
#include <ass/ass.h>
}

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what, const std::string& detail = {})
{
  ++checks;
  if (!ok)
  {
    ++failures;
    std::printf("FAIL: %s%s%s\n", what.c_str(), detail.empty() ? "" : ": ", detail.c_str());
  }
}

struct Box
{
  int x1{1 << 30}, y1{1 << 30}, x2{-1}, y2{-1};
  int lines{0};
  bool Empty() const { return x2 < 0; }
};

ASS_Library* library = nullptr;
ASS_Renderer* renderer = nullptr;

// What libass draws of one event, as Kodi asks for it (DVDSubtitlesLibass.cpp, RenderImage())
Box Render(const char* text, int frameWidth, int frameHeight, double par)
{
  std::string script = "[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nWrapStyle: 1\n"
                       "ScaledBorderAndShadow: yes\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, "
                       "PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, "
                       "StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                       "MarginL, MarginR, MarginV, Encoding\nStyle: Default,Noto Sans,48,&H00FFFFFF,&H00FFFFFF,"
                       "&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,40,40,40,1\n\n[Events]\nFormat: Layer, "
                       "Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:00.00,"
                       "0:00:10.00,Default,,0,0,0,,";
  script += text;
  script += "\n";
  ASS_Track* track = ass_read_memory(library, script.data(), script.size(), nullptr);
  Box box;
  if (!track)
    return box;
  ass_set_pixel_aspect(renderer, par);
  ass_set_frame_size(renderer, frameWidth, frameHeight);
  ass_set_storage_size(renderer, 0, 0);
  ass_set_margins(renderer, 0, 0, 0, 0);
  ass_set_use_margins(renderer, 0);
  ass_set_font_scale(renderer, 1.0);
  int changed = 0;
  // (the ink: the pixels of the images that are more than a quarter covered)
  for (ASS_Image* img = ass_render_frame(renderer, track, 1000, &changed); img; img = img->next)
  {
    for (int y = 0; y < img->h; ++y)
    {
      const unsigned char* row = img->bitmap + static_cast<size_t>(y) * img->stride;
      for (int x = 0; x < img->w; ++x)
      {
        if (row[x] <= 64)
          continue;
        box.x1 = std::min(box.x1, img->dst_x + x);
        box.y1 = std::min(box.y1, img->dst_y + y);
        box.x2 = std::max(box.x2, img->dst_x + x + 1);
        box.y2 = std::max(box.y2, img->dst_y + y + 1);
      }
    }
  }
  // (the lines: the rows of the frame that hold ink, in runs)
  if (!box.Empty())
  {
    std::string rows(static_cast<size_t>(frameHeight), '\0');
    for (ASS_Image* img = ass_render_frame(renderer, track, 1000, &changed); img; img = img->next)
    {
      for (int y = 0; y < img->h; ++y)
      {
        const unsigned char* row = img->bitmap + static_cast<size_t>(y) * img->stride;
        for (int x = 0; x < img->w; ++x)
          if (row[x] > 64)
          {
            rows[static_cast<size_t>(img->dst_y + y)] = 1;
            break;
          }
      }
    }
    // a gap of a tenth of the text's height or more between two runs of ink is the space between lines
    const int gap = std::max(2, (box.y2 - box.y1) / 40);
    int blank = gap;
    for (int y = 0; y < frameHeight; ++y)
    {
      if (rows[static_cast<size_t>(y)])
      {
        if (blank >= gap)
          ++box.lines;
        blank = 0;
      }
      else
        ++blank;
    }
  }
  ass_free_track(track);
  return box;
}

struct Shown
{
  double glyphAspect{0}; // width over height of "HHHHHHHH" as the display shows it (eight glyphs: a pixel of
                         // rounding is small against their width)
  double lineWidth{0}; // the width of a line of text as shown, over the width of the shown frame
  double height{0}; // the height of "HHHHHHHH" as shown, over the height of the shown frame
  int lines{0}; // the lines of a long subtitle
};

// one eye of eyeWidth x eyeHeight whose pixels are shown 'ratio' times as wide as tall
Shown Eye(int eyeWidth, int eyeHeight, float ratio)
{
  const double par = KODI::SUBTITLES::STYLE::LibassPixelAspect(ratio);
  const double r = static_cast<double>(ratio);
  const Box h = Render("HHHHHHHH", eyeWidth, eyeHeight, par);
  const Box line = Render("one two three four", eyeWidth, eyeHeight, par);
  const Box many = Render("This is a long subtitle that a film puts on the screen when somebody talks for a long "
                          "time, and it wraps over more than one line",
                          eyeWidth, eyeHeight, par);
  Shown s;
  if (h.Empty() || line.Empty() || many.Empty())
    return s;
  const double shownWidth = eyeWidth * r; // the frame as shown, in pixels of its height
  s.glyphAspect = (h.x2 - h.x1) * r / (h.y2 - h.y1);
  s.lineWidth = (line.x2 - line.x1) * r / shownWidth;
  s.height = static_cast<double>(h.y2 - h.y1) / eyeHeight;
  s.lines = many.lines;
  return s;
}

void Compare(const char* name, const Shown& eye, const Shown& flat)
{
  const auto near = [](double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance * b; };
  char detail[256];
  std::snprintf(detail, sizeof(detail), "eye %.3f / %.3f / %.4f / %d lines; 2D %.3f / %.3f / %.4f / %d lines",
                eye.glyphAspect, eye.lineWidth, eye.height, eye.lines, flat.glyphAspect, flat.lineWidth, flat.height,
                flat.lines);
  check(eye.glyphAspect > 0 && near(eye.glyphAspect, flat.glyphAspect, 0.06),
        std::string(name) + ": a glyph is shown in its 2D shape", detail);
  check(eye.lineWidth > 0 && near(eye.lineWidth, flat.lineWidth, 0.04),
        std::string(name) + ": a line is shown as wide as in 2D", detail);
  check(eye.height > 0 && near(eye.height, flat.height, 0.06),
        std::string(name) + ": the text is shown as tall as in 2D", detail);
  check(eye.lines > 0 && eye.lines == flat.lines, std::string(name) + ": as many lines as in 2D", detail);
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::printf("usage: libass_par_test <font file>\n");
    return 2;
  }
  library = ass_library_init();
  if (library)
    ass_set_message_cb(library, [](int, const char*, va_list, void*) {}, nullptr);
  renderer = library ? ass_renderer_init(library) : nullptr;
  if (!renderer)
  {
    std::printf("FAIL: libass did not start\n1 checks\n1 failed\n");
    return 1;
  }
  ass_set_fonts(renderer, argv[1], "Noto Sans", ASS_FONTPROVIDER_NONE, nullptr, 0);

  const Shown flat1080 = Eye(1920, 1080, 1.0f);
  const Shown flat2160 = Eye(3840, 2160, 1.0f);
  check(flat1080.lines >= 2, "2D: the long subtitle wraps", std::to_string(flat1080.lines));
  check(flat1080.glyphAspect > 4.0 && flat1080.glyphAspect < 12.0, "2D: the eight glyphs measured",
        std::to_string(flat1080.glyphAspect));
  Compare("side by side, 1080p (an eye of 960x1080)", Eye(960, 1080, 2.0f), flat1080);
  Compare("top and bottom, 1080p (an eye of 1920x540)", Eye(1920, 540, 0.5f), flat1080);
  Compare("side by side, 2160p (an eye of 1920x2160)", Eye(1920, 2160, 2.0f), flat2160);
  Compare("top and bottom, 2160p (an eye of 3840x1080)", Eye(3840, 1080, 0.5f), flat2160);
  // a resolution whose pixels are not square in 2D (4096x2160 on a 16:9 display)
  Compare("4096x2160 on a 16:9 display (pixel ratio 0.9375)", Eye(4096, 2160, 16.0f / 9.0f / (4096.0f / 2160.0f)),
          flat2160);

  ass_renderer_done(renderer);
  ass_library_done(library);
  std::printf("%d checks\n%d failed\n", checks, failures);
  return failures ? 1 : 0;
}
