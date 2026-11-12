// LibreELEC Hades installer for Windows: the look (colours, scale, the mark) and the few controls drawn here.
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026-present Hades
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.Win32;

namespace Hades.Installer
{
    // Everything that is a colour, a size or a font comes from here, so that the window looks the same in
    // light and in dark and at any scale of the display. Sizes are written in pixels of a display at 100 %
    // and go through S(); fonts are in pixels too (never points: a point is already scaled by Windows
    // where the process is aware of the DPI, and a font scaled twice is the usual fault of a window that
    // "supports" high DPI).
    static class Ui
    {
        public static float Scale = 1f;     // the display's DPI / 96 (the tests set it to draw 150 % and 200 %)
        public static bool FixedScale;      // the tests: the scale is theirs, not the display's
        public static bool Dark;            // Windows' "app mode" (the tests set it)

        public static int S(int px) { return (int)Math.Round(px * Scale); }
        public static float Sf(float px) { return px * Scale; }

        // the colours of the mode that is on (Apply)
        public static Color Back, Surface, Text, Soft, Faint, Hair, Ink, InkText, InkSoft, Primary, PrimaryText, PrimaryHover,
                            Warm, WarmText, Ok, Warn, Danger, CalloutBack, CalloutBar, Field, Disabled, DisabledText,
                            Pick, PickText, Green, Disc, TextOff;
        public static readonly Color TextOffOnInk = Color.FromArgb(128, 134, 150);

        // The brand, from the script that draws its pictures (packages/mediacenter/kodi/brand/src/make-brand-images.py).
        public static readonly Color Silver = Color.FromArgb(219, 230, 255);
        public static readonly Color FirstLight = Color.FromArgb(255, 222, 179);

        // Windows' choice for the apps (Settings, Personalization, Colors): the value is 0 for dark. Where it is
        // not there, light.
        public static bool WindowsIsDark()
        {
            try
            {
                using (RegistryKey k = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
                {
                    object v = k == null ? null : k.GetValue("AppsUseLightTheme");
                    return v is int && (int)v == 0;
                }
            }
            catch (Exception) { return false; }
        }

        public static void Apply()
        {
            Disc = Color.FromArgb(5, 5, 8);
            Ink = Color.FromArgb(10, 11, 16);
            InkText = Color.White;
            InkSoft = Color.FromArgb(165, 173, 190);
            Warm = FirstLight;
            Green = Color.FromArgb(22, 132, 74);
            if (!Dark)
            {
                Back = Color.White;
                Surface = Color.FromArgb(244, 245, 248);
                Text = Color.FromArgb(22, 24, 32);
                Soft = Color.FromArgb(92, 98, 112);
                Faint = Color.FromArgb(108, 114, 128);
                TextOff = Color.FromArgb(108, 114, 128);
                Hair = Color.FromArgb(218, 222, 230);
                Primary = Color.FromArgb(16, 18, 26);
                PrimaryText = Color.White;
                PrimaryHover = Color.FromArgb(44, 48, 62);
                WarmText = Color.FromArgb(150, 92, 10);
                Ok = Color.FromArgb(18, 120, 50);
                Warn = Color.FromArgb(160, 100, 0);
                Danger = Color.FromArgb(176, 32, 32);
                CalloutBack = Color.FromArgb(255, 248, 228);
                CalloutBar = Color.FromArgb(232, 170, 80);
                Field = Color.White;
                Disabled = Color.FromArgb(218, 222, 230);
                DisabledText = Color.FromArgb(92, 98, 112);
                Pick = Color.FromArgb(16, 18, 26);
                PickText = Color.White;
            }
            else
            {
                Back = Color.FromArgb(15, 16, 21);
                Surface = Color.FromArgb(24, 26, 34);
                Text = Color.FromArgb(234, 237, 244);
                Soft = Color.FromArgb(152, 160, 176);
                Faint = Color.FromArgb(132, 140, 156);
                TextOff = Color.FromArgb(132, 140, 156);
                Hair = Color.FromArgb(46, 50, 62);
                Primary = Color.FromArgb(226, 233, 248);
                PrimaryText = Color.FromArgb(10, 11, 16);
                PrimaryHover = Color.White;
                WarmText = Color.FromArgb(240, 190, 120);
                Ok = Color.FromArgb(84, 200, 124);
                Warn = Color.FromArgb(236, 176, 64);
                Danger = Color.FromArgb(240, 100, 100);
                CalloutBack = Color.FromArgb(44, 38, 26);
                CalloutBar = Color.FromArgb(232, 170, 80);
                Field = Color.FromArgb(30, 32, 42);
                Disabled = Color.FromArgb(52, 56, 68);
                DisabledText = Color.FromArgb(150, 158, 174);
                Pick = Color.FromArgb(226, 233, 248);
                PickText = Color.FromArgb(10, 11, 16);
            }
        }

        static readonly Dictionary<string, Font> fonts = new Dictionary<string, Font>();

        // size: in pixels at 100 %. "" is Segoe UI; "semi" its semibold; "light" its light.
        public static Font F(float size, string kind = "")
        {
            string key = kind + "|" + Math.Round(size * Scale, 2).ToString(System.Globalization.CultureInfo.InvariantCulture);
            Font f;
            if (fonts.TryGetValue(key, out f)) return f;
            string name = kind == "semi" ? "Segoe UI Semibold" : kind == "light" ? "Segoe UI Light" : "Segoe UI";
            f = new Font(name, size * Scale, FontStyle.Regular, GraphicsUnit.Pixel);
            fonts[key] = f;
            return f;
        }

        public static Color Blend(Color a, Color b, float t)
        {
            return Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }

        public static GraphicsPath Round(RectangleF r, float radius)
        {
            var p = new GraphicsPath();
            float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            if (d <= 0.5f) { p.AddRectangle(r); return p; }
            p.AddArc(r.Left, r.Top, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Top, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.Left, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }

        // A line with the letters spaced apart (tracking: a part of the size of the font).
        public static float DrawTracked(Graphics g, string text, Font f, Color c, float x, float y, float tracking)
        {
            using (var b = new SolidBrush(c))
            using (var fmt = new StringFormat(StringFormat.GenericTypographic))
            {
                fmt.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces;
                g.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;
                foreach (char ch in text)
                {
                    string s = ch.ToString();
                    g.DrawString(s, f, b, x, y, fmt);
                    float w = ch == ' ' ? f.Size * 0.28f : g.MeasureString(s, f, 1000, fmt).Width;
                    x += w + tracking * f.Size;
                }
            }
            return x;
        }

        // The mark: a dark disc, the thin ring of light around it, thick where the light breaks through and a hair on
        // the far side, that point, and the glow. The same drawing as the pictures of the build, in vector form (the
        // script of the brand computes it per pixel; this is its outline). disc: the colour of the disc, which is the
        // colour of what is behind the mark.
        public static void DrawMark(Graphics g, float cx, float cy, float r, Color disc, bool glow, bool streak)
        {
            SmoothingMode before = g.SmoothingMode;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            const double first = -0.72; // radians: upper right
            float ux = (float)Math.Cos(first), uy = (float)Math.Sin(first);
            if (glow)
            {
                using (var path = new GraphicsPath())
                {
                    float gr = r * 3.0f;
                    path.AddEllipse(cx - gr, cy - gr, 2 * gr, 2 * gr);
                    using (var pg = new PathGradientBrush(path))
                    {
                        pg.CenterPoint = new PointF(cx + ux * r * 0.8f, cy + uy * r * 0.8f);
                        pg.CenterColor = Color.FromArgb(Dark ? 70 : 64, Silver);
                        pg.SurroundColors = new[] { Color.FromArgb(0, Silver) };
                        pg.SetBlendTriangularShape(0.5f, 1f);
                        g.FillPath(pg, path);
                    }
                }
            }
            using (var lg = new LinearGradientBrush(new PointF(cx + ux * r, cy + uy * r), new PointF(cx - ux * r, cy - uy * r),
                                                    Color.FromArgb(255, 253, 247), Color.FromArgb(104, 112, 132)))
                g.FillEllipse(lg, cx - r, cy - r, 2 * r, 2 * r);
            float r0 = 0.94f * r, off = 0.05f * r;
            using (var b = new SolidBrush(disc))
                g.FillEllipse(b, cx - ux * off - r0, cy - uy * off - r0, 2 * r0, 2 * r0);
            float px = cx + ux * r * 0.97f, py = cy + uy * r * 0.97f;
            using (var path = new GraphicsPath())
            {
                float hr = Math.Max(r * 0.42f, 3f);
                path.AddEllipse(px - hr, py - hr, 2 * hr, 2 * hr);
                using (var pg = new PathGradientBrush(path))
                {
                    pg.CenterColor = Color.FromArgb(235, FirstLight);
                    pg.SurroundColors = new[] { Color.FromArgb(0, FirstLight) };
                    g.FillPath(pg, path);
                }
            }
            float bead = Math.Max(r * 0.075f, 1.1f);
            using (var b = new SolidBrush(Color.FromArgb(255, 252, 240)))
                g.FillEllipse(b, px - bead, py - bead, 2 * bead, 2 * bead);
            if (streak)
            {
                float len = r * 1.9f;
                using (var lg = new LinearGradientBrush(new PointF(px, py), new PointF(px + len, py), Color.FromArgb(120, 255, 244, 224), Color.FromArgb(0, 255, 244, 224)))
                using (var pen = new Pen(lg, Math.Max(1f, r * 0.018f)))
                    g.DrawLine(pen, px, py, px + len, py);
            }
            g.SmoothingMode = before;
        }

        [DllImport("user32.dll")] static extern uint GetDpiForSystem();
        [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hwnd);
        [DllImport("user32.dll")] static extern int GetSystemMetricsForDpi(int index, uint dpi);

        // The DPI of the display (Windows 10 1607 and later; elsewhere 96, and Windows scales the window itself).
        public static float SystemScale()
        {
            try { return GetDpiForSystem() / 96f; } catch (Exception) { return 1f; }
        }

        public static float WindowScale(IntPtr hwnd)
        {
            try { uint d = GetDpiForWindow(hwnd); return d == 0 ? SystemScale() : d / 96f; } catch (Exception) { return SystemScale(); }
        }

        static System.IO.Stream IconStream()
        {
            return System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("Hades.Installer.app.ico");
        }

        // The icon of the window from the one of the program (embedded a second time as a resource, for it holds all its sizes):
        // the one of the title bar and the one of the taskbar and Alt+Tab, each at the size that the display wants.
        public static Icon AppIcon(bool small)
        {
            try
            {
                using (System.IO.Stream st = IconStream())
                {
                    if (st == null) return null;
                    uint dpi = (uint)Math.Round(Scale * 96);
                    int n = 0;
                    try { n = GetSystemMetricsForDpi(small ? 49 : 11, dpi); } catch (Exception) { }
                    if (n <= 0) n = (int)Math.Round((small ? 16 : 32) * Scale);
                    return new Icon(st, new Size(n, n));
                }
            }
            catch (Exception) { return null; }
        }

        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
        static extern int SetWindowTheme(IntPtr hwnd, string subApp, string subIdList);

        // The drop-down list and the spin box in the dark mode: the parts of Windows that draw them have a dark theme
        // (Windows 10 1809 and later; elsewhere the call fails and they stay as they are).
        public static void DarkControl(Control c)
        {
            if (!Dark || !c.IsHandleCreated) return;
            try { SetWindowTheme(c.Handle, "DarkMode_CFD", null); } catch (Exception) { }
        }

        // The title bar of the dark mode (Windows 10 1903 and later; elsewhere the call is refused and nothing happens).
        [DllImport("dwmapi.dll")]
        static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

        public static void DarkTitleBar(IntPtr handle)
        {
            try
            {
                int on = Dark ? 1 : 0;
                if (DwmSetWindowAttribute(handle, 20, ref on, 4) != 0) DwmSetWindowAttribute(handle, 19, ref on, 4);
            }
            catch (Exception) { }
        }
    }

    // A numbered heading of a step: a circle with its number (or a tick, once the step is done) and its name.
    sealed class StepHead : Control
    {
        public int Number;
        public int State; // 0 not yet, 1 now, 2 done
        public bool Numbered = true;

        public StepHead()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            BackColor = Color.Transparent;
        }

        public void SetState(int state)
        {
            if (state == State) return;
            State = state;
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float d = Ui.Sf(24);
            float x = 0;
            if (Numbered)
            {
                var box = new RectangleF(1, (Height - d) / 2f, d - 2, d - 2);
                if (State == 2)
                {
                    using (var b = new SolidBrush(Ui.Ok)) g.FillEllipse(b, box);
                    using (var pen = new Pen(Color.White, Math.Max(1.6f, Ui.Sf(2f))))
                    {
                        pen.StartCap = pen.EndCap = LineCap.Round;
                        pen.LineJoin = LineJoin.Round;
                        g.DrawLines(pen, new[] { new PointF(box.Left + box.Width * 0.27f, box.Top + box.Height * 0.53f),
                                                 new PointF(box.Left + box.Width * 0.44f, box.Top + box.Height * 0.69f),
                                                 new PointF(box.Left + box.Width * 0.73f, box.Top + box.Height * 0.33f) });
                    }
                }
                else
                {
                    if (State == 1) using (var b = new SolidBrush(Ui.Primary)) g.FillEllipse(b, box);
                    else using (var pen = new Pen(Ui.Faint, Math.Max(1f, Ui.Sf(1.4f)))) g.DrawEllipse(pen, box);
                    TextRenderer.DrawText(g, Number.ToString(), Ui.F(12.5f, "semi"), Rectangle.Round(box),
                                          State == 1 ? Ui.PrimaryText : Ui.Soft,
                                          TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
                }
                x = d + Ui.Sf(10);
            }
            TextRenderer.DrawText(g, Text, Ui.F(15f, "semi"), new Rectangle((int)x, 0, Width - (int)x, Height), Ui.Text,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
        }
    }

    // The big buttons: the colour is the BackColor that the window sets, the text the ForeColor; off, it is grey.
    sealed class PillButton : Button
    {
        bool hover, down;
        public bool Outline;    // a quiet button: its colour is the surface, with a hairline

        public PillButton()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
        }

        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { down = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { down = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Ui.Back);
            Color fill = Outline ? Ui.Surface : Enabled ? BackColor : Ui.Disabled;
            if (Enabled && hover) fill = Mix(fill, Ui.Dark ? Color.White : Color.FromArgb(70, 76, 96), 0.18f);
            if (Enabled && down) fill = Mix(fill, Color.Black, 0.18f);
            var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            using (GraphicsPath p = Ui.Round(r, Ui.Sf(8)))
            using (var b = new SolidBrush(fill))
            {
                g.FillPath(b, p);
                if (Outline) using (var pen = new Pen(Ui.Hair)) g.DrawPath(pen, p);
            }
            TextRenderer.DrawText(g, Text, Font, new Rectangle(Ui.S(10), 0, Width - Ui.S(20), Height), Enabled ? ForeColor : Ui.DisabledText,
                                  TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
            if (Focused && ShowFocusCues)
                using (GraphicsPath p = Ui.Round(new RectangleF(Ui.Sf(3), Ui.Sf(3), Width - Ui.Sf(7), Height - Ui.Sf(7)), Ui.Sf(6)))
                using (var pen = new Pen(Color.FromArgb(150, ForeColor)) { DashStyle = DashStyle.Dot })
                    g.DrawPath(pen, p);
        }

        static Color Mix(Color a, Color b, float t)
        {
            return Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
        }
    }

    // A text on a tinted ground with a bar at its left: the notices and the explanations.
    sealed class Callout : Label
    {
        public Color Bar = Ui.CalloutBar;

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using (var b = new SolidBrush(Bar))
                e.Graphics.FillRectangle(b, 0, 0, Math.Max(3, Ui.S(4)), Height);
        }
    }

    // The progress, thin and round.
    sealed class SlimBar : Control
    {
        int value;
        public int Maximum = 1000;

        public SlimBar() { SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true); }

        public int Value
        {
            get { return value; }
            set { if (this.value != value) { this.value = value; Invalidate(); } }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Ui.Back);
            var r = new RectangleF(0, Height / 2f - Ui.Sf(4), Width - 1, Ui.Sf(8));
            using (GraphicsPath p = Ui.Round(r, Ui.Sf(4)))
            using (var b = new SolidBrush(Ui.Hair))
                g.FillPath(b, p);
            float w = r.Width * Math.Max(0, Math.Min(Maximum, value)) / Maximum;
            if (w >= 1)
                using (GraphicsPath p = Ui.Round(new RectangleF(r.Left, r.Top, w, r.Height), Ui.Sf(4)))
                using (var b = new LinearGradientBrush(new RectangleF(r.Left, r.Top, Math.Max(w, 2), r.Height), Ui.Primary,
                                                       Ui.Dark ? Ui.FirstLight : Color.FromArgb(150, 92, 10), LinearGradientMode.Horizontal))
                    g.FillPath(b, p);
        }
    }

    // Short words in rounded boxes, wrapped over the width: the keys of the boot menu.
    sealed class Chips : Control
    {
        public string[] Items = new string[0];

        public Chips()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        }

        // where each chip goes, and the height that they take
        public int Flow(Graphics g, List<Rectangle> into)
        {
            Font f = Ui.F(12.5f, "semi");
            int x = 0, y = 0, h = Ui.S(28), gap = Ui.S(8);
            foreach (string s in Items)
            {
                int w = TextRenderer.MeasureText(g, s, f, new Size(1000, 100), TextFormatFlags.NoPadding).Width + Ui.S(22);
                if (x > 0 && x + w > Width) { x = 0; y += h + gap; }
                if (into != null) into.Add(new Rectangle(x, y, w, h));
                x += w + gap;
            }
            return y + h;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Ui.Back);
            var at = new List<Rectangle>();
            Flow(g, at);
            for (int i = 0; i < at.Count; ++i)
            {
                Rectangle r = at[i];
                using (GraphicsPath p = Ui.Round(new RectangleF(r.Left + 0.5f, r.Top + 0.5f, r.Width - 1, r.Height - 1), r.Height / 2f))
                {
                    using (var b = new SolidBrush(Ui.Surface)) g.FillPath(b, p);
                    using (var pen = new Pen(Ui.Hair)) g.DrawPath(pen, p);
                }
                TextRenderer.DrawText(g, Items[i], Ui.F(12.5f, "semi"), r, Ui.Text,
                                      TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            }
        }
    }

    // One step of "how to start it": its number, what to do, and a line more.
    sealed class StepCard : Control
    {
        public int Number;
        public string Title = "", Body = "";

        public StepCard()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Ui.Back);
            using (GraphicsPath p = Ui.Round(new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), Ui.Sf(10)))
            {
                using (var b = new SolidBrush(Ui.Surface)) g.FillPath(b, p);
                using (var pen = new Pen(Ui.Hair)) g.DrawPath(pen, p);
            }
            var box = new RectangleF(Ui.Sf(14), Ui.Sf(14), Ui.Sf(28), Ui.Sf(28));
            using (var b = new SolidBrush(Ui.Primary)) g.FillEllipse(b, box);
            TextRenderer.DrawText(g, Number.ToString(), Ui.F(14f, "semi"), Rectangle.Round(box), Ui.PrimaryText,
                                  TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            TextRenderer.DrawText(g, Title, Ui.F(13.5f, "semi"), new Rectangle(Ui.S(50), Ui.S(14), Width - Ui.S(54), Ui.S(28)), Ui.Text,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
            TextRenderer.DrawText(g, Body, Ui.F(12.5f), new Rectangle(Ui.S(14), Ui.S(52), Width - Ui.S(28), Height - Ui.S(58)), Ui.Soft,
                                  TextFormatFlags.WordBreak | TextFormatFlags.NoPadding);
        }
    }

    // What is being done to the disk while it is being done: the three stages, the one that runs now, and the warning.
    // It follows the progress that the operations report (writing is the first part of the bar, reading it back the rest).
    sealed class WorkList : Control
    {
        public int Permille;
        public string[] Stages = new string[0];
        public string Warning = "";

        public WorkList()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        }

        public int[] Cuts = new[] { 50, 600 };   // where in the progress each stage after the first one starts
        public int Stage { get { int n = 0; foreach (int c in Cuts) if (Permille >= c) ++n; return n; } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Parent != null ? Parent.BackColor : Ui.Back);
            int row = Ui.S(40), now = Stage;
            for (int i = 0; i < Stages.Length; ++i)
            {
                float cy = Ui.Sf(20) + i * row, d = Ui.Sf(24);
                var box = new RectangleF(Ui.Sf(8), cy - d / 2f, d, d);
                bool done = i < now || Permille >= 1000;
                if (done)
                {
                    using (var b = new SolidBrush(Ui.Ok)) g.FillEllipse(b, box);
                    using (var pen = new Pen(Color.White, Math.Max(1.6f, Ui.Sf(2f))) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
                        g.DrawLines(pen, new[] { new PointF(box.Left + d * 0.27f, box.Top + d * 0.53f), new PointF(box.Left + d * 0.44f, box.Top + d * 0.69f),
                                                 new PointF(box.Left + d * 0.73f, box.Top + d * 0.33f) });
                }
                else if (i == now)
                {
                    using (var b = new SolidBrush(Ui.Primary)) g.FillEllipse(b, box);
                    using (var b = new SolidBrush(Ui.FirstLight)) g.FillEllipse(b, box.Left + d * 0.36f, box.Top + d * 0.36f, d * 0.28f, d * 0.28f);
                }
                else using (var pen = new Pen(Ui.Faint, Math.Max(1f, Ui.Sf(1.4f)))) g.DrawEllipse(pen, box);
                TextRenderer.DrawText(g, Stages[i], Ui.F(14f, i == now && !done ? "semi" : ""), new Rectangle(Ui.S(48), (int)(cy - Ui.Sf(12)), Width - Ui.S(56), Ui.S(24)),
                                      done || i == now ? Ui.Text : Ui.Faint,
                                      TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            }
            TextRenderer.DrawText(g, Warning, Ui.F(13f, "semi"), new Rectangle(Ui.S(8), Stages.Length * row + Ui.S(10), Width - Ui.S(16), Ui.S(24)), Ui.Warn,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
        }
    }

    // Radio buttons and check boxes drawn here, so that the disabled ones keep a colour that can be read in both modes
    // (Windows draws them in its grey, which nearly disappears on a dark ground).
    static class Toggle
    {
        public static void Paint(Graphics g, Control c, bool radio, bool isChecked)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(c.Parent != null ? c.Parent.BackColor : Ui.Back);
            bool on = c.Enabled;
            Color edge = on ? Ui.Soft : Ui.TextOff;
            float d = Ui.Sf(16), x = Ui.Sf(1), y = (c.Height - d) / 2f;
            var box = new RectangleF(x, y, d, d);
            using (var pen = new Pen(edge, Math.Max(1.2f, Ui.Sf(1.5f))))
            using (var fill = new SolidBrush(isChecked && !radio ? (on ? Ui.Primary : Ui.TextOff) : Ui.Back))
            {
                if (radio) { g.FillEllipse(fill, box); g.DrawEllipse(pen, box); }
                else
                {
                    using (GraphicsPath p = Ui.Round(box, Ui.Sf(3))) { g.FillPath(fill, p); g.DrawPath(pen, p); }
                }
            }
            if (isChecked)
            {
                if (radio)
                    using (var b = new SolidBrush(on ? Ui.Primary : Ui.TextOff))
                        g.FillEllipse(b, x + d * 0.27f, y + d * 0.27f, d * 0.46f, d * 0.46f);
                else
                    using (var pen = new Pen(Ui.PrimaryText, Math.Max(1.5f, Ui.Sf(2f))) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
                        g.DrawLines(pen, new[] { new PointF(x + d * 0.24f, y + d * 0.52f), new PointF(x + d * 0.43f, y + d * 0.71f), new PointF(x + d * 0.77f, y + d * 0.30f) });
            }
            var text = new Rectangle((int)(d + Ui.Sf(9)), 0, c.Width - (int)(d + Ui.Sf(9)), c.Height);
            TextRenderer.DrawText(g, c.Text, c.Font, text, on ? Ui.Text : Ui.TextOff,
                                  TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            if (c.Focused && ((c is RadioButton && ((RadioButton)c).Checked) || c is CheckBox))
            {
                Size s = TextRenderer.MeasureText(g, c.Text, c.Font, new Size(text.Width, c.Height), TextFormatFlags.NoPadding);
                if (c.Focused) ControlPaint.DrawFocusRectangle(g, new Rectangle(text.Left - 2, (c.Height - s.Height) / 2 - 1, Math.Min(s.Width, text.Width) + 4, s.Height + 2));
            }
        }
    }

    sealed class ThemedRadio : RadioButton
    {
        public ThemedRadio() { SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true); }
        protected override void OnPaint(PaintEventArgs e) { Toggle.Paint(e.Graphics, this, true, Checked); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
    }

    sealed class ThemedCheck : CheckBox
    {
        public ThemedCheck() { SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true); }
        protected override void OnPaint(PaintEventArgs e) { Toggle.Paint(e.Graphics, this, false, Checked); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
    }
}
